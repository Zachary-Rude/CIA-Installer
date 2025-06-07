#include "common.h"
#include "cia.h"
#include "err.h"
#include "draw.h"
#include <math.h>
#define LINE_BLANK      "                                         "
#define LINE_BLANK_LONG "                                                  "

double percentage = 0.0;
u32 bytesPerSecond;
Handle amHandle;

char *basename(char const *path) {
	char *s = strrchr(path, '/');
	if (!s)
		return strdup(path);
	else
		return strdup(s + 1);
}

static const char *humanSize(uint64_t bytes) {
	char *suffix[] = {"B", "KB", "MB", "GB", "TB"};
	char length = sizeof(suffix) / sizeof(suffix[0]);

	int i = 0;
	double dblBytes = bytes;

	if (bytes > 1024) {
		for (i = 0; (bytes / 1024) > 0 && i<length-1; i++, bytes /= 1024)
			dblBytes = bytes / 1024.0;
	}

	static char output[200];
	sprintf(output, "%.02lf %s", dblBytes, suffix[i]);
	return output;
}

FS_MediaType getTitleDestination(u64 titleId) {
	u16 platform = (u16) ((titleId >> 48) & 0xFFFF);
	u16 category = (u16) ((titleId >> 32) & 0xFFFF);
	u8 variation = (u8) (titleId & 0xFF);

	return platform == 0x0003 || (platform == 0x0004 && ((category & 0x8011) != 0 || (category == 0x0000 && variation == 0x02))) ? MEDIATYPE_NAND : MEDIATYPE_SD;
}

// the following code is from
// https://github.com/LiquidFenrir/MultiUpdater/blob/rewrite/source/cia.c
static void deletePrevious(u64 titleid) {
	u32 titles_amount = 0;
	if (R_FAILED(result = AM_GetTitleCount(MEDIATYPE_SD, &titles_amount)))
		return;
	
	u32 read_titles = 0;
	u64* titleIDs = malloc(titles_amount * sizeof(u64));
	if (R_FAILED(result = AM_GetTitleList(&read_titles, MEDIATYPE_SD, titles_amount, titleIDs))) {
		free(titleIDs);
		return;
	}
	
	for (u32 i = 0; i < read_titles; i++) {
		if (titleIDs[i] == titleid) {
			result = AM_DeleteAppTitle(MEDIATYPE_SD, titleid);
			break;
		}
	}
	
	free(titleIDs);
}

#define ProgressBarPadding 4

void consoleInitProgress(const char* header, const char* text, const float progress) {
	consoleClear();

	consoleSelect(&top_screen);
	// Print progress bar borders
	int progressBarY = top_screen.consoleHeight / 2 - 1;
	top_screen.cursorY = progressBarY;

	int startX = ProgressBarPadding;
	int endX = top_screen.consoleWidth - ProgressBarPadding + 1;

	int startY = progressBarY;

	for (int i = startX; i < endX; i++) {
		// Draw left and right border
		for (int j = 0; j < 12; j++) {
			top_screen.frameBuffer[((startX * 8 - 3) * 240) + (230 - (startY * 8)) + j] = 0xffff;
			top_screen.frameBuffer[((endX * 8 - 6) * 240) + (230 - (startY * 8)) + j] = 0xffff;
		}
		// Draw top and bottom borders
		for (int j = 0; j < (i < endX - 1 ? 8 : 6); j++) {
			top_screen.frameBuffer[((i * 8 + j - 3) * 240) + (239 - (startY * 8 - 3))] = 0xffff;
			top_screen.frameBuffer[((i * 8 + j - 3) * 240) + (239 - ((startY + 1) * 8 + 2))] = 0xffff;
		}
	}

	// Print header
	top_screen.cursorY = progressBarY - 3;
	top_screen.cursorX = ProgressBarPadding;
	printf("%s%s%s", FG_YELLOW, header, RESET);

	// Set data
	consoleSetProgressData(text, progress);
}

void consoleSetProgressData(const char* text, const double progress) {
	consoleSetProgressText(text);
	consoleSetProgressValue(progress);
}

void consoleSetProgressText(const char* text) {
	// Move to approriate row
	int progressBarY = top_screen.consoleHeight / 2 - 1;
	top_screen.cursorY = ((int)progressBarY) + 1;

	// Clear line
	top_screen.cursorX = 0;
	printf("%.*s", top_screen.consoleWidth, LINE_BLANK_LONG);

	// Write text
	top_screen.cursorX = ProgressBarPadding;
	printf("%s", text);
}

void consoleSetProgressValue(const double progress) {
	// Move to approriate row
	top_screen.cursorY = top_screen.consoleHeight / 2 - 2;

	// Move to beginning of progress bar
	int progressBarLength = top_screen.consoleWidth - ProgressBarPadding*2;
	top_screen.cursorX = ProgressBarPadding;


	// Fill progress
	int progressBarFill = (int)(progressBarLength * progress);
	top_screen.flags |= CONSOLE_COLOR_REVERSE;
	printf("%.*s", progressBarFill, LINE_BLANK);
	top_screen.flags &= ~CONSOLE_COLOR_REVERSE;
	printf("%.*s", progressBarLength - progressBarFill, LINE_BLANK);
}

Result AM_StartCiaInstallOverwrite(FS_MediaType mediatype, Handle *ciaHandle) {
	Result ret = 0;
	u32 *cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = IPC_MakeHeader(0x418, 1, 0);; // 0x04180040
	cmdbuf[1] = mediatype;
	printf("0x%08lX\n", cmdbuf[0]);

	if (R_FAILED(ret = svcSendSyncRequest(amHandle))) err_show_res(ret, "svcSendSyncRequest");

	if (ciaHandle) *ciaHandle = cmdbuf[3];

	return (Result)cmdbuf[1];
}

Result Launch(u64 titleId, FS_MediaType mediaType) {
	Result ret = 0;
	u8 param[0x300];
	u8 hmac[0x20];

	if (R_FAILED(ret = APT_PrepareToDoApplicationJump(0, titleId, mediaType))) {
		err_show_res(ret, "APT_PrepareToDoApplicationJump");
		return ret;
	}

	if (R_FAILED(ret = APT_DoApplicationJump(param, sizeof(param), hmac))) {
		err_show_res(ret, "APT_DoApplicationJump");
		return ret;
	}

	return 0;
}

Result installCia(FS_MediaType mediaType, bool deleteWhenDone, bool showMessage) {
	Handle ciaHandle, fileHandle;
	AM_TitleEntry info;
	Result res;
	ssize_t bytesRead = 0;
	ssize_t installOffset = 0;
	amHandle = amGetSessionHandle();
	// allocate memory for the buffer to write cia file data to
	void *ciaBuf = malloc(BUFSIZE);
	draw_clearscrn();
	char file_path[MAX_PATH_SIZE];
	strcpy(file_path, current_path);
	strcat(file_path, file_arr[selected + scroll].name);
	consoleSelect(&debug_screen);
	// open cia file with fopen
	FILE *ciaFile = fopen(file_path, "r");
	if (ciaFile == NULL) {
		consoleClear();
		err_show_errno(errno, "fopen");
		result = MAKERESULT(RL_STATUS, RS_NOTFOUND, RM_FS, 120);
		return;
	}
	// get size of cia file with fseek and ftell, then return to file beginning
	fseek(ciaFile, 0, SEEK_END);
	size_t ciaSize = ftell(ciaFile);
	fseek(ciaFile, 0, SEEK_SET);

	result = FSUSER_OpenFileDirectly(&fileHandle, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, file_path + 5), FS_OPEN_READ, 0);
	if (R_FAILED(result)) {
		err_show_res(result, "FSUSER_OpenFileDirectly");
		return;
	}
	if (R_FAILED(result = AM_GetCiaFileInfo(MEDIATYPE_SD, &info, fileHandle))) {
		err_show_res(result, "AM_GetCiaFileInfo");
		return;
	}
	deletePrevious(info.titleID);
	// start cia install for provided mediatype with the handle provided earlier
	result = AM_StartCiaInstall(mediaType, &ciaHandle);
	if (R_FAILED(result)) {
		consoleClear();
		AM_CancelCIAInstall(ciaHandle);
		err_show_res(result, "AM_StartCiaInstall");
		return;
	}
	
	u64 lastBytesPerSecondUpdate = osGetTime();
	u32 bytesSinceUpdate = 0;
	u64 remainingTime = 0;

	char progressText[256];
	// go into a loop where data is written until all of the data is written,
	// that means the installoffset which is increased everytime we read
	// something, is equal to the size of the cia when all of the data has been
	// read (and theres a final write step)
	consoleSelect(&top_screen);
	consoleInitProgress("Installing CIA", "0.0% complete (0 B/s)", 0);
	do {
		percentage = ((double)installOffset) / ((double)ciaSize);
		snprintf(progressText, sizeof progressText, "%.1f%% complete (%s/s)\r", percentage * 100, humanSize(bytesPerSecond));
		consoleSetProgressData(progressText, percentage);

		// read data with BUFSIZE size into ciaBuf
		bytesRead = fread(ciaBuf, 1, BUFSIZE, ciaFile);
		// write ciaBuf to the handle at the offset where we are at right now
		res = FSFILE_Write(ciaHandle, NULL, installOffset, ciaBuf, BUFSIZE,
								FS_WRITE_FLUSH);
		if (R_FAILED(res))
			break;
		// add how far weve gotten to our offset variable
		installOffset += bytesRead;
		bytesSinceUpdate += bytesRead;
		u64 time = osGetTime();
		u64 elapsed = time - lastBytesPerSecondUpdate;
		remainingTime = (elapsed / installOffset) * (ciaSize - installOffset);
		if (elapsed >= 1000) {
			bytesPerSecond = (u32) (bytesSinceUpdate / (elapsed / 1000.0f));
			bytesSinceUpdate = 0;
			lastBytesPerSecondUpdate = time;
		}
	} while (installOffset < ciaSize);
	if (R_FAILED(res)) {
		FSFILE_Close(fileHandle);
		AM_CancelCIAInstall(ciaHandle);
		result = res;
		err_show_res(result, "FSFILE_Write");
		return;
	}
	// end the cia install for provided handle we used to start cia install and
	// where we wrote the data to
	result = AM_FinishCiaInstall(ciaHandle);
	if (R_FAILED(res)) {
		fclose(ciaFile);
		free(ciaBuf);
		FSFILE_Close(fileHandle);
		consoleClear();
		err_show_res(result, "AM_FinishCiaInstall");
		return;
	}
	// close the cia file
	fclose(ciaFile);
	free(ciaBuf);
	FSFILE_Close(fileHandle);
	//FSFILE_Close(ciaHandle);
	consoleSelect(&debug_screen);
	if (deleteWhenDone && access(file_path, F_OK) == 0) {
		int ret = remove(file_path);
		if (ret != 0) {
			consoleClear();
			err_show_errno(errno, "remove");
			return;
		}
	}
	if (!showMessage) {
		printf("%sSuccessfully installed %s%s\n", FG_GREEN, basename(file_path), RESET);
	} else {
		draw_clearscrn();
		printf("\n\n\n\t\t%sCIA installed successfully.%s", FG_GREEN, RESET);
		printf("\n\n\t\tPress [A] to continue");
		while (aptMainLoop()) {
			gspWaitForVBlank();
			hidScanInput();
			u32 exitkDown = hidKeysDown();
			if (exitkDown & KEY_A)
				break;
			gfxFlushBuffers();
			gfxSwapBuffers();
		}
	}
	return res;
}

Result installCiaFromFile(char filePath[MAX_PATH_SIZE], FS_MediaType mediaType, bool deleteWhenDone, bool showMessage) {
	Handle ciaHandle, fileHandle;
	AM_TitleEntry info;
	Result res;
	ssize_t bytesRead = 0;
	ssize_t installOffset = 0;
	amHandle = amGetSessionHandle();
	// allocate memory for the buffer to write cia file data to
	void *ciaBuf = malloc(BUFSIZE);
	draw_clearscrn();
	consoleSelect(&debug_screen);
	// open cia file with fopen
	FILE *ciaFile = fopen(filePath, "r");
	if (ciaFile == NULL) {
		consoleClear();
		err_show_errno(errno, "fopen");
		result = MAKERESULT(RL_STATUS, RS_NOTFOUND, RM_FS, 120);
		return;
	}
	// get size of cia file with fseek and ftell, then return to file beginning
	fseek(ciaFile, 0, SEEK_END);
	size_t ciaSize = ftell(ciaFile);
	fseek(ciaFile, 0, SEEK_SET);

	result = FSUSER_OpenFileDirectly(&fileHandle, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, filePath + 5), FS_OPEN_READ, 0);
	if (R_FAILED(result)) {
		err_show_res(result, "FSUSER_OpenFileDirectly");
		return;
	}
	if (R_FAILED(result = AM_GetCiaFileInfo(MEDIATYPE_SD, &info, fileHandle))) {
		err_show_res(result, "AM_GetCiaFileInfo");
		return;
	}
	deletePrevious(info.titleID);
	
	// start cia install for provided mediatype with the handle provided earlier
	result = AM_StartCiaInstall(mediaType, &ciaHandle);
	if (R_FAILED(res)) {
		consoleClear();
		err_show_res(result, "AM_StartCiaInstall");
		return;
	}
	
	u64 lastBytesPerSecondUpdate = osGetTime();
	u32 bytesSinceUpdate = 0;
	u64 remainingTime = 0;

	char progressText[256];
	// go into a loop where data is written until all of the data is written,
	// that means the installoffset which is increased everytime we read
	// something, is equal to the size of the cia when all of the data has been
	// read (and theres a final write step)
	consoleSelect(&top_screen);
	consoleInitProgress("Installing CIA", "0.0% complete (0 B/s)", 0);
	do {
		percentage = ((double)installOffset) / ((double)ciaSize);
		snprintf(progressText, sizeof progressText, "%.1f%% complete (%s/s)\r", percentage * 100, humanSize(bytesPerSecond));
		consoleSetProgressData(progressText, percentage);

		// read data with BUFSIZE size into ciaBuf
		bytesRead = fread(ciaBuf, 1, BUFSIZE, ciaFile);
		// write ciaBuf to the handle at the offset where we are at right now
		result = FSFILE_Write(ciaHandle, NULL, installOffset, ciaBuf, BUFSIZE,
								FS_WRITE_FLUSH);
		if (R_FAILED(result))
			break;
		// add how far weve gotten to our offset variable
		installOffset += bytesRead;
		bytesSinceUpdate += bytesRead;
		u64 time = osGetTime();
		u64 elapsed = time - lastBytesPerSecondUpdate;
		remainingTime = (elapsed / installOffset) * (ciaSize - installOffset);
		if (elapsed >= 1000) {
			bytesPerSecond = (u32) (bytesSinceUpdate / (elapsed / 1000.0f));
			bytesSinceUpdate = 0;
			lastBytesPerSecondUpdate = time;
		}
	} while (installOffset < ciaSize);
	if (R_FAILED(result)) {
		FSFILE_Close(fileHandle);
		AM_CancelCIAInstall(ciaHandle);
		err_show_res(result, "FSFILE_Write");
		return;
	}
	// end the cia install for provided handle we used to start cia install and
	// where we wrote the data to
	result = AM_FinishCiaInstall(ciaHandle);
	if (R_FAILED(result)) {
		fclose(ciaFile);
		free(ciaBuf);
		FSFILE_Close(fileHandle);
		consoleClear();
		err_show_res(result, "AM_FinishCiaInstall");
		return;
	}
	// close the cia file
	fclose(ciaFile);
	free(ciaBuf);
	FSFILE_Close(fileHandle);
	//FSFILE_Close(ciaHandle);
	consoleSelect(&debug_screen);
	if (deleteWhenDone && access(filePath, F_OK) != -1) {
		if (deleteWhenDone && access(filePath, F_OK) == 0) {
			int ret = remove(filePath);
			if (ret != 0) {
				consoleClear();
				result = -1;
				err_show_errno(errno, "remove");
				return;
			}
		}
	}
	if (!showMessage) {
		printf("%sSuccessfully installed %s%s\n", FG_GREEN, basename(filePath), RESET);
	} else {
		draw_clearscrn();
		printf("\n\n\n\t\t%sCIA installed successfully.%s", FG_GREEN, RESET);
		printf("\n\n\t\tPress [A] to continue");
		while (aptMainLoop()) {
			gspWaitForVBlank();
			hidScanInput();
			u32 exitkDown = hidKeysDown();
			if (exitkDown & KEY_A)
				break;
			gfxFlushBuffers();
			gfxSwapBuffers();
		}
	}
	return;
}
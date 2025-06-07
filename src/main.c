#include <malloc.h>
#include <curl/curl.h>
#include <jansson.h>
#include "quirc/quirc.h"
#include "common.h"
#include "fs.h"
#include "draw.h"
#include "btn.h"
#include "ctm.h"
#include "err.h"
#include "cia.h"

#define SOC_ALIGN       0x1000
#define SOC_BUFFERSIZE  0x100000
#define CHUNK_SIZE 8192
#define WRITEBUFFERSIZE (1024 * 1024)
#define MAXFILENAME (256)
#define BUFFER_SIZE (256 * 1024)
#define FOPEN_FUNC(filename, mode) fopen(filename, mode)
#define FTELLO_FUNC(stream) ftello(stream)
#define FSEEKO_FUNC(stream, offset, origin) fseeko(stream, offset, origin)
#define dir_delimter '/'
#define PBSTR "████████████████████████████████████████████████████████████"
#define PBWIDTH 60
#define RESET             "\x1b[0m"
#define BLACK_ON_WHITE    "\x1b[47;30m"

// Type to store all our QR thread/data informations
typedef struct {
	u16*          camera_buffer;
	Handle        mutex;
	volatile bool finished;
	Handle        cancel;
	bool          capturing;
	struct quirc* context;
	char*         state;
} qr_data;

struct progress {
  char *private;
  size_t size;
};

// increase the stack in order to allow quirc to decode large qrs
int __stacksize__ = 64 * 1024;

char current_path[511];
int selected = 0;
int scroll   = 0;
u64 scrollCooldown;

file_entry *file_arr;
int size_of_file_array;

PrintConsole top_screen, header_screen, instruction_screen, debug_screen;

CB clipboard;
bool quit_for_err = false;

char progressText[256];

static void qrScanner(void);
static void qrHandler(qr_data* data);
static void qrExit(qr_data* data);
static void camThread(void* arg);
static void download(const uint8_t* url);
static void downloadURL(const char* url);
static size_t handle_data(char* ptr, size_t size, size_t nmemb, void* userdata);

static void qrScanner(void) {
	draw_clearscrn();
	consoleSelect(&top_screen);
	// init qr_data struct variables
	qr_data* data = malloc(sizeof(qr_data));
	data->capturing = false;
	data->finished = false;
	data->context = quirc_new();
	quirc_resize(data->context, 400, 232);
	data->camera_buffer = calloc(1, 400 * 232 * sizeof(u16));
	qrHandler(data);
}

// main QR code loop
static void qrHandler(qr_data *data) {
	if (!data->capturing) {
		// create cam thread
		data->mutex = 0;
		data->cancel = 0;
		svcCreateEvent(&data->cancel, RESET_STICKY);
		svcCreateMutex(&data->mutex, false);
		if (threadCreate(camThread, data, 0x10000, 0x1A, 1, true) != NULL) {
			data->capturing = true;
			//printf("capturing\n");
		} else {
			//printf("qrExit: not capturing\n");
			qrExit(data);
			return;
		}
	}

	if (data->finished) {
		//printf("qrExit: finished");
		qrExit(data);
		return;
	}

	int w, h;
	u8* image = (u8*)quirc_begin(data->context, &w, &h);
	svcWaitSynchronization(data->mutex, U64_MAX);
	for (ssize_t x = 0; x < w; x++) {
		for (ssize_t y = 0; y < h; y++) {
			u16 px = data->camera_buffer[y * 400 + x];
			image[y * w + x] = (u8)(((((px >> 11) & 0x1F) << 3) + (((px >> 5) & 0x3F) << 2) + ((px & 0x1F) << 3)) / 3);
		}
	}
	svcReleaseMutex(data->mutex);
	quirc_end(data->context);
	camExit();
	if (quirc_count(data->context) > 0) {
		struct quirc_code code;
		struct quirc_data scan_data;
		quirc_extract(data->context, 0, &code);  
		if (!quirc_decode(&code, &scan_data)) {
			char url[scan_data.payload_len];
			memcpy(url, scan_data.payload, scan_data.payload_len);
			if (strstr(strlwr(url), ".cia") != NULL) {
				data->state = "Downloading";
				download(scan_data.payload);
				// install from file
				if (R_SUCCEEDED(result)) {
					data->state = "Installing";
					installCiaFromFile("sdmc:/tmp.cia", MEDIATYPE_SD, true, false);
					
				}
			} else {
				consoleSelect(&debug_screen);
				printf("%sScanned QR code does not contain a CIA file%s\n", FG_RED, RESET);
			}
			data->state = "Ready";
			fs_populate_filarr(current_path);
			draw_filearr(1);
		}
	}
}

static void qrExit(qr_data *data) {
	svcSignalEvent(data->cancel);
	while (!data->finished)
		svcSleepThread(1000000);
	data->capturing = false;
	free(data->camera_buffer);
	quirc_destroy(data->context);
	free(data);
	camExit();
	fs_populate_filarr(current_path);
	draw_filearr(1);
}

static void camThread(void *arg) {
	qr_data* data = (qr_data*) arg;
	Handle events[3] = {0};
	events[0] = data->cancel;
	u32 transferUnit;

	u16 *buffer = malloc(400 * 232 * sizeof(u16));
	//printf("camera initialized\n");
	CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
	CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
	CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_30);
	CAMU_SetNoiseFilter(SELECT_OUT1, true);
	CAMU_SetAutoExposure(SELECT_OUT1, true);
	CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
	CAMU_Activate(SELECT_OUT1);
	CAMU_GetBufferErrorInterruptEvent(&events[2], PORT_CAM1);
	CAMU_SetTrimming(PORT_CAM1, false);
	CAMU_GetMaxBytes(&transferUnit, 400, 232);
	CAMU_SetTransferBytes(PORT_CAM1, transferUnit, 400, 232);
	CAMU_ClearBuffer(PORT_CAM1);
	CAMU_SetReceiving(&events[1], buffer, PORT_CAM1, 400 * 232 * sizeof(u16), (s16) transferUnit);
	CAMU_StartCapture(PORT_CAM1);
	//printf("capture started\n");
	bool cancel = false;
	while (!cancel) {
		draw_clearscrn();
		consoleSelect(&top_screen);
		s32 index = 0;
		u32 kDown = hidKeysDown();
		if (kDown & KEY_START) {
			//printf("capture canceled\n");
			cancel = true;
			break;
		}
		svcWaitSynchronizationN(&index, events, 3, false, U64_MAX);
		switch (index) {
			case 0:
				//printf("capture canceled\n");
				cancel = true;
				break;
			case 1:
				svcCloseHandle(events[1]);
				events[1] = 0;
				svcWaitSynchronization(data->mutex, U64_MAX);
				memcpy(data->camera_buffer, buffer, 400 * 232 * sizeof(u16));
				GSPGPU_FlushDataCache(data->camera_buffer, 400 * 232 * sizeof(u16));
				svcReleaseMutex(data->mutex);
				CAMU_SetReceiving(&events[1], buffer, PORT_CAM1, 400 * 232 * sizeof(u16), transferUnit);
				//printf("camera receiving\n");
				break;
			case 2:
				svcCloseHandle(events[1]);
				events[1] = 0;
				CAMU_ClearBuffer(PORT_CAM1);
				//printf("camera buffer cleared\n");
				CAMU_SetReceiving(&events[1], buffer, PORT_CAM1, 400 * 232 * sizeof(u16), transferUnit);
				CAMU_StartCapture(PORT_CAM1);
				//printf("capture started\n");
				break;
			default:
				break;
		}
	}

	CAMU_StopCapture(PORT_CAM1);
	//printf("capture stopped\n");

	bool busy = false;
	while (R_SUCCEEDED(CAMU_IsBusy(&busy, PORT_CAM1)) && busy)
		svcSleepThread(1000000);

	CAMU_ClearBuffer(PORT_CAM1);
	CAMU_Activate(SELECT_NONE);
	//printf("camera service exited\n");
	free(buffer);
	for (int i = 0; i < 3; i++) {
		if (events[i] != 0) {
			svcCloseHandle(events[i]);
			events[i] = 0;
		}
	}
	svcCloseHandle(data->mutex);
	//printf("capture finished\n");
	data->finished = true;
	camExit();
	fs_populate_filarr(current_path);
	draw_filearr(1);
}

static char* result_buf = NULL;
static size_t result_sz = 0;
static size_t result_written = 0;

// following code is from 
// https://github.com/angelsl/libctrfgh/blob/master/curl_test/src/main.c
size_t handle_data(char* ptr, size_t size, size_t nmemb, void* userdata) {
	(void) userdata;
	const size_t bsz = size*nmemb;

	if (result_sz == 0 || !result_buf) {
		result_sz = 0x1000;
		result_buf = malloc(result_sz);
	}

	bool need_realloc = false;
	while (result_written + bsz > result_sz) {
		result_sz <<= 1;
		need_realloc = true;
	}

	if (need_realloc) {
		char *new_buf = realloc(result_buf, result_sz);
		if (!new_buf)
			return 0;
		result_buf = new_buf;
	}

	if (!result_buf)
		return 0;

	memcpy(result_buf + result_written, ptr, bsz);
	result_written += bsz;
	return bsz;
}

void download(const uint8_t* url) {
	draw_clearscrn();
	printf("%sDownloading %s%s\n\n", BLACK_ON_WHITE, url, RESET);
	consoleSelect(&debug_screen);
	void *socubuf = memalign(0x1000, 0x100000);
	if (!socubuf) {
		result = -1;
		return;
	}

	if (R_FAILED(result = socInit(socubuf, 0x100000))) {
		printf("%sDownload failed:  %08lX%s\n", FG_RED, result, RESET);
		return;
	}

	CURL *hnd = curl_easy_init();
	curl_easy_setopt(hnd, CURLOPT_BUFFERSIZE, 102400L);
	curl_easy_setopt(hnd, CURLOPT_URL, url);
	//curl_easy_setopt(hnd, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(hnd, CURLOPT_USERAGENT, "CIAInstaller-curl/7.58.0");
	curl_easy_setopt(hnd, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(hnd, CURLOPT_MAXREDIRS, 50L);
	curl_easy_setopt(hnd, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
	curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, handle_data);
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(hnd, CURLOPT_VERBOSE, 1L);
	CURLcode cres = curl_easy_perform(hnd);
		
	// cleanup
	curl_easy_cleanup(hnd);
	socExit();
	free(socubuf);
	result_sz = 0;

	if (cres == CURLE_OK) {
		// write to disk
		FILE* file = fopen("sdmc:/tmp.cia", "wb");
		fwrite(result_buf, result_written, 1, file);
		fclose(file);
	}

	free(result_buf);
	result_buf = NULL;
	result_written = 0;
	result = cres == CURLE_OK ? 0 : (Result)cres;
	if (R_SUCCEEDED(result))
		printf("%sSuccessfully downloaded %s%s\n", FG_GREEN, url, RESET);
	else
		printf("%sDownload failed:  %08lX%s\n", FG_RED, result, RESET);
}

int download_progress(void *bar, double t, double d, double ultotal, double ulnow) {
	double progress = d / t;
	snprintf(progressText, sizeof progressText, "%.1f%% complete", progress * 100);
	consoleSetProgressData(progressText, progress);
	return 0;
}

void downloadURL(const char* url) {
	draw_clearscrn();
	consoleInitProgress("Downloading CIA", "0.0% complete", 0);
	void *socubuf = memalign(0x1000, 0x100000);
	if (!socubuf) {
		result = -1;
		return;
	}

	if (R_FAILED(result = socInit(socubuf, 0x100000))) {
		err_show_res(result, "socInit");
		return;
	}

	CURL *hnd = curl_easy_init();
	struct progress data;
	curl_easy_setopt(hnd, CURLOPT_BUFFERSIZE, 102400L);
	curl_easy_setopt(hnd, CURLOPT_URL, url);
	curl_easy_setopt(hnd, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(hnd, CURLOPT_USERAGENT, "CIAInstaller-curl/7.58.0");
	curl_easy_setopt(hnd, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(hnd, CURLOPT_MAXREDIRS, 50L);
	curl_easy_setopt(hnd, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
	curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, handle_data);
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(hnd, CURLOPT_PROGRESSDATA, &data);
	curl_easy_setopt(hnd, CURLOPT_PROGRESSFUNCTION, download_progress);
	//curl_easy_setopt(hnd, CURLOPT_VERBOSE, 1L);
	CURLcode cres = curl_easy_perform(hnd);
		
	// cleanup
	curl_easy_cleanup(hnd);
	socExit();
	free(socubuf);
	result_sz = 0;

	if (cres == CURLE_OK) {
		// write to disk
		FILE* file = fopen("sdmc:/tmp.cia", "wb");
		fwrite(result_buf, result_written, 1, file);
		fclose(file);
	}

	free(result_buf);
	result_buf = NULL;
	result_written = 0;
	result = cres == CURLE_OK ? 0 : (Result)cres;
	if (R_SUCCEEDED(result))
		printf("%sSuccessfully downloaded %s%s\n", FG_GREEN, url, RESET);
	else
		err_show_res(result, "Download");
}

int main(int argc, char *argv[]) {
	
	FS_MediaType media = MEDIATYPE_SD;
	Handle ciaHandle, fileHandle;
	AM_TitleEntry info;
	Result res = 0;
	gfxInitDefault();
	consoleInit(GFX_TOP, &top_screen);
	consoleInit(GFX_TOP, &header_screen);
	consoleInit(GFX_BOTTOM, &debug_screen);
	consoleInit(GFX_BOTTOM, &instruction_screen);
	fsInit();
	amInit();
	AM_InitializeExternalTitleDatabase(false);
	// store the old time limit to reset when the app ends
	u32 old_time_limit;

	APT_GetAppCpuTimeLimit(&old_time_limit);
	APT_SetAppCpuTimeLimit(30);
	// x, y, width, height
	consoleSetWindow(&instruction_screen, 0, 0, 40, 9);
	consoleSetWindow(&debug_screen, 0, 9, 40, 21);

	consoleSetWindow(&header_screen, 0, 0, 50, 1);
	consoleSetWindow(&top_screen, 0, 1, 50, 29);
	camInit();

	consoleSelect(&instruction_screen);
	printf("A - cd / open context menu\nB - go up a directory\nX - install from URL\nY - start queue\nR - delete dir/file\nDPAD/Circle Pad - up and down\nSTART - exit");
	printf("\n----------------------------------------");

	consoleSelect(&header_screen);
	printf("\e[47;30m\e[2J\e[H"); //invert colors and clear screen
	printf("CIA Installer v%s", APP_VERSION);

	consoleSelect(&debug_screen);
	printf("Started...\n");

	// Initial allocation
	file_arr = malloc(1 * sizeof(file_entry));
	if (file_arr == NULL) {
		err_show("main.c", __LINE__-2, "file_arr memory allocation failed");
	}

	static SwkbdState swkbd;
	static char url[512];
	static SwkbdStatusData swkbdStatus;
	static SwkbdLearningData swkbdLearning;
	SwkbdButton button = SWKBD_BUTTON_NONE;
	bool didit = false;

	scrollCooldown = osGetTime();

	// For when it is first realloc(ed)
	size_of_file_array = 1;
	// Copy root dir into current_path
	strcpy(current_path, "sdmc:/");
	// Init context menu
	ctm_init();
	// Fill file name array with file names
	fs_populate_filarr(current_path);
	// Print all in root dir
	draw_filearr(1);
	// Main loop
	while (aptMainLoop()) {
		gspWaitForVBlank();
		hidScanInput();

		if (quit_for_err)
			break;

		u32 kDown = hidKeysDown();
		u32 kHeld = hidKeysHeld();

		if (kDown & KEY_START) {
			break;
		} else if ((kDown & KEY_UP) || ((kHeld & KEY_UP) && osGetTime() >= scrollCooldown)) {
			btn_up();
			scrollCooldown = osGetTime() + ((kDown & KEY_UP) ? 500 : 100);
			draw_filearr(0);
		} else if ((kDown & KEY_DOWN) || ((kHeld & KEY_DOWN) && osGetTime() >= scrollCooldown)) {
			btn_down();
			scrollCooldown = osGetTime() + ((kDown & KEY_DOWN) ? 500 : 100);
			draw_filearr(0);
		} else if (kDown & KEY_LEFT) {
			btn_left();
			draw_filearr(0);
		} else if (kDown & KEY_RIGHT) {
			btn_right();
			draw_filearr(0);
		} else if (kDown & KEY_X) {
			swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, -1);
			swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, SWKBD_FILTER_BACKSLASH, 0);
			swkbdSetFeatures(&swkbd, SWKBD_DARKEN_TOP_SCREEN | SWKBD_ALLOW_HOME | SWKBD_ALLOW_RESET | SWKBD_ALLOW_POWER);
			swkbdSetHintText(&swkbd, "Enter a URL for a CIA file");
			button = swkbdInputText(&swkbd, url, sizeof(url));
			if (button == SWKBD_BUTTON_CONFIRM) {
				if (strstr(strlwr(url), ".cia") != NULL) {
					downloadURL(url);
					// install from file
					if (R_SUCCEEDED(result)) {
						installCiaFromFile("sdmc:/tmp.cia", MEDIATYPE_SD, true, true);
					}
				} else {
					consoleSelect(&debug_screen);
					printf("%sURL is not for a CIA file%s\n", FG_RED, RESET);
				}
			}
			fs_populate_filarr(current_path);
			draw_filearr(1);
		}

		else if (kDown & KEY_A) { btn_a_pressed(); }
		else if (kDown & KEY_B) { btn_b_pressed(); }

		else if (kDown & KEY_R) { btn_r_pressed(); }

		else if (kDown & KEY_Y) { 
			FILE * fp;
			int line = 1;
			char buf[MAX_PATH_SIZE] = "";

			fp = fopen("sdmc:/3ds/CIA-Installer/queue.txt", "r");
			if (fp == NULL) {
				err_show_errno(errno, "fopen");
				continue;
			}

			int errors = 0;

			while (fgets(buf, sizeof buf, fp)) {
				if (buf[0] != '\n' || buf[0] != '\r') {
					int len = strlen(buf);
					if (len > 0 && buf[len - 1] == '\n')
						buf[len-1] = 0;
					installCiaFromFile(buf, MEDIATYPE_SD, false, false);
					if (R_FAILED(result)) 
						errors++;
					line++;
				}
				else {
					continue;
				}
			}
			draw_clearscrn();
			if (errors > 0)
				printf("\n\n\n\t\t%sOne or more queue items failed to install.%s", FG_RED, RESET);
			else
				printf("\n\n\n\t\t%sQueue finished successfully.%s", FG_GREEN, RESET);
			printf("\n\n\t\tPress [A] to continue");
			errors = 0;

			while (aptMainLoop()) {
				gspWaitForVBlank();
				hidScanInput();
				u32 exitkDown = hidKeysDown();
				if (exitkDown & KEY_A)
					break;
				gfxFlushBuffers();
				gfxSwapBuffers();
			}
			fs_populate_filarr(current_path);
			draw_filearr(1);

			fclose(fp);
			remove("sdmc:/3ds/CIA-Installer/queue.txt");
		}

		// Flush and swap framebuffers
		gfxFlushBuffers();
		gfxSwapBuffers();
	}

	free(file_arr);
	fsExit();
	amExit();
	gfxExit();

	if (old_time_limit != UINT32_MAX)
		APT_SetAppCpuTimeLimit(old_time_limit);
	return 0;
}
#include "common.h"
#include "fs.h"
#include "ctm.h"
#include "cia.h"
#include "draw.h"

#define ctm_items_len 3
#define ctm_item_str_len 21

char ctm_items[ctm_items_len][ctm_item_str_len];
int ctm_selected;

void ctm_init(void) {
	strncpy(ctm_items[0], "Install CIA", ctm_item_str_len);
	strncpy(ctm_items[1], "Install & Delete CIA", ctm_item_str_len);
	strncpy(ctm_items[2], "Add to Queue", ctm_item_str_len);
}

void ctm_draw_menu_(int clr) {
	if (clr)
		draw_clearscrn();
	consoleSelect(&top_screen);
	printf(RESET_TO_TOP_LEFT);
	printf("%s\n\n", file_arr[selected + scroll].name);
	for (int i = 0; i < ctm_items_len; i++) {
		if (i == ctm_selected)
			printf("\t-> %s\n", ctm_items[i]);
		else
			printf("\t   %s\n", ctm_items[i]);
	}
	printf("\nPress B to go back\n");
}

void ctm_open(void) {
	ctm_selected = 0;
	bool breakLoop = false;
	ctm_draw_menu_(1);
	while (aptMainLoop()) {
		gspWaitForVBlank();
		hidScanInput();
		u32 ctm_kDown = hidKeysDown();

		if (ctm_kDown & KEY_UP) {
			if (!ctm_selected)
				ctm_selected = ctm_items_len - 1;
			else
				ctm_selected--;
			ctm_draw_menu_(0);
		} else if (ctm_kDown & KEY_DOWN) {
			if (ctm_selected == ctm_items_len - 1)
				ctm_selected = 0;
			else
				ctm_selected++;
			ctm_draw_menu_(0);
		} else if (ctm_kDown & KEY_A) {
			switch (ctm_selected) {
				case 0:
					if (stringEndsWith(file_arr[selected + scroll].name, ".cia")) {
						installCia(MEDIATYPE_SD, false, true);
					} else {
						consoleSelect(&debug_screen);
						printf("%s%s is not a CIA file%s\n", FG_RED, file_arr[selected + scroll].name, RESET);
					}
					breakLoop = true;
					break;
				case 1:
					if (stringEndsWith(file_arr[selected + scroll].name, ".cia")) {
						installCia(MEDIATYPE_SD, true, true);
					} else {
						consoleSelect(&debug_screen);
						printf("%s%s is not a CIA file%s\n", FG_RED, file_arr[selected + scroll].name, RESET);
					}
					breakLoop = true;
					break;
				case 2:
					if (stringEndsWith(file_arr[selected + scroll].name, ".cia")) {
						FILE *queueFile;
						queueFile = fopen("sdmc:/3ds/CIA-Installer/queue.txt", "a");
						if (queueFile == NULL) {
							err_show("ctm.c", __LINE__-2, "failed to open queue file");
							breakLoop = true;
							break;
						}
						setvbuf(queueFile, NULL, _IOLBF, 1024);
						char file_path[MAX_PATH_SIZE];
						strcpy(file_path, current_path);
						strcat(file_path, file_arr[selected + scroll].name);
						fprintf(queueFile, "%s\n", file_path);
						fclose(queueFile);
						breakLoop = true;
						break;
					} else {
						consoleSelect(&debug_screen);
						printf("%s%s is not a CIA file%s\n", FG_RED, file_arr[selected + scroll].name, RESET);
					}
					ctm_draw_menu_(1);
					break;
			}
		} else if ((ctm_kDown & KEY_B) || breakLoop) {
			draw_clearscrn();
			break;
		}

		gfxFlushBuffers();
		gfxSwapBuffers();
	}
}

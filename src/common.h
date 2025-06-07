#ifndef COMMON_H
#define COMMON_H

#include <citro2d.h>
#include <citro3d.h>
#include <3ds.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <malloc.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <utime.h>
#include <regex.h>
#include <inttypes.h>
#include <dirent.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/statvfs.h>

#include "colors.h" // Console colors

#define MAX_DIR_NAME_SIZE 261
#define MAX_FILES_ON_SCREEN 24 // 26
#define MAX_PATH_SIZE 511
#define BUFSIZE 0x200000
#define R_APP_INVALID_ARGUMENT MAKERESULT(RL_PERMANENT, RS_INVALIDARG, RM_APPLICATION, 1)
#define R_APP_CANCELLED MAKERESULT(RL_PERMANENT, RS_CANCELED, RM_APPLICATION, 2)
#define R_APP_SKIPPED MAKERESULT(RL_PERMANENT, RS_NOTSUPPORTED, RM_APPLICATION, 3)

#define R_APP_THREAD_CREATE_FAILED MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 4)

#define R_APP_PARSE_FAILED MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 5)
#define R_APP_BAD_DATA MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 6)

#define R_APP_HTTP_TOO_MANY_REDIRECTS MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 7)
#define R_APP_HTTP_ERROR_BASE MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 8)
#define R_APP_HTTP_ERROR_END (R_APP_HTTP_ERROR_BASE + 600)

#define R_APP_CURL_INIT_FAILED R_APP_HTTP_ERROR_END
#define R_APP_CURL_ERROR_BASE (R_APP_CURL_INIT_FAILED + 1)
#define R_APP_CURL_ERROR_END (R_APP_CURL_ERROR_BASE + 100)

#define R_APP_NOT_IMPLEMENTED MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, RD_NOT_IMPLEMENTED)
#define R_APP_OUT_OF_MEMORY MAKERESULT(RL_FATAL, RS_OUTOFRESOURCE, RM_APPLICATION, RD_OUT_OF_MEMORY)
#define R_APP_OUT_OF_RANGE MAKERESULT(RL_PERMANENT, RS_INVALIDARG, RM_APPLICATION, RD_OUT_OF_RANGE)

#define APP_VERSION "1.0.0"

// Struct to hold clipboard things in
// copy_type, 1 = cut, 2 = copy
typedef struct CB {
	char path[MAX_PATH_SIZE];
	char filename[MAX_DIR_NAME_SIZE];
	int copy_type;
} CB;

// For file_arr
typedef struct file_entry {
	char name[MAX_DIR_NAME_SIZE];
	bool isfile;
} file_entry;

extern char current_path[MAX_PATH_SIZE];
extern int selected;           // Selected file index
extern int scroll;             // Used to offset what is printed from the file array, to allow "scrolling"

extern file_entry *file_arr;   // An array of file entries that is resizable
extern int size_of_file_array; // Size of file name array

extern PrintConsole top_screen; // One PrintConsole for each screen
extern PrintConsole header_screen;
extern PrintConsole instruction_screen;
extern PrintConsole debug_screen;

extern CB clipboard;
extern bool quit_for_err; // Closes the main loop if an error happens

static Result result;

#endif

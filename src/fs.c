// fs.c : For all filesystem related functions
#include "common.h"
#include "fs.h"
#include "err.h"

// Check if a string ends with a substring
bool stringEndsWith(const char *str, const char *suffix) {
	int str_len = strlen(str);
	int suffix_len = strlen(suffix);

	return (str_len >= suffix_len) && (0 == strcmp(str + (str_len - suffix_len), suffix));
}

// Comparison function for array sorting
int compare_file_entry(const void *a, const void *b) {
	file_entry *fileA = (file_entry *)a;
	file_entry *fileB = (file_entry *)b;

	if (fileA->isfile != fileB->isfile)
		return fileA->isfile ? 1 : -1;
	else
		return strcasecmp(fileA->name, fileB->name);
}

// Sets current_path to its upper directory
void fs_get_ud(void) {
	consoleSelect(&debug_screen);
	char path_to_iterate[MAX_PATH_SIZE];
	char looking_for[] = "/";
	char *token;
	char dummy1[MAX_PATH_SIZE] = "";
	char dummy2[MAX_PATH_SIZE] = "";

	strncpy(path_to_iterate, current_path, MAX_PATH_SIZE);
	token = strtok(path_to_iterate, looking_for);
	while (token != NULL) {
		strcat(dummy1, token);
		strcat(dummy1, "/");
		// If dummy1 has been fully constructed into current_path
		if (!strcmp(dummy1, current_path)) {
			// dummy2 happens after this, so will have 1 less token
			strncpy(current_path, dummy2, MAX_PATH_SIZE);
			break;
		}
		strcat(dummy2, token);
		strcat(dummy2, "/");
		// get the next token
		token = strtok(NULL, looking_for);
	}
	printf("%snew path: %s%s\n", FG_GREEN, current_path, RESET);
}

file_entry* mergeArrays(file_entry *arr1[], int n1, file_entry *arr2[], int n2) {
  
	// Resultant array to store merged array
	file_entry *res = (file_entry*)malloc(sizeof(file_entry) * n1 * n2);

	// Copy elements of the first array
	memcpy(res, arr1, n1 * sizeof(file_entry));

	// Copy elements of the second array
	memcpy(res + n1, arr2, n2 * sizeof(file_entry));

	return res;
}

// Fills file array with all files in a given directory
void fs_populate_filarr(char dir_to_get[]) {
	consoleSelect(&debug_screen);
	// 2 of each for 2 iterations
	DIR *d;
	DIR *nd;
	d = opendir(dir_to_get);
	nd = opendir(dir_to_get);

	if (d) {
		struct dirent *dir;
		struct dirent *ndir;
		int count  = 0;
		selected   = 0;
		scroll     = 0;
		// While readdir returns something other than NULL. The variable dir will change each loop
		while ((dir = readdir(d)) != NULL) {
			if ((dir->d_type == 8 && stringEndsWith(dir->d_name, ".cia") || dir->d_type == 4))
				count++;
		}
		size_of_file_array = count;
		count = 0;
		file_arr = realloc(file_arr, (size_of_file_array+1) * sizeof(file_entry));
		if (file_arr == NULL) {
			err_show("fs.c", __LINE__-2, "file_arr memory allocation failed");
		} else {
			// Iterate over dir again, this time adding filenames to created 2D array
			while ((ndir = readdir(nd)) != NULL) {
				// Get d_name from the dir struct and copy into array
				if ((ndir->d_type == 8 && stringEndsWith(ndir->d_name, ".cia") || ndir->d_type == 4)) {
					strncpy(file_arr[count].name, ndir->d_name, MAX_DIR_NAME_SIZE);
					// If d_type is a file
					file_arr[count].isfile = (ndir->d_type == 8);
					count++;
				}
			}
			qsort(file_arr, size_of_file_array, sizeof(file_entry), compare_file_entry);
		}
	}
	closedir(d);
	closedir(nd);
}

void fs_delete_dir_recursivley_(char path_to_delete[MAX_PATH_SIZE]) {
	consoleSelect(&debug_screen);
	DIR *d;
	d = opendir(path_to_delete);
	int ret;

	if (d) {
		struct dirent *dir;
		char new_path_to_delete[MAX_PATH_SIZE];

		while ((dir = readdir(d)) != NULL) {
			strncpy(new_path_to_delete, path_to_delete, MAX_PATH_SIZE);
			strcat(new_path_to_delete, "/");
			strcat(new_path_to_delete, dir->d_name);
			if (dir->d_type == 8) {
				ret = remove(new_path_to_delete);
				if (ret)
					err_show_errno(errno, "remove");
				else
					printf("%sDeleted file: %s%s\n", FG_MAGENTA, dir->d_name, RESET);
			} else {
				fs_delete_dir_recursivley_(new_path_to_delete);
			}
		}
		closedir(d);
		ret = rmdir(path_to_delete);
		if (ret)
			err_show_errno(errno, "rmdir");
		else
			printf("%sDeleted dir %s%s\n", FG_MAGENTA, path_to_delete, RESET);
	} else {
		// Dir doesen't exist?
		closedir(d);
	}
}

void fs_delete_selected(void) {
	consoleSelect(&debug_screen);
	char filepath[MAX_PATH_SIZE];
	strncpy(filepath, current_path, MAX_PATH_SIZE);
	strcat(filepath, file_arr[selected+scroll].name);
	int ret;

	// If it is a dir
	if (!file_arr[selected + scroll].isfile) {
		ret = rmdir(filepath);
		if (!ret)
			printf("%s%s deleted%s\n", FG_MAGENTA, file_arr[selected + scroll].name, RESET);
		else
			fs_delete_dir_recursivley_(filepath);
	} else {
		ret = remove(filepath);
		if (!ret)
			printf("%s%s deleted%s\n", FG_MAGENTA, file_arr[selected + scroll].name, RESET);
		else
			err_show_errno(errno, "remove");
	}
}

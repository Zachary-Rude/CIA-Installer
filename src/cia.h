#ifndef CIA_H
#define CIA_H

char *basename(char const *path);
char *humanSize(uint64_t bytes);
static void deletePrevious(u64 titleid);
void consoleInitProgress(const char* header, const char* text, const float progress);
void consoleSetProgressData(const char* text, const double progress);
Result installCia(FS_MediaType mediaType, bool deleteWhenDone, bool selfUpdate);
Result installCiaFromFile(char filePath[MAX_PATH_SIZE], FS_MediaType mediaType, bool deleteWhenDone, bool showMessage);

#endif
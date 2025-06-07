#ifndef FS_H
#define FS_H

bool stringEndsWith(const char *str, const char *suffix);
void fs_get_ud(void);
void fs_populate_filarr(char dir_to_show[]);
void fs_delete_selected(void);

#endif

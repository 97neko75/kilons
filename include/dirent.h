#ifndef KILONS_DIRENT_H
#define KILONS_DIRENT_H
struct dirent {
    unsigned long d_ino;
    char d_name[128];
};
typedef struct directory_stream DIR;
DIR *opendir(const char *);
struct dirent *readdir(DIR *);
int closedir(DIR *);
#endif

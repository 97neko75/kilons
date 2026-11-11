#ifndef KILONS_SYS_STAT_H
#define KILONS_SYS_STAT_H
struct stat {
    unsigned long st_mode;
    long st_size;
    unsigned long st_ino;
    unsigned st_uid,st_gid;
};
#define S_ISDIR(m) (((m)&0170000)==0040000)
#define S_ISCHR(m) (((m)&0170000)==0020000)
int stat(const char *,struct stat *);
int fstat(int,struct stat *);
int chmod(const char *,unsigned);
int mkdir(const char *,unsigned);
#endif

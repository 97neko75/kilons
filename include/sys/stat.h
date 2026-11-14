#ifndef KILONS_SYS_STAT_H
#define KILONS_SYS_STAT_H
#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC
struct timespec {long tv_sec,tv_nsec;};
#endif
struct stat {
    unsigned long st_mode;
    long st_size;
    unsigned long st_ino;
    unsigned st_uid,st_gid;
    unsigned long st_dev,st_nlink;
    unsigned long st_rdev;
    long st_blksize,st_blocks;
    struct timespec st_atim,st_mtim,st_ctim;
};
#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_ISBLK(m) (((m)&0170000)==0060000)
#define S_ISSOCK(m) (((m)&0170000)==0140000)
#define S_ISLNK(m) (((m)&0170000)==0120000)
#define S_ISFIFO(m) (((m)&0170000)==0010000)
#define S_ISREG(m) (((m)&0170000)==0100000)
#define S_ISDIR(m) (((m)&0170000)==0040000)
#define S_ISCHR(m) (((m)&0170000)==0020000)
int stat(const char *,struct stat *);
int lstat(const char *,struct stat *);
int fstatat(int,const char *,struct stat *,int);
int fstat(int,struct stat *);
#define UTIME_NOW 1073741823L
#define UTIME_OMIT 1073741822L
int futimens(int,const struct timespec [2]);
int utimensat(int,const char *,const struct timespec [2],int);
unsigned umask(unsigned);
int fchmod(int,unsigned);
int fchmodat(int,const char *,unsigned,int);
int mkdirat(int,const char *,unsigned);
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXU 0700
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IRWXG 0070
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_IRWXO 0007
int chmod(const char *,unsigned);
int mkdir(const char *,unsigned);
#endif

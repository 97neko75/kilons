#ifndef KILONS_RECORD_LOCK_H
#define KILONS_RECORD_LOCK_H
#define F_OFD_GETLK 36
#define F_OFD_SETLK 37
#define F_OFD_SETLKW 38
#define F_GETLK 5
#define F_SETLK 6
#define F_SETLKW 7
#define F_RDLCK 0
#define F_WRLCK 1
#define F_UNLCK 2
/* AMD64 public ABI: off_t is long and pid_t is int. */
struct flock {
    short l_type, l_whence;
    long l_start, l_len;
    int l_pid;
};
#endif

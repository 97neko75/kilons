#ifndef KILONS_KERRNO_H
#define KILONS_KERRNO_H
/* Small Linux-numbered negative errno ABI used by the current PKS libc.
 * POSIX does not standardize numerical errno values. */
#define K_EPERM 1
#define K_ENOENT 2
#define K_ENXIO 6
#define K_EIO 5
#define K_EBADF 9
#define K_EBUSY 16
#define K_EAGAIN 11
#define K_EACCES 13
#define K_EFAULT 14
#define K_EEXIST 17
#define K_EXDEV 18
#define K_ENOTDIR 20
#define K_EISDIR 21
#define K_EINVAL 22
#define K_ENFILE 23
#define K_ENOSPC 28
#define K_ESPIPE 29
#define K_EROFS 30
#define K_ENAMETOOLONG 36
#define K_ENOTEMPTY 39
#define K_ELOOP 40
#define K_EOVERFLOW 75
#define K_EDEADLK 35
#define K_ENOLCK 37
#define K_ENOTSUP 95
#endif

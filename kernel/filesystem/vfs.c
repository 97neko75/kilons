#include <kilons/vfs.h>
#include <kilons/ramfs.h>
/* Small kernel VFS facade: /tmp/<name> maps to RAMFS. No userland yet. */
#define FD_COUNT 16
struct descriptor { int active; int backend_fd; };
static struct descriptor fds[FD_COUNT];
static const char *ram_name(const char *path) {
    if (!path || path[0]!='/' || path[1]!='t' || path[2]!='m' || path[3]!='p' || path[4]!='/') return 0;
    path+=5;
    if (!*path || (*path=='.' && (path[1]==0 || (path[1]=='.' && path[2]==0)))) return 0;
    return path;
}
static int slot(int fd) { return fd>=3 && fd<3+FD_COUNT && fds[fd-3].active; }
void vfs_init(void) { for(int i=0;i<FD_COUNT;i++) fds[i].active=0; }
int vfs_open(const char *path, unsigned flags, int create) {
    const char *name=ram_name(path);
    if(!name || !flags || (flags & ~(VFS_READ|VFS_WRITE))) return -1;
    int free_slot=-1;
    for(int i=0;i<FD_COUNT;i++) if(!fds[i].active) { free_slot=i; break; }
    if(free_slot<0) return -1;
    int backend=ramfs_open(name,flags,create);
    if(backend<0) return -1;
    fds[free_slot].backend_fd=backend; fds[free_slot].active=1;
    return free_slot+3;
}
int vfs_close(int fd) {
    if(!slot(fd)) return -1;
    int result=ramfs_close(fds[fd-3].backend_fd);
    if(result==0) fds[fd-3].active=0;
    return result;
}
int vfs_read(int fd, void *buf, size_t n) { return slot(fd)?ramfs_read(fds[fd-3].backend_fd,buf,n):-1; }
int vfs_write(int fd, const void *buf, size_t n) { return slot(fd)?ramfs_write(fds[fd-3].backend_fd,buf,n):-1; }
int vfs_seek(int fd, size_t offset) { return slot(fd)?ramfs_seek(fds[fd-3].backend_fd,offset):-1; }
int vfs_unlink(const char *path) { const char *name=ram_name(path); return name?ramfs_unlink(name):-1; }
int vfs_selftest(void) {
    static const char message[]="vfs check";
    char readback[sizeof(message)]={0};
    int fd=vfs_open("/tmp/.vfs-check",VFS_READ|VFS_WRITE,1);
    if(fd<3) return 0;
    int ok=vfs_write(fd,message,sizeof(message))==(int)sizeof(message)
        && vfs_seek(fd,0)==0
        && vfs_read(fd,readback,sizeof(readback))==(int)sizeof(readback);
    for(size_t i=0;i<sizeof(message);i++) if(readback[i]!=message[i]) ok=0;
    if(vfs_close(fd)!=0) ok=0;
    if(vfs_read(fd,readback,1)!=-1) ok=0;
    if(vfs_unlink("/tmp/.vfs-check")!=0) ok=0;
    if(vfs_open("/invalid",VFS_READ,0)!=-1) ok=0;
    return ok;
}

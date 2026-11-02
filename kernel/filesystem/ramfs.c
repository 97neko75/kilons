#include <stddef.h>
#include <stdint.h>
#include <kilons/ramfs.h>
/* Static, bounded RAM filesystem. No heap dependency and no disk I/O. */
#define NAME_CAP 31
struct file { char name[NAME_CAP+1]; uint8_t data[RAMFS_FILE_CAPACITY]; size_t size; int used; };
struct handle { int file; size_t offset; unsigned flags; int used; };
static struct file files[RAMFS_MAX_FILES];
static struct handle handles[RAMFS_MAX_OPEN];
static int equal(const char *a,const char *b) { while (*a && *b && *a==*b) { a++; b++; } return *a==*b; }
static int valid_name(const char *name) {
    if (!name || !*name) return 0;
    size_t i=0;
    while(name[i]) {
        if (i==NAME_CAP || name[i]=='/' || name[i]=='\\') return 0;
        i++;
    }
    return 1;
}
static int lookup(const char *name) {
    for(int i=0;i<RAMFS_MAX_FILES;i++) if(files[i].used && equal(files[i].name,name)) return i;
    return -1;
}
void ramfs_init(void) {
    for(int i=0;i<RAMFS_MAX_FILES;i++) { files[i].used=0; files[i].size=0; files[i].name[0]=0; }
    for(int i=0;i<RAMFS_MAX_OPEN;i++) handles[i].used=0;
}
int ramfs_open(const char *name,unsigned flags,int create) {
    if(!valid_name(name) || !(flags&(RAMFS_READ|RAMFS_WRITE)) || (flags&~(RAMFS_READ|RAMFS_WRITE))) return -1;
    int idx=lookup(name);
    if(idx<0 && create) {
        for(int i=0;i<RAMFS_MAX_FILES;i++) if(!files[i].used) { idx=i; break; }
        if(idx>=0) {
            size_t i=0; while(name[i]) { files[idx].name[i]=name[i]; i++; }
            files[idx].name[i]=0; files[idx].size=0; files[idx].used=1;
        }
    }
    if(idx<0) return -1;
    for(int fd=0;fd<RAMFS_MAX_OPEN;fd++) if(!handles[fd].used) {
        handles[fd].file=idx; handles[fd].offset=0; handles[fd].flags=flags; handles[fd].used=1;
        return fd;
    }
    return -1;
}
int ramfs_close(int fd) {
    if(fd<0 || fd>=RAMFS_MAX_OPEN || !handles[fd].used) return -1;
    handles[fd].used=0; return 0;
}
int ramfs_read(int fd,void *buffer,size_t count) {
    if(fd<0 || fd>=RAMFS_MAX_OPEN || !handles[fd].used || !(handles[fd].flags&RAMFS_READ) || (!buffer && count)) return -1;
    struct handle *h=&handles[fd]; struct file *f=&files[h->file];
    if(h->offset>f->size) return 0;
    size_t n=f->size-h->offset; if(count<n) n=count;
    if(n>2147483647U) n=2147483647U;
    for(size_t i=0;i<n;i++) ((uint8_t*)buffer)[i]=f->data[h->offset+i];
    h->offset+=n; return (int)n;
}
int ramfs_write(int fd,const void *buffer,size_t count) {
    if(fd<0 || fd>=RAMFS_MAX_OPEN || !handles[fd].used || !(handles[fd].flags&RAMFS_WRITE) || (!buffer && count)) return -1;
    struct handle *h=&handles[fd]; struct file *f=&files[h->file];
    if(h->offset>RAMFS_FILE_CAPACITY) return -1;
    size_t n=RAMFS_FILE_CAPACITY-h->offset; if(count<n) n=count;
    for(size_t i=0;i<n;i++) f->data[h->offset+i]=((const uint8_t*)buffer)[i];
    h->offset+=n; if(h->offset>f->size) f->size=h->offset;
    return (int)n;
}
int ramfs_seek(int fd,size_t offset) {
    if(fd<0 || fd>=RAMFS_MAX_OPEN || !handles[fd].used || offset>RAMFS_FILE_CAPACITY) return -1;
    handles[fd].offset=offset; return 0;
}
int ramfs_unlink(const char *name) {
    if(!valid_name(name)) return -1;
    int idx=lookup(name); if(idx<0) return -1;
    for(int i=0;i<RAMFS_MAX_OPEN;i++) if(handles[i].used && handles[i].file==idx) return -1;
    files[idx].used=0; files[idx].size=0; return 0;
}
size_t ramfs_size(const char *name) { int idx=valid_name(name)?lookup(name):-1; return idx<0 ? (size_t)-1 : files[idx].size; }
unsigned ramfs_file_count(void) { unsigned n=0; for(int i=0;i<RAMFS_MAX_FILES;i++) if(files[i].used) n++; return n; }
int ramfs_selftest(void) {
    static const char data[]="kilons RAMFS test";
    char out[sizeof(data)]={0};
    int fd=ramfs_open(".ramfs-test",RAMFS_READ|RAMFS_WRITE,1);
    if(fd<0) return 0;
    int ok=ramfs_write(fd,data,sizeof(data))==(int)sizeof(data)
        && ramfs_seek(fd,0)==0
        && ramfs_read(fd,out,sizeof(out))==(int)sizeof(out);
    for(size_t i=0;i<sizeof(data);i++) if(out[i]!=data[i]) ok=0;
    if(ramfs_close(fd)<0 || ramfs_unlink(".ramfs-test")<0) ok=0;
    return ok;
}

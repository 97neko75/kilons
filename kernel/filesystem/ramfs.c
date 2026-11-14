#include <stddef.h>
#include <stdint.h>
#include <kilons/ramfs.h>
#include <kilons/kerrno.h>
/* Static, bounded RAM filesystem. No heap dependency and no disk I/O. */
#define NAME_CAP 127
#define DIR_CAP 16
struct file { uint8_t data[RAMFS_FILE_CAPACITY]; size_t size; int used; unsigned links; unsigned opens; unsigned mode; uint32_t uid,gid; uint64_t ino; struct file_times times; };
struct directory { char name[NAME_CAP+1]; int used; unsigned mode; uint32_t uid,gid; uint64_t ino; struct file_times times; };
static uint32_t current_uid,current_gid;
static struct file_times root_times;
static unsigned root_uid,root_gid,root_mode;
static uint64_t next_ino=2;
void ramfs_set_identity(uint32_t uid,uint32_t gid) {current_uid=uid;current_gid=gid;}
static struct directory dirs[DIR_CAP];
struct handle { int file; size_t offset; unsigned flags; int used; };
#define ENTRY_CAP (RAMFS_MAX_FILES * 2)
struct file_entry { char name[NAME_CAP+1]; int file; int used; };
static struct file_entry entries[ENTRY_CAP];
static struct file files[RAMFS_MAX_FILES];
static struct handle handles[RAMFS_MAX_OPEN];
static int equal(const char *,const char *);
static int valid_name(const char *);
static int lookup(const char *);
static int prefix_path(const char *path,const char *prefix) {
    size_t n=0; while(prefix[n] && path[n]==prefix[n]) n++;
    return !prefix[n] && (path[n]==0 || path[n]=='/');
}
static int dir_exists(const char *name) {
    if (!*name) return 1;
    for (int i=0;i<DIR_CAP;i++) if (dirs[i].used && equal(dirs[i].name,name)) return 1;
    return 0;
}
static int parent_exists(const char *name) {
    char parent[NAME_CAP+1]; size_t n=0, last=0;
    while(name[n]) { if(name[n]=='/') last=n+1; n++; }
    if (!last) return 1;
    for(size_t i=0;i<last-1;i++) parent[i]=name[i];
    parent[last-1]=0; return dir_exists(parent);
}
static void touch_parent(const char *name) {
    const char *last=NULL;
    for(const char *s=name;*s;s++)if(*s=='/')last=s;
    struct file_times *times=&root_times;
    if(last) {
        size_t length=(size_t)(last-name);
        for(int i=0;i<DIR_CAP;i++) {
            if(!dirs[i].used)continue;
            size_t n=0;
            while(n<length && dirs[i].name[n]==name[n])n++;
            if(n==length && !dirs[i].name[n]){times=&dirs[i].times;break;}
        }
    }
    times->modify=times->change=file_now();
}
static int dir_nonempty(const char *name) {
    size_t n=0; while(name[n]) n++;
    for(int i=0;i<DIR_CAP;i++) if(dirs[i].used && !equal(dirs[i].name,name)) {
        size_t j=0; while(j<n && dirs[i].name[j]==name[j]) j++;
        if(j==n && dirs[i].name[j]=='/') return 1;
    }
    for(int i=0;i<ENTRY_CAP;i++) if(entries[i].used) {
        size_t j=0; while(j<n && entries[i].name[j]==name[j]) j++;
        if(j==n && entries[i].name[j]=='/') return 1;
    }
    return 0;
}
int ramfs_mkdir(const char *name) {
    return ramfs_mkdir_mode(name,0755U);
}
int ramfs_mkdir_mode(const char *name,unsigned mode) {
    if(!valid_name(name) || dir_exists(name) || lookup(name)>=0 || !parent_exists(name)) return -1;
    for(int i=0;i<DIR_CAP;i++) if(!dirs[i].used) {
        size_t j=0; while(name[j]) { dirs[i].name[j]=name[j]; j++; }
        dirs[i].name[j]=0; dirs[i].used=1; dirs[i].mode=mode&07777U; dirs[i].uid=current_uid; dirs[i].gid=current_gid; dirs[i].ino=next_ino++; dirs[i].times=file_created(); touch_parent(name); return 0;
    }
    return -1;
}
int ramfs_rmdir(const char *name) {
    if(!valid_name(name) || dir_nonempty(name)) return -1;
    for(int i=0;i<DIR_CAP;i++) if(dirs[i].used && equal(dirs[i].name,name)) { dirs[i].used=0; touch_parent(name); return 0; }
    return -1;
}

static int equal(const char *a,const char *b) { while (*a && *b && *a==*b) { a++; b++; } return *a==*b; }
static int valid_name(const char *name) {
    if (!name || !*name) return 0;
    if (name[0]=='/') return 0;
    size_t i=0;
    while(name[i]) {
        if (i==NAME_CAP || (name[i]=='/' && (i==0 || name[i+1]==0 || name[i-1]=='/'))) return 0;
        i++;
    }
    return 1;
}
static int find_entry(const char *name) {
    for(int i=0;i<ENTRY_CAP;i++)
        if(entries[i].used && equal(entries[i].name,name))return i;
    return -1;
}
static int lookup(const char *name) {
    int i=find_entry(name);
    return i<0?-1:entries[i].file;
}
static int add_entry(const char *name,int file) {
    for(int i=0;i<ENTRY_CAP;i++) {
        if(entries[i].used)continue;
        size_t j=0;
        do { entries[i].name[j]=name[j]; } while(name[j++]);
        entries[i].file=file;
        entries[i].used=1;
        touch_parent(name);
        files[file].links++;
        files[file].times.change=file_now();
        return 0;
    }
    return -K_ENOSPC;
}
int ramfs_link(const char *old,const char *name) {
    if(!old || !valid_name(name))return -K_EFAULT;
    if(!parent_exists(name))return -K_ENOENT;
    if(dir_exists(old))return -K_EPERM;
    int file=lookup(old);
    if(file<0)return -K_ENOENT;
    if(dir_exists(name) || lookup(name)>=0)return -K_EEXIST;
    return add_entry(name,file);
}
void ramfs_init(void) {
    for(int i=0;i<ENTRY_CAP;i++)entries[i].used=0;
    root_times=file_created();root_uid=root_gid=0;root_mode=0755;
    current_uid=0;current_gid=0;next_ino=2;
    for(int i=0;i<RAMFS_MAX_FILES;i++) { files[i].used=0; files[i].size=0; files[i].links=0; files[i].opens=0; }
    for(int i=0;i<RAMFS_MAX_OPEN;i++) handles[i].used=0;
    for(int i=0;i<DIR_CAP;i++) dirs[i].used=0;
    (void)ramfs_mkdir("tmp");
    (void)ramfs_mkdir("dev");
    (void)ramfs_mkdir("mnt");
    (void)ramfs_chmod("tmp",01777U);
}
int ramfs_open(const char *name,unsigned flags,int create) {
    return ramfs_open_mode(name,flags,create,0644U);
}
int ramfs_open_mode(const char *name,unsigned flags,int create,unsigned mode) {
    if(!valid_name(name) || !parent_exists(name) || dir_exists(name) || !(flags&(RAMFS_READ|RAMFS_WRITE)) || (flags&~(RAMFS_READ|RAMFS_WRITE))) return -1;
    int idx=lookup(name);
    if(idx<0 && create) {
        int available=0; for(int fd=0;fd<RAMFS_MAX_OPEN;fd++) if(!handles[fd].used) {available=1;break;}
        if(!available) return -1;
        for(int i=0;i<RAMFS_MAX_FILES;i++) if(!files[i].used) { idx=i; break; }
        if(idx>=0) {
            struct file *file=&files[idx];
            file->size=0; file->used=1; file->links=0; file->opens=0;
            file->mode=mode&07777U; file->uid=current_uid;file->gid=current_gid;
            file->ino=next_ino; file->times=file_created();
            int rc=add_entry(name,idx);
            if(rc<0) {
                file->used=0;file->size=0;file->links=0;file->opens=0;
                return rc;
            }
            next_ino++;
        }
    }
    if(idx<0) return -1;
    for(int fd=0;fd<RAMFS_MAX_OPEN;fd++) if(!handles[fd].used) {
        handles[fd].file=idx; handles[fd].offset=0; handles[fd].flags=flags; handles[fd].used=1; files[idx].opens++;
        return fd;
    }
    return -1;
}
int ramfs_symlink(const char *target,const char *name) {
    if(!target)return -K_EFAULT;
    if(!valid_name(name))return -K_ENAMETOOLONG;
    if(dir_exists(name) || lookup(name)>=0)return -K_EEXIST;
    size_t n=0;
    while(target[n])n++;
    if(!n)return -K_ENOENT;
    if(n>=256)return -K_ENAMETOOLONG;
    int fd=ramfs_open_mode(name,RAMFS_WRITE,1,0777);
    if(fd<0)return -K_ENOSPC;
    struct file *link=&files[handles[fd].file];
    for(size_t i=0;i<n;i++)link->data[i]=(uint8_t)target[i];
    link->size=n;
    link->mode=0120777;
    return ramfs_close(fd);
}
int ramfs_readlink(const char *name,char *buf,size_t cap) {
    int idx=lookup(name);
    if(idx<0)return -K_ENOENT;
    const struct file *link=&files[idx];
    if((link->mode&0170000)!=0120000)return -K_EINVAL;
    if(!buf&&cap)return -K_EFAULT;
    size_t n=link->size<cap?link->size:cap;
    for(size_t i=0;i<n;i++)buf[i]=(char)link->data[i];
    return (int)n;
}
int ramfs_close(int fd) {
    if(fd<0 || fd>=RAMFS_MAX_OPEN || !handles[fd].used) return -1;
    int idx=handles[fd].file; handles[fd].used=0;
    if(files[idx].opens) files[idx].opens--;
    if(!files[idx].opens && !files[idx].links) { files[idx].used=0; files[idx].size=0; }
    return 0;
}
int ramfs_read(int fd,void *buffer,size_t count) {
    if(fd<0 || fd>=RAMFS_MAX_OPEN || !handles[fd].used || !(handles[fd].flags&RAMFS_READ) || (!buffer && count)) return -1;
    struct handle *h=&handles[fd]; struct file *f=&files[h->file];
    if(count)f->times.access=file_now();
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
    if(n) {
        if(current_uid)f->mode&=~06000U;
        f->times.modify=f->times.change=file_now();
        for(size_t i=f->size;i<h->offset;i++) f->data[i]=0;
        for(size_t i=0;i<n;i++) f->data[h->offset+i]=((const uint8_t*)buffer)[i];
    }
    h->offset+=n; if(n && h->offset>f->size) f->size=h->offset;
    return (int)n;
}
int ramfs_seek(int fd,size_t offset) {
    if(fd<0 || fd>=RAMFS_MAX_OPEN || !handles[fd].used) return -1;
    handles[fd].offset=offset; return 0;
}
int ramfs_unlink(const char *name) {
    if(!valid_name(name)) return -1;
    int idx=lookup(name); if(idx<0) return -1;
    entries[find_entry(name)].used=0;
    touch_parent(name);
    files[idx].links--;
    files[idx].times.change=file_now();
    if(!files[idx].opens && !files[idx].links) { files[idx].used=0; files[idx].size=0; }
    return 0;
}
/* Size changes preserve the inode and all existing open handles. */
static int resize_file(int idx, size_t size) {
    if(idx<0 || size>RAMFS_FILE_CAPACITY) return -1;
    struct file *f=&files[idx];
    if(size>f->size) for(size_t i=f->size;i<size;i++) f->data[i]=0;
    if(current_uid)f->mode&=~06000U;
    f->size=size; f->times.modify=f->times.change=file_now();
    return 0;
}
int ramfs_truncate(const char *name, size_t size) {
    if(!valid_name(name)) return -1;
    return resize_file(lookup(name),size);
}
int ramfs_ftruncate(int fd, size_t size) {
    if(fd<0 || fd>=RAMFS_MAX_OPEN || !handles[fd].used ||
       !(handles[fd].flags&RAMFS_WRITE)) return -1;
    return resize_file(handles[fd].file,size);
}
/* Rename a regular file or directory subtree in one RAMFS instance. */
int ramfs_rename(const char *oldname, const char *newname) {
    if(!valid_name(oldname) || !valid_name(newname) || !parent_exists(newname)) return -1;
    if(equal(oldname,newname)) return lookup(oldname)>=0 || dir_exists(oldname) ? 0 : -1;
    int file=lookup(oldname);
    int directory=file<0 && dir_exists(oldname);
    if(file<0 && !directory) return -1;
    if(directory && prefix_path(newname,oldname)) return -K_EINVAL;
    int dest_dir=dir_exists(newname);
    int dest_file=lookup(newname);
    if(file>=0 && file==dest_file)return 0;
    if(dest_dir && !directory)return -K_EISDIR;
    if(dest_file>=0 && directory)return -K_ENOTDIR;
    if(dest_dir && dir_nonempty(newname))return -K_ENOTEMPTY;
    size_t oldlen=0,newlen=0;
    while(oldname[oldlen]) oldlen++;
    while(newname[newlen]) newlen++;
    /* Preflight every descendant before mutating anything. */
    if(directory) {
        for(int i=0;i<DIR_CAP;i++) if(dirs[i].used && prefix_path(dirs[i].name,oldname)) {
            size_t tail=0;while(dirs[i].name[oldlen+tail])tail++;
            if(newlen+tail>NAME_CAP) return -K_ENAMETOOLONG;
        }
        for(int i=0;i<ENTRY_CAP;i++) if(entries[i].used && prefix_path(entries[i].name,oldname)) {
            size_t tail=0;while(entries[i].name[oldlen+tail])tail++;
            if(newlen+tail>NAME_CAP) return -K_ENAMETOOLONG;
        }
    }
    /* All fallible checks precede replacement. Unlinked open files retain data. */
    if(dest_file>=0 && ramfs_unlink(newname)<0)return -1;
    if(dest_dir && ramfs_rmdir(newname)<0)return -1;
    touch_parent(oldname);
    touch_parent(newname);
    if(file>=0) {
        files[file].times.change=file_now();
        int entry=find_entry(oldname);
        for(size_t i=0;i<=newlen;i++) entries[entry].name[i]=newname[i];
        return 0;
    }
    for(int i=0;i<DIR_CAP;i++) if(dirs[i].used && prefix_path(dirs[i].name,oldname)) {
        if(equal(dirs[i].name,oldname))dirs[i].times.change=file_now();
        char next[NAME_CAP+1];size_t j=0;
        for(;j<newlen;j++) next[j]=newname[j];
        size_t k=oldlen;while(dirs[i].name[k]) next[j++]=dirs[i].name[k++];next[j]=0;
        for(k=0;k<=j;k++) dirs[i].name[k]=next[k];
    }
    for(int i=0;i<ENTRY_CAP;i++) if(entries[i].used && prefix_path(entries[i].name,oldname)) {
        char next[NAME_CAP+1];size_t j=0;
        for(;j<newlen;j++) next[j]=newname[j];
        size_t k=oldlen;while(entries[i].name[k]) next[j++]=entries[i].name[k++];next[j]=0;
        for(k=0;k<=j;k++) entries[i].name[k]=next[k];
    }
    return 0;
}
/* Directory enumeration uses an index, not a persistent backend handle. */
int ramfs_stat(const char *name, struct ramfs_info *info) {
    if (!name || !info) return -1;
    if (!*name || dir_exists(name)) {
        *info=(struct ramfs_info){0};
        info->times=root_times;info->is_dir=1; info->links=1;info->size=0; info->mode=root_mode;info->uid=root_uid;info->gid=root_gid;info->ino=1;
        for(int i=0;i<DIR_CAP;i++)if(dirs[i].used && equal(dirs[i].name,name)) {info->mode=dirs[i].mode;info->uid=dirs[i].uid;info->gid=dirs[i].gid;info->ino=dirs[i].ino;info->times=dirs[i].times;break;}
        return 0;
    }
    int idx=lookup(name);
    if (idx<0) return -1;
    info->is_dir=0; info->size=files[idx].size;info->mode=files[idx].mode;info->uid=files[idx].uid;info->gid=files[idx].gid;info->ino=files[idx].ino;info->links=files[idx].links;info->times=files[idx].times;info->blocks=(files[idx].size+511)/512;return 0;
}
static int child_name(const char *parent, const char *candidate, char *out, size_t cap) {
    size_t prefix=0;
    while(parent[prefix]) prefix++;
    for(size_t i=0;i<prefix;i++) if(!candidate[i] || candidate[i]!=parent[i]) return 0;
    if(prefix) { if(candidate[prefix]!='/') return 0; prefix++; }
    const char *child=candidate+prefix;
    if(!*child) return 0;
    size_t len=0;
    while(child[len]) { if(child[len]=='/') return 0; len++; }
    if(len>=cap) return 0;
    for(size_t i=0;i<len;i++) out[i]=child[i];
    out[len]=0; return 1;
}
int ramfs_readdir(const char *name, unsigned index, struct ramfs_dirent *entry) {
    if(!name || !entry || !dir_exists(name)) return -1;
    unsigned seen=0;
    for(int i=0;i<DIR_CAP;i++) if(dirs[i].used) {
        if(child_name(name,dirs[i].name,entry->name,sizeof(entry->name))) {
            if(seen++==index) { entry->is_dir=1; return 1; }
        }
    }
    for(int i=0;i<ENTRY_CAP;i++) if(entries[i].used) {
        if(child_name(name,entries[i].name,entry->name,sizeof(entry->name))) {
            if(seen++==index) { entry->is_dir=0; return 1; }
        }
    }
    return 0;
}
size_t ramfs_size(const char *name) { int idx=valid_name(name)?lookup(name):-1; return idx<0 ? (size_t)-1 : files[idx].size; }
unsigned ramfs_file_count(void) { unsigned n=0; for(int i=0;i<ENTRY_CAP;i++) if(entries[i].used) n++; return n; }
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

int ramfs_fstat(int fd,struct ramfs_info *out) {
    if(!out || fd<0 || fd>=RAMFS_MAX_OPEN || !handles[fd].used)return -1;
    struct file *f=&files[handles[fd].file];out->is_dir=0;out->size=f->size;out->mode=f->mode;out->uid=f->uid;out->gid=f->gid;out->ino=f->ino;out->links=f->links;out->times=f->times;out->blocks=(f->size+511)/512;return 0;
}

/* Only the permission bits are mutable in this early RAMFS.
 * VFS owns owner checks and cache invalidation. */
int ramfs_fchmod(int fd,unsigned mode) {
    if(fd<0 || fd>=RAMFS_MAX_OPEN || !handles[fd].used)return -K_EBADF;
    files[handles[fd].file].mode=mode&07777U; files[handles[fd].file].times.change=file_now();
    return 0;
}

int ramfs_update_metadata(const char *path,int fd,const struct inode_update *update) {
    struct file *file=NULL;
    struct directory *dir=NULL;
    if(!update)return -K_EFAULT;
    if(path) {
        int idx=lookup(path);
        if(idx>=0)file=&files[idx];
        else if(*path) {
            for(int i=0;i<DIR_CAP;i++)
                if(dirs[i].used && equal(dirs[i].name,path)){dir=&dirs[i];break;}
            if(!dir)return -K_ENOENT;
        }
    } else {
        if(fd<0 || fd>=RAMFS_MAX_OPEN || !handles[fd].used)return -K_EBADF;
        file=&files[handles[fd].file];
    }
    struct file_times *times=file?&file->times:dir?&dir->times:&root_times;
    if(update->ownership) {
        if(file){file->uid=update->uid;file->gid=update->gid;file->mode&=~06000U;}
        else if(dir){dir->uid=update->uid;dir->gid=update->gid;}
        else {root_uid=update->uid;root_gid=update->gid;}
    }
    change_times(times,update);
    return 0;
}
int ramfs_chmod(const char *name,unsigned mode) {
    if(!name || mode>07777U)return -1;
    if(!*name){root_mode=mode;root_times.change=file_now();return 0;}
    int idx=lookup(name);
    if(idx>=0) { files[idx].mode=mode;files[idx].times.change=file_now();return 0; }
    for(int i=0;i<DIR_CAP;i++) if(dirs[i].used && equal(dirs[i].name,name)) {dirs[i].mode=mode;dirs[i].times.change=file_now();return 0;}
    return -1;
}

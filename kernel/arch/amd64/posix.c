#include <stdint.h>
#include <stddef.h>
#include <kilons/vfs.h>
#include "../../../include/fcntl_flags.h"
#include <kilons/record_lock.h>
#include "../../../include/sys/stat.h"
#include <kilons/console.h>
#include <kilons/posix.h>
#include <kilons/vm.h>
#include <kilons/keyboard.h>
#include <kilons/ring3.h>
#include <kilons/power.h>
#include <kilons/process.h>
#include <kilons/memory.h>
#include <kilons/interrupt.h>
#include <kilons/kerrno.h>
/* Prototype int 0x80 ABI. POSIX specifies semantics, not syscall numbers. */
#define ENOSYS 38
#define EFAULT 14
#define EBADF 9
#define EINVAL 22
#define EMFILE 24
#define ENOENT 2
#define EACCES 13
#define ENAMETOOLONG 36
#define EAGAIN 11
#define FD_MAX 24
#define PIPE_SLOTS 4
#define PIPE_CAPACITY 1024U
#define EPIPE 32
#define ESPIPE 29
#define TASK_IO_SLOTS 12U
struct task_fd { int backend; unsigned mode; int used; };
struct task_io { uint32_t pid,uid,gid,ruid,rgid,suid,sgid; int cwd_fd; unsigned mask; struct task_fd fd[FD_MAX]; unsigned fd_flags[FD_MAX]; };
static struct task_io io[TASK_IO_SLOTS];
static struct task_fd *task_fds;
static int task_active;
static uint32_t task_pid;
struct kernel_pipe {
    unsigned used,readers,writers,head,count;
    unsigned status[2];
    char data[PIPE_CAPACITY];
};
static struct kernel_pipe pipes[PIPE_SLOTS];
static int pipe_id(int backend) {
    int off=-100-backend;
    return off>=0 && off<PIPE_SLOTS*2?off/2:-1;
}
static bool pipe_write_end(int backend) {return ((-100-backend)&1)!=0;}
static void pipe_retain(int backend) {
    int i=pipe_id(backend);
    if(i<0)return;
    if(pipe_write_end(backend))pipes[i].writers++;
    else pipes[i].readers++;
}
static void pipe_release(int backend) {
    int i=pipe_id(backend);
    if(i<0)return;
    if(pipe_write_end(backend)) {if(pipes[i].writers)pipes[i].writers--;}
    else if(pipes[i].readers)pipes[i].readers--;
    if(!pipes[i].readers && !pipes[i].writers)pipes[i].used=0;
}
static int pipe_read_data(int backend,char *buffer,unsigned size) {
    int i=pipe_id(backend);
    if(i<0 || pipe_write_end(backend) || !pipes[i].used || !pipes[i].readers)return -EBADF;
    struct kernel_pipe *p=&pipes[i];
    if(!p->count)return p->writers?-EAGAIN:0;
    unsigned n=size<p->count?size:p->count;
    for(unsigned j=0;j<n;j++)buffer[j]=p->data[(p->head+j)%PIPE_CAPACITY];
    p->head=(p->head+n)%PIPE_CAPACITY;p->count-=n;
    return (int)n;
}
static int pipe_write_data(int backend,const char *buffer,unsigned size,bool atomic) {
    int i=pipe_id(backend);
    if(i<0 || !pipe_write_end(backend) || !pipes[i].used || !pipes[i].writers)return -EBADF;
    struct kernel_pipe *p=&pipes[i];
    if(!p->readers)return -EPIPE;
    /* POSIX PIPE_BUF atomicity for the supported nonblocking pipe mode:
     * writes of at most PIPE_CAPACITY bytes must never partially commit.
     * Larger writes may make partial progress when capacity permits. */
    unsigned room=PIPE_CAPACITY-p->count;
    if(!room || (atomic && size>room))return -EAGAIN;
    unsigned n=size<room?size:room;
    for(unsigned j=0;j<n;j++)p->data[(p->head+p->count+j)%PIPE_CAPACITY]=buffer[j];
    p->count+=n;return (int)n;
}

static unsigned terminal_flags[3];
static const char *terminal_sequence="";
static struct task_io *active_io;
static struct task_io *lookup_io(uint32_t pid) {
    for(unsigned i=0;i<TASK_IO_SLOTS;i++)if(io[i].pid==pid && pid)return &io[i];
    return NULL;
}
/* Called with the process blocked or at the syscall boundary. Endpoint
 * flags belong to the open description, including all dup/fork aliases. */
bool posix_blocking_pipe_writer(uint32_t pid,uint64_t fd) {
    struct task_io *task=lookup_io(pid);
    if(!task || fd>=FD_MAX || !task->fd[fd].used)return false;
    int backend=task->fd[fd].backend,id=pipe_id(backend);
    return id>=0 && pipe_write_end(backend) && !(pipes[id].status[1]&O_NONBLOCK);
}
bool posix_io_should_wait(uint32_t pid,uint64_t fd,bool write,uint64_t bytes){
    struct task_io *task=lookup_io(pid);
    if(!task || fd>=FD_MAX || !task->fd[fd].used || !bytes)return false;
    int backend=task->fd[fd].backend,id=pipe_id(backend);
    if(!write && backend==-1)return !(terminal_flags[0]&O_NONBLOCK) && !*terminal_sequence && !keyboard_available();
    if(id<0 || pipe_write_end(backend)!=write)return false;
    struct kernel_pipe *p=&pipes[id];
    if(p->status[write]&O_NONBLOCK)return false;
    if(!write)return !p->count && p->writers;
    unsigned required=bytes<=PIPE_CAPACITY?(unsigned)bytes:1;
    return p->readers && PIPE_CAPACITY-p->count<required;
}
static const char *cwd_path(void) {
    return active_io->cwd_fd<0?"/":vfs_directory_path(active_io->cwd_fd);
}
int posix_open_exec(const char *path) {
    if(!active_io || !*path)return -ENOENT;
    struct vfs_base base={0};
    if(*path!='/') {
        base.dir=cwd_path();
        if(!base.dir)return -ENOENT;
    }
    int fd=vfs_open_mode_from(base,path,VFS_READ|VFS_EXEC,0,0);
    return fd<0?-vfs_errno():fd;
}

/* A duplicated descriptor shares its backend and current file offset. */
static int fd_release(unsigned fd) {
    if(fd>=FD_MAX || !task_fds[fd].used) return -EBADF;
    int backend=task_fds[fd].backend;
    struct lock_file file;
    int locked=backend>=0 && vfs_lock_file(backend,&file);
    int last=locked && vfs_last_reference(backend);
    if(backend>=0) {
        int rc=vfs_close(backend);
        if(rc<0){int err=vfs_errno();return -(err?err:EBADF);}
        if(locked){record_close(task_pid,file);if(last)record_close(-backend,file);}
    } else if(pipe_id(backend)>=0)pipe_release(backend);
    task_fds[fd].used=0;
    if(active_io)active_io->fd_flags[fd]=0;
    return 0;
}
/* Call after validating the complete destination. User buffers may be unaligned. */
static void store_user(uint64_t address,const void *value,size_t size) {
    unsigned char *dst=(unsigned char *)(uintptr_t)address;
    const unsigned char *src=value;
    for(size_t i=0;i<size;i++)dst[i]=src[i];
}
/* The keyboard driver reports local key events. The terminal delivers CSI
 * byte sequences, including across separate one-byte reads. */
static char terminal_byte(void){
    if(*terminal_sequence)return *terminal_sequence++;
    unsigned ch=(unsigned char)keyboard_poll();
    static const char *const tail[]={"[A","[B","[D","[C","[H","[F","[3~"};
    if(ch<17 || ch>23)return (char)ch;
    terminal_sequence=tail[ch-17];return 27;
}
static int user_range(uint64_t p,uint64_t n,int write) {
    return vm_user_check(p,n,write);
}
/* Returns 0 on success, negative errno on invalid or unterminated paths. */
static int user_string(uint64_t p,char *dst,size_t cap) {
    if(!user_range(p,1,0)) return -EFAULT;
    for(size_t i=0;i<cap;i++) {
        if(p>UINT64_MAX-i)return -EFAULT;
        if(!user_range(p+i,1,0)) return -EFAULT;
        dst[i]=((const char *)(uintptr_t)p)[i];
        if(!dst[i]) return 0;
    }
    return -ENAMETOOLONG;
}
void posix_task_end(void) {
    if(!task_active)return;
    record_exit(task_pid);
    if(active_io->cwd_fd>=0)(void)vfs_close(active_io->cwd_fd);
    for(unsigned i=0;i<FD_MAX;i++) if(task_fds[i].used)(void)fd_release(i);
    for(unsigned i=0;i<TASK_IO_SLOTS;i++) if(io[i].pid==task_pid) {
        io[i].pid=0;break;
    }
    task_active=0;task_fds=NULL;task_pid=0;active_io=NULL;vfs_set_credentials(0,0);
}
void posix_task_activate(uint32_t pid) {
    task_active=0;task_fds=NULL;task_pid=0;active_io=NULL;
    vfs_set_credentials(0,0);
    for(unsigned i=0;i<TASK_IO_SLOTS;i++) if(io[i].pid==pid && pid) {
        task_fds=io[i].fd;task_pid=pid;task_active=1;active_io=&io[i];
        vfs_set_credentials(io[i].uid,io[i].gid);return;
    }
}
bool posix_task_begin(uint32_t pid) {
    if(!pid || lookup_io(pid))return false;
    struct task_io *slot=NULL;
    for(unsigned i=0;i<TASK_IO_SLOTS;i++)if(!io[i].pid) {
        slot=&io[i];break;
    }
    /* A silent failure would run a user task without credentials or a valid
     * descriptor table. The launcher must treat this as a spawn failure. */
    if(!slot){posix_task_activate(0);return false;}
    slot->pid=pid;
    posix_task_activate(pid);
    if(!task_active){slot->pid=0;return false;}
    struct task_io *parent=lookup_io(process_parent_of(pid));
    slot->cwd_fd=parent?parent->cwd_fd:-1;
    if(slot->cwd_fd>=0 && vfs_retain(slot->cwd_fd)<0) {
        slot->pid=0;posix_task_activate(0);return false;
    }
    slot->uid=parent?parent->uid:1000;
    slot->gid=parent?parent->gid:1000;
    slot->ruid=parent?parent->ruid:slot->uid;
    slot->rgid=parent?parent->rgid:slot->gid;
    slot->suid=slot->uid;slot->sgid=slot->gid;
    slot->mask=parent?parent->mask:0022;
    for(int i=0;i<FD_MAX;i++) {task_fds[i].used=0;task_fds[i].mode=0;task_fds[i].backend=-1;active_io->fd_flags[i]=0;}
    /* Standard terminal descriptors are task-local. */
    task_fds[0].used=1;task_fds[0].mode=VFS_READ;task_fds[0].backend=-1;
    task_fds[1].used=1;task_fds[1].mode=VFS_WRITE;task_fds[1].backend=-2;
    task_fds[2].used=1;task_fds[2].mode=VFS_WRITE;task_fds[2].backend=-3;
    task_pid=pid;
    task_active=1;vfs_set_credentials(active_io->uid,active_io->gid);
    return true;
}

/* POSIX close-on-exec affects only the calling process' descriptor table.
 * Call only after exec image validation and successful replacement. */
unsigned posix_saved_uid(void){return active_io->suid;}
unsigned posix_effective_uid(void){return active_io->uid;}
unsigned posix_real_uid(void) {return active_io->ruid;}
void posix_exec_ids(unsigned mode,unsigned uid,unsigned gid) {
    if(mode&04000)active_io->uid=uid;
    if(mode&02000)active_io->gid=gid;
    active_io->suid=active_io->uid;
    active_io->sgid=active_io->gid;
    vfs_set_credentials(active_io->uid,active_io->gid);
}
static int set_identity(int group,int effective,unsigned value) {
    uint32_t *real=group?&active_io->rgid:&active_io->ruid;
    uint32_t *current=group?&active_io->gid:&active_io->uid;
    uint32_t *saved=group?&active_io->sgid:&active_io->suid;
    if(value==UINT32_MAX)return -EINVAL;
    if(active_io->uid==0) {
        if(!effective)*real=*saved=value;
    } else if(value!=*real && value!=*saved && !(effective && value==*current))return -K_EPERM;
    *current=value;
    vfs_set_credentials(active_io->uid,active_io->gid);
    return 0;
}
void posix_task_exec_close(uint32_t pid){
    struct task_io *entry=lookup_io(pid);
    if(!entry)return;
    for(unsigned i=0;i<FD_MAX;i++)if((entry->fd_flags[i]&FD_CLOEXEC) && entry->fd[i].used){
        /* Successful exec always operates on the currently active task. */
        if(entry==active_io)(void)fd_release(i);
    }
}
/* Clone all open descriptions with independent process-local descriptor numbers.
 * Partial failure rolls back every extra VFS reference. */
bool posix_task_clone(uint32_t parent,uint32_t child) {
    struct task_io *src=lookup_io(parent);
    if(!src || lookup_io(child) || !child)return false;
    struct task_io *dst=NULL;
    for(unsigned i=0;i<TASK_IO_SLOTS;i++)if(!io[i].pid){dst=&io[i];break;}
    if(!dst)return false;
    if(src->cwd_fd>=0 && vfs_retain(src->cwd_fd)<0)return false;
    *dst=*src;dst->pid=child;
    for(int i=0;i<FD_MAX;i++)if(dst->fd[i].used) {
        if(dst->fd_flags[i]&FD_CLOFORK) {
            dst->fd[i].used=0;
            dst->fd_flags[i]=0;
            continue;
        }
        int backend=dst->fd[i].backend;
        if(backend>=0 && vfs_retain(backend)<0) {
            for(int j=0;j<i;j++)if(dst->fd[j].used) {
                int prior=dst->fd[j].backend;
                if(prior>=0)(void)vfs_close(prior);
                else if(pipe_id(prior)>=0)pipe_release(prior);
            }
            if(dst->cwd_fd>=0)(void)vfs_close(dst->cwd_fd);
            dst->pid=0;return false;
        }
        if(pipe_id(backend)>=0)pipe_retain(backend);
    }
    return true;
}
void posix_task_discard(uint32_t pid) {
    record_exit(pid);
    struct task_io *entry=lookup_io(pid);
    if(!entry)return;
    for(int i=0;i<FD_MAX;i++)if(entry->fd[i].used) {
        int backend=entry->fd[i].backend;
        struct lock_file file;
        int locked=backend>=0 && vfs_lock_file(backend,&file);
        int last=locked && vfs_last_reference(backend);
        if(backend>=0)(void)vfs_close(backend);
        else if(pipe_id(backend)>=0)pipe_release(backend);
        if(locked){record_close(pid,file);if(last)record_close(-backend,file);}
        entry->fd[i].used=0;
    }
    if(entry->cwd_fd>=0)(void)vfs_close(entry->cwd_fd);
    entry->pid=0;
    if(task_pid==pid) {
        task_active=0;task_fds=NULL;task_pid=0;active_io=NULL;
        vfs_set_credentials(0,0);
    }
}
static int valid_fd(uint64_t fd) {return fd<FD_MAX && task_fds[fd].used;}
int posix_exec_backend(int fd) {
    if(!valid_fd(fd))return -EBADF;
    return task_fds[fd].backend<0?-EACCES:task_fds[fd].backend;
}
/* Previous executables passed zero and allocated only the original prefix. */
static size_t stat_size(uint64_t requested) {
    if(!requested)return offsetof(struct stat,st_dev);
    if(requested==offsetof(struct stat,st_rdev))return requested;
    return requested==sizeof(struct stat)?sizeof(struct stat):0;
}
static int copy_stat(uint64_t out,const struct vfs_info *info,unsigned type,size_t bytes) {
    if(!type)type=info->mode&0170000U;
    if(!type)type=info->is_dir?0040000U:0100000U;
    struct stat result={type|info->mode,(long)info->size,info->ino,info->uid,info->gid,info->dev,info->links,
                        type==0020000U?info->dev:0,4096,info->blocks,
                        {info->times.access,0},{info->times.modify,0},{info->times.change,0}};
    store_user(out,&result,bytes);
    return 0;
}

static int user_path_at(int dirfd,uint64_t name,char path[256],struct vfs_base *base) {
    int rc=user_string(name,path,256);
    if(rc<0)return rc;
    if(!path[0] || !active_io)return -ENOENT;
    if(path[0]=='/')return 0;
    if(dirfd==AT_FDCWD) {
        base->dir=cwd_path();
        return base->dir?0:-ENOENT;
    }
    if(!valid_fd(dirfd))return -EBADF;
    if(task_fds[dirfd].backend<0)return -K_ENOTDIR;
    base->dir=vfs_directory_path(task_fds[dirfd].backend);
    base->search=task_fds[dirfd].mode==O_SEARCH;
    return base->dir?0:-vfs_errno();
}
long posix_fstatat(int dirfd,uint64_t name,uint64_t out,int flags,uint64_t requested) {
    if(flags & ~0x100)return -EINVAL;
    size_t bytes=stat_size(requested);
    if(!bytes)return -EINVAL;
    if(!user_range(out,bytes,1))return -EFAULT;
    struct vfs_base base={0};
    char full[256];
    int rc=user_path_at(dirfd,name,full,&base);
    if(rc)return rc;
    struct vfs_info info;
    if(vfs_stat_from(base,full,&info,flags)<0)return -vfs_errno();
    return copy_stat(out,&info,0,bytes);
}
static long posix_symlinkat(uint64_t source,int dirfd,uint64_t name) {
    struct vfs_base base={0};
    char full[256],target[256];
    int rc=user_path_at(dirfd,name,full,&base);
    if(rc)return rc;
    rc=user_string(source,target,sizeof target);
    if(rc)return rc;
    return vfs_symlink_from(base,target,full)<0?-vfs_errno():0;
}
long posix_readlinkat(int dirfd,uint64_t name,uint64_t buf,uint64_t size) {
    struct vfs_base base={0};
    char full[256],target[256];
    if(!size)return -EINVAL;
    int rc=user_path_at(dirfd,name,full,&base);
    if(rc)return rc;
    rc=vfs_readlink_from(base,full,target,size<sizeof target?(size_t)size:sizeof target);
    if(rc<0)return -vfs_errno();
    if(!user_range(buf,(unsigned)rc,1))return -EFAULT;
    store_user(buf,target,(unsigned)rc);
    return rc;
}
static unsigned descriptor_flags(uint64_t flags) {
    unsigned result=0;
    if(flags&O_CLOEXEC)result|=FD_CLOEXEC;
    if(flags&O_CLOFORK)result|=FD_CLOFORK;
    return result;
}
long posix_openat(int dirfd,uint64_t name,uint64_t flags,unsigned mode) {
    struct vfs_base base={0};
    char full[256];
    int rc=user_path_at(dirfd,name,full,&base);
    if(rc)return rc;
    if(flags & ~(uint64_t)(O_ACCMODE|O_SYNC|O_NOCTTY|O_CREAT|O_EXCL|O_TRUNC|O_APPEND|O_NONBLOCK|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC|O_CLOFORK)) return -EINVAL;
    unsigned access=(unsigned)flags&O_ACCMODE;
    if(access>2 && access!=O_EXEC && access!=O_SEARCH)return -EINVAL;
    if(access==3 || ((flags&01000) && access==0))return -EINVAL;
    unsigned vf=access==0?VFS_READ:access==1?VFS_WRITE:VFS_READ|VFS_WRITE;
    if(access==O_EXEC || access==O_SEARCH) {
        if(flags&O_TRUNC)return -EINVAL;
        vf=VFS_READ|VFS_EXEC;
        if(access==O_SEARCH)vf|=VFS_DIRECTORY;
    }
    int free_fd=-1;
    for(int i=0;i<FD_MAX;i++) if(!task_fds[i].used) {free_fd=i;break;}
    if(free_fd<0) return -EMFILE;
    if((flags&0300)==0300)vf|=VFS_EXCLUSIVE;
    if((flags&0200000) && (flags&0100))return -EINVAL;
    if(flags&0200000)vf|=VFS_DIRECTORY;
    if(flags&0400000)vf|=VFS_NOFOLLOW;
    int backend=vfs_open_mode_from(base,full,vf,!!(flags&0100),mode&~active_io->mask);
    vf&=~(VFS_EXCLUSIVE|VFS_NOFOLLOW|VFS_DIRECTORY);
    if(backend<0) return -(long)vfs_errno();
    if((flags&01000) && vfs_ftruncate(backend,0)<0) {
        int err=vfs_errno();
        (void)vfs_close(backend);return -(long)err;
    }
    task_fds[free_fd].backend=backend;
    task_fds[free_fd].mode=(access==O_SEARCH || access==O_EXEC)?access:vf;
    if(flags&O_SYNC)vfs_set_sync(backend,(unsigned)flags&O_SYNC);
    task_fds[free_fd].used=1;
    active_io->fd_flags[free_fd]=descriptor_flags(flags);
    if(flags&O_NONBLOCK)(void)vfs_set_nonblock(backend,1);
    if((flags&02000) && vfs_set_append(backend,1)<0){
        int err=vfs_errno();
        (void)fd_release((unsigned)free_fd);
        return err?-(long)err:-EBADF;
    }
    return free_fd;
}
static long posix_mkdirat(int dirfd,uint64_t name,unsigned mode) {
    struct vfs_base base={0};
    char full[256];
    int rc=user_path_at(dirfd,name,full,&base);
    if(rc)return rc;
    return vfs_mkdir_mode_from(base,full,mode&~active_io->mask)<0?-vfs_errno():0;
}
static long posix_unlinkat(int dirfd,uint64_t name,int flags) {
    if(flags!=0 && flags!=AT_REMOVEDIR)return -EINVAL;
    struct vfs_base base={0};
    char full[256];
    int rc=user_path_at(dirfd,name,full,&base);
    if(rc)return rc;
    rc=flags?vfs_rmdir_from(base,full):vfs_unlink_from(base,full);
    return rc<0?-vfs_errno():0;
}
long posix_chownat(int fd,uint64_t name,unsigned uid,unsigned gid,int flags,int by_fd) {
    if(flags&~AT_SYMLINK_NOFOLLOW)return -EINVAL;
    struct inode_update change={.ownership=1,.uid=uid,.gid=gid};
    struct vfs_base base={0};
    char path[256];
    if(by_fd) {
        if(!valid_fd(fd))return -EBADF;
        if(task_fds[fd].backend<0)return -EINVAL;
        fd=task_fds[fd].backend;
    } else {
        int rc=user_path_at(fd,name,path,&base);
        if(rc<0)return rc;
    }
    return vfs_update_metadata_from(base,by_fd?NULL:path,fd,flags,&change)<0?-vfs_errno():0;
}
long posix_utimens(int fd,uint64_t name,uint64_t times,int flags,int by_fd) {
    if(flags&~AT_SYMLINK_NOFOLLOW)return -EINVAL;
    struct inode_update update={0};
    int64_t now=file_now();
    for(unsigned i=0;i<2;i++) {
        struct timespec value={0,UTIME_NOW};
        if(times) {
            if(!user_range(times,2*sizeof(value),0))return -EFAULT;
            const unsigned char *src=(const unsigned char *)(uintptr_t)times+i*sizeof(value);
            for(unsigned j=0;j<sizeof(value);j++)((unsigned char *)&value)[j]=src[j];
        }
        if(value.tv_nsec==UTIME_OMIT)continue;
        if(value.tv_nsec!=UTIME_NOW && (value.tv_nsec<0 || value.tv_nsec>=1000000000))return -EINVAL;
        update.seconds[i]=value.tv_nsec==UTIME_NOW?now:value.tv_sec;
        update.mask|=1U<<i;
        if(value.tv_nsec!=UTIME_NOW)update.explicit_time=1;
    }
    if(update.mask!=3)update.explicit_time=1;
    struct vfs_base base={0};
    char path[256];
    if(by_fd) {
        if(!valid_fd(fd))return -EBADF;
        if(task_fds[fd].backend<0)return -EINVAL;
        fd=task_fds[fd].backend;
    } else {
        int rc=user_path_at(fd,name,path,&base);
        if(rc<0)return rc;
    }
    return vfs_update_metadata_from(base,by_fd?NULL:path,fd,flags,&update)<0?-vfs_errno():0;
}
long posix_linkat(int srcfd,uint64_t src,int dstfd,uint64_t dst,int flags) {
    if(flags & ~AT_SYMLINK_FOLLOW)return -EINVAL;
    struct vfs_base base={0};
    struct vfs_base destination={0};
    char from[256],to[256];
    int rc=user_path_at(srcfd,src,from,&base);
    if(rc<0)return rc;
    rc=user_path_at(dstfd,dst,to,&destination);
    if(rc<0)return rc;
    return vfs_link_from(base,destination,from,to,flags&AT_SYMLINK_FOLLOW)<0?-vfs_errno():0;
}
long posix_renameat(int srcfd,uint64_t src,int dstfd,uint64_t dst) {
    struct vfs_base base={0};
    struct vfs_base destination={0};
    char from[256],to[256];
    int rc=user_path_at(srcfd,src,from,&base);
    if(rc)return rc;
    rc=user_path_at(dstfd,dst,to,&destination);
    if(rc)return rc;
    return vfs_rename_from(base,destination,from,to)<0?-vfs_errno():0;
}
long posix_faccessat(int dirfd,uint64_t name,int mode,int flags) {
    if(flags&~(AT_EACCESS|AT_SYMLINK_NOFOLLOW) || (mode&~7))return -EINVAL;
    struct vfs_base base={0};
    char full[256];
    int rc=user_path_at(dirfd,name,full,&base);
    if(rc)return rc;
    if(!(flags&AT_EACCESS))vfs_set_credentials(active_io->ruid,active_io->rgid);
    rc=vfs_accessat_from(base,full,(unsigned)mode,flags&AT_SYMLINK_NOFOLLOW)<0?-vfs_errno():0;
    vfs_set_credentials(active_io->uid,active_io->gid);
    return rc;
}
long posix_fchmodat(int dirfd,uint64_t name,unsigned mode,int flags) {
    if(flags&~AT_SYMLINK_NOFOLLOW || mode>07777)return -EINVAL;
    struct vfs_base base={0};
    char full[256];
    int rc=user_path_at(dirfd,name,full,&base);
    if(rc)return rc;
    return vfs_chmodat_from(base,full,mode,flags&AT_SYMLINK_NOFOLLOW)<0?-vfs_errno():0;
}
static long transfer(bool writing,uint64_t a,uint64_t b,uint64_t c) {
    if(!valid_fd(a) || !(task_fds[a].mode&(writing?VFS_WRITE:VFS_READ)))
        return -EBADF;
    if(!c)return 0;
    if(c>INT64_MAX)return -EINVAL;
    /* A user buffer can span multiple pages. Operate one page or less
     * at a time, respecting partial completion on EAGAIN, EOF, or a
     * later invalid page. Never touch bytes outside validated pages. */
    uint64_t done=0;
    int backend=task_fds[a].backend;
    size_t record=vfs_directory_record_size(backend);
    if(!writing && record){
        if(c<record)return -EINVAL;
        if(!user_range(b,record,1))return -EFAULT;
        int n=vfs_read(backend,(void *)(uintptr_t)b,record);
        return n<0?-vfs_errno():n;
    }
    /* Do not split a <= PIPE_BUF write at a user page boundary.
     * Validate its entire source first, then commit all bytes or none;
     * otherwise the per-page loop would violate pipe atomicity. */
    if(writing && pipe_id(backend)>=0 && c<=PIPE_CAPACITY) {
        if(!user_range(b,c,0))return -EFAULT;
        return pipe_write_data(backend,(const char *)(uintptr_t)b,(unsigned)c,true);
    }
    while(done<c) {
        if(b>UINT64_MAX-done)return done?(long)done:-EFAULT;
        uint64_t address=b+done;
        uint64_t count=c-done;
        uint64_t page_remaining=4096U-(address&4095U);
        if(count>page_remaining)count=page_remaining;
        if(!user_range(address,count,!writing))return done?(long)done:-EFAULT;
        char *buffer=(char *)(uintptr_t)address;
        int result=0;
        if(writing) {
            if(backend==-2 || backend==-3) {
                for(uint64_t j=0;j<count;j++)console_putc(buffer[j]);
                result=(int)count;
            } else if(pipe_id(backend)>=0) {
                result=pipe_write_data(backend,buffer,(unsigned)count,false);
            } else {
                result=vfs_write(backend,buffer,(size_t)count);
                if(result<0)result=-vfs_errno();
            }
        } else {
            if(backend==-1) {
                char key=terminal_byte();
                if(!key)result=-EAGAIN;
                else if(key==4)result=0; /* One EOF on the keyboard-backed terminal. */
                else {buffer[0]=key;result=1;}
            } else if(pipe_id(backend)>=0) {
                result=pipe_read_data(backend,buffer,(unsigned)count);
            } else {
                result=vfs_read(backend,buffer,(size_t)count);
                if(result<0)result=-vfs_errno();
            }
        }
        if(result<=0)return done?(long)done:result;
        done+=(uint64_t)result;
        if((uint64_t)result<count || (!writing && backend==-1))break;
    }
    return (long)done;
}
long posix_positioned_io(bool writing,int fd,uint64_t address,uint64_t count,int64_t offset) {
    if(!valid_fd(fd) || !(task_fds[fd].mode&(writing?VFS_WRITE:VFS_READ)))return -EBADF;
    int backend=task_fds[fd].backend;
    if(backend<0)return -ESPIPE;
    if(offset<0 || count>INT64_MAX)return -EINVAL;
    if(vfs_directory_record_size(backend))return -K_EISDIR;
    /* The current dispatcher runs each nonblocking regular-file transfer to
     * completion. Restore the open description even after a user-page fault. */
    int64_t saved=vfs_offset(backend);
    if(vfs_seek(backend,(size_t)offset)<0)return -vfs_errno();
    int append=writing && vfs_get_append(backend)>0;
    if(append)(void)vfs_set_append(backend,0);
    long rc=transfer(writing,(unsigned)fd,address,count);
    if(append)(void)vfs_set_append(backend,1);
    if(vfs_seek(backend,(size_t)saved)<0)return -vfs_errno();
    return rc;
}
long posix_dispatch(uint64_t nr,uint64_t a,uint64_t b,uint64_t c) {
    if(!task_active) return -ENOSYS;
    if(nr<=1)return transfer(nr==1,a,b,c);
    if(nr==228) { /* clock_gettime(CLOCK_MONOTONIC, timespec*) */
        if(a>1)return -EINVAL;
        if(!user_range(b,16,1))return -EFAULT;
        uint64_t ticks=interrupt_ticks(); /* nominal PIT 100 Hz */
        int64_t stamp[2];
        stamp[0]=(int64_t)(ticks/100);
        stamp[1]=(int64_t)((ticks%100)*10000000ULL);
        if(a==0){stamp[0]=file_now();stamp[1]=0;}
        store_user(b,stamp,sizeof stamp);
        return 0;
    }
    if(nr==74) { /* fsync(fd) */
        if(!valid_fd(a))return -EBADF;
        if(task_fds[a].backend<0)return -EINVAL; /* fsync has no meaning on TTY/pipe */
        int rc=vfs_fsync(task_fds[a].backend);
        return rc<0?-(long)vfs_errno():0;
    }
    if(nr==22 || nr==293) {
        unsigned flags=nr==22?0:(unsigned)b;
        if(flags&~(O_NONBLOCK|O_CLOEXEC|O_CLOFORK))return -EINVAL;
        if(!user_range(a,2*sizeof(int),1))return -EFAULT;
        int first=-1,second=-1,which=-1;
        for(int i=0;i<FD_MAX;i++)if(!task_fds[i].used) {
            if(first<0)first=i;
            else {second=i;break;}
        }
        if(second<0)return -EMFILE;
        for(int i=0;i<PIPE_SLOTS;i++)if(!pipes[i].used){which=i;break;}
        if(which<0)return -EMFILE;
        pipes[which]=(struct kernel_pipe){0};
        pipes[which].used=1;pipes[which].readers=1;pipes[which].writers=1;
        task_fds[first]=(struct task_fd){-100-which*2,VFS_READ,1};
        task_fds[second]=(struct task_fd){-101-which*2,VFS_WRITE,1};
        active_io->fd_flags[first]=active_io->fd_flags[second]=descriptor_flags(flags);
        pipes[which].status[0]=pipes[which].status[1]=flags&O_NONBLOCK;
        int descriptors[]={first,second};
        store_user(a,descriptors,sizeof descriptors);
        return 0;
    }
    if(nr==2)return posix_openat(-100,a,b,c);
    if(nr==4)return posix_fstatat(-100,a,b,0,c);
    if(nr==5){
        size_t bytes=stat_size(c);
        if(!bytes)return -EINVAL;
        if(!user_range(b,bytes,1))return -EFAULT;
        struct vfs_info info={0};
        unsigned type=0;
        if(!valid_fd(a))return -EBADF;
        int backend=task_fds[a].backend;
        if(backend>=0){
            if(vfs_fstat(backend,&info)<0)return -vfs_errno();
        } else {
            int pipe=pipe_id(backend);
            type=pipe>=0?0010000U:0020000U;
            info.dev=pipe>=0?5:6;
            info.ino=pipe>=0?(unsigned)pipe+1:1;
        }
        return copy_stat(b,&info,type,bytes);
    }
    if(nr==88)return posix_symlinkat(a,-100,b);
    if(nr==266)return posix_symlinkat(a,(int)b,c);
    if(nr==89)return posix_readlinkat(-100,a,b,c);
    if(nr==83)return posix_mkdirat(-100,a,(unsigned)b);
    if(nr==258)return posix_mkdirat((int)a,b,(unsigned)c);
    if(nr==84 || nr==87)return posix_unlinkat(-100,a,nr==84?AT_REMOVEDIR:0);
    if(nr==263)return posix_unlinkat((int)a,b,(int)c);
    if(nr==82)return posix_renameat(-100,a,-100,b);
    if(nr==21)return posix_faccessat(-100,a,(int)b,0);
    if(nr==95) {
        unsigned previous=active_io->mask;
        active_io->mask=(unsigned)a&0777;
        return previous;
    }
    if(nr==76) { /* truncate(path,length) */
        char path[256];
        struct vfs_base base={0};
        int rc=user_path_at(AT_FDCWD,a,path,&base);
        if(rc)return rc;
        if(b>32768)return -EINVAL;
        return vfs_truncate_from(base,path,(size_t)b)<0?-vfs_errno():0;
    }
    if(nr==77) { /* ftruncate(fd,length) */
        if(!valid_fd(a) || task_fds[a].backend<0 ||
           !(task_fds[a].mode&VFS_WRITE)) return -EBADF;
        if(b>32768) return -EINVAL;
        return vfs_ftruncate(task_fds[a].backend,(size_t)b)<0 ? -(long)vfs_errno() : 0;
    }
    if(nr==3) { /* close */
        if(a>=FD_MAX) return -EBADF;
        return fd_release((unsigned)a);
    }
    if(nr==32 || nr==33 || nr==292) {
        if(nr==292 && (a==b || (c&~(uint64_t)(O_CLOEXEC|O_CLOFORK))))return -EINVAL; /* dup / dup2: shared open file description */
        if(!valid_fd(a)) return -EBADF;
        int target=-1;
        if(nr==32) {
            for(int i=0;i<FD_MAX;i++) if(!task_fds[i].used) {target=i;break;}
            if(target<0) return -EMFILE;
        } else {
            if(b>=FD_MAX) return -EBADF;
            target=(int)b;
            if((unsigned)target==a) return target;
        }
        /* Retain source before closing dup2 target; failed ref acquisition
         * must never destroy a perfectly valid target descriptor. */
        if(task_fds[a].backend>=0 && vfs_retain(task_fds[a].backend)<0)return -EBADF;
        if(pipe_id(task_fds[a].backend)>=0)pipe_retain(task_fds[a].backend);
        if(nr!=32 && task_fds[target].used)(void)fd_release((unsigned)target);
        task_fds[target]=task_fds[a];
        active_io->fd_flags[target]=nr==292?descriptor_flags(c):0;
        return target;
    }
    if(nr==72) { /* fcntl(fd, cmd, arg): stable small core */
        if(!valid_fd(a))return -EBADF;
        /* POSIX socket ownership commands require a socket descriptor. */
        if(b==F_SETOWN || b==F_GETOWN)return -22;
        bool query=b==F_GETLK || b==F_OFD_GETLK;
        if(query || b==F_SETLK || b==F_SETLKW || b==F_OFD_SETLK || b==F_OFD_SETLKW) {
            struct flock lock;
            if(!user_range(c,sizeof lock,0) ||
               (query && !user_range(c,sizeof lock,1)))return -EFAULT;
            const unsigned char *bytes=(const unsigned char *)(uintptr_t)c;
            for(unsigned i=0;i<sizeof lock;i++)((unsigned char *)&lock)[i]=bytes[i];
            if(!query) {
                unsigned mode=task_fds[a].mode;
                if(lock.l_type==F_RDLCK && !(mode&VFS_READ))return -EBADF;
                if(lock.l_type==F_WRLCK && !(mode&VFS_WRITE))return -EBADF;
            }
            int rc=record_request(task_pid,task_fds[a].backend,(int)b,&lock);
            if(!rc && query)store_user(c,&lock,sizeof lock);
            return rc;
        }
        if(b==F_GETFD)return active_io->fd_flags[a]; /* F_GETFD */
        if(b==2){ /* F_SETFD */
            if(c&~(uint64_t)(FD_CLOEXEC|FD_CLOFORK))return -EINVAL;
            active_io->fd_flags[a]=(unsigned)c;return 0;
        }
        if(b==3){ /* F_GETFL */
            int mode=task_fds[a].mode;
            int acc=(mode&(O_SEARCH|O_EXEC))?mode:(mode&VFS_READ)?((mode&VFS_WRITE)?2:0):1;
            if(task_fds[a].backend>=0)acc|=vfs_get_sync(task_fds[a].backend);
            if(task_fds[a].backend>=0 && vfs_get_append(task_fds[a].backend)>0)acc|=02000;
            if(task_fds[a].backend>=0 && vfs_get_nonblock(task_fds[a].backend)>0)acc|=O_NONBLOCK;
            int id=pipe_id(task_fds[a].backend);
            if(id>=0)acc|=pipes[id].status[pipe_write_end(task_fds[a].backend)];
            else if(task_fds[a].backend<0)acc|=terminal_flags[-1-task_fds[a].backend];
            return acc;
        }
        if(b==F_SETFL) {
            const unsigned known=O_ACCMODE|O_APPEND|O_NONBLOCK|O_SYNC|O_NOCTTY|O_CREAT|
                                 O_EXCL|O_TRUNC|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC|O_CLOFORK;
            if(c&~(uint64_t)known)return -EINVAL;
            int id=pipe_id(task_fds[a].backend);
            if(id>=0){
                pipes[id].status[pipe_write_end(task_fds[a].backend)]=c&(O_NONBLOCK|O_APPEND);
                return 0;
            }
            if(task_fds[a].backend<0) {
                terminal_flags[-1-task_fds[a].backend]=c&(O_NONBLOCK|O_APPEND);
                return 0;
            }
            (void)vfs_set_nonblock(task_fds[a].backend,!!(c&O_NONBLOCK));
            return vfs_set_append(task_fds[a].backend,!!(c&O_APPEND))<0?-EBADF:0;
        }
        if(b==F_DUPFD || b==F_DUPFD_CLOEXEC || b==F_DUPFD_CLOFORK) { /* F_DUPFD / F_DUPFD_CLOEXEC */
            if(c>=FD_MAX)return -EINVAL;
            int to=-1;
            for(unsigned i=(unsigned)c;i<FD_MAX;i++)if(!task_fds[i].used){to=(int)i;break;}
            if(to<0)return -EMFILE;
            int backend=task_fds[a].backend;
            if(backend>=0 && vfs_retain(backend)<0)return -EBADF;
            if(pipe_id(backend)>=0)pipe_retain(backend);
            task_fds[to]=task_fds[a];
            active_io->fd_flags[to]=b==F_DUPFD_CLOEXEC?FD_CLOEXEC:b==F_DUPFD_CLOFORK?FD_CLOFORK:0;return to;
        }
        return -EINVAL;
    }
    if(nr==8) { /* POSIX SEEK_SET / SEEK_CUR / SEEK_END */
        if(!valid_fd(a)) return -EBADF;
        /* dup2() can attach a seekable file to stdin/stdout/stderr.
         * Terminals and pipes remain non-seekable regardless of fd number. */
        if(task_fds[a].backend<0) return -ESPIPE;
        return (long)vfs_lseek(task_fds[a].backend,(int64_t)b,(unsigned)c);
    }
    if(nr==90)return posix_fchmodat(-100,a,(unsigned)b,0);
    if(nr==91) {
        if(!valid_fd(a))return -EBADF;
        if(task_fds[a].backend<0)return -EINVAL;
        return vfs_fchmod(task_fds[a].backend,(unsigned)b)<0?-vfs_errno():0;
    }
    if(nr==80 || nr==81) {
        int fd;
        if(nr==80) {
            char full[256];
            struct vfs_base base={0};
            int rc=user_path_at(AT_FDCWD,a,full,&base);
            if(rc)return rc;
            fd=vfs_open_mode_from(base,full,VFS_READ|VFS_DIRECTORY|VFS_EXEC,0,0);
            if(fd<0)return -vfs_errno();
        } else {
            if(!valid_fd(a))return -EBADF;
            fd=task_fds[a].backend;
            if(fd<0)return -K_ENOTDIR;
            struct vfs_info info;
            if(vfs_fstat(fd,&info)<0)return -vfs_errno();
            if(!info.is_dir)return -K_ENOTDIR;
            if((task_fds[a].mode!=O_SEARCH && vfs_faccess(fd,1)<0) || vfs_retain(fd)<0)return -vfs_errno();
        }
        if(active_io->cwd_fd>=0)(void)vfs_close(active_io->cwd_fd);
        active_io->cwd_fd=fd;
        return 0;
    }
    if(nr==79) {
        if(!b)return -EINVAL;
        const char *cwd=cwd_path();
        if(!cwd)return -ENOENT;
        unsigned n=0;while(cwd[n])n++;n++;
        if(b<n)return -34; /* ERANGE */
        if(!user_range(a,n,1))return -EFAULT;
        for(unsigned i=0;i<n;i++)((char *)(uintptr_t)a)[i]=cwd[i];
        return (long)a;
    }
    if(nr==102)return active_io->ruid;
    if(nr==104)return active_io->rgid;
    if(nr==107)return active_io->uid;
    if(nr==108)return active_io->gid;
    if(nr==105 || nr==106 || nr==117 || nr==119)
        return set_identity(nr==106 || nr==119,nr==117 || nr==119,(unsigned)a);
    if(nr==39) return (long)task_pid;
    if(nr==110) return (long)process_parent_of(task_pid);
    if(nr==61) { /* waitpid(pid,status,0|WNOHANG), including pid=-1 */
        if(c>1)return -EINVAL;
        if(b && !user_range(b,sizeof(int),1))return -EFAULT;
        int status=0;
        int rc=ring3_wait_status(task_pid,(int)a,&status);
        if(rc==-EAGAIN && c==1)return 0;
        if(rc>0 && b)store_user(b,&status,sizeof status);
        return rc;
    }
    if(nr==60) return -ENOSYS; /* exit is handled in ring3 interrupt stub */
    return -ENOSYS;
}

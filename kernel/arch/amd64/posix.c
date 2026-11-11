#include <stdint.h>
#include <stddef.h>
#include <kilons/vfs.h>
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
struct task_io { uint32_t pid,uid,gid; char cwd[256]; struct task_fd fd[FD_MAX]; unsigned cloexec[FD_MAX]; };
static struct task_io io[TASK_IO_SLOTS];
static struct task_fd *task_fds;
static int task_active;
static uint32_t task_pid;
struct kernel_pipe {
    unsigned used,readers,writers,head,count;
    bool nonblock[2];
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

static const char *terminal_sequence="";
static struct task_io *active_io;
static struct task_io *lookup_io(uint32_t pid) {
    for(unsigned i=0;i<TASK_IO_SLOTS;i++)if(io[i].pid==pid && pid)return &io[i];
    return NULL;
}
/* Called with the process blocked or at the syscall boundary. Endpoint
 * flags belong to the open description, including all dup/fork aliases. */
bool posix_io_should_wait(uint32_t pid,uint64_t fd,bool write,uint64_t bytes){
    struct task_io *task=lookup_io(pid);
    if(!task || fd>=FD_MAX || !task->fd[fd].used || !bytes)return false;
    int backend=task->fd[fd].backend,id=pipe_id(backend);
    if(!write && backend==-1)return !*terminal_sequence && !keyboard_available();
    if(id<0 || pipe_write_end(backend)!=write)return false;
    struct kernel_pipe *p=&pipes[id];
    if(p->nonblock[write])return false;
    if(!write)return !p->count && p->writers;
    unsigned required=bytes<=PIPE_CAPACITY?(unsigned)bytes:1;
    return p->readers && PIPE_CAPACITY-p->count<required;
}
static void copy_text(char *dst,const char *src,unsigned cap) {
    unsigned i=0;while(i+1<cap && src[i]){dst[i]=src[i];i++;}dst[i]=0;
}
/* Join relative paths to the process CWD. Canonicalization stays in VFS. */
int posix_task_path(const char *src,char *dst,size_t cap) {
    if(!src[0] || !active_io)return -ENOENT;
    if(src[0]=='/') {
        size_t n=0;while(src[n]){if(n+1>=cap)return -ENAMETOOLONG;n++;}
        copy_text(dst,src,(unsigned)cap);return 0;
    }
    size_t n=0;
    while(active_io->cwd[n] && n<cap-1){dst[n]=active_io->cwd[n];n++;}
    if(n>=cap-2)return -ENAMETOOLONG;
    if(n!=1)dst[n++]='/';
    for(size_t j=0;src[j];j++) {
        if(n>=cap-1)return -ENAMETOOLONG;
        dst[n++]=src[j];
    }
    dst[n]=0;return 0;
}

/* A duplicated descriptor shares its backend and current file offset. */
static int fd_release(unsigned fd) {
    if(fd>=FD_MAX || !task_fds[fd].used) return -EBADF;
    int backend=task_fds[fd].backend;
    task_fds[fd].used=0;
    if(active_io)active_io->cloexec[fd]=0;
    if(backend>=0)return vfs_close(backend)<0 ? -EBADF : 0;
    if(pipe_id(backend)>=0)pipe_release(backend);
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
        if(!user_range(p+i,1,0)) return -EFAULT;
        dst[i]=((const char *)(uintptr_t)p)[i];
        if(!dst[i]) return 0;
    }
    return -ENAMETOOLONG;
}
void posix_task_end(void) {
    if(!task_active)return;
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
    char *cwd=NULL;
    for(unsigned i=0;i<TASK_IO_SLOTS;i++)if(io[i].pid==pid) {cwd=io[i].cwd;break;}
    if(cwd) {
        struct task_io *parent=lookup_io(process_parent_of(pid));
        if(parent){copy_text(cwd,parent->cwd,256);active_io->uid=parent->uid;active_io->gid=parent->gid;}
        else {cwd[0]='/';cwd[1]=0;active_io->uid=1000;active_io->gid=1000;}
    }
    for(int i=0;i<FD_MAX;i++) {task_fds[i].used=0;task_fds[i].mode=0;task_fds[i].backend=-1;active_io->cloexec[i]=0;}
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
void posix_task_exec_close(uint32_t pid){
    struct task_io *entry=lookup_io(pid);
    if(!entry)return;
    for(unsigned i=0;i<FD_MAX;i++)if(entry->cloexec[i]&&entry->fd[i].used){
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
    *dst=*src;dst->pid=child;
    for(int i=0;i<FD_MAX;i++)if(dst->fd[i].used) {
        int backend=dst->fd[i].backend;
        if(backend>=0 && vfs_retain(backend)<0) {
            for(int j=0;j<i;j++)if(dst->fd[j].used) {
                int prior=dst->fd[j].backend;
                if(prior>=0)(void)vfs_close(prior);
                else if(pipe_id(prior)>=0)pipe_release(prior);
            }
            dst->pid=0;return false;
        }
        if(pipe_id(backend)>=0)pipe_retain(backend);
    }
    return true;
}
void posix_task_discard(uint32_t pid) {
    struct task_io *entry=lookup_io(pid);
    if(!entry)return;
    for(int i=0;i<FD_MAX;i++)if(entry->fd[i].used) {
        if(entry->fd[i].backend>=0)(void)vfs_close(entry->fd[i].backend);
        else if(pipe_id(entry->fd[i].backend)>=0)pipe_release(entry->fd[i].backend);
        entry->fd[i].used=0;
    }
    entry->pid=0;
    if(task_pid==pid) {
        task_active=0;task_fds=NULL;task_pid=0;active_io=NULL;
        vfs_set_credentials(0,0);
    }
}
static int valid_fd(uint64_t fd) {return fd<FD_MAX && task_fds[fd].used;}
long posix_dispatch(uint64_t nr,uint64_t a,uint64_t b,uint64_t c) {
    if(!task_active) return -ENOSYS;
    if(nr==1 || nr==0) { /* read/write: bounded, partial I/O semantics */
        bool writing=nr==1;
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
    if(nr==228) { /* clock_gettime(CLOCK_MONOTONIC, timespec*) */
        if(a!=1)return -EINVAL;
        if(!user_range(b,16,1))return -EFAULT;
        uint64_t ticks=interrupt_ticks(); /* nominal PIT 100 Hz */
        int64_t stamp[2];
        stamp[0]=(int64_t)(ticks/100);
        stamp[1]=(int64_t)((ticks%100)*10000000ULL);
        store_user(b,stamp,sizeof stamp);
        return 0;
    }
    if(nr==74) { /* fsync(fd) */
        if(!valid_fd(a))return -EBADF;
        if(task_fds[a].backend<0)return -EINVAL; /* fsync has no meaning on TTY/pipe */
        int rc=vfs_fsync(task_fds[a].backend);
        return rc<0?-(long)vfs_errno():0;
    }
    if(nr==22) { /* pipe(fds) -- nonblocking when empty/full */
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
        int descriptors[]={first,second};
        store_user(a,descriptors,sizeof descriptors);
        return 0;
    }
    if(nr==2) { /* open(path,flags,mode) -- small supported subset */
        char path[256];
        int path_rc=user_string(a,path,sizeof(path));
        if(path_rc) return path_rc;
        char full[256];int path_err=posix_task_path(path,full,sizeof(full));if(path_err)return path_err;
        if(b & ~(uint64_t)(3|0100|01000|02000)) return -EINVAL;
        unsigned access=(unsigned)b&3U;
        if(access==3 || ((b&01000) && access==0))return -EINVAL;
        unsigned vf=access==0?VFS_READ:access==1?VFS_WRITE:VFS_READ|VFS_WRITE;
        int free_fd=-1;
        for(int i=0;i<FD_MAX;i++) if(!task_fds[i].used) {free_fd=i;break;}
        if(free_fd<0) return -EMFILE;
        int backend=vfs_open_mode(full,vf,!!(b&0100),(unsigned)c);
        if(backend<0) return -(long)vfs_errno();
        if((b&01000) && vfs_ftruncate(backend,0)<0) {
            int err=vfs_errno();
            (void)vfs_close(backend);return -(long)err;
        }
        task_fds[free_fd].backend=backend;
        task_fds[free_fd].mode=vf;
        task_fds[free_fd].used=1;
        if((b&02000) && vfs_set_append(backend,1)<0){
            int err=vfs_errno();
            (void)fd_release((unsigned)free_fd);
            return err?-(long)err:-EBADF;
        }
        return free_fd;
    }
    if(nr==4 || nr==5){
        if(!user_range(b,sizeof(struct stat),1))return -EFAULT;
        struct vfs_info info={0};
        unsigned type=0;
        if(nr==4){
            char path[256],full[256];
            int rc=user_string(a,path,sizeof path);if(rc)return rc;
            rc=posix_task_path(path,full,sizeof full);if(rc)return rc;
            if(vfs_stat(full,&info)<0)return -vfs_errno();
        }else{
            if(!valid_fd(a))return -EBADF;
            int backend=task_fds[a].backend;
            if(backend>=0){
                if(vfs_fstat(backend,&info)<0)return -vfs_errno();
            }else type=pipe_id(backend)>=0?0010000U:0020000U;
        }
        if(!type)type=info.is_dir?0040000U:0100000U;
        struct stat result={type|info.mode,(long)info.size,info.ino,info.uid,info.gid};
        store_user(b,&result,sizeof result);return 0;
    }
    if(nr==83 || nr==84 || nr==87) { /* mkdir, rmdir, unlink */
        char path[256];
        int path_rc=user_string(a,path,sizeof(path));
        if(path_rc) return path_rc;
        char full[256];int path_err=posix_task_path(path,full,sizeof(full));if(path_err)return path_err;
        if(nr==83) {
            struct vfs_info info;
            if(vfs_stat(full,&info)==0)return -17; /* EEXIST */
            return vfs_mkdir_mode(full,(unsigned)b)<0 ? -(long)vfs_errno() : 0;
        }
        struct vfs_info info;
        if(vfs_stat(full,&info)<0)return -(long)vfs_errno();
        if(nr==84) {
            if(!info.is_dir)return -20; /* ENOTDIR */
            return vfs_rmdir(full)<0 ? -(long)vfs_errno() : 0; /* ENOTEMPTY */
        }
        if(info.is_dir)return -21; /* EISDIR */
        return vfs_unlink(full)<0 ? -(long)vfs_errno() : 0;
    }
    if(nr==82) { /* rename(oldpath,newpath) */
        char oldpath[256],newpath[256];
        int rc=user_string(a,oldpath,sizeof(oldpath));if(rc) return rc;
        rc=user_string(b,newpath,sizeof(newpath));if(rc) return rc;
        char oldfull[256],newfull[256];
        rc=posix_task_path(oldpath,oldfull,sizeof(oldfull));if(rc)return rc;
        rc=posix_task_path(newpath,newfull,sizeof(newfull));if(rc)return rc;
        return vfs_rename(oldfull,newfull)<0 ? -(long)vfs_errno() : 0;
    }
    if(nr==76) { /* truncate(path,length) */
        char path[256];
        int rc=user_string(a,path,sizeof(path));if(rc) return rc;
        char full[256];rc=posix_task_path(path,full,sizeof(full));if(rc)return rc;
        if(b>32768) return -EINVAL;
        return vfs_truncate(full,(size_t)b)<0 ? -(long)vfs_errno() : 0;
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
    if(nr==32 || nr==33) { /* dup / dup2: shared open file description */
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
        if(nr==33 && task_fds[target].used)(void)fd_release((unsigned)target);
        task_fds[target]=task_fds[a];
        active_io->cloexec[target]=0;
        return target;
    }
    if(nr==72) { /* fcntl(fd, cmd, arg): stable small core */
        if(!valid_fd(a))return -EBADF;
        if(b==1)return (long)(active_io->cloexec[a]?1:0); /* F_GETFD */
        if(b==2){ /* F_SETFD */
            if(c&~1ULL)return -EINVAL;
            active_io->cloexec[a]=(unsigned)(c&1U);return 0;
        }
        if(b==3){ /* F_GETFL */
            int mode=task_fds[a].mode;
            int acc=(mode&VFS_READ)?((mode&VFS_WRITE)?2:0):1;
            if(task_fds[a].backend>=0 && vfs_get_append(task_fds[a].backend)>0)acc|=02000;
            int id=pipe_id(task_fds[a].backend);
            if(id>=0 && pipes[id].nonblock[pipe_write_end(task_fds[a].backend)])acc|=04000;
            return acc;
        }
        if(b==4){ /* F_SETFL: only O_APPEND is currently mutable */
            int id=pipe_id(task_fds[a].backend);
            if(id>=0){
                if(c&~04003ULL)return -EINVAL;
                pipes[id].nonblock[pipe_write_end(task_fds[a].backend)]=!!(c&04000);
                return 0;
            }
            if(c&~(uint64_t)02003U)return -EINVAL;
            if(task_fds[a].backend<0)return -EINVAL;
            return vfs_set_append(task_fds[a].backend,!!(c&02000U))<0?-EBADF:0;
        }
        if(b==0 || b==1030) { /* F_DUPFD / F_DUPFD_CLOEXEC */
            if(c>=FD_MAX)return -EINVAL;
            int to=-1;
            for(unsigned i=(unsigned)c;i<FD_MAX;i++)if(!task_fds[i].used){to=(int)i;break;}
            if(to<0)return -EMFILE;
            int backend=task_fds[a].backend;
            if(backend>=0 && vfs_retain(backend)<0)return -EBADF;
            if(pipe_id(backend)>=0)pipe_retain(backend);
            task_fds[to]=task_fds[a];
            active_io->cloexec[to]=(b==1030);return to;
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
    if(nr==90) { /* chmod(path, mode), owner/root only */
        char path[256],full[256];
        int rc=user_string(a,path,sizeof(path));if(rc)return rc;
        rc=posix_task_path(path,full,sizeof(full));if(rc)return rc;
        if(b>07777U)return -EINVAL;
        return vfs_chmod(full,(unsigned)b)<0?-(long)vfs_errno():0;
    }
    if(nr==80) { /* chdir */
        char path[256],full[256];
        int rc=user_string(a,path,sizeof(path));if(rc)return rc;
        rc=posix_task_path(path,full,sizeof(full));if(rc)return rc;
        struct vfs_info info;
        if(vfs_stat(full,&info)<0)return -(long)vfs_errno();
        if(!info.is_dir)return -20;
        if(vfs_access(full,1U)<0)return -(long)vfs_errno();
        /* Obtain a canonical absolute path by walking via a VFS helper. */
        if(vfs_normalize(full,active_io->cwd,256)<0)return -ENAMETOOLONG;
        return 0;
    }
    if(nr==79) { /* getcwd(buffer,size): Linux-compatible primitive */
        unsigned n=0;while(active_io->cwd[n])n++;n++;
        if(b<n)return -34; /* ERANGE */
        if(!user_range(a,n,1))return -EFAULT;
        for(unsigned i=0;i<n;i++)((char *)(uintptr_t)a)[i]=active_io->cwd[i];
        return (long)a;
    }
    if(nr==39) return (long)task_pid;
    if(nr==110) return (long)process_parent_of(task_pid);
    if(nr==61) { /* waitpid(pid,status,0|WNOHANG), including pid=-1 */
        if(c>1 || (a!=UINT64_MAX && (a==0 || a>0x7fffffffULL)))return -EINVAL;
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

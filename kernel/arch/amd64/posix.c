#include <stdint.h>
#include <stddef.h>
#include <kilons/vfs.h>
#include <kilons/console.h>
#include <kilons/posix.h>
#include <kilons/vm.h>
#include <kilons/keyboard.h>
#include <kilons/ring3.h>
#include <kilons/power.h>
#include <kilons/process.h>
#include <kilons/memory.h>
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
struct task_io { uint32_t pid; char cwd[256]; struct task_fd fd[FD_MAX]; };
static struct task_io io[TASK_IO_SLOTS];
static struct task_fd *task_fds;
static int task_active;
static uint32_t task_pid;
struct kernel_pipe {
    unsigned used,readers,writers,head,count;
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
    if(i<0 || pipe_write_end(backend))return -EBADF;
    struct kernel_pipe *p=&pipes[i];
    if(!p->count)return p->writers?-EAGAIN:0;
    unsigned n=size<p->count?size:p->count;
    for(unsigned j=0;j<n;j++)buffer[j]=p->data[(p->head+j)%PIPE_CAPACITY];
    p->head=(p->head+n)%PIPE_CAPACITY;p->count-=n;
    return (int)n;
}
static int pipe_write_data(int backend,const char *buffer,unsigned size) {
    int i=pipe_id(backend);
    if(i<0 || !pipe_write_end(backend))return -EBADF;
    struct kernel_pipe *p=&pipes[i];
    if(!p->readers)return -EPIPE;
    if(p->count==PIPE_CAPACITY)return -EAGAIN;
    unsigned n=size<PIPE_CAPACITY-p->count?size:PIPE_CAPACITY-p->count;
    for(unsigned j=0;j<n;j++)p->data[(p->head+p->count+j)%PIPE_CAPACITY]=buffer[j];
    p->count+=n;return (int)n;
}

static struct task_io *active_io;
static struct task_io *lookup_io(uint32_t pid) {
    for(unsigned i=0;i<TASK_IO_SLOTS;i++)if(io[i].pid==pid && pid)return &io[i];
    return NULL;
}
static void copy_text(char *dst,const char *src,unsigned cap) {
    unsigned i=0;while(i+1<cap && src[i]){dst[i]=src[i];i++;}dst[i]=0;
}
/* Join relative paths to the process CWD. Canonicalization stays in VFS. */
static int task_path(const char *src,char *dst,size_t cap) {
    if(!src[0] || !active_io)return -ENOENT;
    if(src[0]=='/') {copy_text(dst,src,(unsigned)cap);return 0;}
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
    if(backend>=0)return vfs_close(backend)<0 ? -EBADF : 0;
    if(pipe_id(backend)>=0)pipe_release(backend);
    return 0;
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
    task_active=0;task_fds=NULL;task_pid=0;active_io=NULL;
}
void posix_task_activate(uint32_t pid) {
    task_active=0;task_fds=NULL;task_pid=0;active_io=NULL;
    for(unsigned i=0;i<TASK_IO_SLOTS;i++) if(io[i].pid==pid && pid) {
        task_fds=io[i].fd;task_pid=pid;task_active=1;active_io=&io[i];return;
    }
}
void posix_task_begin(uint32_t pid) {
    if(!pid)return;
    for(unsigned i=0;i<TASK_IO_SLOTS;i++) if(!io[i].pid) {
        io[i].pid=pid;break;
    }
    posix_task_activate(pid);
    if(!task_active)return;
    char *cwd=NULL;
    for(unsigned i=0;i<TASK_IO_SLOTS;i++)if(io[i].pid==pid) {cwd=io[i].cwd;break;}
    if(cwd) {
        struct task_io *parent=lookup_io(process_parent_of(pid));
        if(parent)copy_text(cwd,parent->cwd,256);
        else {cwd[0]='/';cwd[1]=0;}
    }
    for(int i=0;i<FD_MAX;i++) {task_fds[i].used=0;task_fds[i].mode=0;task_fds[i].backend=-1;}
    /* Standard terminal descriptors are task-local. */
    task_fds[0].used=1;task_fds[0].mode=VFS_READ;task_fds[0].backend=-1;
    task_fds[1].used=1;task_fds[1].mode=VFS_WRITE;task_fds[1].backend=-2;
    task_fds[2].used=1;task_fds[2].mode=VFS_WRITE;task_fds[2].backend=-3;
    task_pid=pid;
    task_active=1;
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
    if(task_pid==pid) {task_active=0;task_fds=NULL;task_pid=0;active_io=NULL;}
}
static int valid_fd(uint64_t fd) {return fd<FD_MAX && task_fds[fd].used;}
long posix_dispatch(uint64_t nr,uint64_t a,uint64_t b,uint64_t c) {
    if(!task_active) return -ENOSYS;
    if(nr==1) { /* write(fd,buf,count) */
        if(c>4096) return -EINVAL;
        if(c && !user_range(b,c,0)) return -EFAULT;
        if(!valid_fd(a)) return -EBADF;
        if(!(task_fds[a].mode&VFS_WRITE)) return -EBADF;
        if(!c) return 0;
        if(task_fds[a].backend==-2 || task_fds[a].backend==-3) {
            const char *p=(const char *)(uintptr_t)b;
            for(uint64_t i=0;i<c;i++) console_putc(p[i]);
            return (long)c;
        }
        if(task_fds[a].mode&4U) { /* O_APPEND at write time */
            struct vfs_info info;
            if(vfs_fstat(task_fds[a].backend,&info)<0 ||
               vfs_seek(task_fds[a].backend,info.size)<0)return -EBADF;
        }
        if(pipe_id(task_fds[a].backend)>=0)
            return pipe_write_data(task_fds[a].backend,(const char *)(uintptr_t)b,(unsigned)c);
        int rc=vfs_write(task_fds[a].backend,(const void *)(uintptr_t)b,(size_t)c);
        return rc<0 ? -EBADF : rc;
    }
    if(nr==0) { /* read(fd,buf,count) */
        if(c>4096) return -EINVAL;
        if(c && !user_range(b,c,1)) return -EFAULT;
        if(!valid_fd(a) || !(task_fds[a].mode&VFS_READ)) return -EBADF;
        if(!c) return 0;
        if(task_fds[a].backend==-1) {
            char key=keyboard_poll();
            if(!key) return -EAGAIN; /* early nonblocking terminal */
            ((char *)(uintptr_t)b)[0]=key;
            return 1;
        }
        if(pipe_id(task_fds[a].backend)>=0)
            return pipe_read_data(task_fds[a].backend,(char *)(uintptr_t)b,(unsigned)c);
        int rc=vfs_read(task_fds[a].backend,(void *)(uintptr_t)b,(size_t)c);
        return rc<0 ? -EBADF : rc;
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
        int *descriptors=(int *)(uintptr_t)a;
        descriptors[0]=first;descriptors[1]=second;
        return 0;
    }
    if(nr==2) { /* open(path,flags,mode) -- small supported subset */
        char path[256];
        int path_rc=user_string(a,path,sizeof(path));
        if(path_rc) return path_rc;
        char full[256];int path_err=task_path(path,full,sizeof(full));if(path_err)return path_err;
        if(b & ~(uint64_t)(3|0100|01000|02000)) return -EINVAL;
        unsigned access=(unsigned)b&3U;
        if(access==3 || ((b&01000) && access==0))return -EINVAL;
        unsigned vf=access==0?VFS_READ:access==1?VFS_WRITE:VFS_READ|VFS_WRITE;
        int free_fd=-1;
        for(int i=3;i<FD_MAX;i++) if(!task_fds[i].used) {free_fd=i;break;}
        if(free_fd<0) return -EMFILE;
        int backend=vfs_open(full,vf,!!(b&0100));
        if(backend<0) return -ENOENT;
        if((b&01000) && vfs_ftruncate(backend,0)<0) {
            (void)vfs_close(backend);return -EACCES;
        }
        task_fds[free_fd].backend=backend;
        task_fds[free_fd].mode=vf|((b&02000)?4U:0U);
        task_fds[free_fd].used=1;
        return free_fd;
    }
    if(nr==4 || nr==5) { /* small stat/fstat ABI */
        struct vfs_info info={0};
        if(!user_range(b,16,1))return -EFAULT;
        if(nr==4) {
            char path[256],full[256];
            int rc=user_string(a,path,sizeof(path));if(rc)return rc;
            rc=task_path(path,full,sizeof(full));if(rc)return rc;
            if(vfs_stat(full,&info)<0)return -ENOENT;
        } else {
            if(!valid_fd(a) || task_fds[a].backend<0)return -EBADF;
            if(vfs_fstat(task_fds[a].backend,&info)<0)return -EBADF;
        }
        uint64_t *output=(uint64_t *)(uintptr_t)b;
        output[0]=info.is_dir?0040000ULL:0100000ULL;
        output[1]=info.size;
        return 0;
    }
    if(nr==83 || nr==84 || nr==87) { /* mkdir, rmdir, unlink */
        char path[256];
        int path_rc=user_string(a,path,sizeof(path));
        if(path_rc) return path_rc;
        char full[256];int path_err=task_path(path,full,sizeof(full));if(path_err)return path_err;
        if(nr==83) {
            struct vfs_info info;
            if(vfs_stat(full,&info)==0)return -17; /* EEXIST */
            return vfs_mkdir(full)<0 ? -ENOENT : 0;
        }
        struct vfs_info info;
        if(vfs_stat(full,&info)<0)return -ENOENT;
        if(nr==84) {
            if(!info.is_dir)return -20; /* ENOTDIR */
            return vfs_rmdir(full)<0 ? -39 : 0; /* ENOTEMPTY */
        }
        if(info.is_dir)return -21; /* EISDIR */
        return vfs_unlink(full)<0 ? -EACCES : 0;
    }
    if(nr==82) { /* rename(oldpath,newpath) */
        char oldpath[256],newpath[256];
        int rc=user_string(a,oldpath,sizeof(oldpath));if(rc) return rc;
        rc=user_string(b,newpath,sizeof(newpath));if(rc) return rc;
        char oldfull[256],newfull[256];
        rc=task_path(oldpath,oldfull,sizeof(oldfull));if(rc)return rc;
        rc=task_path(newpath,newfull,sizeof(newfull));if(rc)return rc;
        return vfs_rename(oldfull,newfull)<0 ? -EACCES : 0;
    }
    if(nr==76) { /* truncate(path,length) */
        char path[256];
        int rc=user_string(a,path,sizeof(path));if(rc) return rc;
        char full[256];rc=task_path(path,full,sizeof(full));if(rc)return rc;
        if(b>32768) return -EINVAL;
        return vfs_truncate(full,(size_t)b)<0 ? -EACCES : 0;
    }
    if(nr==77) { /* ftruncate(fd,length) */
        if(!valid_fd(a) || task_fds[a].backend<0 ||
           !(task_fds[a].mode&VFS_WRITE)) return -EBADF;
        if(b>32768) return -EINVAL;
        return vfs_ftruncate(task_fds[a].backend,(size_t)b)<0 ? -EACCES : 0;
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
            if(task_fds[target].used) (void)fd_release((unsigned)target);
        }
        if(task_fds[a].backend>=0 && vfs_retain(task_fds[a].backend)<0)return -EBADF;
        if(pipe_id(task_fds[a].backend)>=0)pipe_retain(task_fds[a].backend);
        task_fds[target]=task_fds[a];
        return target;
    }
    if(nr==8) { /* lseek: SEEK_SET only */
        if(!valid_fd(a)) return -EBADF;
        /* dup2() can attach a seekable file to stdin/stdout/stderr.
         * Terminals and pipes remain non-seekable regardless of fd number. */
        if(task_fds[a].backend<0) return -ESPIPE;
        if(c!=0 || b>0x7fffffffULL) return -EINVAL;
        int rc=vfs_seek(task_fds[a].backend,(size_t)b);
        return rc<0?-EINVAL:(long)b;
    }
    if(nr==80) { /* chdir */
        char path[256],full[256];
        int rc=user_string(a,path,sizeof(path));if(rc)return rc;
        rc=task_path(path,full,sizeof(full));if(rc)return rc;
        struct vfs_info info;
        if(vfs_stat(full,&info)<0)return -ENOENT;
        if(!info.is_dir)return -20;
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
    if(nr==400) { /* spawn one of the built-in ELF64 programs */
        char name[32];
        int rc=user_string(a,name,sizeof(name));if(rc)return rc;
        return ring3_spawn(name,task_pid);
    }
    if(nr==61) { /* waitpid(pid,status,0|WNOHANG), including pid=-1 */
        if(c>1 || (a!=UINT64_MAX && (a==0 || a>0x7fffffffULL)))return -EINVAL;
        if(b && !user_range(b,sizeof(int),1))return -EFAULT;
        int status=0;
        int rc=ring3_wait_status(task_pid,(int)a,&status);
        if(rc==-EAGAIN && c==1)return 0;
        if(rc>0 && b)*(int *)(uintptr_t)b=status;
        return rc;
    }
    if(nr==402) { /* readdir(path,index,entry), fixed-size private ABI */
        char path[256];
        int rc=user_string(a,path,sizeof(path));if(rc)return rc;
        char full[256];rc=task_path(path,full,sizeof(full));if(rc)return rc;
        if(!user_range(b,sizeof(struct vfs_dirent),1))return -EFAULT;
        if(c>128)return -EINVAL;
        struct vfs_dirent entry;
        rc=vfs_readdir(full,(unsigned)c,&entry);
        if(rc==1) *(struct vfs_dirent *)(uintptr_t)b=entry;
        return rc<0?-ENOENT:rc;
    }
    if(nr==406) return (long)((process_live_count()<<16)|process_zombie_count());
    if(nr==407) return (long)memory_free_pages();
    if(nr==408) {
        if(!ring3_is_init(task_pid))return -EACCES;
        console_clear();return 0;
    }
    if(nr==404) { /* PKS-only power management (init capability) */
        if(!ring3_is_init(task_pid))return -EACCES;
        console_write("kilons: requesting ACPI S5 power off...\n");
        return power_shutdown()?0:-ENOSYS;
    }
    if(nr==60) return -ENOSYS; /* exit is handled in ring3 interrupt stub */
    return -ENOSYS;
}

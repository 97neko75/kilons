/* Hosted syscall/descriptor regression. User-pointer validation is mocked;
 * QEMU must separately validate real page-table user access. */
#include <kilons/posix.h>
#include <kilons/record_lock.h>
#include "../include/fcntl_flags.h"
#include <kilons/kerrno.h>
#include "../include/sys/stat.h"
#include <kilons/vfs.h>
#include <kilons/ramfs.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
uint64_t interrupt_ticks(void){return 12345;}
bool vm_user_check(uint64_t address,uint64_t size,bool writable) {
    (void)size;(void)writable;return address!=0;
}
void console_putc(char ch) {(void)ch;}
void console_write(const char *p) {(void)p;}
void console_clear(void) {}
int keyboard_available(void){return 0;}
char keyboard_poll(void) {return 0;}
int ring3_wait_status(uint32_t parent,int child,int *status) {(void)parent;(void)child;(void)status;return -10;}
uint32_t process_parent_of(uint32_t pid) {return pid==2?1:0;}
unsigned process_live_count(void) {return 2;}
unsigned process_zombie_count(void) {return 0;}
uint32_t memory_free_pages(void) {return 1000;}
static long call(long nr,long a,long b,long c) {
    return posix_dispatch((uint64_t)nr,(uint64_t)a,(uint64_t)b,(uint64_t)c);
}
static void search_descriptor_test(void) {
    assert(call(83,(long)"/tmp/ancestor",0700,0)==0);
    assert(call(83,(long)"/tmp/ancestor/dir",0700,0)==0);
    int rd=(int)call(2,(long)"/tmp/ancestor/dir",O_RDONLY|O_DIRECTORY,0);
    int dir=(int)call(2,(long)"/tmp/ancestor/dir",O_SEARCH,0);
    assert(dir>=0 && rd>=0);
    assert(call(90,(long)"/tmp/ancestor",0,0)==0);
    assert(call(91,dir,0600,0)==0);
    int file=posix_openat(dir,(long)"item",O_RDWR|O_CREAT|O_EXCL,0600);
    assert(file>=0 && call(3,file,0,0)==0);
    assert(call(266,(long)"item",dir,(long)"alias")==0);
    struct stat st;
    assert(posix_fstatat(dir,(long)"alias",(long)&st,0,sizeof st)==0);
    assert(posix_fstatat(rd,(long)"item",(long)&st,0,sizeof st)==-K_EACCES);
    assert(posix_linkat(dir,(long)"item",dir,(long)"linked",0)==0);
    assert(posix_renameat(dir,(long)"linked",dir,(long)"moved")==0);
    assert(call(91,dir,0400,0)==0);
    assert(posix_openat(dir,(long)"denied",O_CREAT|O_EXCL|O_RDWR,0600)==-K_EACCES);
    assert(call(91,dir,0700,0)==0);
    assert(posix_fstatat(rd,(long)"item",(long)&st,0,sizeof st)==0);
    assert(call(263,dir,(long)"alias",0)==0 && call(263,dir,(long)"moved",0)==0);
    assert(call(263,dir,(long)"item",0)==0);
    assert(call(3,dir,0,0)==0 && call(3,rd,0,0)==0);
    assert(call(90,(long)"/tmp/ancestor",0700,0)==0);
    assert(call(84,(long)"/tmp/ancestor/dir",0,0)==0 && call(84,(long)"/tmp/ancestor",0,0)==0);
}
static long lock_op(int fd,int cmd,short type,long start,long length) {
    struct flock lock={.l_type=type,.l_start=start,.l_len=length};
    return call(72,fd,cmd,(long)&lock);
}
static void query_lock(int fd,long at,short type,int pid,long start,long len) {
    struct flock q={.l_type=F_WRLCK,.l_start=at,.l_len=1};
    assert(call(72,fd,F_GETLK,(long)&q)==0 && q.l_type==type);
    if(type!=F_UNLCK)assert(q.l_pid==pid && q.l_start==start && q.l_len==len && !q.l_whence);
}
static void record_lock_test(void) {
    const char *path="/tmp/lock-test";
    int fd=(int)call(2,(long)path,0100|2,0600);
    int other=(int)call(2,(long)path,0,0);
    assert(fd>=0 && other>=0);
    assert(lock_op(other,F_SETLK,F_WRLCK,0,0)==-K_EBADF);
    assert(lock_op(fd,F_SETLK,F_WRLCK,0,0)==0);
    assert(lock_op(fd,F_SETLK,F_RDLCK,5,10)==0);
    assert(lock_op(fd,F_SETLK,F_UNLCK,13,-4)==0);
    assert(posix_task_clone(1,2));
    posix_task_activate(2);
    query_lock(fd,0,F_WRLCK,1,0,5);
    query_lock(fd,6,F_RDLCK,1,5,4);
    query_lock(fd,10,F_UNLCK,0,0,0);
    query_lock(fd,13,F_RDLCK,1,13,2);
    query_lock(fd,INT64_MAX,F_WRLCK,1,15,0);
    assert(lock_op(fd,F_SETLK,F_WRLCK,7,1)==-K_EAGAIN);
    assert(lock_op(fd,F_SETLK,F_RDLCK,7,1)==0);
    assert(lock_op(fd,F_SETLK,F_WRLCK,10,1)==0);
    assert(call(8,fd,2,0)==2);
    struct flock rel={.l_type=F_WRLCK,.l_whence=1,.l_start=-2,.l_len=1};
    assert(call(72,fd,F_SETLKW,(long)&rel)==-K_EAGAIN);
    assert(record_resume(2)==-K_EAGAIN);
    posix_task_activate(1);
    assert(lock_op(fd,F_SETLKW,F_RDLCK,10,1)==-K_EDEADLK);
    assert(call(8,fd,50,0)==50); /* Waiter's range was fixed at entry. */
    assert(call(3,other,0,0)==0); /* independent open, same inode */
    assert(record_resume(2)==0);
    query_lock(fd,0,F_WRLCK,2,0,1);
    query_lock(fd,50,F_UNLCK,0,0,0);
    assert(call(87,(long)path,0,0)==0);
    int fresh=(int)call(2,(long)path,0100|2,0600);
    assert(fresh>=0);
    query_lock(fresh,0,F_UNLCK,0,0,0);
    posix_task_discard(2);
    query_lock(fd,0,F_UNLCK,0,0,0);
    /* Force a split at capacity. Failed conversion preserves the old lock. */
    assert(lock_op(fd,F_SETLK,F_WRLCK,0,3)==0);
    int count=1;
    for(;count<1024;count++) {
        long rc=lock_op(fd,F_SETLK,F_WRLCK,4+2*count,1);
        if(rc==-K_ENOLCK)break;
        assert(rc==0);
    }
    assert(count<1024 && lock_op(fd,F_SETLK,F_RDLCK,1,1)==-K_ENOLCK);
    assert(posix_task_clone(1,2));
    posix_task_activate(2);
    query_lock(fd,1,F_WRLCK,1,0,3);
    posix_task_end();
    posix_task_activate(1);
    assert(call(3,fd,0,0)==0 && call(3,fresh,0,0)==0);
    assert(call(87,(long)path,0,0)==0);
}
static void symlink_test(void) {
    int dir=(int)call(2,(long)"/tmp",0,0);
    assert(dir>=0);
    assert(call(266,(long)"target",dir,(long)"alias")==0);
    char buf[256]={'x','x','x','x'};
    assert(posix_readlinkat(dir,(long)"alias",(long)buf,3)==3 && !memcmp(buf,"tarx",4));
    struct stat st;
    assert(posix_fstatat(dir,(long)"alias",(long)&st,0x100,sizeof(struct stat))==0 && S_ISLNK(st.st_mode) && st.st_size==6);
    assert(call(4,(long)"/tmp/alias",(long)&st,0)==-2);
    assert(call(2,(long)"/tmp/alias",0300|2,0600)==-17);
    assert(call(2,(long)"/tmp/alias",0400000|2,0)==-40);
    int fd=(int)call(2,(long)"/tmp/alias",0100|2,0600);
    assert(fd>=0 && call(3,fd,0,0)==0);
    int via_dir=(int)posix_openat(dir,(long)"alias",0,0);
    assert(via_dir>=0 && call(3,via_dir,0,0)==0);
    assert(posix_openat(dir,(long)"alias",0200000,0)==-20);
    assert(posix_openat(-1,(long)"alias",0,0)==-9);
    assert(call(4,(long)"/tmp/target",(long)&st,0)==0 && S_ISREG(st.st_mode));
    assert(call(88,(long)"/tmp",(long)"/tmp/subdir",0)==0);
    assert(call(80,(long)"/tmp/subdir",0,0)==0);
    assert(call(79,(long)buf,sizeof buf,0)==(long)buf && !strcmp(buf,"/tmp"));
    assert(call(80,(long)"/",0,0)==0);
    assert(call(82,(long)"/tmp/subdir/alias",(long)"/tmp/moved",0)==0);
    assert(call(4,(long)"/tmp/moved",(long)&st,0)==0 && S_ISREG(st.st_mode));
    assert(call(87,(long)"/tmp/moved",0,0)==0);
    assert(call(87,(long)"/tmp/target",0,0)==0);
    assert(call(88,(long)"loop",(long)"/tmp/loop",0)==0);
    assert(call(4,(long)"/tmp/loop",(long)&st,0)==-40);
    assert(call(87,(long)"/tmp/loop",0,0)==0 && call(87,(long)"/tmp/subdir",0,0)==0);
    assert(call(3,dir,0,0)==0);
}
static void clofork_test(void) {
    int pf[2];
    assert(call(293,(long)pf,O_CLOFORK|O_CLOEXEC,0)==0);
    assert(call(72,pf[0],F_GETFD,0)==(FD_CLOFORK|FD_CLOEXEC));
    int visible=(int)call(32,pf[0],0,0);
    int hidden=(int)call(72,pf[1],F_DUPFD_CLOFORK,12);
    assert(hidden>=12 && visible>=0);
    assert(posix_task_clone(1,2));
    posix_task_activate(2);
    assert(call(72,pf[0],F_GETFD,0)==-9 && call(72,hidden,F_GETFD,0)==-9);
    assert(call(72,visible,F_GETFD,0)==0);
    posix_task_activate(1);
    posix_task_exec_close(1);
    assert(call(72,hidden,F_GETFD,0)==FD_CLOFORK);
    assert(call(3,hidden,0,0)==0 && call(3,visible,0,0)==0);
    posix_task_activate(2);
    char c;
    assert(call(0,visible,(long)&c,1)==0);
    posix_task_end();
    posix_task_activate(1);
}
static void relative_io_test(void) {
    int base=(int)call(2,(long)"/tmp",0200000,0);
    assert(base>=0 && call(95,0007,0,0)==0022);
    assert(call(258,base,(long)"perm",0777)==0);
    int dir=(int)posix_openat(base,(long)"perm",0200000,0);
    assert(dir>=0 && call(81,dir,0,0)==0);
    assert(posix_renameat(base,(long)"perm",base,(long)"moved")==0);
    char buf[64]={0};
    assert(call(79,(long)buf,sizeof buf,0)==(long)buf && !strcmp(buf,"/tmp/moved"));
    int fd=(int)posix_openat(-100,(long)"f",0100|02000|2,0666);
    struct stat st;
    assert(fd>=0 && call(5,fd,(long)&st,0)==0);
    assert((st.st_mode&0777)==0660 && call(102,0,0,0)==st.st_uid && call(104,0,0,0)==st.st_gid);
    assert(call(1,fd,(long)"abc",3)==3);
    assert(posix_positioned_io(true,fd,(long)"X",1,1)==1);
    assert(posix_positioned_io(false,fd,(long)buf,3,0)==3 && !memcmp(buf,"aXc",3));
    assert(posix_positioned_io(true,fd,0,1,0)==-14);
    assert(call(8,fd,0,1)==3 && call(72,fd,3,0)==(02000|2));
    assert(posix_chownat(dir,(long)"f",0,0,0,0)==-1);
    assert(posix_chownat(fd,0,1000,1000,0,1)==0);
    int independent=(int)posix_openat(dir,(long)"f",O_RDWR,0);
    struct flock owner={.l_type=F_WRLCK}, conflict={.l_type=F_WRLCK};
    assert(independent>=0 && call(72,fd,F_OFD_SETLK,(long)&owner)==0);
    assert(call(72,independent,F_GETLK,(long)&conflict)==0 && conflict.l_pid==-1);
    assert(call(72,independent,F_OFD_SETLK,(long)&owner)==-11);
    int shared=(int)call(32,fd,0,0);
    assert(shared>=0 && call(3,fd,0,0)==0);
    assert(call(72,independent,F_OFD_SETLK,(long)&owner)==-11);
    owner.l_type=F_UNLCK;
    assert(call(72,shared,F_OFD_SETLK,(long)&owner)==0 && call(3,independent,0,0)==0);
    fd=shared;
    assert(posix_linkat(dir,(long)"f",dir,(long)"alias",0)==0);
    struct timespec times[2]={{123,0},{456,5000000}};
    assert(posix_utimens(dir,(long)"alias",(long)times,0,0)==0);
    assert(call(5,fd,(long)&st,sizeof st)==0 && st.st_nlink==2 && st.st_mtime==456 && st.st_atime==123);
    assert(posix_renameat(dir,(long)"alias",dir,(long)"f")==0);
    assert(call(263,dir,(long)"alias",0)==0);
    assert(posix_fchmodat(dir,(long)"f",0400,0)==0);
    assert(posix_faccessat(dir,(long)"f",2,0)==-13 && posix_faccessat(dir,(long)"f",4,0)==0);
    assert(posix_task_clone(1,2));
    posix_task_activate(2);
    assert(call(95,0777,0,0)==0007);
    assert(call(80,(long)"/",0,0)==0);
    posix_task_end();
    posix_task_activate(1);
    assert(call(95,0022,0,0)==0007);
    assert(call(263,dir,(long)"f",0)==0 && call(91,fd,0620,0)==0);
    assert(call(5,fd,(long)&st,0)==0 && (st.st_mode&0777)==0620);
    int second_dir=(int)posix_openat(base,(long)"moved",O_DIRECTORY,0);
    assert(second_dir>=0 && call(91,dir,0710,0)==0);
    assert(call(263,base,(long)"moved",0x200)==0);
    assert(call(5,second_dir,(long)&st,sizeof st)==0 && (st.st_mode&0777)==0710 && !st.st_nlink);
    assert(call(91,dir,0701,0)==0);
    assert(call(5,second_dir,(long)&st,sizeof st)==0 && (st.st_mode&0777)==0701);
    assert(call(3,second_dir,0,0)==0);
    assert(call(79,(long)buf,sizeof buf,0)==-2);
    assert(call(258,base,(long)"moved",0700)==0);
    assert(posix_openat(dir,(long)"stale",0100|2,0644)==-2);
    assert(call(81,base,0,0)==0 && call(263,base,(long)"moved",0x200)==0);
    assert(call(80,(long)"/",0,0)==0);
    assert(call(5,fd,(long)&st,sizeof st)==0 && st.st_nlink==0 && st.st_dev!=0);
    st.st_dev=12345;
    assert(call(5,fd,(long)&st,0)==0 && st.st_dev==12345);
    assert(call(3,dir,0,0)==0 && call(3,base,0,0)==0 && call(3,fd,0,0)==0);

    int pf[2];
    assert(call(293,(long)pf,04000|02000000,0)==0);
    assert(posix_positioned_io(false,pf[0],(long)buf,1,0)==-29);
    assert(call(0,pf[0],(long)buf,1)==-11 && call(72,pf[1],1,0)==1);
    assert(call(292,pf[1],pf[1],0)==-22);
    assert(call(292,pf[1],18,02000000)==18 && call(72,18,1,0)==1);
    posix_task_exec_close(1);
    assert(call(3,18,0,0)==-9 && call(3,pf[0],0,0)==-9 && call(3,pf[1],0,0)==-9);
}
int main(void) {
    ramfs_init();vfs_init();
    posix_task_begin(1);
    assert(call(105,0,0,0)==-1 && call(106,0,0,0)==-1);
    assert(call(117,1000,0,0)==0 && call(119,1000,0,0)==0);
    posix_exec_ids(04000,0,0);
    assert(call(102,0,0,0)==1000 && call(107,0,0,0)==0);
    assert(call(117,1000,0,0)==0 && call(117,0,0,0)==0);
    assert(call(105,1000,0,0)==0 && call(117,0,0,0)==-1);
    clofork_test();
    search_descriptor_test();
    relative_io_test();
    symlink_test();
    record_lock_test();
    int64_t ts[2]={0,0};
    assert(call(228,1,(long)(uintptr_t)ts,0)==0);
    assert(ts[0]==123 && ts[1]==450000000);
    assert(call(228,0,(long)(uintptr_t)ts,0)==0 && ts[0]>0);
    assert(call(228,2,(long)(uintptr_t)ts,0)==-22);
    int pipefd[2]={-1,-1};
    assert(call(22,(long)(uintptr_t)pipefd,0,0)==0);
    assert(pipefd[0]>=3 && pipefd[1]>=3 && pipefd[0]!=pipefd[1]);
    assert(call(8,pipefd[0],0,0)==-29); /* ESPIPE */
    assert(call(8,pipefd[1],0,0)==-29);
    assert(call(1,pipefd[1],(long)(uintptr_t)"AB",2)==2);
    assert(posix_task_clone(1,2));
    assert(call(3,pipefd[0],0,0)==0);
    assert(call(3,pipefd[1],0,0)==0);
    posix_task_activate(2);
    char c[8]={0};
    assert(call(0,pipefd[0],(long)(uintptr_t)c,2)==2);
    assert(c[0]=='A' && c[1]=='B');
    assert(call(0,pipefd[0],(long)(uintptr_t)c,1)==-11); /* no data yet */
    assert(call(3,pipefd[1],0,0)==0);
    assert(call(0,pipefd[0],(long)(uintptr_t)c,1)==0); /* EOF */
    assert(call(3,pipefd[0],0,0)==0);
    posix_task_end();
    posix_task_activate(1);
    assert(call(8,1,0,0)==-29); /* TTY stdout is not seekable */
    char cwd[256];
    assert(call(80,(long)(uintptr_t)"/tmp",0,0)==0);
    assert(call(79,(long)(uintptr_t)cwd,sizeof(cwd),0)==(long)(uintptr_t)cwd);
    assert(strcmp(cwd,"/tmp")==0);
    int ex=(int)call(2,(long)"exclusive",02000000|0300|2,0600);
    assert(ex>=0 && call(72,ex,1,0)==1);
    assert(call(1,ex,(long)"X",1)==1);
    assert(call(2,(long)"exclusive",0300|01000|2,0600)==-17);
    assert(call(8,ex,0,2)==1);
    int kept=(int)call(32,ex,0,0);
    posix_task_exec_close(1);
    assert(call(72,ex,1,0)==-9 && call(72,kept,1,0)==0);
    assert(call(3,kept,0,0)==0);
    assert(call(90,(long)"exclusive",0700,0)==0);
    int image=(int)call(2,(long)"exclusive",0,0);
    assert(image>=0 && call(8,image,1,0)==1);
    char byte=0;
    assert(vfs_exec_image(posix_exec_backend(image),&byte,1)==1 && byte=='X');
    assert(call(8,image,0,1)==1);
    assert(call(90,(long)"exclusive",0600,0)==0);
    assert(vfs_exec_image(posix_exec_backend(image),&byte,1)==-13);
    assert(call(3,image,0,0)==0);
    struct stat at;
    int dir=(int)call(2,(long)"/tmp",0,0);
    assert(dir>=0);
    assert(posix_fstatat(dir,(long)"exclusive",(long)&at,0x100,sizeof(struct stat))==0 && at.st_size==1);
    assert(posix_fstatat(-99,(long)"/tmp/exclusive",(long)&at,0,sizeof(struct stat))==0);
    assert(posix_fstatat(-99,(long)"exclusive",(long)&at,0,sizeof(struct stat))==-9);
    assert(posix_fstatat(0,(long)"exclusive",(long)&at,0,sizeof(struct stat))==-20);
    assert(call(3,dir,0,0)==0);
    int fd=(int)call(2,(long)(uintptr_t)"relative",0100|2,0644);
    assert(fd>=3);
    assert(call(1,fd,(long)(uintptr_t)"longer",6)==6);
    assert(call(3,fd,0,0)==0);
    fd=(int)call(2,(long)(uintptr_t)"relative",01000|2,0);
    assert(fd>=3);
    assert(call(1,fd,(long)(uintptr_t)"A",1)==1);
    assert(call(3,fd,0,0)==0);
    fd=(int)call(2,(long)(uintptr_t)"relative",02000|2,0);
    assert(fd>=3);
    assert(call(72,fd,3,0)==(2|02000)); /* F_GETFL + O_APPEND */
    assert(call(72,fd,1,0)==0); /* F_GETFD */
    assert(call(72,fd,2,1)==0); /* F_SETFD: FD_CLOEXEC */
    assert(call(72,fd,1,0)==1);
    int fd_clone=(int)call(72,fd,0,5); /* F_DUPFD */
    assert(fd_clone>=5);
    assert(call(72,fd_clone,1,0)==0); /* CLOEXEC not inherited by dup */
    assert(call(72,fd_clone,3,0)==(2|02000));
    assert(call(3,fd_clone,0,0)==0);
    assert(call(72,fd,4,2)==0); /* F_SETFL clears O_APPEND */
    assert(call(72,fd,3,0)==2);
    assert(call(72,fd,4,02000|2)==0); /* restore append */
    assert(call(8,fd,0,0)==0);
    assert(call(1,fd,(long)(uintptr_t)"B",1)==1);
    assert(call(8,fd,0,0)==0);
    assert(call(0,fd,(long)(uintptr_t)c,2)==2);
    assert(c[0]=='A' && c[1]=='B');
    struct stat result;
    assert(call(5,fd,(long)(uintptr_t)&result,0)==0);
    assert(result.st_size==2 && (result.st_mode&0100000));
    assert(call(3,fd,0,0)==0);
    assert(call(87,(long)(uintptr_t)"relative",0,0)==0);
    /* A writable redirected stdout is a regular file, not a terminal.
     * ftruncate(1) must succeed, while a read-only fd must reject it. */
    fd=(int)call(2,(long)(uintptr_t)"/tmp/redirect-test",0100|01000|1,0644);
    assert(fd>=3);
    assert(call(33,fd,1,0)==1);
    assert(call(3,fd,0,0)==0);
    assert(call(1,1,(long)(uintptr_t)"hello",5)==5);
    assert(call(77,1,0,0)==0);
    assert(call(4,(long)(uintptr_t)"/tmp/redirect-test",(long)(uintptr_t)&result,0)==0);
    assert(result.st_size==0);
    assert(call(8,1,0,0)==0); /* seekable even though descriptor number is 1 */
    assert(call(1,1,(long)(uintptr_t)"persist",7)==7);
    assert(call(8,1,-3,2)==4); /* SEEK_END with negative offset */
    assert(call(8,1,-2,1)==2); /* SEEK_CUR with negative offset */
    assert(call(8,1,-3,0)==-22); /* SEEK_SET must not seek before start */
    assert(call(8,1,3,1)==5);
    assert(call(8,1,0,2)==7);
    int readonly=(int)call(2,(long)(uintptr_t)"/tmp/redirect-test",0,0);
    assert(readonly>=3);
    assert(call(77,readonly,0,0)==-9); /* EBADF */
    assert(call(5,readonly,(long)(uintptr_t)&result,0)==0 && result.st_size==7);
    assert(call(3,readonly,0,0)==0);
    assert(call(3,1,0,0)==0);
    assert(call(87,(long)(uintptr_t)"/tmp/redirect-test",0,0)==0);
    posix_task_end();
    puts("host POSIX cwd/pipe/fork-FD/refcount/O_TRUNC/O_APPEND/stat/redirection PASS");
    return 0;
}

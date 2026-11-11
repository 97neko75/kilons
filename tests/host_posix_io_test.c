/* Hosted syscall/descriptor regression. User-pointer validation is mocked;
 * QEMU must separately validate real page-table user access. */
#include <kilons/posix.h>
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
int main(void) {
    ramfs_init();vfs_init();
    posix_task_begin(1);
    int64_t ts[2]={0,0};
    assert(call(228,1,(long)(uintptr_t)ts,0)==0);
    assert(ts[0]==123 && ts[1]==450000000);
    assert(call(228,0,(long)(uintptr_t)ts,0)==-22);
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

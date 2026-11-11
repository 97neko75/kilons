/* Host-only syscall boundary/FD-lifetime regression. VFS and POSIX backend
 * are real; user-memory permissions are modeled with explicit test regions.
 * Actual hardware page-table behavior requires the QEMU guest tests. */
#include <kilons/posix.h>
#include "../include/dirent.h"
#include <kilons/vfs.h>
#include <kilons/ramfs.h>
#include <kilons/process.h>
#include <kilons/memory.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define MAX_REGIONS 32
struct region {uintptr_t a,b;bool writable;};
static struct region regions[MAX_REGIONS];
static unsigned nregions;
static void reset_regions(void){nregions=0;}
static void region(const void *p,size_t n,bool writable) {
    assert(nregions<MAX_REGIONS && n>0);
    uintptr_t start=(uintptr_t)p;
    assert(start+n>=start);
    regions[nregions++]=(struct region){start,start+n,writable};
}
bool vm_user_check(uint64_t address,uint64_t len,bool writable) {
    if(!address || len>UINTPTR_MAX || address>UINTPTR_MAX-len)return false;
    for(unsigned i=0;i<nregions;i++) {
        if(address>=regions[i].a && address+len<=regions[i].b &&
           (!writable || regions[i].writable))return true;
    }
    return false;
}
uint64_t interrupt_ticks(void){return 0;}
void console_putc(char c){(void)c;}
void console_write(const char *s){(void)s;}
void console_clear(void){}
int keyboard_available(void){return 0;}
char keyboard_poll(void){return 0;}
int ring3_wait_status(uint32_t parent,int child,int *status){(void)parent;(void)child;(void)status;return -10;}
uint32_t process_parent_of(uint32_t pid){return pid==2?1:0;}
unsigned process_live_count(void){return 2;}
unsigned process_zombie_count(void){return 0;}
uint32_t memory_free_pages(void){return 2048;}
static long call(long nr,uint64_t a,uint64_t b,uint64_t c){return posix_dispatch((uint64_t)nr,a,b,c);}
#define PTR(v) ((uint64_t)(uintptr_t)(v))
int main(void) {
    ramfs_init();vfs_init();posix_task_begin(1);
    reset_regions();
    assert(call(1,1,0x1234,3)==-14); /* EFAULT */
    assert(call(0,0,0x1234,1)==-14);
    assert(call(1,1,0,0)==0); /* zero bytes require no user mapping */
    assert(call(2,0x1234,0,0)==-14);
    assert(call(22,0x1234,0,0)==-14);
    assert(call(74,1,0,0)==-22); /* fsync(stdout) is not a success */
    assert(call(228,1,0x1234,0)==-14);
    assert(call(4,0x1234,0x5678,0)==-14);
    char bad[256];memset(bad,'Z',sizeof bad);
    region(bad,sizeof bad,false);
    assert(call(2,PTR(bad),0,0)==-36); /* unterminated path */
    int descriptors[2]={-1,-1};
    region(descriptors,sizeof descriptors,true);
    for(unsigned i=0;i<4;i++) {
        assert(call(22,PTR(descriptors),0,0)==0);
        assert(descriptors[0]>=3 && descriptors[1]>=3);
        assert(call(74,descriptors[1],0,0)==-22);
        /* Invalid copy-out must never allocate a pipe. */
        assert(call(22,0x1234,0,0)==-14);
        assert(call(3,descriptors[0],0,0)==0);
        assert(call(3,descriptors[1],0,0)==0);
    }
    /* Bounded FIFO: wraparound, full/empty behavior, inherited references,
     * dup-close lifetime, EOF and EPIPE. Avoid treating EAGAIN as blocking. */
    char pipe_data[1024],pipe_back[1024];
    for(unsigned i=0;i<1024;i++)pipe_data[i]=(char)(i%251);
    memset(pipe_back,0,sizeof pipe_back);
    region(pipe_data,sizeof pipe_data,false);
    region(pipe_back,sizeof pipe_back,true);
    assert(call(22,PTR(descriptors),0,0)==0);
    int rd=descriptors[0],wr=descriptors[1];
    assert(posix_io_should_wait(1,rd,false,1));
    int alias=(int)call(32,rd,0,0);
    assert(call(72,alias,4,04000)==0 && call(72,rd,3,0)==04000);
    assert(!posix_io_should_wait(1,rd,false,1));
    assert(call(72,rd,4,0)==0 && call(3,alias,0,0)==0);
    assert(call(0,rd,PTR(pipe_back),1)==-11);  /* empty, writer alive */
    assert(call(1,wr,PTR(pipe_data),1024)==1024);
    assert(call(1,wr,PTR(pipe_data),1)==-11); /* internal retry condition */
    assert(posix_io_should_wait(1,wr,true,1));
    assert(!posix_io_should_wait(1,rd,false,1));
    assert(call(0,rd,PTR(pipe_back),333)==333);
    assert(memcmp(pipe_back,pipe_data,333)==0);
    assert(call(1,wr,PTR(pipe_data),333)==333); /* wrap FIFO tail */
    memset(pipe_back,0,sizeof pipe_back);
    assert(call(0,rd,PTR(pipe_back),1024)==1024);
    assert(memcmp(pipe_back,pipe_data+333,691)==0);
    assert(memcmp(pipe_back+691,pipe_data,333)==0);
    /* A small nonblocking write must be ALL or NOTHING (PIPE_BUF):
     * 950 bytes buffered, room=74, write(100) returns EAGAIN. */
    assert(call(1,wr,PTR(pipe_data),950)==950);
    assert(call(1,wr,PTR(pipe_data),100)==-11);
    assert(call(0,rd,PTR(pipe_back),1024)==950);
    assert(memcmp(pipe_back,pipe_data,950)==0);
    /* PIPE_BUF atomicity also applies when one userspace write crosses a
     * page boundary. Previously page-wise write() could commit the first
     * fragment and return a short count. */
    char across[8192] __attribute__((aligned(4096)));
    memset(across,'Q',sizeof across);
    region(across,sizeof across,false);
    assert(call(1,wr,PTR(pipe_data),990)==990);
    assert(call(1,wr,PTR(across+4096-24),64)==-11);
    assert(call(0,rd,PTR(pipe_back),1024)==990);
    assert(memcmp(pipe_back,pipe_data,990)==0);
    assert(call(1,wr,PTR(across+4096-24),64)==64);
    assert(call(0,rd,PTR(pipe_back),64)==64);
    for(unsigned i=0;i<64;i++)assert(pipe_back[i]=='Q');
    /* A large write may start with a small page fragment. */
    assert(call(1,wr,PTR(pipe_data),1000)==1000);
    assert(call(1,wr,PTR(across+4096-64),2048)==24);
    assert(call(0,rd,PTR(pipe_back),1024)==1024);
    /* Unmapped second page: fail before writing even the first fragment. */
    reset_regions();
    region(across,4096,false);
    region(pipe_back,sizeof pipe_back,true);
    assert(call(1,wr,PTR(across+4096-24),64)==-14);
    assert(call(0,rd,PTR(pipe_back),1)==-11);
    reset_regions();
    region(pipe_data,sizeof pipe_data,false);
    region(pipe_back,sizeof pipe_back,true);
    region(descriptors,sizeof descriptors,true);
    int borrowed=(int)call(32,wr,0,0);
    assert(borrowed>=0);
    assert(posix_task_clone(1,2));
    assert(call(3,wr,0,0)==0);
    assert(call(3,borrowed,0,0)==0);
    assert(call(0,rd,PTR(pipe_back),1)==-11); /* child writer lives */
    posix_task_activate(2);
    assert(call(3,rd,0,0)==0); /* inherited reader */
    assert(call(3,wr,0,0)==0);
    assert(call(3,borrowed,0,0)==0);
    posix_task_end();
    posix_task_activate(1);
    assert(call(0,rd,PTR(pipe_back),1)==0); /* no writers -> EOF */
    assert(call(3,rd,0,0)==0);
    /* EPIPE must not be confused with a full FIFO. */
    assert(call(22,PTR(descriptors),0,0)==0);
    rd=descriptors[0];wr=descriptors[1];
    assert(call(3,rd,0,0)==0);
    assert(call(1,wr,PTR(pipe_data),1)==-32);
    assert(call(3,wr,0,0)==0);
    reset_regions();
    const char path[]="/tmp/p0-syscalls";
    region(path,sizeof path,false);
    int fd=(int)call(2,PTR(path),0100|2,0600);
    assert(fd>=3);
    assert(call(1,fd,0x1234,4)==-14);
    assert(call(0,fd,0x1234,4)==-14);
    /* User I/O longer than one page must no longer be rejected as EINVAL.
     * The fixture validates exact ranges: later unmapped bytes return the
     * bytes already transferred rather than erasing prior progress. */
    char data[8192] __attribute__((aligned(4096)));
    char copy[8192] __attribute__((aligned(4096)));
    for(unsigned j=0;j<sizeof data;j++)data[j]=(char)(j&127);
    memset(copy,0,sizeof copy);
    region(data,sizeof data,false);
    region(copy,sizeof copy,true);
    assert(call(1,fd,PTR(data),sizeof data)==(long)sizeof data);
    assert(call(8,fd,0,0)==0);
    assert(call(0,fd,PTR(copy),sizeof copy)==(long)sizeof copy);
    assert(memcmp(data,copy,sizeof data)==0);
    assert(call(8,fd,0,0)==0);
    /* Allow only the first page of a longer request. */
    reset_regions();
    region(data,4096,false);
    region(copy,4096,true);
    assert(call(1,fd,PTR(data),8192)==4096);
    assert(call(8,fd,0,0)==0);
    assert(call(0,fd,PTR(copy),8192)==4096);
    reset_regions();
    region(path,sizeof path,false);
    region(data,sizeof data,false);
    assert(call(1,fd,PTR(data),UINT64_MAX)==-22);
    /* No-space failures must report ENOSPC (28), not a false zero-byte
     * success, once the bounded RAMFS backing inode reaches capacity. */
    assert(call(77,fd,0,0)==0);
    assert(call(8,fd,0,0)==0);
    for(unsigned j=0;j<4;j++)
        assert(call(1,fd,PTR(data),8192)==8192);
    assert(call(1,fd,PTR(data),1)==-28);
    reset_regions();
    region(path,sizeof path,false);
    assert(call(72,fd,1,0)==0);
    assert(call(72,fd,2,1)==0);
    assert(call(72,fd,1,0)==1);
    int another=(int)call(72,fd,1030,10);
    assert(another>=10 && call(72,another,1,0)==1);
    /* Child gets inherited descriptions but its own descriptor flags. */
    assert(posix_task_clone(1,2));
    posix_task_activate(2);
    assert(call(72,fd,1,0)==1 && call(72,another,1,0)==1);
    posix_task_exec_close(2);
    assert(call(3,fd,0,0)==-9 && call(3,another,0,0)==-9);
    posix_task_end();
    posix_task_activate(1);
    assert(call(72,fd,1,0)==1 && call(72,another,1,0)==1);
    int target=(int)call(32,fd,0,0);
    assert(target>=3 && call(72,target,1,0)==0);
    assert(call(33,fd,target,0)==target);
    assert(call(72,target,1,0)==0);
    assert(call(33,fd,fd,0)==fd); /* dup2 self is a no-op */
    assert(call(33,fd,123456,0)==-9);
    assert(call(72,fd,0,999)==-22);
    assert(call(72,fd,4,~0ULL)==-22);
    assert(call(3,target,0,0)==0);
    assert(call(3,another,0,0)==0);
    assert(call(3,fd,0,0)==0);
    /* A file once closed in all processes cannot be addressed via stale fd. */
    assert(call(1,fd,PTR(path),1)==-9);
    /* Descriptor table exhaustion must not truncate or steal a live fd. */
    int open_fds[21];
    for(unsigned i=0;i<21;i++) {
        open_fds[i]=(int)call(2,PTR(path),0100|2,0600);
        assert(open_fds[i]>=3);
    }
    assert(call(2,PTR(path),2,0)==-24); /* EMFILE */
    for(unsigned i=0;i<21;i++)assert(call(3,open_fds[i],0,0)==0);
    assert(call(3,0,0,0)==0);
    assert(call(2,PTR(path),0,0)==0);
    _Alignas(8) unsigned char output[sizeof(struct dirent)+1];
    region(output,sizeof output,true);
    assert(call(5,0,PTR(output+1),0)==0);
    assert(call(228,1,PTR(output+1),0)==0);
    assert(call(22,PTR(output+1),0,0)==0);
    memcpy(descriptors,output+1,sizeof descriptors);
    assert(call(3,descriptors[0],0,0)==0 && call(3,descriptors[1],0,0)==0);
    const char dir[]="/tmp";
    region(dir,sizeof dir,false);
    memset(output,0xab,sizeof output);
    for(unsigned nr=400;nr<=408;nr++)assert(call(nr,0,0,0)==-38);
    int directory=(int)call(2,PTR(dir),0,0);assert(directory>=0);
    assert(call(0,directory,PTR(output+1),sizeof(struct dirent))==(long)sizeof(struct dirent));
    struct dirent copied;memcpy(&copied,output+1,sizeof copied);
    assert(copied.d_ino && strcmp(copied.d_name,"p0-syscalls")==0);
    for(size_t i=strlen(copied.d_name)+1;i<sizeof copied.d_name;i++)assert(!copied.d_name[i]);
    assert(call(0,directory,PTR(output+1),sizeof copied)==0);
    assert(call(3,directory,0,0)==0);
    assert(call(87,PTR(path),0,0)==0);
    posix_task_end();
    /* A full process-I/O table must report exhaustion explicitly, then
     * become reusable after exit/discard. */
    for(uint32_t pid=3;pid<15;pid++)assert(posix_task_begin(pid));
    assert(!posix_task_begin(15));
    for(uint32_t pid=3;pid<15;pid++)posix_task_discard(pid);
    assert(posix_task_begin(15));
    posix_task_end();
    puts("P0 syscall crossing-pages / partial I-O / FD rollback / cloexec PASS");
    return 0;
}

#include "../libc/unistd.h"
static const char hello[]="Ring 3 ELF64: user libc write() works\n";
static const char content[]="kilons user process file I/O\n";
static char buffer[sizeof(content)];
void _start(void) {
    if(write(1,hello,sizeof(hello)-1)!=(long)sizeof(hello)-1) _exit(1);
    if(getpid()<2 || getppid()<1) _exit(2);
    if((shutdown_system()!=-1 || errno!=EACCES)) _exit(39); /* non-init cannot power off */
    /* A second process may now execute at the same user virtual addresses.
     * Verify this process's private writable page survives rescheduling. */
    buffer[0]='D';
    static const char yielding[]="Ring 3 first ELF64: yielding to another PID\n";
    (void)write(1,yielding,sizeof(yielding)-1);
    if(sched_yield()!=0 || buffer[0]!='D') _exit(38);
    if((write(0,hello,1)!=-1 || errno!=EBADF)) _exit(3); /* EBADF: stdin is not writable */
    if((read(1,buffer,1)!=-1 || errno!=EBADF)) _exit(4); /* EBADF: stdout is not readable */
    if((write(1,(const void *)1,1)!=-1 || errno!=EFAULT)) _exit(5); /* EFAULT */
    if((read(0,(void *)1,1)!=-1 || errno!=EFAULT)) _exit(6); /* EFAULT */
    if(write(1,hello,0)!=0 || read(0,buffer,0)!=0) _exit(7);
    int fd=open("/tmp/user-test",O_CREAT|O_RDWR,0644);
    if(fd<3) _exit(8);
    if(write(fd,content,sizeof(content)-1)!=(long)sizeof(content)-1) _exit(9);
    /* Keep the descriptor open while the other program uses fd=3 as well. */
    if(sched_yield()!=0) _exit(40);
    if(lseek(fd,0,SEEK_SET)!=0) _exit(10);
    if(read(fd,buffer,sizeof(content)-1)!=(long)sizeof(content)-1) _exit(11);
    for(unsigned i=0;i<sizeof(content)-1;i++) if(buffer[i]!=content[i]) _exit(12);
    int shared=dup(fd);
    if(shared<3 || shared==fd) _exit(16);
    if(dup2(fd,shared)!=shared || dup2(fd,fd)!=fd) _exit(17);
    if(dup2(fd,24)!=-1 || errno!=EBADF || dup(24)!=-1 || errno!=EBADF) _exit(18);
    if(close(fd)!=0) _exit(13);
    if(lseek(shared,0,SEEK_SET)!=0) _exit(19);
    if(read(shared,buffer,sizeof(content)-1)!=(long)sizeof(content)-1) _exit(20);
    if(close(shared)!=0) _exit(21);
    static const char dup_message[]="Ring 3: dup(stdout) write check PASS\n";
    int stdout_copy=dup(1);
    if(stdout_copy<3 ||
       write(stdout_copy,dup_message,sizeof(dup_message)-1)!=(long)sizeof(dup_message)-1) _exit(22);
    if(close(stdout_copy)!=0) _exit(23);
    if((read(fd,buffer,1)!=-1 || errno!=EBADF)) _exit(14); /* closed descriptor */
    /* Exercise the VFS namespace from Ring 3, not through KDS. */
    if(mkdir("/tmp/user-dir",0755)!=0) _exit(24);
    int nested=open("/tmp/user-dir/file",O_CREAT|O_RDWR,0644);
    if(nested<3) _exit(25);
    if(write(nested,content,sizeof(content)-1)!=(long)sizeof(content)-1) _exit(26);
    if(rmdir("/tmp/user-dir")==0) _exit(27); /* nonempty directory */
    if(rename("/tmp/user-dir/file","/tmp/user-dir/renamed")!=0) _exit(33);
    if((rename((const char *)1,"/tmp/invalid")!=-1 || errno!=EFAULT)) _exit(34);
    if(ftruncate(nested,4)!=0) _exit(35);
    if(truncate("/tmp/user-dir/renamed",sizeof(content)-1)!=0) _exit(36);
    /* stdout is not necessarily a TTY: PKS may redirect it to a writable
     * regular file, where ftruncate(1, 0) is valid and destroys output.
     * Test EBADF on an explicitly read-only descriptor instead. */
    int readonly=open("/tmp/user-dir/renamed",O_RDONLY,0);
    if(readonly<3 || ftruncate(readonly,0)!=-1 || errno!=EBADF ||
       close(readonly)!=0 || truncate((const char *)1,0)!=-1 || errno!=EFAULT)
        _exit(37);
    if(unlink("/tmp/user-dir/renamed")!=0) _exit(28);
    if(lseek(nested,0,SEEK_SET)!=0 ||
       read(nested,buffer,sizeof(content)-1)!=(long)sizeof(content)-1) _exit(29);
    if(close(nested)!=0 || rmdir("/tmp/user-dir")!=0) _exit(30);
    if((unlink((const char *)1)!=-1 || errno!=EFAULT) || (mkdir((const char *)1,0)!=-1 || errno!=EFAULT)) _exit(31);
    if((open((const char *)1,O_RDONLY,0)!=-1 || errno!=EFAULT)) _exit(32);
    static const char ok[]="Ring 3: file descriptors and syscall checks PASS\n";
    if(write(1,ok,sizeof(ok)-1)!=(long)sizeof(ok)-1) _exit(15);
    _exit(0);
}

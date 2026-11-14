#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int equal(const char *a,const char *b) {
    while(*a && *a==*b){a++;b++;}
    return *a==*b;
}
static int directory_check(int dir) {
    int copy=dup(dir);
    if(copy<0)return 0;
    DIR *stream=fdopendir(copy);
    if(!stream){close(copy);return 0;}
    long position=telldir(stream);
    struct dirent *first=readdir(stream);
    int ok=position>=0 && first && equal(first->d_name,"renamed");
    if(ok) {
        seekdir(stream,position);
        first=readdir(stream);
        ok=first && equal(first->d_name,"renamed");
        rewinddir(stream);
        first=readdir(stream);
        ok=ok && first && equal(first->d_name,"renamed") && dirfd(stream)==copy;
    }
    return closedir(stream)==0 && ok;
}
static int file_check(int dir) {
    int fd=openat(dir,"original",O_CREAT|O_EXCL|O_RDWR|O_APPEND,0666);
    if(fd<0)return 0;
    struct stat info;
    char buf[4]={0};
    int ok=fstat(fd,&info)==0 && (info.st_mode&0777)==0660 &&
           info.st_uid==getuid() && info.st_gid==getgid() &&
           write(fd,"abc",3)==3 && pwrite(fd,"Z",1,1)==1 &&
           pread(fd,buf,3,0)==3 && equal(buf,"aZc") && lseek(fd,0,SEEK_CUR)==3 &&
           renameat(dir,"original",dir,"renamed")==0 &&
           fchmodat(dir,"renamed",0400,0)==0 && faccessat(dir,"renamed",R_OK,0)==0 &&
           directory_check(dir);
    const struct timespec times[2]={{946684800,0},{1234567890,123}};
    if(ok)ok=linkat(dir,"renamed",dir,"alias",0)==0 &&
             futimens(fd,times)==0 && fstatat(dir,"alias",&info,0)==0 &&
             info.st_nlink==2 && info.st_atime==946684800 && info.st_mtime==1234567890 &&
             lseek(fd,0,SEEK_HOLE)==3 && unlinkat(dir,"alias",0)==0;
    if(unlinkat(dir,"renamed",0)<0) {
        unlinkat(dir,"original",0);
        ok=0;
    }
    if(fchmod(fd,0600)<0 || fstat(fd,&info)<0 || (info.st_mode&0777)!=0600)ok=0;
    return close(fd)==0 && ok;
}
static int group_check(void) {
    int gate[2];
    if(pipe2(gate,O_CLOEXEC)<0)return 0;
    pid_t sid=getsid(0);
    pid_t child=fork();
    if(!child) {
        char token;
        close(gate[1]);
        if(read(gate[0],&token,1)!=1)_exit(2);
        _exit(getpgrp()==getpid() && getsid(0)==sid?23:3);
    }
    close(gate[0]);
    if(child<0){close(gate[1]);return 0;}
    int ok=setpgid(child,child)==0 && getpgid(child)==child;
    if(write(gate[1],"R",1)!=1)ok=0;
    close(gate[1]);
    int status=0;
    if(waitpid(ok?-child:child,&status,0)!=child || !WIFEXITED(status) || WEXITSTATUS(status)!=23)return 0;
    child=fork();
    if(!child) {
        pid_t pid=getpid();
        _exit(setsid()==pid && getsid(0)==pid && getpgrp()==pid?31:4);
    }
    return child>0 && wait(&status)==child && WIFEXITED(status) && WEXITSTATUS(status)==31 && ok;
}
static int stop_check(void) {
    pid_t child=fork();
    if(child==0) {
        if(kill(getpid(),SIGSTOP)<0)_exit(1);
        _exit(42);
    }
    if(child<0)return 0;
    int status=0;
    if(waitpid(child,&status,WUNTRACED)!=child || !WIFSTOPPED(status) ||
       WSTOPSIG(status)!=SIGSTOP)return 0;
    if(kill(child,SIGCONT)<0)return 0;
    return waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==42;
}
static int exec_check(void) {
    int fd=open("/bin/echo",O_EXEC|O_CLOEXEC);
    if(fd<0)return 0;
    char *args[]={"echo","vfork/fexecve",0};
    char *env[]={0};
    pid_t child=vfork();
    if(child==0) {
        fexecve(fd,args,env);
        _exit(127);
    }
    close(fd);
    int status=0;
    siginfo_t info;
    return child>0 && waitid(P_PID,(id_t)child,&info,WEXITED|WNOWAIT)==0 &&
           info.si_pid==child && info.si_code==CLD_EXITED && info.si_status==0 &&
           waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0;
}
int main(void) {
    const char *root="/tmp/posixcheck.run";
    mode_t saved=umask(0007);
    if(mkdirat(AT_FDCWD,root,0777)<0)return 1;
    int dir=open(root,O_DIRECTORY|O_RDONLY);
    char cwd[64];
    int ok=dir>=0 && fchdir(dir)==0 && file_check(dir) &&
           getcwd(cwd,sizeof cwd) && equal(cwd,root) && group_check() && stop_check() && exec_check();
    if(dir>=0)close(dir);
    if(chdir("/")<0 || rmdir(root)<0)ok=0;
    umask(saved);
    if(ok)write(1,"posixcheck: PASS\n",17);
    return ok?0:1;
}

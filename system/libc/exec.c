#include "unistd.h"
#include "env.h"
#include <stdarg.h>
#include "../../include/exec_limits.h"
static int shell_script(const char *path,char *const argv[]){
    unsigned n=2;
    for(unsigned i=1;argv[0] && argv[i];i++){
        if(n==EXEC_MAX_STRINGS){errno=E2BIG;return -1;}
        n++;
    }
    char *args[n+1];
    args[0]="sh";
    args[1]=(char *)path;
    for(unsigned i=2;i<n;i++)args[i]=argv[i-1];
    args[n]=0;return execve("/bin/sh",args,environ);
}
int execvp(const char *name,char *const argv[]){
    if(!*name){errno=ENOENT;return -1;}
    unsigned size=0;
    while(name[size]){
        if(name[size++]=='/') {
            execve(name,argv,environ);
            return errno==ENOEXEC?shell_script(name,argv):-1;
        }
    }
    const char *part=getenv("PATH");
    if(!part)part="/bin";
    int saved=ENOENT;
    for(;;){
        unsigned n=0;
        while(part[n] && part[n]!=':')n++;
        char candidate[256];
        unsigned prefix=n?n+1:0;
        if(prefix+size<sizeof candidate){
            for(unsigned j=0;j<n;j++)candidate[j]=part[j];
            if(n)candidate[n]='/';
            for(unsigned j=0;j<=size;j++)candidate[prefix+j]=name[j];
            execve(candidate,argv,environ);
            if(errno==ENOEXEC)return shell_script(candidate,argv);
            if(errno==EACCES)saved=EACCES;
            else if(errno!=ENOENT && errno!=ENOTDIR)return -1;
        }else if(saved==ENOENT)saved=ENAMETOOLONG;
        if(!part[n])break;
        part+=n+1;
    }
    errno=saved;return -1;
}

int execv(const char *path,char *const argv[]) {
    return execve(path,argv,environ);
}

static int exec_list(const char *path,const char *first,va_list ap,
                     int search_path,int explicit_env) {
    unsigned count=0;
    const char *arg=first;
    va_list scan;
    va_copy(scan,ap);
    while(arg && count<EXEC_MAX_STRINGS) {
        count++;
        arg=va_arg(scan,char *);
    }
    va_end(scan);
    if(arg){errno=E2BIG;return -1;}
    char *argv[count+1];
    arg=first;
    for(unsigned i=0;i<count;i++){
        argv[i]=(char *)arg;
        arg=va_arg(ap,char *);
    }
    argv[count]=0;
    if(search_path)return execvp(path,argv);
    char *const *envp=explicit_env?va_arg(ap,char **):environ;
    return execve(path,argv,envp);
}

int execl(const char *path,const char *first,...) {
    va_list ap;
    va_start(ap,first);
    int result=exec_list(path,first,ap,0,0);
    va_end(ap);
    return result;
}

int execlp(const char *path,const char *first,...) {
    va_list ap;
    va_start(ap,first);
    int result=exec_list(path,first,ap,1,0);
    va_end(ap);
    return result;
}

int execle(const char *path,const char *first,...) {
    va_list ap;
    va_start(ap,first);
    int result=exec_list(path,first,ap,0,1);
    va_end(ap);
    return result;
}

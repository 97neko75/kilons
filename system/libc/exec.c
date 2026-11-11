#include "unistd.h"
#include "env.h"
static int attempt(const char *path,char *const argv[]){
    execve(path,argv,environ);
    if(errno!=8)return -1;
    char *args[10]={"sh",(char *)path};
    unsigned n=2;
    for(unsigned i=1;argv[i];i++){
        if(n>=8){errno=7;return -1;}
        args[n++]=argv[i];
    }
    args[n]=0;return execve("/bin/sh",args,environ);
}
int execvp(const char *name,char *const argv[]){
    if(!*name){errno=ENOENT;return -1;}
    unsigned size=0;
    while(name[size]){
        if(name[size++]=='/')return attempt(name,argv);
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
            attempt(candidate,argv);
            if(errno==EACCES)saved=EACCES;
            else if(errno!=ENOENT && errno!=20)return -1;
        }else if(saved==ENOENT)saved=36;
        if(!part[n])break;
        part+=n+1;
    }
    errno=saved;return -1;
}

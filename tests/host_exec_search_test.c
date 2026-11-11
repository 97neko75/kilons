/* Link the target libc files, replacing only the final kernel transition. */
#include <assert.h>
#include <string.h>
#include "../system/libc/env.h"
#include "../system/libc/unistd.h"
int errno;
static char attempted[8][256];
static unsigned calls;
static const char *script;
int execve(const char *path,char *const argv[],char *const env[]){
    assert(calls<8 && strlen(path)<sizeof attempted[0] && env==environ);
    strcpy(attempted[calls++],path);
    if(script && !strcmp(path,script))errno=8;
    else if(!strcmp(path,"/bin/sh")){
        assert(!strcmp(argv[1],script) && !strcmp(argv[2],"arg"));errno=13;
    }else errno=!strncmp(path,"denied/",7)?EACCES:ENOENT;
    return -1;
}
int main(void){
    char *initial[]={"INHERITED=yes","PATH=denied::/bin",0};
    char *args[]={"x","arg",0};
    environ=initial;
    assert(!strcmp(getenv("INHERITED"),"yes"));
    assert(execvp("x",args)==-1 && errno==EACCES && calls==3);
    assert(!strcmp(attempted[0],"denied/x") && !strcmp(attempted[1],"x") && !strcmp(attempted[2],"/bin/x"));
    calls=0;assert(execvp("./x",args)<0 && calls==1 && !strcmp(attempted[0],"./x"));
    assert(setenv("PATH","other",1)==0 && !strcmp(getenv("INHERITED"),"yes"));
    calls=0;script="./file";assert(execvp(script,args)<0 && calls==2 && !strcmp(attempted[1],"/bin/sh"));
    assert(unsetenv("INHERITED")==0 && getenv("INHERITED")==0 && !strcmp(getenv("PATH"),"other"));
}

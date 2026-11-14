/* Link the target libc files, replacing only the final kernel transition. */
#include <assert.h>
#include <string.h>
#include "../system/libc/env.h"
#include "../system/libc/unistd.h"
int errno;
static char attempted[8][256];
static unsigned calls;
static const char *script;
static char **expected_env;
static int check_args;
int execve(const char *path,char *const argv[],char *const env[]){
    assert(calls<8 && strlen(path)<sizeof attempted[0] && env==(expected_env?expected_env:environ));
    if(check_args)assert(!strcmp(argv[0],"zero") && !strcmp(argv[1],"one") && !argv[2]);
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
    script=0;calls=0;check_args=1;
    char *vector[]={"zero","one",0},*custom[]={"SPECIAL=yes",0};
    assert(execv("./x",vector)<0 && calls==1);
    assert(execl("./x","zero","one",(char *)0)<0 && calls==2);
    assert(execlp("x","zero","one",(char *)0)<0 && calls==3);
    assert(!strcmp(attempted[2],"other/x"));
    expected_env=custom;
    assert(execle("./x","zero","one",(char *)0,custom)<0 && calls==4);
    expected_env=0;check_args=0;
    assert(execl("./x","0","1","2","3","4","5","6","7","8",(char *)0)<0 && errno==ENOENT && calls==5);
    assert(unsetenv("INHERITED")==0 && getenv("INHERITED")==0 && !strcmp(getenv("PATH"),"other"));
    /* Removing an external entry must not corrupt owned entries which were
     * allocated in later slots and then compacted toward the front. */
    char *mixed[]={(char *)"EXTERNAL=yes",(char *)"OWNED=old",(char *)"TAIL=old",0};
    environ=mixed;
    assert(setenv("OWNED","new",1)==0);
    assert(setenv("TAIL","tail",1)==0);
    assert(unsetenv("EXTERNAL")==0);
    assert(!strcmp(getenv("OWNED"),"new") && !strcmp(getenv("TAIL"),"tail"));
}

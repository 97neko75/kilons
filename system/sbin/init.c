#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* Give only the initial interactive shell the write end of the session pipe. */
static int session(void){
    int control[2];
    if(pipe(control)<0)return -1;
    int shell=fork();
    if(shell==0){
        close(control[0]);
        if(control[1]!=3){
            if(dup2(control[1],3)<0)_exit(127);
            close(control[1]);
        }
        char *args[]={"sh","--boot-console",0};
        execve("/bin/sh",args,environ);
        _exit(127);
    }
    close(control[1]);
    if(shell<0){close(control[0]);return -1;}
    int status, pid, shell_ok=0;
    while((pid=waitpid(-1,&status,0))>0){
        if(pid==shell)shell_ok=WIFEXITED(status);
    }
    int drained=errno==ECHILD;
    char request;
    int ready=read(control[0],&request,1)==1 && request=='R';
    int result=ready ? read(control[0],&request,1) : -1;
    close(control[0]);
    if(!drained || !shell_ok || result<0)return -1;
    return result==1 && request=='S';
}

int main(void){
    int result;
    do {result=session();} while(result==0);
    if(result<0){
        static const char error[]="init: console session failed\n";
        write(2,error,sizeof error-1);
        return 1;
    }
    return 0;
}

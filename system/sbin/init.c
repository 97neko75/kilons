#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
extern char **environ;
int main(void){
    int shell=fork();
    if(shell<0){write(2,"init: fork failed\n",18);return 1;}
    if(shell==0){
        char *argv[]={"sh",0};
        execve("/bin/sh",argv,environ);
        write(2,"init: cannot execute /bin/sh\n",29);
        _exit(127);
    }
    int shell_status=1;
    for(;;){
        int status,pid=waitpid(-1,&status,0);
        if(pid<0)return errno==ECHILD?shell_status:1;
        if(pid==shell)shell_status=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
    }
}

#include <unistd.h>
#include <sys/wait.h>
static pid_t launch(char *name){
    pid_t pid=fork();
    if(pid==0){
        char *args[]={name,0};
        execvp(name,args);
        _exit(127);
    }
    return pid;
}
int main(void){
    pid_t hog=launch("hog");
    if(hog<0)return 1;
    pid_t quick=launch("quick");
    if(quick<0){waitpid(hog,0,0);return 1;}
    int a,b;
    pid_t first=waitpid(-1,&a,0);
    pid_t second=waitpid(-1,&b,0);
    int ok=first==quick && second==hog && WIFEXITED(a) && WEXITSTATUS(a)==0
           && WIFEXITED(b) && WEXITSTATUS(b)==0;
    if(ok)write(1,"preempt: PASS\n",14);
    else write(2,"preempt: FAIL\n",14);
    return !ok;
}

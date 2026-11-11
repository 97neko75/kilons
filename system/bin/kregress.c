#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
/* Run from an isolated process: waitpid(-1) only observes this test's children. */
static int start(char *name){
    int pid=fork();
    if(pid==0){
        char *args[]={name,0};
        execvp(name,args);_exit(127);
    }
    return pid;
}
static int collect(int pid,int signal){
    int status;
    if(pid<1 || waitpid(pid,&status,0)!=pid)return 0;
    return signal?(WIFSIGNALED(status) && WTERMSIG(status)==signal):
                  (WIFEXITED(status) && WEXITSTATUS(status)==0);
}
int main(void){
    char *images[]={"forkprobe","execprobe","mapprobe","fault","ud","divide"};
    int signals[]={0,0,0,SIGSEGV,SIGILL,SIGFPE};
    for(unsigned i=0;i<sizeof images/sizeof images[0];i++){
        if(!collect(start(images[i]),signals[i])){
            size_t n=0;while(images[i][n])n++;
            write(2,"failed: ",8);write(2,images[i],n);write(2,"\n",1);return 1;
        }
    }
    for(unsigned i=0;i<20;i++){
        int a=start("probe"),b=start("probe");
        int ok=collect(a,0);
        if(!collect(b,0) || !ok)return 1;
    }
    int status;
    if(waitpid(-1,&status,WNOHANG)!=-1 || errno!=ECHILD)return 1;
    write(1,"kregress: PASS\n",15);return 0;
}

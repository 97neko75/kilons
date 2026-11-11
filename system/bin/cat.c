#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sched.h>
static int copy(int fd){
    char data[512];
    for(;;){
        long n=read(fd,data,sizeof data);
        if(n<0 && errno==EAGAIN){sched_yield();continue;}
        if(n<=0)return n<0;
        for(long i=0;i<n;){
            long k=write(1,data+i,(size_t)(n-i));
            if(k<0 && errno==EAGAIN){sched_yield();continue;}
            if(k<=0)return 1;
            i+=k;
        }
    }
}
int main(int argc,char **argv){
    int failed=0;
    if(argc==1)return copy(0);
    for(int i=1;i<argc;i++){
        int stdin_arg=argv[i][0]=='-' && !argv[i][1];
        int fd=stdin_arg?0:open(argv[i],O_RDONLY);
        if(fd<0 || copy(fd))failed=1;
        if(fd>=0 && !stdin_arg && close(fd)<0)failed=1;
    }
    if(failed)write(2,"cat: I/O error\n",15);
    return failed;
}

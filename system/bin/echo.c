#include <unistd.h>
#include <errno.h>
#include <sched.h>
static int put(const char *s){
    size_t n=0;
    while(s[n])n++;
    while(n){
        long k=write(1,s,n);
        if(k<0 && errno==EAGAIN){sched_yield();continue;}
        if(k<=0)return 1;
        s+=k;n-=(size_t)k;
    }
    return 0;
}
int main(int argc,char **argv){
    for(int i=1;i<argc;i++)
        if((i>1 && put(" ")) || put(argv[i]))return 1;
    return put("\n");
}

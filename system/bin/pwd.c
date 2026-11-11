#include <unistd.h>
int main(void){
    char path[256];
    if(!getcwd(path,sizeof path))return 1;
    size_t n=0;while(path[n])n++;
    return write(1,path,n)!=(long)n || write(1,"\n",1)!=1;
}

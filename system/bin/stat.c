#include <sys/stat.h>
#include <unistd.h>
int main(int argc,char **argv){
    if(argc!=2)return 2;
    struct stat info;
    if(stat(argv[1],&info)<0){write(2,"stat: unavailable\n",18);return 1;}
    const char *kind=S_ISDIR(info.st_mode)?"directory size=":"file size=";
    size_t n=0;
    while(kind[n])n++;
    char digits[24];
    size_t i=sizeof digits;
    digits[--i]='\n';
    unsigned long size=(unsigned long)info.st_size;
    do {digits[--i]='0'+size%10;size/=10;} while(size);
    return write(1,kind,n)!=(long)n || write(1,digits+i,sizeof digits-i)!=(long)(sizeof digits-i);
}

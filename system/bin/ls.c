#include <dirent.h>
#include <errno.h>
#include <unistd.h>
int main(int argc,char **argv){
    if(argc>2)return 2;
    DIR *dir=opendir(argc==2?argv[1]:".");
    if(!dir){write(2,"ls: cannot open directory\n",26);return 1;}
    int rc=0;
    for(;;){
        errno=0;
        struct dirent *e=readdir(dir);
        if(!e){rc=errno!=0;break;}
        if(e->d_name[0]=='.')continue;
        size_t n=0;while(e->d_name[n])n++;
        if(write(1,e->d_name,n)!=(long)n || write(1,"\n",1)!=1){rc=1;break;}
    }
    return closedir(dir)<0 || rc;
}

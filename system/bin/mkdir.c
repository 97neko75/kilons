#include <sys/stat.h>
#include <unistd.h>
int main(int argc,char **argv){
    int failed=argc<2;
    for(int i=1;i<argc;i++)if(mkdir(argv[i],0777)<0)failed=1;
    if(failed)write(2,"mkdir: operand failed\n",sizeof("mkdir: operand failed\n")-1);
    return failed;
}

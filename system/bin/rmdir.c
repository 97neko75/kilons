#include <unistd.h>
int main(int argc,char **argv){
    int failed=argc<2;
    for(int i=1;i<argc;i++)if(rmdir(argv[i])<0)failed=1;
    if(failed)write(2,"rmdir: operand failed\n",sizeof("rmdir: operand failed\n")-1);
    return failed;
}

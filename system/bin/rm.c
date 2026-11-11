#include <unistd.h>
int main(int argc,char **argv){
    int failed=argc<2;
    for(int i=1;i<argc;i++)if(unlink(argv[i])<0)failed=1;
    if(failed)write(2,"rm: operand failed\n",sizeof("rm: operand failed\n")-1);
    return failed;
}

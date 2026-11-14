#include <unistd.h>
int main(void){
    static const char reset[]="\033[2J\033[H";
    return write(1,reset,sizeof reset-1)!=(long)(sizeof reset-1);
}

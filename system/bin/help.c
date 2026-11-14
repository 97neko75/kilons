#include <unistd.h>
int main(void){
    static const char help[]=
        "cd DIR, export NAME=value, exit [STATUS], reap\n"
        "shutdown   power off from the boot console\n"
        "ls /bin    list installed programs\n"
        "kregress   run process and fault checks (stress, test19, preempt also available)\n"
        "Use | for pipelines and <, >, >> for redirection.\n";
    return write(1,help,sizeof help-1)!=(long)(sizeof help-1);
}

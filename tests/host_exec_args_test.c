/* Exercise the production argument copier and AMD64 initial stack layout.
 * The old and new virtual address spaces use distinct modeled pages. */
#include <kilons/exec_args.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned char source[16384],dest[16384];
static struct vm_space old,new;
bool vm_space_copyin(const struct vm_space *space,void *buf,uint64_t ptr,size_t n) {
    assert(space==&old);
    if(ptr>=sizeof source || n>sizeof source-ptr)return false;
    memcpy(buf,source+ptr,n);return true;
}
bool vm_space_copyout(const struct vm_space *space,uint64_t ptr,const void *buf,size_t n) {
    assert(space==&new);
    if(ptr>=sizeof dest || n>sizeof dest-ptr)return false;
    memcpy(dest+ptr,buf,n);return true;
}
static uint64_t word(uint64_t addr) {
    uint64_t value;
    assert(addr<=sizeof dest-sizeof value);
    memcpy(&value,dest+addr,sizeof value);return value;
}
static void pointer(unsigned addr,uint64_t value) {
    memcpy(source+addr,&value,sizeof value);
}
int main(void) {
    struct exec_args args;
    uint64_t sp;
    assert(exec_args_read(NULL,&old,0,0)==-14);
    assert(exec_args_read(&args,NULL,0,0)==-14);
    /* 12 args, plus one long environment entry crossing a page. */
    for(unsigned i=0;i<12;i++)pointer(16+i*8,200);
    strcpy((char *)source+200,"argument");
    pointer(256,4000);
    memset(source+4000,'e',500);
    assert(exec_args_read(&args,&old,16,256)==0);
    assert(exec_args_stack(&args,&new,sizeof dest,&sp));
    assert(!(sp%16) && word(sp)==12);
    for(unsigned i=0;i<12;i++)assert(!strcmp((char *)dest+word(sp+8+i*8),"argument"));
    assert(word(sp+13*8)==0 && strlen((char *)dest+word(sp+14*8))==500);
    assert(!word(sp+15*8) && !word(sp+16*8) && !word(sp+17*8));
    pointer(8,5000);pointer(16,0);
    memset(source+5000,'a',EXEC_ARG_BYTES-9);
    assert(exec_args_read(&args,&old,8,0)==0);
    source[5000+EXEC_ARG_BYTES-9]='a';
    assert(exec_args_read(&args,&old,8,0)==-7);
    pointer(8,UINT64_MAX);
    assert(exec_args_read(&args,&old,0,8)==-14);
    assert(exec_args_read(&args,&old,0,0)==0);
    assert(!exec_args_stack(&args,&new,0,&sp));
    assert(exec_args_stack(&args,&new,sizeof dest,&sp));
    for(unsigned i=0;i<5;i++)assert(word(sp+i*8)==0);
    puts("exec argv/envp budget, invalid address, aligned initial stack PASS");
}

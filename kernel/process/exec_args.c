#include <kilons/exec_args.h>
#include <kilons/kerrno.h>
#include <limits.h>

/* Copy into kernel memory while the old mapping is still valid. */
static int append_vector(struct exec_args *args,const struct vm_space *source,uint64_t vector) {
    if(!vector)return 0;
    for(;;) {
        uint64_t ptr;
        if(vector>UINT64_MAX-sizeof ptr)return -K_EFAULT;
        if(!vm_space_copyin(source,&ptr,vector,sizeof ptr))return -K_EFAULT;
        if(!ptr)return 0;
        if(args->count>UINT_MAX/sizeof ptr-1U)return -7;
        size_t pointer_bytes=((size_t)args->count+1U)*sizeof ptr;
        char byte;
        do {
            if(args->used>=EXEC_ARG_BYTES || pointer_bytes>=EXEC_ARG_BYTES-args->used)return -7;
            if(!vm_space_copyin(source,&byte,ptr,1))return -K_EFAULT;
            args->text[args->used++]=byte;
            if(byte && ptr==UINT64_MAX)return -K_EFAULT;
            ptr++;
        } while(byte);
        args->count++;
        if(vector>UINT64_MAX-sizeof ptr)return -K_EFAULT;
        vector+=sizeof ptr;
    }
}
int exec_args_read(struct exec_args *args,const struct vm_space *source,uint64_t argv,uint64_t envp) {
    if(!args||!source)return -K_EFAULT;
    args->used=0;
    args->count=0;
    int rc=append_vector(args,source,argv);
    if(rc<0)return rc;
    args->argc=args->count;
    return append_vector(args,source,envp);
}
bool exec_args_stack(const struct exec_args *args,struct vm_space *space,uint64_t top,uint64_t *rsp) {
    if(!args||!space||!rsp || args->used>EXEC_ARG_BYTES || args->argc>args->count)return false;
    uint64_t header=((uint64_t)args->count+5U)*sizeof(uint64_t);
    if(top<args->used) return false;
    uint64_t strings=top-args->used;
    if(strings<header)return false;
    uint64_t frame=(strings-header) & ~UINT64_C(15);
    if(args->used && !vm_space_copyout(space,strings,args->text,args->used))return false;
    uint64_t argc=args->argc;
    if(!vm_space_copyout(space,frame,&argc,sizeof argc))return false;
    uint64_t slot=frame+sizeof argc;
    unsigned offset=0;
    for(unsigned i=0;i<=args->count;i++) {
        uint64_t zero=0;
        if(i==args->argc) {
            if(!vm_space_copyout(space,slot,&zero,sizeof zero))return false;
            slot+=sizeof zero;
        }
        if(i==args->count)break;
        if(offset>=args->used)return false;
        uint64_t address=strings+offset;
        if(!vm_space_copyout(space,slot,&address,sizeof address))return false;
        slot+=sizeof address;
        do {
            if(offset>=args->used)return false;
        } while(args->text[offset++]);
    }
    uint64_t tail[3]={0}; /* envp terminator and AT_NULL auxiliary pair */
    if(!vm_space_copyout(space,slot,tail,sizeof tail))return false;
    *rsp=frame;
    return true;
}

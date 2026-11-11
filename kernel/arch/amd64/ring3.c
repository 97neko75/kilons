#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <kilons/memory.h>
#include <kilons/vm.h>
#include <kilons/tss.h>
#include <kilons/console.h>
#include <kilons/posix.h>
#include <kilons/process.h>
#include <kilons/scheduler.h>
#include <kilons/vfs.h>
#include <kilons/ring3.h>
_Static_assert(offsetof(struct user_context,r15)==0, "ring3 frame r15 offset");
_Static_assert(offsetof(struct user_context,rax)==112, "ring3 frame rax offset");
_Static_assert(offsetof(struct user_context,rip)==120, "ring3 frame RIP offset");
_Static_assert(offsetof(struct user_context,ss)==152, "ring3 frame SS offset");
_Static_assert(sizeof(struct user_context)==160, "ring3 saved-frame size");
#define PAGE 4096ULL
#define IMAGE_PAGES 16U
#define STACK_PAGE 24U
#define TASK_SLOTS 12U
static uint32_t next_pid=1,init_pid;
struct ehdr { unsigned char ident[16];uint16_t type,machine;uint32_t version;uint64_t entry,phoff,shoff;uint32_t flags;uint16_t ehsize,phentsize,phnum,shentsize,shnum,shstrndx; } __attribute__((packed));
struct phdr {uint32_t type,flags;uint64_t offset,vaddr,paddr,filesz,memsz,align;} __attribute__((packed));
struct user_task {
    uint32_t pid;
    bool used,started;
    int reason,exit_status; /* 1=yield, 2=exit/fault, 3=wait */
    int wait_child,wait_status;
    uint64_t wait_user_status;
    bool wait_write_pending;
    struct vm_space space;
    struct user_context regs;
    uint8_t supervisor_stack[8192] __attribute__((aligned(16)));
};
static struct user_task tasks[TASK_SLOTS];
static struct user_task *current_task;
static void print_hex64(uint64_t n) {
    static const char digits[]="0123456789abcdef";
    console_write("0x");
    for(int shift=60;shift>=0;shift-=4)console_putc(digits[(n>>shift)&15]);
}
static void print_uint(uint64_t n) {
    char buffer[21];unsigned i=20;buffer[i]=0;
    do {buffer[--i]=(char)('0'+n%10);n/=10;} while(n);
    console_write(buffer+i);
}
/* All ELF pages are allocated before a process becomes runnable. A separate
 * PML4/CR3 and physical frame set exist for every loaded image. */
static bool load_elf(struct vm_space *space,const unsigned char *file,size_t len,uint64_t *entry) {
    if(len<sizeof(struct ehdr))return false;
    const struct ehdr *h=(const struct ehdr *)file;
    if(h->ident[0]!=127 || h->ident[1]!='E' || h->ident[2]!='L' || h->ident[3]!='F' ||
       h->ident[4]!=2 || h->ident[5]!=1 || h->ident[6]!=1 || h->type!=2 ||
       h->machine!=62 || h->version!=1 || h->ehsize!=sizeof(struct ehdr) ||
       h->phentsize!=sizeof(struct phdr) || !h->phnum || h->phnum>16 ||
       h->phoff>len || (uint64_t)h->phnum*sizeof(struct phdr)>len-h->phoff)return false;
    uint8_t writable[IMAGE_PAGES]={0},executable_page[IMAGE_PAGES]={0};
    bool used[IMAGE_PAGES]={0},loaded=false,executable=false;
    uint64_t physical[IMAGE_PAGES]={0};
    const struct phdr *ph=(const struct phdr *)(file+h->phoff);
    for(unsigned i=0;i<h->phnum;i++) {
        const struct phdr *seg=&ph[i];
        if(seg->type!=1)continue;
        /* GNU ld may emit an empty RW PT_LOAD for a text-only ELF. */
        if(!seg->memsz && !seg->filesz)continue;
        loaded=true;
        if(!seg->memsz || seg->filesz>seg->memsz || seg->offset>len ||
           seg->filesz>len-seg->offset || seg->vaddr<VM_USER_BASE ||
           seg->memsz>IMAGE_PAGES*PAGE || seg->vaddr-VM_USER_BASE>IMAGE_PAGES*PAGE-seg->memsz ||
           (seg->flags&~7U) || ((seg->flags&2U)&&(seg->flags&1U)))return false;
        for(unsigned k=0;k<i;k++) {
            const struct phdr *other=&ph[k];
            if(other->type==1 && other->vaddr<seg->vaddr+seg->memsz &&
               seg->vaddr<other->vaddr+other->memsz)return false;
        }
        unsigned first=(unsigned)((seg->vaddr-VM_USER_BASE)/PAGE);
        unsigned last=(unsigned)((seg->vaddr-VM_USER_BASE+seg->memsz-1)/PAGE);
        for(unsigned j=first;j<=last;j++) {
            if(used[j] && (writable[j]!=(uint8_t)!!(seg->flags&2) ||
                           executable_page[j]!=(uint8_t)!!(seg->flags&1)))return false;
            used[j]=true;writable[j]=(uint8_t)!!(seg->flags&2);
            executable_page[j]=(uint8_t)!!(seg->flags&1);
        }
        if((seg->flags&1U) && h->entry>=seg->vaddr && h->entry-seg->vaddr<seg->memsz)executable=true;
    }
    if(!loaded || !executable)return false;
    for(unsigned j=0;j<IMAGE_PAGES;j++) if(used[j]) {
        uint64_t pa=page_alloc();if(!pa)return false;
        physical[j]=pa;
        uint8_t *dst=(uint8_t *)(uintptr_t)pa;
        for(unsigned k=0;k<PAGE;k++)dst[k]=0;
        if(!vm_space_map(space,j,pa,writable[j],executable_page[j])) { (void)page_free(pa);return false; }
    }
    for(unsigned i=0;i<h->phnum;i++) {
        const struct phdr *seg=&ph[i];if(seg->type!=1)continue;
        for(uint64_t j=0;j<seg->filesz;j++) {
            uint64_t offset=seg->vaddr+j-VM_USER_BASE;
            ((uint8_t *)(uintptr_t)physical[offset/PAGE])[offset%PAGE]=file[seg->offset+j];
        }
    }
    *entry=h->entry;
    return true;
}
static bool load_stack(struct vm_space *space) {
    for(unsigned i=STACK_PAGE;i<STACK_PAGE+4;i++) {
        uint64_t pa=page_alloc();if(!pa)return false;
        uint8_t *dst=(uint8_t *)(uintptr_t)pa;
        for(unsigned j=0;j<PAGE;j++)dst[j]=0;
        if(!vm_space_map(space,i,pa,true,false)) { (void)page_free(pa);return false; }
    }
    return true;
}
#define PROGRAM_LIMIT (32U*1024U) /* Maximum executable file staged before ELF validation. */
static uint8_t image_buffer[PROGRAM_LIMIT];
/* A bounded, kernel-only VFS read; avoids keeping a global file descriptor
 * open and ensures that an ELF cannot exceed its staging buffer. */
static int load_program(struct vm_space *space,const char *path,uint64_t *entry) {
    struct vfs_info info;
    if(vfs_stat(path,&info)<0)return -vfs_errno();
    if(info.is_dir)return -13;
    if(vfs_access(path,1U)<0)return -13; /* executable permission */
    if(!info.size || info.size>PROGRAM_LIMIT)return -8;
    int fd=vfs_open(path,VFS_READ,0);
    if(fd<0)return -vfs_errno();
    size_t done=0;int n=0;
    while(done<info.size) {
        n=vfs_read(fd,image_buffer+done,info.size-done);
        if(n<=0)break;
        done+=(unsigned)n;
    }
    (void)vfs_close(fd);
    if(done!=info.size)return -8;
    if(!load_elf(space,image_buffer,done,entry))return -8; /* ENOEXEC */
    if(!load_stack(space))return -12; /* ENOMEM, caller rolls back */
    return 0;
}
static struct user_task *free_task(void) {
    for(unsigned i=0;i<TASK_SLOTS;i++)if(!tasks[i].used)return &tasks[i];
    return NULL;
}
#define EXEC_MAX_STRINGS 8U
#define EXEC_STRING_LIMIT 64U
struct exec_args {unsigned argc,envc;char argv[EXEC_MAX_STRINGS][EXEC_STRING_LIMIT];char env[EXEC_MAX_STRINGS][EXEC_STRING_LIMIT];};
static bool setup_exec_stack(struct vm_space *,struct user_context *,const struct exec_args *);
static int setup_task(struct user_task *t,const char *path) {
    t->regs=(struct user_context){0};
    t->space=(struct vm_space){0};
    if(!vm_space_create(&t->space))return -12;
    uint64_t entry=0;
    int result=load_program(&t->space,path,&entry);
    if(result<0) {
        vm_space_destroy(&t->space);
        return result;
    }
    t->regs.rip=entry;t->regs.cs=0x2b;t->regs.flags=0x202;
    t->regs.ss=0x33;
    static const struct exec_args initial={.argc=1,.envc=1,.argv={"/sbin/init"},.env={"PATH=/bin"}};
    if(!setup_exec_stack(&t->space,&t->regs,&initial)){
        vm_space_destroy(&t->space);return -12;
    }
    return 0;
}
static int start_init(void) {
    struct user_task *t=free_task();if(!t)return -11;
    if(next_pid>=0x7fffffffU)return -11;
    uint32_t pid=next_pid++;
    int rc=setup_task(t,"/sbin/init");
    if(rc<0)return rc;
    if(!process_create_child(pid,0)) {
        vm_space_destroy(&t->space);return -11;
    }
    t->pid=pid;t->reason=0;t->exit_status=255;t->started=false;
    t->wait_child=0;t->wait_user_status=0;t->wait_write_pending=false;
    t->used=true;
    return (int)pid;
}
/* fork duplicates the saved user register state and all writable pages;
 * child sees rax=0, parent sees new PID. */
int ring3_fork(const struct user_context *parent_context) {
    if(!current_task || !parent_context)return -22;
    struct user_task *t=free_task();if(!t)return -11;
    if(next_pid>=0x7fffffffU)return -11;
    uint32_t pid=next_pid++;
    t->space=(struct vm_space){0};
    if(!vm_space_clone(&t->space,&current_task->space))return -12;
    if(!process_create_child(pid,current_task->pid)) {
        vm_space_destroy(&t->space);return -11;
    }
    if(!posix_task_clone(current_task->pid,pid)) {
        (void)process_abort(pid);vm_space_destroy(&t->space);return -24;
    }
    t->pid=pid;t->regs=*parent_context;t->regs.rax=0;
    t->reason=0;t->exit_status=255;t->started=true;
    t->wait_child=0;t->wait_user_status=0;t->wait_write_pending=false;
    t->used=true;
    return (int)pid;
}
/* Bounded exec argv/envp copied before replacing the old address space.
 * The new initial stack follows the System V AMD64 argc/argv/envp layout. */
static int read_strv(uint64_t address,char arr[][EXEC_STRING_LIMIT],unsigned *count) {
    *count=0;if(!address)return 0;
    for(unsigned i=0;i<=EXEC_MAX_STRINGS;i++){
        uint64_t ptr;
        if(!vm_space_copyin(&current_task->space,&ptr,address+i*8,sizeof ptr))return -14;
        if(!ptr)return 0;
        if(i==EXEC_MAX_STRINGS)return -7;
        for(unsigned j=0;;j++){
            if(j==EXEC_STRING_LIMIT)return -7;
            if(!vm_space_copyin(&current_task->space,&arr[i][j],ptr+j,1))return -14;
            if(!arr[i][j])break;
        }
        (*count)++;
    }
    return -7;
}
static bool setup_exec_stack(struct vm_space *space,struct user_context *regs,const struct exec_args *args) {
    uint64_t sp=VM_USER_BASE+(STACK_PAGE+4)*PAGE;
    uint64_t argv[EXEC_MAX_STRINGS],env[EXEC_MAX_STRINGS];
    for(unsigned group=0;group<2;group++) {
        unsigned count=group?args->envc:args->argc;
        for(unsigned i=0;i<count;i++) {
            const char *str=group?args->env[i]:args->argv[i];
            unsigned len=0;while(str[len])len++;len++;
            sp-=len;
            if(!vm_space_copyout(space,sp,str,len))return false;
            if(group)env[i]=sp;else argv[i]=sp;
        }
    }
    sp&=~15ULL;
    /* The final 16-byte aligned stack includes a minimal AT_NULL vector. */
    uint64_t words[3+EXEC_MAX_STRINGS*2+2]={0};unsigned n=0;
    words[n++]=args->argc;
    for(unsigned i=0;i<args->argc;i++)words[n++]=argv[i];
    words[n++]=0;
    for(unsigned i=0;i<args->envc;i++)words[n++]=env[i];
    words[n++]=0;words[n++]=0;words[n++]=0;
    if(n&1U)sp-=8; /* argc begins on 16-byte-aligned initial RSP */
    sp-=(uint64_t)n*8;
    if(!vm_space_copyout(space,sp,words,n*8))return false;
    regs->rsp=sp;return true;
}
int ring3_execve(uint64_t path,uint64_t argv,uint64_t envp) {
    if(!current_task)return -22;
    char name[256],full[256];
    for(unsigned i=0;;i++){
        if(i==sizeof name)return -36;
        if(!vm_space_copyin(&current_task->space,&name[i],path+i,1))return -14;
        if(!name[i])break;
    }
    int rc=posix_task_path(name,full,sizeof full);
    if(rc<0)return rc;
    struct exec_args args={0};
    rc=read_strv(argv,args.argv,&args.argc);if(rc<0)return rc;
    rc=read_strv(envp,args.env,&args.envc);if(rc<0)return rc;
    struct vm_space replacement={0};
    if(!vm_space_create(&replacement))return -12;
    uint64_t entry=0;
    rc=load_program(&replacement,full,&entry);
    if(rc<0) {
        vm_space_destroy(&replacement);return rc;
    }
    struct user_context regs={0};
    regs.rip=entry;regs.cs=0x2b;regs.ss=0x33;regs.flags=0x202;
    if(!setup_exec_stack(&replacement,&regs,&args)) {
        vm_space_destroy(&replacement);return -12;
    }
    vm_space_activate(NULL);
    vm_space_destroy(&current_task->space);
    current_task->space=replacement;
    posix_task_exec_close(current_task->pid);
    current_task->regs=regs;
    current_task->reason=1;
    return 0;
}
/* 8-page anonymous arena; no demand paging, shared maps or file mappings. */
long ring3_mmap(uint64_t addr,uint64_t length,uint64_t prot,uint64_t flags) {
    if(!current_task || addr || !length || length>8*PAGE ||
       (prot&~3ULL) || !(prot&1ULL) ||
       flags!=0x22ULL)return -22; /* MAP_PRIVATE | MAP_ANONYMOUS */
    uint64_t pages=(length+PAGE-1)/PAGE;
    uint64_t where=vm_space_anonymous(&current_task->space,(unsigned)pages);
    if(!where)return -12;
    if(!(prot&2ULL) &&
       !vm_space_protect_anonymous(&current_task->space,where,(unsigned)pages,false)) {
        (void)vm_space_unmap_anonymous(&current_task->space,where,(unsigned)pages);
        return -12;
    }
    return (long)where;
}
long ring3_munmap(uint64_t addr,uint64_t length) {
    if(!current_task || !length || length>8*PAGE || (addr&(PAGE-1)))return -22;
    return vm_space_unmap_anonymous(&current_task->space,addr,(unsigned)((length+PAGE-1)/PAGE))?0:-22;
}
long ring3_mprotect(uint64_t addr,uint64_t length,uint64_t prot) {
    if(!current_task || !length || length>8*PAGE || (addr&(PAGE-1)) ||
       (prot&~3ULL) || !(prot&1ULL))return -22;
    return vm_space_protect_anonymous(&current_task->space,addr,(unsigned)((length+PAGE-1)/PAGE),!!(prot&2ULL))?0:-22;
}
int ring3_wait_status(uint32_t caller,int child,int *status) {
    if(child==-1) {
        int result=process_collect_any(caller,status);
        return result==0?-11:result;
    }
    if(child<=0 || !process_is_child(caller,(uint32_t)child))return -10;
    if(process_state_of((uint32_t)child)!=PROCESS_ZOMBIE)return -11;
    if(!process_collect(caller,(uint32_t)child,status))return -10;
    return child;
}

int ring3_trap_syscall(struct user_context *ctx) {
    static bool boot_syscall_seen;
    if(!current_task)return 1;
    if(!boot_syscall_seen) {
        boot_syscall_seen=true;
        console_write("kilons: P0 BOOT first user syscall nr=");
        print_uint(ctx->rax);
        console_write("\n");
    }
    if(ctx->rax==60) { /* _exit(status) */
        current_task->reason=2;
        current_task->exit_status=(int)((ctx->rdi&255U)<<8);
        return 1;
    }
    if(ctx->rax==9) { /* mmap: validate all six ABI arguments */
        /* The only implemented mode is private anonymous mapping, with
         * fd=-1 and offset=0. Never silently reinterpret unsupported flags
         * as an anonymous private mapping. */
        if(ctx->r8!=UINT64_MAX || ctx->r9!=0)
            ctx->rax=(uint64_t)(int64_t)-22;
        else
            ctx->rax=(uint64_t)ring3_mmap(ctx->rdi,ctx->rsi,ctx->rdx,ctx->r10);
        return 0;
    }
    if(ctx->rax==11) { /* munmap */
        ctx->rax=(uint64_t)ring3_munmap(ctx->rdi,ctx->rsi);
        return 0;
    }
    if(ctx->rax==10) { /* mprotect */
        ctx->rax=(uint64_t)ring3_mprotect(ctx->rdi,ctx->rsi,ctx->rdx);
        return 0;
    }
    if(ctx->rax==57) { /* fork */
        ctx->rax=(uint64_t)(int64_t)ring3_fork(ctx);
        return 0;
    }
    if(ctx->rax==59) { /* execve: successful call never returns to old image */
        int rc=ring3_execve(ctx->rdi,ctx->rsi,ctx->rdx);
        if(rc==0)return 1;
        ctx->rax=(uint64_t)(int64_t)rc;return 0;
    }
    if(ctx->rax==24) { /* sched_yield() */
        ctx->rax=0;
        current_task->regs=*ctx;
        current_task->reason=1;
        return 1;
    }
    uint64_t nr=ctx->rax;
    long result=posix_dispatch(nr,ctx->rdi,ctx->rsi,ctx->rdx);
    if(nr==1 && result==-14 && current_task->pid==init_pid) {
        static unsigned failed_writes;
        if(failed_writes++<3) {
            console_write("kilons: P0 BOOT EFAULT write(fd=");print_uint(ctx->rdi);
            console_write(", buf=");print_hex64(ctx->rsi);
            console_write(", len=");print_uint(ctx->rdx);
            console_write(", reason=");
            uint64_t count=ctx->rdx;
            if(count>4096U-(ctx->rsi&4095U))count=4096U-(ctx->rsi&4095U);
            print_uint((unsigned)vm_user_check_reason(ctx->rsi,count,false));
            console_write(")\n");
        }
    }
    if(nr<=1 && result==-11 && posix_io_should_wait(current_task->pid,ctx->rdi,nr==1,ctx->rdx)){
        /* No bytes transferred. Resume at the int $0x80 instruction with
         * its original arguments once a peer makes progress or closes. */
        current_task->regs=*ctx;
        current_task->regs.rip-=2;
        current_task->reason=4;
        return 1;
    }
    if(nr==61 && result==-11 && ctx->rdx==0) {
        /* The child exists but has not exited. Suspend this parent instead
         * of making userspace poll in a sched_yield() loop. */
        current_task->wait_child=(int)ctx->rdi;
        current_task->wait_user_status=ctx->rsi;
        current_task->wait_write_pending=false;
        ctx->rax=0;
        current_task->regs=*ctx;
        current_task->reason=3;
        return 1;
    }
    ctx->rax=(uint64_t)result;
    return 0;
}
bool ring3_preempt(const uint64_t *regs,uint64_t rip,uint64_t cs,
                   uint64_t flags,uint64_t rsp,uint64_t ss) {
    if(!current_task || (cs&3U)!=3U)return false;
    /* IRQ stack layout matches the saved registers; vector and error code
     * are excluded from the resulting user iret frame. */
    struct user_context *r=&current_task->regs;
    r->r15=regs[0];r->r14=regs[1];r->r13=regs[2];r->r12=regs[3];
    r->r11=regs[4];r->r10=regs[5];r->r9=regs[6];r->r8=regs[7];
    r->rdi=regs[8];r->rsi=regs[9];r->rbp=regs[10];r->rbx=regs[11];
    r->rdx=regs[12];r->rcx=regs[13];r->rax=regs[14];
    current_task->regs.rip=rip;
    current_task->regs.cs=cs;
    current_task->regs.flags=flags;
    current_task->regs.rsp=rsp;
    current_task->regs.ss=ss;
    current_task->reason=1;
    return true;
}
void ring3_on_fault(uint64_t vector,uint64_t error,uint64_t rip,uint64_t address) {
    (void)error;(void)rip;(void)address;
    if(!current_task) { console_write("kilons: invalid user fault context\n");for(;;)__asm__ volatile("cli;hlt"); }
    current_task->reason=2;
    current_task->exit_status=vector==0?8:vector==6?4:11; /* SIGFPE, SIGILL, SIGSEGV */
    console_write("kilons: isolated user exception PID=");print_uint(current_task->pid);
    console_write(" vector=");print_uint(vector);console_write("; kernel continues\n");
}
static void run_task(struct user_task *t) {
    static bool first_init_dispatch=true;
    bool trace_init = t->pid==init_pid && first_init_dispatch;
    if(trace_init)first_init_dispatch=false;
    if(!process_start(t->pid)) {
        if(t->pid==init_pid)console_write("kilons: P0 BOOT ERROR init not READY\n");
        return;
    }
    if(trace_init)console_write("kilons: P0 BOOT init process selected\n");
    bool first_run = !t->started;
    if(!t->started) {
        if(!posix_task_begin(t->pid)) {
            /* A missing task_io slot is fatal to this process, not to the
             * kernel. Report status through the normal wait/reap channel. */
            console_write("kilons: process IO allocation failed PID=");
            print_uint(t->pid);console_write("\n");
            (void)process_exit(t->pid,255<<8);
            vm_space_destroy(&t->space);
            t->used=false;
            return;
        }
        t->started=true;
    }
    posix_task_activate(t->pid);
    /* Check the private address space on first dispatch. Rechecking all
     * 512 entries on every keyboard-poll yield would harm responsiveness. */
    if(first_run && !vm_space_validate(&t->space)) {
        console_write("kilons: P0 BOOT ERROR user page-table audit FAIL\n");
        posix_task_end();
        (void)process_exit(t->pid,255<<8);
        vm_space_destroy(&t->space);
        t->used=false;
        posix_task_activate(0);
        return;
    }
    if(trace_init)console_write("kilons: P0 BOOT init IO and VM verified\n");
    vm_space_activate(&t->space);
    if(t->wait_write_pending) {
        if(t->wait_user_status &&
           !vm_space_copyout(&t->space,t->wait_user_status,&t->wait_status,sizeof(int)))
            t->regs.rax=(uint64_t)(int64_t)-14; /* EFAULT rather than kernel fault */
        t->wait_write_pending=false;
    }
    tss_set_rsp0((uint64_t)(uintptr_t)(t->supervisor_stack+sizeof(t->supervisor_stack)));
    current_task=t;
    t->reason=0;
    if(trace_init)console_write("kilons: P0 BOOT entering user iret\n");
    ring3_resume(&t->regs);
    current_task=NULL;
    vm_space_activate(NULL);
    if(t->reason==1) {
        (void)process_suspend(t->pid);
    } else if(t->reason==3 || t->reason==4) {
        (void)process_block(t->pid);
    } else {
        if(t->reason!=2)t->exit_status=255<<8;
        posix_task_end();
        (void)process_exit(t->pid,t->exit_status);
        vm_space_destroy(&t->space);
        console_write("kilons: user PID=");print_uint(t->pid);
        console_write(" exited status=");print_uint((unsigned)t->exit_status);console_write("\n");
        t->used=false;
    }
    /* Exit must release task descriptors BEFORE clearing the active task.
     * A yielding/blocked task keeps its own IO table for its next run. */
    posix_task_activate(0);
}
/* Complete a sleeping wait only after the child's resources were released.
 * User memory is written later under the parent's own CR3. */
static void wake_waiters(void) {
    for(unsigned i=0;i<TASK_SLOTS;i++) {
        struct user_task *t=&tasks[i];
        if(!t->used || process_state_of(t->pid)!=PROCESS_BLOCKED)continue;
        if(t->reason==4){
            if(!posix_io_should_wait(t->pid,t->regs.rdi,t->regs.rax==1,t->regs.rdx))
                (void)process_wake(t->pid);
            continue;
        }
        int status=0;
        int result=ring3_wait_status(t->pid,t->wait_child,&status);
        if(result==-11)continue;
        t->regs.rax=(uint64_t)(int64_t)result;
        if(result>0 && t->wait_user_status) {
            t->wait_status=status;t->wait_write_pending=true;
        }
        (void)process_wake(t->pid);
    }
}
void ring3_boot(void) {
    if(!vm_user_init()) {console_write("kilons: user VM unavailable\n");goto fail;}

    if(!vm_space_selftest()) {console_write("kilons: independent CR3 selftest FAIL\n");goto fail;}
    console_write("kilons: independent CR3 mappings selftest PASS\n");
    int init=start_init();
    if(init<1) {console_write("kilons: cannot start /sbin/init\n");goto fail;}
    init_pid=(uint32_t)init;
    process_set_reaper(init_pid);
    console_write("kilons: started /sbin/init\n");
    unsigned cursor=0;
    for(;;) {
        bool ran=false;
        for(unsigned step=0;step<TASK_SLOTS;step++) {
            unsigned id=(cursor+step)%TASK_SLOTS;
            struct user_task *t=&tasks[id];
            if(t->used && process_state_of(t->pid)==PROCESS_READY) {
                run_task(t);wake_waiters();cursor=(id+1)%TASK_SLOTS;ran=true;
                break;
            }
        }
        if(!ran) {
            if(!process_live_count())break;
            /* Guest boot fix: do not switch kernel worker contexts while
             * the Ring 3 dispatcher owns the launcher's saved stack. */
            __asm__ volatile("sti;hlt");
            wake_waiters();
        }
    }
    console_write("kilons: init exited; no privileged shell fallback\n");
fail:
    for(;;)__asm__ volatile("cli;hlt");
}

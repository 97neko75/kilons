#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <kilons/memory.h>
#include <kilons/vm.h>
#include <kilons/tss.h>
#include <kilons/console.h>
#include <kilons/posix.h>
#include <kilons/record_lock.h>
#include <kilons/process.h>
#include <kilons/user_signal.h>
#include <kilons/interrupt.h>
#include <kilons/scheduler.h>
#include <kilons/vfs.h>
#include <kilons/ring3.h>
#include <kilons/power.h>
_Static_assert(offsetof(struct user_context,r15)==0, "ring3 frame r15 offset");
_Static_assert(offsetof(struct user_context,rax)==112, "ring3 frame rax offset");
_Static_assert(offsetof(struct user_context,rip)==120, "ring3 frame RIP offset");
_Static_assert(offsetof(struct user_context,ss)==152, "ring3 frame SS offset");
_Static_assert(sizeof(struct user_context)==160, "ring3 saved-frame size");
#define PAGE 4096ULL
#define IMAGE_PAGES 16U
#define STACK_PAGE 24U
#define TASK_SLOTS 12U
static bool init_completed;
static uint32_t next_pid=1,init_pid;
struct ehdr { unsigned char ident[16];uint16_t type,machine;uint32_t version;uint64_t entry,phoff,shoff;uint32_t flags;uint16_t ehsize,phentsize,phnum,shentsize,shnum,shstrndx; } __attribute__((packed));
struct phdr {uint32_t type,flags;uint64_t offset,vaddr,paddr,filesz,memsz,align;} __attribute__((packed));
struct user_task {
    uint32_t pid;
    bool used,started;
    int reason,exit_status; /* 1=yield, 2=exit/fault, 3=wait */
    int wait_child,wait_status;
    uint32_t wait_group;
    uint64_t wait_user_status;
    bool wait_write_pending,wait_peek,wait_info;
    uint32_t wait_uid;
    unsigned wait_events;
    uint64_t pipe_written;
    uint64_t sleep_start,sleep_sec,sleep_nsec,sleep_remaining;
    struct user_signals signals;
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
static int load_program(struct vm_space *space,const char *path,int backend,uint64_t *entry,struct vfs_info *executable) {
    if(path) {
        backend=vfs_open(path,VFS_READ|VFS_EXEC,0);
        if(backend<0)return -vfs_errno();
    }
    int count=vfs_exec_image(backend,image_buffer,sizeof image_buffer);
    if(count>=0 && executable && vfs_fstat(backend,executable)<0)count=-vfs_errno();
    if(path)(void)vfs_close(backend);
    if(count<0)return count;
    if(!load_elf(space,image_buffer,(size_t)count,entry))return -8;
    if(!load_stack(space))return -12; /* ENOMEM, caller rolls back */
    return 0;
}
static struct user_task *free_task(void) {
    for(unsigned i=0;i<TASK_SLOTS;i++)if(!tasks[i].used)return &tasks[i];
    return NULL;
}
#include <kilons/exec_args.h>
#define STACK_TOP (VM_USER_BASE+(STACK_PAGE+4)*PAGE)
static int setup_task(struct user_task *t,const char *path) {
    t->regs=(struct user_context){0};
    t->space=(struct vm_space){0};
    if(!vm_space_create(&t->space))return -12;
    uint64_t entry=0;
    int result=load_program(&t->space,path,-1,&entry,NULL);
    if(result<0) {
        vm_space_destroy(&t->space);
        return result;
    }
    t->regs.rip=entry;t->regs.cs=0x2b;t->regs.flags=0x202;
    t->regs.ss=0x33;
    static const struct exec_args initial={
        .argc=1,.count=2,.used=sizeof("/sbin/init")+sizeof("PATH=/bin"),
        .text="/sbin/init\0PATH=/bin"
    };
    if(!exec_args_stack(&initial,&t->space,STACK_TOP,&t->regs.rsp)){
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
    /* Only live handler frames need to survive a fork inside a handler. */
    struct user_signals *dst=&t->signals,*src=&current_task->signals;
    for(unsigned i=0;i<32;i++) {
        dst->actions[i]=src->actions[i];
        dst->restorers[i]=src->restorers[i];
    }
    dst->mask=src->mask;
    dst->pending=0;
    dst->suspended=false;
    dst->suspend_mask=src->suspend_mask;
    dst->depth=src->depth;
    for(unsigned i=0;i<src->depth;i++)dst->frames[i]=src->frames[i];
    t->pid=pid;t->regs=*parent_context;t->regs.rax=0;
    t->reason=0;t->exit_status=255;t->started=true;
    t->wait_child=0;t->wait_user_status=0;t->wait_write_pending=false;
    t->used=true;
    return (int)pid;
}
/* A vfork parent resumes after image replacement or child termination.
 * It still owns the right to reap the child through waitpid. */
enum { WAIT_VFORK = 5, WAIT_RECORD_LOCK = 6, WAIT_SIGNAL = 7, WAIT_TIMER = 8, WAIT_PIPE_WRITE = 9 };
static void release_vfork_parent(uint32_t child) {
    for(unsigned i=0;i<TASK_SLOTS;i++) {
        struct user_task *parent=&tasks[i];
        if(parent->used && parent->reason==WAIT_VFORK && parent->wait_child==(int)child)
            (void)process_wake(parent->pid);
    }
}
static int replace_image(int backend,uint64_t argv,uint64_t envp) {
    /* ELF loading and argument staging run in the serialized dispatcher. */
    static struct exec_args args;
    int rc=exec_args_read(&args,&current_task->space,argv,envp);
    if(rc<0)return rc;
    struct vm_space replacement={0};
    if(!vm_space_create(&replacement))return -12;
    uint64_t entry=0;
    struct vfs_info executable;
    rc=load_program(&replacement,NULL,backend,&entry,&executable);
    if(rc<0) {
        vm_space_destroy(&replacement);return rc;
    }
    struct user_context regs={0};
    regs.rip=entry;regs.cs=0x2b;regs.ss=0x33;regs.flags=0x202;
    if(!exec_args_stack(&args,&replacement,STACK_TOP,&regs.rsp)) {
        vm_space_destroy(&replacement);return -12;
    }
    vm_space_activate(NULL);
    vm_space_destroy(&current_task->space);
    current_task->space=replacement;
    posix_exec_ids(executable.mode,executable.uid,executable.gid);
    posix_task_exec_close(current_task->pid);
    process_mark_exec(current_task->pid);
    signal_after_exec(&current_task->signals);
    current_task->regs=regs;
    release_vfork_parent(current_task->pid);
    current_task->reason=1;
    return 0;
}
int ring3_execve(uint64_t path,uint64_t argv,uint64_t envp) {
    if(!current_task)return -22;
    char name[256];
    for(unsigned i=0;;i++){
        if(i==sizeof name)return -36;
        if(path>UINT64_MAX-i)return -14;
        if(!vm_space_copyin(&current_task->space,&name[i],path+i,1))return -14;
        if(!name[i])break;
    }
    int fd=posix_open_exec(name);
    if(fd<0)return fd;
    int rc=replace_image(fd,argv,envp);
    (void)vfs_close(fd);
    return rc;
}
/* 8-page anonymous arena; no demand paging, shared maps or file mappings. */
long ring3_mmap(uint64_t addr,uint64_t length,uint64_t prot,uint64_t flags) {
    if(!current_task || addr || !length || length>8*PAGE ||
       (prot&~3ULL) || !prot ||
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
       (prot&~3ULL) || !prot)return -22;
    return vm_space_protect_anonymous(&current_task->space,addr,(unsigned)((length+PAGE-1)/PAGE),!!(prot&2ULL))?0:-22;
}
int ring3_wait_status(uint32_t caller,int child,int *status) {
    int result=process_collect_selected(caller,child,status);
    return result==0?-11:result;
}

static struct user_task *task_for_pid(uint32_t pid) {
    for(unsigned i=0;i<TASK_SLOTS;i++)
        if(tasks[i].used && tasks[i].pid==pid)return &tasks[i];
    return NULL;
}
static void notify_parent(uint32_t child,bool job_event) {
    struct user_task *parent=task_for_pid(process_parent_of(child));
    if(parent && !(job_event && (parent->signals.actions[SIGCHLD].flags&SA_NOCLDSTOP)))
        signal_queue(&parent->signals,SIGCHLD);
}
static void queue_signal(uint32_t pid,int sig) {
    struct user_task *t=task_for_pid(pid);
    if(!t)return;
    bool stopped=process_is_stopped(pid);
    signal_queue(&t->signals,sig);
    if(sig==SIGSTOP || sig==SIGCONT) {
        if(stopped==(sig==SIGCONT))notify_parent(pid,true);
    } else if(signal_stop(sig) && !t->signals.actions[sig].handler &&
              !(t->signals.mask & (1ULL<<sig))) {
        t->signals.pending&=~(1ULL<<sig);
        process_stop(pid,sig);
        if(!stopped)notify_parent(pid,true);
    }
}
static int dispatch(struct user_context *ctx) {
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
    if(ctx->rax==57 || ctx->rax==58) {
        bool suspend_parent=ctx->rax==58;
        int child=ring3_fork(ctx);
        ctx->rax=(uint64_t)(int64_t)child;
        if(suspend_parent && child>0) {
            current_task->regs=*ctx;
            current_task->reason=WAIT_VFORK;
            current_task->wait_child=child;
            return 1;
        }
        return 0;
    }
    if(ctx->rax==109) {
        ctx->rax=(uint64_t)(int64_t)process_set_group(current_task->pid,(int)ctx->rdi,(int)ctx->rsi);
        return 0;
    }
    if(ctx->rax==121 || ctx->rax==124) {
        ctx->rax=(uint64_t)(int64_t)process_group_id(current_task->pid,(int)ctx->rdi,ctx->rax==124);
        return 0;
    }
    if(ctx->rax==112) {
        ctx->rax=(uint64_t)(int64_t)process_new_session(current_task->pid);
        return 0;
    }
    if(ctx->rax==17 || ctx->rax==18) {
        ctx->rax=(uint64_t)posix_positioned_io(ctx->rax==18,(int)ctx->rdi,ctx->rsi,ctx->rdx,(int64_t)ctx->r10);
        return 0;
    }
    if(ctx->rax==260) {
        ctx->rax=(uint64_t)posix_chownat((int)ctx->rdi,ctx->rsi,(unsigned)ctx->rdx,(unsigned)ctx->r10,(int)ctx->r8,(int)ctx->r9);
        return 0;
    }
    if(ctx->rax==280) {
        ctx->rax=(uint64_t)posix_utimens((int)ctx->rdi,ctx->rsi,ctx->rdx,(int)ctx->r10,(int)ctx->r8);
        return 0;
    }
    struct user_signals *signals=&current_task->signals;
    if(ctx->rax==13 || ctx->rax==14) {
        ctx->rax=ctx->rax==13?signal_action(signals,&current_task->space,ctx):
                             signal_mask(signals,&current_task->space,ctx);
        return 0;
    }
    if(ctx->rax==15) {
        if(!signal_return(signals,ctx))ctx->rax=(uint64_t)-22;
        return 0;
    }
    if(ctx->rax==127) {
        uint64_t pending=signals->pending & signals->mask;
        ctx->rax=vm_space_copyout(&current_task->space,ctx->rdi,&pending,sizeof pending)?0:(uint64_t)-14;
        return 0;
    }
    if(ctx->rax==35) {
        struct {int64_t sec,nsec;} req;
        if(!vm_space_copyin(&current_task->space,&req,ctx->rdi,sizeof req)) {
            ctx->rax=(uint64_t)-14;
            return 0;
        }
        if(req.sec<0 || req.nsec<0 || req.nsec>=1000000000) {
            ctx->rax=(uint64_t)-22;
            return 0;
        }
        if(!req.sec && !req.nsec){ctx->rax=0;return 0;}
        /* Count only complete ticks; the first IRQ may be imminent. */
        current_task->sleep_sec=(uint64_t)req.sec;
        current_task->sleep_nsec=(uint64_t)req.nsec;
        current_task->sleep_start=interrupt_ticks();
        current_task->sleep_remaining=ctx->rsi;
        current_task->regs=*ctx;
        current_task->regs.rax=0;
        current_task->reason=WAIT_TIMER;
        return 1;
    }
    if(ctx->rax==34 || ctx->rax==130) {
        if(ctx->rax==130 && signal_suspend(signals,&current_task->space,ctx->rdi)<0) {
            ctx->rax=(uint64_t)-14;
            return 0;
        }
        ctx->rax=(uint64_t)-4;
        current_task->regs=*ctx;
        current_task->reason=WAIT_SIGNAL;
        return 1;
    }
    if(ctx->rax==62) {
        int sig=(int)ctx->rsi;
        int rc=(sig && !signal_valid(sig))?-22:
            process_send_signal(current_task->pid,(int)ctx->rdi,sig,posix_real_uid(),posix_effective_uid(),queue_signal);
        ctx->rax=(uint64_t)(int64_t)rc;
        if(!rc && process_is_stopped(current_task->pid)) {
            current_task->regs=*ctx;
            current_task->reason=1;
            return 1;
        }
        return 0;
    }
    if(ctx->rax==247) {
        /* libc uses the same encoded exit status as waitpid. The group
         * parameter can explicitly select pgid 1 independently of "any". */
        int selector=(int)ctx->rdi,status=0;
        uint32_t uid=0;
        unsigned group=(unsigned)ctx->r10,options=(unsigned)ctx->rdx;
        unsigned events=options>>2;
        if(!events || options&~31U || !vm_user_check(ctx->rsi,2*sizeof(status),true)) {
            ctx->rax=(uint64_t)(int64_t)((!events || options&~31U)?-22:-14);
            return 0;
        }
        if(!selector && !group)group=(unsigned)process_group_id(current_task->pid,0,false);
        int rc=process_wait_events(current_task->pid,selector,group,&status,!!(options&2),&uid,events);
        if(rc || (options&1)) {
            unsigned event[2]={(unsigned)status,uid};
            if(rc>0 && !vm_space_copyout(&current_task->space,ctx->rsi,event,sizeof event))rc=-14;
            ctx->rax=(uint64_t)(int64_t)rc;
            return 0;
        }
        current_task->wait_child=selector;
        current_task->wait_group=group;
        current_task->wait_events=events;
        current_task->wait_info=true;
        current_task->wait_peek=!!(options&2);
        current_task->wait_user_status=ctx->rsi;
        current_task->wait_write_pending=false;
        current_task->regs=*ctx;
        current_task->reason=3;
        return 1;
    }
    if(ctx->rax==265) {
        ctx->rax=(uint64_t)posix_linkat((int)ctx->rdi,ctx->rsi,(int)ctx->rdx,ctx->r10,(int)ctx->r8);
        return 0;
    }
    if(ctx->rax==264) {
        ctx->rax=(uint64_t)posix_renameat((int)ctx->rdi,ctx->rsi,(int)ctx->rdx,ctx->r10);
        return 0;
    }
    if(ctx->rax==439 || ctx->rax==452) {
        ctx->rax=ctx->rax==439?(uint64_t)posix_faccessat((int)ctx->rdi,ctx->rsi,(int)ctx->rdx,(int)ctx->r10):
                               (uint64_t)posix_fchmodat((int)ctx->rdi,ctx->rsi,(unsigned)ctx->rdx,(int)ctx->r10);
        return 0;
    }
    if(ctx->rax==257) {
        ctx->rax=(uint64_t)posix_openat((int)ctx->rdi,ctx->rsi,ctx->rdx,(unsigned)ctx->r10);
        return 0;
    }
    if(ctx->rax==267) {
        ctx->rax=(uint64_t)posix_readlinkat((int)ctx->rdi,ctx->rsi,ctx->rdx,ctx->r10);
        return 0;
    }
    if(ctx->rax==262) {
        ctx->rax=(uint64_t)posix_fstatat((int)ctx->rdi,ctx->rsi,ctx->rdx,(int)ctx->r10,ctx->r8);
        return 0;
    }
    if(ctx->rax==322) {
        int backend=posix_exec_backend((int)ctx->rdi);
        int rc=backend<0?backend:replace_image(backend,ctx->rsi,ctx->rdx);
        if(rc==0)return 1;
        ctx->rax=(uint64_t)(int64_t)rc;
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
    if(nr==1 && result>0 && (uint64_t)result<ctx->rdx &&
       posix_blocking_pipe_writer(current_task->pid,ctx->rdi)) {
        current_task->pipe_written=(uint64_t)result;
        current_task->regs=*ctx;
        current_task->reason=WAIT_PIPE_WRITE;
        return 1;
    }
    if(nr==1 && result==-32)signal_queue(signals,SIGPIPE);
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
    if(nr==72 && (ctx->rsi==F_SETLKW || ctx->rsi==F_OFD_SETLKW) && result==-11) {
        current_task->regs=*ctx;
        current_task->reason=WAIT_RECORD_LOCK;
        return 1;
    }
    if(nr==61 && result==-11 && ctx->rdx==0) {
        /* The child exists but has not exited. Suspend this parent instead
         * of making userspace poll in a sched_yield() loop. */
        current_task->wait_events=1;
        current_task->wait_info=false;
        current_task->wait_peek=false;
        current_task->wait_child=(int)ctx->rdi;
        if(!current_task->wait_child)
            current_task->wait_group=(uint32_t)process_group_id(current_task->pid,0,false);
        current_task->wait_user_status=ctx->rsi;
        current_task->wait_write_pending=false;
        current_task->regs=*ctx;
        current_task->reason=3;
        return 1;
    }
    ctx->rax=(uint64_t)result;
    return 0;
}
int ring3_trap_syscall(struct user_context *ctx) {
    if(dispatch(ctx))return 1;
    if(signal_ready(&current_task->signals)) {
        current_task->regs=*ctx;
        current_task->reason=1;
        return 1;
    }
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
    int sig=vector==0?SIGFPE:vector==6?SIGILL:SIGSEGV;
    if(current_task->signals.actions[sig].handler>1 && !(current_task->signals.mask & (1ULL<<sig))) {
        signal_queue(&current_task->signals,sig);
        current_task->reason=1;
        return;
    }
    current_task->reason=2;
    current_task->exit_status=sig; /* SIGFPE, SIGILL, SIGSEGV */
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
            release_vfork_parent(t->pid);
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
        release_vfork_parent(t->pid);
        vm_space_destroy(&t->space);
        t->used=false;
        posix_task_activate(0);
        return;
    }
    if(trace_init)console_write("kilons: P0 BOOT init IO and VM verified\n");
    vm_space_activate(&t->space);
    if(t->wait_write_pending) {
        unsigned event[2]={(unsigned)t->wait_status,t->wait_uid};
        if(t->wait_user_status &&
           !vm_space_copyout(&t->space,t->wait_user_status,event,t->wait_info?sizeof event:sizeof(int)))
            t->regs.rax=(uint64_t)(int64_t)-14; /* EFAULT rather than kernel fault */
        t->wait_write_pending=false;
    }
    tss_set_rsp0((uint64_t)(uintptr_t)(t->supervisor_stack+sizeof(t->supervisor_stack)));
    current_task=t;
    bool finish_pipe=t->reason==WAIT_PIPE_WRITE;
    t->reason=0;
    if(finish_pipe) {
        struct user_context *r=&t->regs;
        if(!signal_ready(&t->signals)) {
            long n=posix_dispatch(1,r->rdi,r->rsi+t->pipe_written,r->rdx-t->pipe_written);
            if(n>0)t->pipe_written+=(uint64_t)n;
            if(n==-32)signal_queue(&t->signals,SIGPIPE);
            if(t->pipe_written<r->rdx && (n>0 || n==-11) &&
               posix_blocking_pipe_writer(t->pid,r->rdi))t->reason=WAIT_PIPE_WRITE;
        }
        r->rax=t->pipe_written;
    }
    if(trace_init)console_write("kilons: P0 BOOT entering user iret\n");
    int sig=signal_ready(&t->signals);
    if(sig) {
        t->signals.pending&=~(1ULL<<sig);
        if(!t->signals.actions[sig].handler) {
            if(signal_stop(sig)) {
                process_stop(t->pid,sig);
                notify_parent(t->pid,true);
                t->reason=1;
            } else {
                t->reason=2;
                t->exit_status=sig;
            }
        } else if(!signal_enter(&t->signals,&t->space,&t->regs,sig)) {
            t->reason=2;
            t->exit_status=SIGSEGV;
        }
    }
    if(!t->reason)ring3_resume(&t->regs);
    process_note_uid(t->pid,posix_real_uid(),posix_saved_uid());
    current_task=NULL;
    vm_space_activate(NULL);
    if(t->reason==1) {
        (void)process_suspend(t->pid);
    } else if(t->reason==3 || t->reason==4 || t->reason==WAIT_VFORK || t->reason==WAIT_RECORD_LOCK || t->reason==WAIT_SIGNAL || t->reason==WAIT_TIMER || t->reason==WAIT_PIPE_WRITE) {
        (void)process_block(t->pid);
    } else {
        release_vfork_parent(t->pid);
        if(t->reason!=2)t->exit_status=255<<8;
        if(t->pid==init_pid)init_completed=t->exit_status==0;
        posix_task_end();
        uint32_t parent_pid=process_parent_of(t->pid);
        struct user_task *parent=task_for_pid(parent_pid);
        notify_parent(t->pid,false);
        (void)process_exit(t->pid,t->exit_status);
        if(parent && (parent->signals.actions[SIGCHLD].handler==1 ||
                      (parent->signals.actions[SIGCHLD].flags&SA_NOCLDWAIT))) {
            int status;
            (void)process_collect(parent_pid,t->pid,&status);
        }
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
        int sig=signal_ready(&t->signals);
        if(t->reason==WAIT_VFORK && sig!=SIGKILL)continue;
        if(sig && signal_stop(sig) && !t->signals.actions[sig].handler) {
            t->signals.pending&=~(1ULL<<sig);
            process_stop(t->pid,sig);
            notify_parent(t->pid,true);
            continue;
        }
        if(process_is_stopped(t->pid))continue;
        if(t->reason==WAIT_PIPE_WRITE) {
            if(sig || !posix_io_should_wait(t->pid,t->regs.rdi,true,t->regs.rdx-t->pipe_written))
                (void)process_wake(t->pid);
            continue;
        }
        if(t->reason==WAIT_TIMER) {
            uint64_t elapsed=interrupt_ticks()-t->sleep_start;
            uint64_t ticks=elapsed?elapsed-1:0;
            uint64_t sec=ticks/100,nsec=(ticks%100)*10000000;
            if(sec<t->sleep_sec || (sec==t->sleep_sec && nsec<t->sleep_nsec)) {
                if(!sig)continue;
                uint64_t remaining[2]={t->sleep_sec-sec,t->sleep_nsec};
                if(remaining[1]<nsec){remaining[0]--;remaining[1]+=1000000000;}
                remaining[1]-=nsec;
                t->regs.rax=(uint64_t)-4;
                if(t->sleep_remaining && !vm_space_copyout(&t->space,t->sleep_remaining,
                                                            remaining,sizeof remaining))
                    t->regs.rax=(uint64_t)-14;
            }
            (void)process_wake(t->pid);
            continue;
        }
        if(t->reason==3) {
            int status=0;
            int result=process_wait_events(t->pid,t->wait_child,t->wait_group,&status,
                                           t->wait_peek,&t->wait_uid,t->wait_events);
            if(result) {
                t->regs.rax=(uint64_t)(int64_t)result;
                if(result>0 && t->wait_user_status) {
                    t->wait_status=status;
                    t->wait_write_pending=true;
                }
                (void)process_wake(t->pid);
                continue;
            }
        }
        if(sig) {
            if(t->reason==WAIT_RECORD_LOCK)record_cancel_wait(t->pid);
            bool restart=(t->signals.actions[sig].flags&SA_RESTART)!=0;
            if(t->reason!=WAIT_SIGNAL && t->reason!=WAIT_VFORK) {
                if(restart && t->reason!=4)t->regs.rip-=2;
                if(!restart) {
                    if(t->reason==4)t->regs.rip+=2;
                    t->regs.rax=(uint64_t)-4;
                }
            }
            (void)process_wake(t->pid);
            continue;
        }
        if(t->reason==WAIT_SIGNAL)continue;
        if(t->reason==WAIT_RECORD_LOCK) {
            int rc=record_resume(t->pid);
            if(rc!=-11) {
                t->regs.rax=(uint64_t)(int64_t)rc;
                (void)process_wake(t->pid);
            }
            continue;
        }
        if(t->reason==4){
            if(!posix_io_should_wait(t->pid,t->regs.rdi,t->regs.rax==1,t->regs.rdx))
                (void)process_wake(t->pid);
            continue;
        }

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
            if(t->used && process_state_of(t->pid)==PROCESS_READY && !process_is_stopped(t->pid)) {
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
    if(init_completed){
        (void)power_shutdown();
        console_write("kilons: firmware did not power off; halting\n");
    }
    console_write("kilons: init exited; no privileged shell fallback\n");
fail:
    for(;;)__asm__ volatile("cli;hlt");
}

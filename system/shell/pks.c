#include "../libc/unistd.h"
/* PKS is an ordinary Ring 3 ELF64 executable. No privileged shell code is
 * entered during normal boot. Input arrives through the keyboard-backed TTY. */
static void out(const char *s) {size_t n=0;while(s[n])n++;(void)write(1,s,n);}
static int equal(const char *a,const char *b) {
    while(*a && *a==*b) {a++;b++;}return *a==*b;
}
static int starts(const char *a,const char *p) {
    while(*p) {if(*a++!=*p++)return 0;}
    return 1;
}
static void number(unsigned n) {
    char buf[16];unsigned i=15;buf[i]=0;
    do {buf[--i]=(char)('0'+n%10);n/=10;}while(n);
    out(buf+i);
}
static void help(void) {
    out("help               show commands\n");
    out("ls [PATH]          list a directory\n");
    out("cat PATH           read a file\n");
    out("stat PATH          inspect a file or directory\n");
    out("run NAME > PATH    execute and redirect stdout to a file\n");
    out("run NAME | filter  connect two ELF64 processes with a pipe\n");
    out("write PATH TEXT    create / write a file\n");
    out("mkdir PATH         create a directory\n");
    out("rm PATH            remove a file\n");
    out("rmdir PATH         remove a directory\n");
    out("run NAME           execute /bin/NAME ELF64 and wait\n");
    out("cd PATH            change working directory\n");
    out("pwd                print working directory\n");
    out("test               concurrency, wait and fault-isolation tests\n");
    out("stress             repeat private-CR3 and wait/reap 20 times\n");
    out("preempt            verify IRQ0 scheduling without voluntary yields\n");
    out("test19             fork, execve, mmap, file descriptor inheritance\n");
    out("reap               collect exited orphaned child records\n");
    out("ps                 show process counts\n");
    out("clear              clear terminal (ANSI for serial)\n");
    out("shutdown           ACPI S5 power off\n");
    out("Up/Down history, Left/Right cursor, Home/End, Del/Backspace edit\n");
}
static void list(const char *path) {
    struct k_dirent entry;
    for(unsigned index=0;index<128;index++) {
        int rc=k_readdir(path,&entry,index);
        if(rc<0) {out("ls: directory unavailable\n");return;}
        if(rc==0)return;
        out(entry.name);
        if(entry.is_dir)out("/");
        out("\n");
    }
}
static void cat(const char *path) {
    int fd=open(path,O_RDONLY,0);
    if(fd<0) {out("cat: cannot open\n");return;}
    char buf[128];long n;
    while((n=read(fd,buf,sizeof(buf)))>0)(void)write(1,buf,(size_t)n);
    if(n<0)out("cat: read error\n");
    out("\n");(void)close(fd);
}
static void write_file(const char *arg) {
    const char *space=arg;
    while(*space && *space!=' ')space++;
    if(!*space) {out("write: expected PATH TEXT\n");return;}
    char path[128];size_t n=(size_t)(space-arg);
    if(n>=sizeof(path)) {out("write: path too long\n");return;}
    for(size_t i=0;i<n;i++)path[i]=arg[i];
    path[n]=0;
    const char *body=space+1;
    int fd=open(path,O_CREAT|O_RDWR|O_TRUNC,0644);
    if(fd<0) {out("write: cannot open file\n");return;}
    size_t size=0;while(body[size])size++;
    if(write(fd,body,size)!=(long)size)out("write: failed\n");
    else out("write: OK\n");
    (void)close(fd);
}
static void show_stat(const char *path) {
    struct k_stat s;
    if(stat(path,&s)<0){out("stat: unavailable\n");return;}
    out((s.mode&0040000UL)?"directory size=":"file size=");number((unsigned)s.size);out("\n");
}
/* An actual fork + dup2 + execve path, not kernel-specific terminal capture. */
static int run_redirect(const char *name,const char *path) {
    int pid=fork();
    if(pid<0)return -1;
    if(pid==0) {
        int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0644);
        if(fd<0 || dup2(fd,1)!=1)_exit(90);
        (void)close(fd);
        char *argv[]={(char *)name,0};
        (void)execve(name,argv,0);
        _exit(91);
    }
    int status=-1;
    return waitpid(pid,&status,0)==pid?status:-1;
}
static int run_pipeline(const char *left,const char *right) {
    int fds[2];if(pipe(fds)<0)return -1;
    int first=fork();
    if(first==0) {
        (void)close(fds[0]);
        if(dup2(fds[1],1)!=1)_exit(105);
        (void)close(fds[1]);
        char *args[]={(char *)left,0};(void)execve(left,args,0);_exit(106);
    }
    if(first<0) {(void)close(fds[0]);(void)close(fds[1]);return -1;}
    int second=fork();
    if(second==0) {
        (void)close(fds[1]);
        if(dup2(fds[0],0)!=0)_exit(107);
        (void)close(fds[0]);
        char *args[]={(char *)right,0};(void)execve(right,args,0);_exit(108);
    }
    (void)close(fds[0]);(void)close(fds[1]);
    int sa=-1,sb=-1;
    if(second<0) {(void)waitpid(first,&sa,0);return -1;}
    int ra=waitpid(first,&sa,0),rb=waitpid(second,&sb,0);
    return ra==first && rb==second && sa==0 && sb==0?0:-1;
}
static void run(const char *name) {
    /* Trim a simple stdout redirection from a user command. */
    const char *sep=name;
    while(*sep && *sep!=' ')sep++;
    if(*sep) {
        const char *part=sep;while(*part==' ')part++;
        if(*part=='|' && part[1]==' ') {
            char left[64];size_t n=(size_t)(sep-name);
            if(n>=sizeof(left)){out("run: executable name too long\n");return;}
            for(size_t i=0;i<n;i++)left[i]=name[i];
            left[n]=0;
            int status=run_pipeline(left,part+2);
            out(status==0?"run: pipeline PASS\n":"run: pipeline FAIL\n");
            return;
        }
        if(*part=='>' && part[1]==' ') {
            char program[64];size_t n=(size_t)(sep-name);
            if(n>=sizeof(program)){out("run: executable name too long\n");return;}
            for(size_t i=0;i<n;i++)program[i]=name[i];
            program[n]=0;
            int status=run_redirect(program,part+2);
            out(status==0?"run: redirected process PASS\n":"run: redirected process FAIL\n");
            return;
        }
    }
    int pid=spawn(name);
    if(pid<0) {out("run: unknown or busy program\n");return;}
    out("PKS: spawned PID=");number((unsigned)pid);out("\n");
    int status=-1;
    int rc=waitpid(pid,&status,0);
    out("PKS: waitpid=");number(rc>=0?(unsigned)rc:0);
    out(" status=");number(status>=0?(unsigned)status:255);out("\n");
}
static int test_fault(const char *image,int expected) {
    int pid=spawn(image),status=-1;
    if(pid<0)return 0;
    return waitpid(pid,&status,0)==pid && status==expected;
}
static void test_all(void) {
    unsigned initial_frames=(unsigned)k_syscall3(407,0,0,0);
    unsigned initial_zombies=(unsigned)k_syscall3(406,0,0,0)&65535u;
    int first=spawn("demo"),second=spawn("second");
    if(first<1 || second<1 || first==second) {
        out("PKS: concurrent ELF64 spawn FAIL\n");
        if(first>0)(void)waitpid(first,0,0);
        if(second>0)(void)waitpid(second,0,0);
        return;
    }
    out("PKS: distinct ELF64 images concurrently scheduled\n");
    int a=255,b=255;
    int pending=waitpid(first,0,WNOHANG);
    int rc1=waitpid(first,&a,0),rc2=waitpid(second,&b,0);
    if(pending==0 && rc1==first && rc2==second && a==0 && b==0 &&
       (waitpid(first,0,WNOHANG)==-1 && errno==ECHILD))
        out("PKS: cooperative scheduling, blocking wait/reap PASS\n");
    else out("PKS: scheduling or wait/reap FAIL\n");
    if(test_fault("fault",142))out("PKS: #PF user fault isolated PASS\n");
    else out("PKS: #PF user fault isolation FAIL\n");
    if(test_fault("ud",134))out("PKS: #UD user fault isolated PASS\n");
    else out("PKS: #UD user fault isolation FAIL\n");
    if(test_fault("divide",128))out("PKS: #DE user fault isolated PASS\n");
    else out("PKS: #DE user fault isolation FAIL\n");
    unsigned final_frames=(unsigned)k_syscall3(407,0,0,0);
    unsigned final_zombies=(unsigned)k_syscall3(406,0,0,0)&65535u;
    if(final_frames==initial_frames && final_zombies==initial_zombies)
        out("PKS: user frames and zombie records reclaimed PASS\n");
    else out("PKS: resource reclamation FAIL\n");
}
static void stress(void) {
    unsigned initial=(unsigned)k_syscall3(407,0,0,0);
    unsigned z0=(unsigned)k_syscall3(406,0,0,0)&65535u;
    for(unsigned round=0;round<20;round++) {
        /* Two instances of THE SAME ELF image map identical VAs to
         * distinct physical pages, with independent runtime data. */
        int a=spawn("probe"),b=spawn("probe");
        if(a<1 || b<1 || a==b) {
            out("PKS: stress spawn FAIL round ");number(round);out("\n");
            if(a>0)(void)waitpid(a,0,0);
            if(b>0)(void)waitpid(b,0,0);
            return;
        }
        int sa=-1,sb=-1;
        int ra=waitpid(a,&sa,0),rb=waitpid(b,&sb,0);
        if(ra!=a || rb!=b || sa!=0 || sb!=0 ||
           (unsigned)k_syscall3(407,0,0,0)!=initial ||
           ((unsigned)k_syscall3(406,0,0,0)&65535u)!=z0) {
            out("PKS: stress resource/process FAIL round ");number(round);out("\n");return;
        }
    }
    out("PKS: 20 rounds of same-ELF private CR3 and blocking reap PASS\n");
}
static void preempt_test(void) {
    unsigned initial=(unsigned)k_syscall3(407,0,0,0);
    int hog=spawn("hog"),quick=spawn("quick");
    if(hog<1 || quick<1 || hog==quick) {
        out("PKS: preempt spawn FAIL\n");
        if(hog>0)(void)waitpid(hog,0,0);
        if(quick>0)(void)waitpid(quick,0,0);
        return;
    }
    int first_status=-1,second_status=-1;
    int first=waitpid(-1,&first_status,0);
    int second=waitpid(-1,&second_status,0);
    if(first==quick && second==hog && first_status==0 && second_status==0 &&
       (unsigned)k_syscall3(407,0,0,0)==initial)
        out("PKS: timer IRQ preempted CPU-bound Ring 3 process PASS\n");
    else out("PKS: preemption/order/resource test FAIL\n");
}
static void reap_children(void) {
    int status=0,pid,count=0;
    while((pid=waitpid(-1,&status,WNOHANG))>0) {
        out("PKS: reaped PID=");number((unsigned)pid);
        out(" status=");number((unsigned)status);out("\n");count++;
    }
    if(!count)out("PKS: no exited children to reap\n");
}
static void test19(void) {
    static const char *images[]={"forkprobe","execprobe","mapprobe"};
    for(unsigned i=0;i<3;i++) {
        int pid=spawn(images[i]),status=-1;
        if(pid<0 || waitpid(pid,&status,0)!=pid || status!=0) {
            out("PKS: v0.3.3 integration FAIL at ");out(images[i]);out("\n");return;
        }
    }
    if(run_redirect("second","/tmp/.redir-check")!=0) {
        out("PKS: stdout redirection FAIL\n");return;
    }
    int fd=open("/tmp/.redir-check",O_RDONLY,0);char ch=0;
    if(fd<0 || read(fd,&ch,1)!=1 || ch!='R') {
        out("PKS: redirected file read FAIL\n");if(fd>=0)(void)close(fd);return;
    }
    (void)close(fd);(void)unlink("/tmp/.redir-check");
    /* The demo also tests file syscalls. It must behave correctly when its
     * stdout is a regular file, and must clean up before a later pipeline. */
    if(run_redirect("demo","/tmp/.redir-demo-check")!=0) {
        out("PKS: demo stdout redirection FAIL\n");return;
    }
    fd=open("/tmp/.redir-demo-check",O_RDONLY,0);
    static const char prefix[]="Ring 3 ELF64: user libc write() works\n";
    char buf[sizeof(prefix)-1];
    long count=fd>=0?read(fd,buf,sizeof(buf)):-1;
    int matches=count==(long)sizeof(buf);
    if(matches)for(size_t i=0;i<sizeof(buf);i++)if(buf[i]!=prefix[i])matches=0;
    if(fd>=0)(void)close(fd);
    (void)unlink("/tmp/.redir-demo-check");
    if(!matches) {out("PKS: demo redirected content FAIL\n");return;}
    if(run_pipeline("demo","filter")!=0 || run_pipeline("demo","filter")!=0) {
        out("PKS: repeated pipe/fork/execve integration FAIL\n");return;
    }
    out("PKS: fork/execve/mmap/cwd/redirect/pipe integration PASS\n");
}
static void pwd(void) {
    char cwd[256];
    if(getcwd(cwd,sizeof(cwd))) {out(cwd);out("\n");}
    else out("pwd: failed\n");
}
static void command(const char *s) {
    if(!*s)return;
    if(equal(s,"help"))help();
    else if(equal(s,"ls"))list("/");
    else if(starts(s,"ls "))list(s+3);
    else if(starts(s,"cat "))cat(s+4);
    else if(starts(s,"stat "))show_stat(s+5);
    else if(starts(s,"write "))write_file(s+6);
    else if(starts(s,"mkdir "))out(mkdir(s+6,0755)==0?"mkdir: OK\n":"mkdir: failed\n");
    else if(starts(s,"rmdir "))out(rmdir(s+6)==0?"rmdir: OK\n":"rmdir: failed\n");
    else if(starts(s,"rm "))out(unlink(s+3)==0?"rm: OK\n":"rm: failed\n");
    else if(starts(s,"run "))run(s+4);
    else if(starts(s,"cd "))out(chdir(s+3)==0?"cd: OK\n":"cd: failed\n");
    else if(equal(s,"pwd"))pwd();
    else if(equal(s,"test19"))test19();
    else if(equal(s,"test"))test_all();
    else if(equal(s,"stress"))stress();
    else if(equal(s,"preempt"))preempt_test();
    else if(equal(s,"reap"))reap_children();
    else if(equal(s,"uname"))out("kilons 0.3.3 amd64\n");
    else if(equal(s,"ps")) {
        unsigned packed=(unsigned)k_syscall3(406,0,0,0);
        out("PKS PID: ");number((unsigned)getpid());out("\n");
        out("live (ready/running/blocked): ");number(packed>>16);
        out("  zombies: ");number(packed&65535U);out("\n");
    }
    else if(equal(s,"clear"))(void)k_syscall3(408,0,0,0);
    else if(equal(s,"shutdown")) {
        if(shutdown_system()<0)out("shutdown: ACPI unavailable\n");
    }
    else out("PKS: unknown command; use help\n");
}
/* Keyboard driver sends 17..23 for the seven extended PS/2 keys.
 * Keep saved commands in Ring 3; no privileged keyboard editor is needed. */
#define LINE_CAP 128u
#define HISTORY_CAP 16u
static char history[HISTORY_CAP][LINE_CAP];
static unsigned history_len;
static char draft[LINE_CAP];

static void copy_line(char *to, const char *from) {
    unsigned i=0;
    while(from[i] && i<LINE_CAP-1u) {to[i]=from[i];i++;}
    to[i]=0;
}
static unsigned str_len(const char *s) {
    unsigned n=0;while(s[n])n++;return n;
}
static void save_command(const char *s) {
    if(!*s)return;
    if(history_len && equal(history[history_len-1u],s))return;
    if(history_len==HISTORY_CAP) {
        for(unsigned i=1;i<HISTORY_CAP;i++)copy_line(history[i-1u],history[i]);
        history_len--;
    }
    copy_line(history[history_len++],s);
}
static void ansi_move(unsigned count,char direction) {
    if(!count)return;
    char digits[12];unsigned n=0;
    do {digits[n++]=(char)('0'+count%10u);count/=10u;}while(count);
    out("\033[");
    while(n) (void)write(1,&digits[--n],1);
    (void)write(1,&direction,1);
}
/* Repaint from the beginning of the editable line. Both VGA and serial
 * output interpret the cursor motion and clear-to-end sequence. */
static void repaint(const char *line,unsigned len,unsigned cursor,
                    unsigned previous_cursor) {
    ansi_move(previous_cursor,'D');
    if(len)(void)write(1,line,len);
    out("\033[K");
    ansi_move(len-cursor,'D');
}
void _start(void) {
    out("\nkilons PKS: Ring 3 interactive shell\n");
    out("Commands are executed in user mode; type help.\n");
    char line[LINE_CAP]={0};
    unsigned len=0,cursor=0,history_pos=0;
    out("pks$ ");
    for(;;) {
        char ch=0;
        long n=read(0,&ch,1);
        if(n<=0){(void)sched_yield();continue;}
        unsigned old_cursor=cursor;
        if(ch=='\r' || ch=='\n') {
            /* Ensure that the newline follows the complete command. */
            ansi_move(len-cursor,'C');
            out("\n");line[len]=0;
            save_command(line);
            command(line);
            line[0]=0;len=0;cursor=0;
            history_pos=history_len;draft[0]=0;
            out("pks$ ");
            continue;
        }
        if(ch==17 || ch==18) { /* Up / Down */
            if(ch==17 && history_pos) {
                if(history_pos==history_len)copy_line(draft,line);
                history_pos--;
                copy_line(line,history[history_pos]);
            } else if(ch==18 && history_pos<history_len) {
                history_pos++;
                copy_line(line,history_pos==history_len?draft:history[history_pos]);
            } else continue;
            len=str_len(line);cursor=len;
        } else if(ch==19) { /* Left */
            if(cursor) {ansi_move(1,'D');cursor--;}
            continue;
        } else if(ch==20) { /* Right */
            if(cursor<len) {ansi_move(1,'C');cursor++;}
            continue;
        } else if(ch==21) { /* Home */
            ansi_move(cursor,'D');cursor=0;continue;
        } else if(ch==22) { /* End */
            ansi_move(len-cursor,'C');cursor=len;continue;
        } else if(ch==23) { /* Delete at cursor */
            if(cursor==len)continue;
            for(unsigned i=cursor;i<len;i++)line[i]=line[i+1u];
            len--;
        } else if(ch=='\b' || (unsigned char)ch==127) {
            if(!cursor)continue;
            for(unsigned i=cursor-1u;i<len;i++)line[i]=line[i+1u];
            cursor--;len--;
        } else if((unsigned char)ch>=32 && (unsigned char)ch<127) {
            if(len==LINE_CAP-1u)continue;
            for(unsigned i=len;i>cursor;i--)line[i]=line[i-1u];
            line[cursor++]=ch;len++;line[len]=0;
        } else continue;
        repaint(line,len,cursor,old_cursor);
    }
}

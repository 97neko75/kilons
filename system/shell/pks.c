#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#define LINE_CAP 256u
/* PKS is an ordinary Ring 3 ELF64 executable. No privileged shell code is
 * entered during normal boot. Input arrives through the keyboard-backed TTY. */
static size_t length(const char *s) {
    size_t n=0;
    while(s[n])n++;
    return n;
}
static int send_bytes(int fd,const char *data,size_t size) {
    while(size) {
        long n=write(fd,data,size);
        if(n<0 && errno==EINTR)continue;
        if(n<0 && errno==EAGAIN){sched_yield();continue;}
        if(n<=0)return -1;
        data+=n;
        size-=(size_t)n;
    }
    return 0;
}
static void out(const char *s) { (void)send_bytes(1,s,length(s)); }
static void err(const char *s) { (void)send_bytes(2,s,length(s)); }
static void failure(const char *operation,const char *path) {
    int code=errno;
    char digits[12];
    unsigned n=sizeof digits;
    unsigned value=(unsigned)code;
    do {digits[--n]=(char)('0'+value%10);value/=10;}while(value);
    err(operation);err(": ");err(path);err(" (errno ");
    (void)send_bytes(2,digits+n,sizeof digits-n);
    err(")\n");
}
static int equal(const char *a,const char *b) {
    while(*a && *a==*b) {a++;b++;}return *a==*b;
}

static void number(unsigned long n) {
    char buf[21];unsigned i=20;buf[i]=0;
    do {buf[--i]=(char)('0'+n%10);n/=10;}while(n);
    out(buf+i);
}
enum token { END, WORD, PIPE, INPUT, TRUNCATE, APPEND, INVALID };
static int blank(char ch){return ch==' ' || ch=='\t';}

/* Read one word into caller-owned storage. Operators only bind outside quotes. */
static enum token token(const char **input,char **storage) {
    const char *p=*input;
    while(blank(*p))p++;
    *input=p;
    if(!*p || *p=='#')return END;
    if(*p=='|'){*input=p+1;return PIPE;}
    if(*p=='<'){*input=p+1;return INPUT;}
    if(*p=='>'){
        if(p[1]=='>'){*input=p+2;return APPEND;}
        *input=p+1;return TRUNCATE;
    }
    char quote=0;
    while(*p) {
        char ch=*p;
        if(!quote && (blank(ch) || ch=='|' || ch=='>' || ch=='<'))break;
        p++;
        if((!quote && (ch==';' || ch=='&' || ch=='(' || ch==')' ||
                        ch=='*' || ch=='?' || ch=='[' || ch=='~')) ||
           (quote!='\'' && (ch=='$' || ch=='`')))return INVALID;
        if(ch==quote){quote=0;continue;}
        if(!quote && (ch=='\'' || ch=='"')){quote=ch;continue;}
        if(ch=='\\' && quote!='\'' && (!quote || *p=='\\' || *p=='"' || *p=='$' || *p=='`')){
            if(!*p)return INVALID;
            ch=*p++;
        }
        *(*storage)++=ch;
    }
    *(*storage)++=0;
    *input=p;
    return quote?INVALID:WORD;
}
#define STAGES 8u
enum builtin_id { EXTERNAL, CHANGE_DIR, EXPORT_ENV, EXIT_SHELL, SHUTDOWN, REAP };
struct program {
    enum builtin_id builtin;
    char **argv;
    unsigned argc;
    char *input,*output;
    int flags;
};
struct job {
    struct program stage[STAGES];
    /* A line can contain at most one single-byte word per two bytes,
     * plus one vector terminator per pipeline stage. */
    char *arguments[LINE_CAP/2+STAGES];
    unsigned count;
};

static int parse(const char *input,char *storage,struct job *job){
    struct program *prog=&job->stage[0];
    unsigned next=0;
    prog->argv=job->arguments;
    for(;;){
        char *word=storage;
        enum token t=token(&input,&storage);
        if(t==END){
            if(!prog->argc)return job->count==0 && !prog->input && !prog->output;
            job->count++;return 1;
        }
        if(t==INVALID)return 0;
        if(t==WORD){
            if(next+1>=sizeof job->arguments/sizeof job->arguments[0])return 0;
            if(!prog->argc){
                if(equal(word,"cd"))prog->builtin=CHANGE_DIR;
                else if(equal(word,"export"))prog->builtin=EXPORT_ENV;
                else if(equal(word,"shutdown"))prog->builtin=SHUTDOWN;
                else if(equal(word,"reap"))prog->builtin=REAP;
                else if(equal(word,"exit"))prog->builtin=EXIT_SHELL;
            }
            job->arguments[next++]=word;
            prog->argc++;
        }else if(t==PIPE){
            if(!prog->argc || job->count+1==STAGES)return 0;
            prog=&job->stage[++job->count];
            prog->argv=&job->arguments[++next];
        }else{
            if(token(&input,&storage)!=WORD || !*word)return 0;
            if(t==INPUT){
                if(prog->input)return 0;
                prog->input=word;
            }else{
                if(prog->output)return 0;
                prog->output=word;prog->flags=t==APPEND?O_APPEND:O_TRUNC;
            }
        }
    }
}
static int attach(int fd,int target){
    if(fd==target)return 0;
    int rc=dup2(fd,target);
    close(fd);return rc<0?-1:0;
}
static int redirect(const struct program *p){
    for(int target=0;target<2;target++){
        const char *path=target?p->output:p->input;
        if(!path)continue;
        int fd=open(path,target?O_WRONLY|O_CREAT|p->flags:O_RDONLY,0666);
        if(fd<0 || attach(fd,target)<0){failure("redirect",path);return -1;}
    }
    return 0;
}
static int last_status;
static int boot_console;
static int builtin(const struct program *p){
    if(p->builtin==SHUTDOWN){
        if(p->argc!=1)return 2;
        if(!boot_console){err("shutdown: use the boot console directly\n");return 1;}
        if(send_bytes(3,"S",1)<0){failure("shutdown","init");return 1;}
        _exit(0);
    }
    if(p->builtin==REAP){
        int status, pid;
        while((pid=waitpid(-1,&status,WNOHANG))>0){
            out("reaped PID=");number((unsigned)pid);out("\n");
        }
        return pid<0 && errno!=ECHILD;
    }
    if(p->builtin==EXIT_SHELL){
        if(p->argc==1)_exit(last_status);
        if(p->argc!=2 || !p->argv[1][0])return 2;
        unsigned n=0;
        for(const char *v=p->argv[1];*v;v++){
            if(*v<'0'||*v>'9')return 2;
            n=(n*10+(unsigned)(*v-'0'))&255;
        }
        _exit((int)n);
    }else if(p->builtin==CHANGE_DIR){
        const char *path=p->argc==1?getenv("HOME"):p->argv[1];
        if(p->argc>2 || !path){err("cd: expected directory\n");return 2;}
        if(chdir(path)<0){failure("cd",path);return 1;}
    }else if(p->builtin==EXPORT_ENV){
        if(p->argc<2){err("export: expected NAME=value\n");return 2;}
        for(unsigned i=1;i<p->argc;i++){
            char *key=p->argv[i],*eq=key;
            while(*eq && *eq!='='){
                char c=*eq;
                if(!(c=='_' || (c>='a' && c<='z') || (c>='A' && c<='Z') ||
                     (eq!=key && c>='0' && c<='9')))return 2;
                eq++;
            }
            if(*eq!='=' || eq==key)return 2;
            *eq=0;
            if(setenv(key,eq+1,1)<0){failure("export",key);return 1;}
        }
    }else return -1;
    return 0;
}
static int execute(struct job *job){
    if(!job->count)return 0;
    struct program *first=&job->stage[0];
    if(job->count==1 && first->builtin!=EXTERNAL){
        int saved[2]={-1,-1};
        if(first->input && (saved[0]=dup(0))<0)return 1;
        if(first->output && (saved[1]=dup(1))<0){if(saved[0]>=0)close(saved[0]);return 1;}
        int rc=redirect(first)<0?1:builtin(first);
        for(int i=0;i<2;i++)if(saved[i]>=0 && attach(saved[i],i)<0)return 1;
        return rc;
    }
    int pids[STAGES],previous=-1,status=1;
    unsigned launched=0;
    for(unsigned i=0;i<job->count;i++){
        int ends[2]={-1,-1};
        if(i+1<job->count && pipe(ends)<0){failure("pipe","pipeline");break;}
        int pid=fork();
        if(pid==0){
            if(boot_console){close(3);boot_console=0;}
            if(ends[0]>=0)close(ends[0]);
            if(previous>=0 && attach(previous,0)<0)_exit(126);
            if(ends[1]>=0 && attach(ends[1],1)<0)_exit(126);
            struct program *p=&job->stage[i];
            if(redirect(p)<0)_exit(1);
            int rc=builtin(p);
            if(rc>=0)_exit(rc);
            execvp(p->argv[0],p->argv);
            rc=errno==ENOENT?127:126;
            failure("exec",p->argv[0]);_exit(rc);
        }
        if(pid<0)failure("fork",job->stage[i].argv[0]);
        if(previous>=0)close(previous);
        previous=ends[0];
        if(ends[1]>=0)close(ends[1]);
        if(pid<0)break;
        pids[launched++]=pid;
    }
    if(previous>=0)close(previous);
    for(unsigned i=0;i<launched;i++){
        int raw;
        pid_t waited;
        do { waited=waitpid(pids[i],&raw,0); } while(waited<0 && errno==EINTR);
        if(waited<0){failure("waitpid","pipeline");continue;}
        if(i+1==job->count)
            status=WIFEXITED(raw)?WEXITSTATUS(raw):128+WTERMSIG(raw);
    }
    return status;
}
static int command(const char *input){
    if(length(input)>=LINE_CAP){err("pks: line too long\n");return 2;}
    char words[2*LINE_CAP];
    struct job job={0};
    if(!parse(input,words,&job)){err("pks: invalid arguments or syntax\n");last_status=2;return last_status;}
    if(job.count)last_status=execute(&job);
    return last_status;
}
#define HISTORY_CAP 16u
static char history[HISTORY_CAP][LINE_CAP];
static unsigned history_len;
static char draft[LINE_CAP];

static void copy_line(char *to, const char *from) {
    unsigned i=0;
    while(from[i] && i<LINE_CAP-1u) {to[i]=from[i];i++;}
    to[i]=0;
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
/* A single-row viewport also works on serial terminals whose CSI D/C
 * stop at the margin. Leave room for the prompt and the hardware cursor. */
#define VIEW_WIDTH 72u
static void repaint(const char *line,unsigned len,unsigned cursor) {
    unsigned left=cursor>VIEW_WIDTH?cursor-VIEW_WIDTH:0;
    unsigned shown=len-left;
    if(shown>VIEW_WIDTH)shown=VIEW_WIDTH;
    out("\rpks$ ");
    (void)send_bytes(1,line+left,shown);
    out("\033[K");
    unsigned back=shown-(cursor-left);
    if(back){out("\033[");number(back);out("D");}
}
enum key { UP=256,DOWN,LEFT,RIGHT,HOME,END_KEY,DELETE_KEY };
static int key_event(int ch){
    static unsigned state,param;
    if(ch==27){state=1;return 0;}
    if(state==1){state=ch=='['?2:0;param=0;return 0;}
    if(state==2){
        if(ch>='0' && ch<='9'){if(param<100)param=param*10+(unsigned)(ch-'0');return 0;}
        state=0;
        switch(ch){
        case 'A':return UP;case 'B':return DOWN;case 'C':return RIGHT;case 'D':return LEFT;
        case 'H':return HOME;case 'F':return END_KEY;case '~':return param==3?DELETE_KEY:0;
        default:return 0;
        }
    }
    return ch;
}
static int script(const char *path){
    int fd=path?open(path,O_RDONLY):0;
    if(fd<0){failure("open",path);return 1;}
    char line[LINE_CAP],ch;
    unsigned n=0;int status=0;
    for(;;){
        long rc=read(fd,&ch,1);
        if(rc<0 && errno==EINTR)continue;
        if(rc<0 && errno==EAGAIN){sched_yield();continue;}
        if(rc<0){failure("read",path?path:"stdin");status=1;break;}
        if(!rc || ch=='\n'){
            line[n]=0;if(n)status=command(line);n=0;
            if(!rc)break;
        }else if(n+1<sizeof line)line[n++]=ch;
        else {err("pks: script line too long\n");status=2;break;}
    }
    if(path)close(fd);
    return status;
}
int main(int argc,char **argv) {
    if(argc>=3 && equal(argv[1],"-c"))return command(argv[2]);
    if(argc==2 && equal(argv[1],"--boot-console") && getppid()==1){
        if(fcntl(3,F_SETFD,FD_CLOEXEC)<0 || send_bytes(3,"R",1)<0)return 1;
        boot_console=1;
    }else if(argc>1)return script(argv[1]);
    if(!isatty(0))return script(0);
    out("\nkilons PKS: Ring 3 interactive shell\n");
    out("Type a command name or pathname.\n");
    char line[LINE_CAP]={0};
    unsigned len=0,cursor=0,history_pos=0,draft_cursor=0;
    int overflow=0;
    out("pks$ ");
    for(;;) {
        unsigned char byte=0;
        long n=read(0,&byte,1);
        if(n<0 && errno==EINTR)continue;
        if(n<0 && errno==EAGAIN){sched_yield();continue;}
        if(n<=0){if(n<0)failure("read","stdin");_exit(n<0?1:0);}
        int ch=key_event(byte);
        if(!ch)continue;
        if(ch=='\r' || ch=='\n') {
            out("\n");line[len]=0;
            if(overflow)err("PKS: line too long; command discarded\n");
            else {save_command(line);command(line);}
            overflow=0;
            line[0]=0;len=0;cursor=0;
            history_pos=history_len;draft[0]=0;
            out("pks$ ");
            continue;
        }
        if(overflow)continue;
        if(ch==UP || ch==DOWN) { /* Up / Down */
            if(ch==UP && history_pos) {
                if(history_pos==history_len){copy_line(draft,line);draft_cursor=cursor;}
                history_pos--;
                copy_line(line,history[history_pos]);
            } else if(ch==DOWN && history_pos<history_len) {
                history_pos++;
                copy_line(line,history_pos==history_len?draft:history[history_pos]);
            } else continue;
            len=(unsigned)length(line);cursor=history_pos==history_len?draft_cursor:len;
        } else if(ch==LEFT) { /* Left */
            if(cursor)cursor--;
        } else if(ch==RIGHT) {
            if(cursor<len)cursor++;
        } else if(ch==HOME) {
            cursor=0;
        } else if(ch==END_KEY) {
            cursor=len;
        } else if(ch==DELETE_KEY) { /* Delete at cursor */
            if(cursor==len)continue;
            for(unsigned i=cursor;i<len;i++)line[i]=line[i+1u];
            len--;
        } else if(ch=='\b' || (unsigned char)ch==127) {
            if(!cursor)continue;
            for(unsigned i=cursor-1u;i<len;i++)line[i]=line[i+1u];
            cursor--;len--;
        } else if(ch=='\t' || ((unsigned char)ch>=32 && (unsigned char)ch<127)) {
            if(ch=='\t')ch=' ';
            if(len==LINE_CAP-1u){overflow=1;out("\a");continue;}
            if(cursor==len && len<VIEW_WIDTH){
                line[len++]=ch;line[len]=0;cursor=len;
                (void)send_bytes(1,line+len-1,1);continue;
            }
            for(unsigned i=len;i>cursor;i--)line[i]=line[i-1u];
            line[cursor++]=ch;len++;line[len]=0;
        } else continue;
        repaint(line,len,cursor);
    }
}

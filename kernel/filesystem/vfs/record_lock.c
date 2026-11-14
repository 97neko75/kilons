#include <kilons/record_lock.h>
#include <kilons/vfs.h>
#include <kilons/kerrno.h>
#include <stdbool.h>

#define LOCK_LIMIT 128
#define WAITER_LIMIT 16
/* Half-open intervals. UINT64_MAX denotes an unbounded right edge. */
struct interval {
    struct lock_file file;
    uint64_t first, end;
    int owner, type;
};
static struct interval held[LOCK_LIMIT], pending[WAITER_LIMIT];
static int waiting_pid[WAITER_LIMIT];
static unsigned used;
/* Syscalls execute serially. Stage a complete update so ENOLCK preserves
 * even those ranges which would have been split by the failed request. */
static struct interval changes[LOCK_LIMIT+2];

static bool same_file(struct lock_file a,struct lock_file b) {
    return a.filesystem==b.filesystem && a.inode==b.inode;
}
static bool conflicts(struct interval a,struct interval b) {
    return a.owner!=b.owner && same_file(a.file,b.file) &&
           a.first<b.end && b.first<a.end && (a.type==F_WRLCK || b.type==F_WRLCK);
}
static int apply(struct interval req) {
    unsigned count=0;
    /* Existing locks of one owner are disjoint; join touching equal types
     * before trimming any previous locks of the opposite type. */
    for(unsigned i=0;i<used;i++) {
        struct interval old=held[i];
        if(old.owner==req.owner && same_file(old.file,req.file) && old.type==req.type &&
           old.first<=req.end && req.first<=old.end) {
            if(old.first<req.first)req.first=old.first;
            if(old.end>req.end)req.end=old.end;
        }
    }
    for(unsigned i=0;i<used;i++) {
        struct interval old=held[i];
        if(old.owner!=req.owner || !same_file(old.file,req.file) ||
           old.end<=req.first || old.first>=req.end) {
            changes[count++]=old;
            continue;
        }
        if(old.first<req.first) {
            changes[count]=old;
            changes[count++].end=req.first;
        }
        if(old.end>req.end) {
            changes[count]=old;
            changes[count++].first=req.end;
        }
    }
    if(req.type!=F_UNLCK)changes[count++]=req;
    if(count>LOCK_LIMIT)return -K_ENOLCK;
    for(unsigned i=0;i<count;i++)held[i]=changes[i];
    used=count;
    return 0;
}
static bool deadlock(struct interval request) {
    /* Follow every blocking owner, including multiple concurrent readers. */
    struct interval queue[WAITER_LIMIT+1]={request};
    bool seen[WAITER_LIMIT]={false};
    unsigned tail=1;
    for(unsigned head=0;head<tail;head++) {
        for(unsigned i=0;i<used;i++) {
            if(!conflicts(queue[head],held[i]))continue;
            if(held[i].owner==request.owner)return true;
            for(unsigned j=0;j<WAITER_LIMIT;j++) {
                if(!seen[j] && pending[j].owner==held[i].owner) {
                    seen[j]=true;
                    queue[tail++]=pending[j];
                }
            }
        }
    }
    return false;
}
static int interval_from(int fd,const struct flock *arg,struct interval *req) {
    int64_t base=0,start;
    if(arg->l_whence==1)base=vfs_offset(fd);
    else if(arg->l_whence==2) {
        struct vfs_info info;
        if(vfs_fstat(fd,&info)<0)return -vfs_errno();
        if(info.size>INT64_MAX)return -K_EOVERFLOW;
        base=(int64_t)info.size;
    } else if(arg->l_whence!=0)return -K_EINVAL;
    if(__builtin_add_overflow(base,arg->l_start,&start))return -K_EOVERFLOW;
    if(start<0)return -K_EINVAL;
    req->first=(uint64_t)start;
    req->end=UINT64_MAX;
    if(arg->l_len<0) {
        if(arg->l_len < -start)return -K_EINVAL;
        req->first=(uint64_t)(start+arg->l_len);
        req->end=(uint64_t)start;
    } else if(arg->l_len>0) {
        if(arg->l_len-1>INT64_MAX-start)return -K_EOVERFLOW;
        req->end=req->first+(uint64_t)arg->l_len;
    }
    return 0;
}
int record_request(int pid,int fd,int command,struct flock *arg) {
    int ofd=command==F_OFD_GETLK || command==F_OFD_SETLK || command==F_OFD_SETLKW;
    if(ofd) {
        if(arg->l_pid!=0)return -K_EINVAL;
        if(command==F_OFD_GETLK)command=F_GETLK;
        else command=command==F_OFD_SETLK?F_SETLK:F_SETLKW;
    }
    /* VFS description numbers stay unique until their last close. */
    struct interval req={.owner=ofd?-fd:pid,.type=arg->l_type};
    if(!vfs_lock_file(fd,&req.file))return -K_EINVAL;
    if(req.type!=F_RDLCK && req.type!=F_WRLCK &&
       (req.type!=F_UNLCK || command==F_GETLK))return -K_EINVAL;
    int rc=interval_from(fd,arg,&req);
    if(rc<0)return rc;
    if(req.type==F_UNLCK)return apply(req);
    for(unsigned i=0;i<used;i++) {
        struct interval old=held[i];
        if(!conflicts(req,old))continue;
        if(command==F_GETLK) {
            uint64_t len=old.end==UINT64_MAX?0:old.end-old.first;
            if(len>INT64_MAX)return -K_EOVERFLOW;
            arg->l_whence=0;
            arg->l_start=(long)old.first;
            arg->l_len=(long)len;
            arg->l_type=(short)old.type;
            arg->l_pid=old.owner<0?-1:old.owner;
            return 0;
        }
        if(command!=F_SETLKW)return -K_EAGAIN;
        if(!ofd && deadlock(req))return -K_EDEADLK;
        for(unsigned j=0;j<WAITER_LIMIT;j++)if(!waiting_pid[j]) {
            waiting_pid[j]=pid;
            pending[j]=req;
            return -K_EAGAIN;
        }
        return -K_ENOLCK;
    }
    if(command!=F_GETLK)return apply(req);
    arg->l_type=F_UNLCK;
    return 0;
}
int record_resume(int pid) {
    for(unsigned j=0;j<WAITER_LIMIT;j++)if(waiting_pid[j]==pid) {
        struct interval req=pending[j];
        for(unsigned i=0;i<used;i++)if(conflicts(req,held[i]))return -K_EAGAIN;
        pending[j].owner=0;waiting_pid[j]=0;
        return apply(req);
    }
    return -K_EINVAL;
}
void record_close(int pid,struct lock_file file) {
    for(unsigned i=0;i<used;) {
        if(held[i].owner==pid && same_file(held[i].file,file))held[i]=held[--used];
        else i++;
    }
}
void record_exit(int pid) {
    for(unsigned i=0;i<used;) {
        if(held[i].owner==pid)held[i]=held[--used];
        else i++;
    }
    record_cancel_wait(pid);
}
void record_cancel_wait(int pid) {
    for(unsigned j=0;j<WAITER_LIMIT;j++)if(waiting_pid[j]==pid){pending[j].owner=0;waiting_pid[j]=0;}
}

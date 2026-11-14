#ifndef KILONS_USER_SIGNAL_H
#define KILONS_USER_SIGNAL_H
#include <kilons/ring3.h>
#include <kilons/vm.h>
#include "../../../include/signal_numbers.h"
#define KILONS_SIGNAL_RESTORER_BYTES 7U
struct user_action {
    uint64_t handler,mask;
    int flags;
    unsigned reserved;
};
struct saved_handler {
    struct user_context context;
    uint64_t mask;
    uint64_t restorer;
};
struct user_signals {
    struct user_action actions[32];
    uint64_t restorers[32],mask,pending,suspend_mask;
    bool suspended;
    unsigned depth;
    struct saved_handler frames[32];
};
bool signal_valid(int sig);
bool signal_stop(int sig);
bool signal_ignored(const struct user_signals *,int);
void signal_queue(struct user_signals *,int);
int signal_ready(const struct user_signals *);
void signal_after_exec(struct user_signals *);
long signal_action(struct user_signals *,const struct vm_space *,const struct user_context *);
long signal_mask(struct user_signals *,const struct vm_space *,const struct user_context *);
long signal_suspend(struct user_signals *,const struct vm_space *,uint64_t);
bool signal_enter(struct user_signals *,const struct vm_space *,struct user_context *,int);
bool signal_return(struct user_signals *,struct user_context *);
#endif

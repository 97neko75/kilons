#ifndef KILONS_SYS_WAIT_H
#define KILONS_SYS_WAIT_H
#include "../signal.h"
typedef enum {P_ALL,P_PID,P_PGID} idtype_t;
#define WEXITED 4
#define WSTOPPED 2
#define WUNTRACED WSTOPPED
#define WCONTINUED 16
#define WIFSTOPPED(s) (((s)&255)==127)
#define WSTOPSIG(s) (((s)>>8)&255)
#define WIFCONTINUED(s) ((s)==0xffff)
#define WNOWAIT 8
int waitid(idtype_t,id_t,siginfo_t *,int);
#define WNOHANG 1
#define WIFEXITED(s) (((s)&127)==0)
#define WEXITSTATUS(s) (((s)>>8)&255)
#define WIFSIGNALED(s) (((s)&127)!=0 && ((s)&127)!=127)
#define WTERMSIG(s) ((s)&127)
int wait(int *);
int waitpid(int,int *,int);
#endif

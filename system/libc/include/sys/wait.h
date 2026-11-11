#ifndef KILONS_SYS_WAIT_H
#define KILONS_SYS_WAIT_H
#define WNOHANG 1
#define WIFEXITED(s) (((s)&127)==0)
#define WEXITSTATUS(s) (((s)>>8)&255)
#define WIFSIGNALED(s) (((s)&127)!=0 && ((s)&127)!=127)
#define WTERMSIG(s) ((s)&127)
int waitpid(int,int *,int);
#endif

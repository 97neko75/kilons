#ifndef KILONS_EXEC_ARGS_H
#define KILONS_EXEC_ARGS_H
#include <kilons/vm.h>
#include "../../../include/exec_limits.h"
struct exec_args {
    unsigned argc, count, used;
    char text[EXEC_ARG_BYTES];
};
int exec_args_read(struct exec_args *,const struct vm_space *,uint64_t argv,uint64_t envp);
bool exec_args_stack(const struct exec_args *,struct vm_space *,uint64_t top,uint64_t *rsp);
#endif

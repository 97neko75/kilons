#ifndef KILONS_RING3_H
#define KILONS_RING3_H
#include <stdint.h>
#include <stdbool.h>
/* Saved user general registers followed by the CPU iret frame. */
struct user_context {
    uint64_t r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rbx,rdx,rcx,rax;
    uint64_t rip,cs,flags,rsp,ss;
};
void ring3_resume(const struct user_context *ctx);
int ring3_trap_syscall(struct user_context *ctx);
void ring3_on_fault(uint64_t vector,uint64_t error,uint64_t rip,uint64_t address);
void ring3_boot(void) __attribute__((noreturn));
int ring3_spawn(const char *name,uint32_t parent);
int ring3_fork(const struct user_context *parent_context);
int ring3_execve(uint64_t path,uint64_t argv,uint64_t envp);
long ring3_mmap(uint64_t address,uint64_t length,uint64_t prot,uint64_t flags);
long ring3_munmap(uint64_t address,uint64_t length);
long ring3_mprotect(uint64_t address,uint64_t length,uint64_t prot);
int ring3_wait_status(uint32_t caller,int child,int *status);
bool ring3_is_init(uint32_t pid);
/* Called by IRQ0 only for a frame that interrupted CPL3. */
bool ring3_preempt(const uint64_t *regs,uint64_t rip,uint64_t cs,
                   uint64_t flags,uint64_t rsp,uint64_t ss);
#endif

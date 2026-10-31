#include <stdint.h>
#include <stddef.h>
#include <kilons/shell.h>
#include <kilons/console.h>
#include <kilons/keyboard.h>
#include <kilons/interrupt.h>
#include <kilons/memory.h>
#include <kilons/heap.h>
#include <kilons/scheduler.h>
#include <kilons/fat32.h>
#define LINE_MAX 128
#define HISTORY_MAX 8
static char history[HISTORY_MAX][LINE_MAX];
static unsigned hist_count;
static void number(uint64_t n) {
    char buf[21]; unsigned i=20; buf[i]=0;
    do { buf[--i]=(char)('0'+n%10); n/=10; } while(n);
    console_write(buf+i);
}
static int same(const char *a,const char *b) {
    while (*a && *b && *a==*b) { a++; b++; }
    return *a==*b;
}
static int space(char c) { return c==' ' || c=='\t'; }
static char *trim(char *s) { while(space(*s)) s++; return s; }
static void halt(void) { for(;;) __asm__ volatile("cli; hlt"); }
static void run(char *line) {
    char *cmd=trim(line);
    if (!*cmd) return;
    char *arg=cmd;
    while(*arg && !space(*arg)) arg++;
    if (*arg) { *arg++=0; arg=trim(arg); }
    if (same(cmd,"help")) console_write("help clear echo uname uptime ticks mem free heap ps threads sched vm history sleep status threadstats selftest mount fsinfo ls cat about halt\n");
    else if (same(cmd,"clear")) console_clear();
    else if (same(cmd,"echo")) { console_write(arg); console_putc('\n'); }
    else if (same(cmd,"uname")) console_write("kilons amd64 (early kernel)\n");
    else if (same(cmd,"about")) console_write("kilons milestone 7: kernel debug shell, not a POSIX shell\n");
    else if (same(cmd,"ticks")) { console_write("PIT ticks: "); number(interrupt_ticks()); console_putc('\n'); }
    else if (same(cmd,"uptime")) { console_write("Uptime (nominal seconds): "); number(interrupt_ticks()/100); console_putc('\n'); }
    else if (same(cmd,"mem") || same(cmd,"free")) { console_write("Free 4KiB physical pages: "); number(memory_free_pages()); console_putc('\n'); }
    else if (same(cmd,"heap")) { console_write("Bootstrap heap used/capacity bytes: "); number(heap_used_bytes()); console_write(" / "); number(heap_capacity_bytes()); console_putc('\n'); }
    else if (same(cmd,"ps") || same(cmd,"threads")) { console_write("Runnable kernel threads: "); number(scheduler_thread_count()); console_write(" | sleeping: "); number(scheduler_sleeping_count()); console_putc('\n'); }
    else if (same(cmd,"sched")) { console_write("Cooperative context switches: "); number(scheduler_switch_count()); console_putc('\n'); }
    else if (same(cmd,"status")) {
        console_write("kilons: cooperative kernel, FAT32 read-only driver present\n");
        console_write("PIT ticks: "); number(interrupt_ticks()); console_putc('\n');
        console_write("Free pages: "); number(memory_free_pages()); console_putc('\n');
        console_write("Runnable: "); number(scheduler_thread_count());
        console_write(" | sleeping: "); number(scheduler_sleeping_count()); console_putc('\n');
    }
    else if (same(cmd,"sleep")) {
        uint64_t seconds=0;
        if (!*arg) console_write("Usage: sleep SECONDS (1..10)\n");
        else {
            int valid=1;
            for (const char *p=arg; *p; ++p) {
                if (*p<'0' || *p>'9') { valid=0; break; }
                seconds=seconds*10+(uint64_t)(*p-'0');
                if (seconds>10) { valid=0; break; }
            }
            if (!valid || !seconds) console_write("sleep: expected integer 1..10\n");
            else { console_write("Sleeping...\n");
                uint64_t end=interrupt_ticks()+seconds*100;
                while ((int64_t)(interrupt_ticks()-end)<0) {
                    thread_yield(); __asm__ volatile("hlt");
                }
                console_write("Awake\n");
            }
        }
    }
    else if (same(cmd,"threadstats")) {
        console_write("Scheduler: cooperative (not preemptive)\nSwitches: "); number(scheduler_switch_count());
        console_write(" | sleeps: "); number(scheduler_sleep_count());
        console_write(" | wakeups: "); number(scheduler_wakeup_count()); console_putc('\n');
        for (unsigned i=0;i<scheduler_thread_slots();i++) {
            console_write("Thread "); number(i); console_write(" scheduled: ");
            number(scheduler_thread_runs(i)); console_putc('\n');
        }
    }
    else if (same(cmd,"selftest")) {
        console_write("Running non-destructive allocator and timer checks...\n");
        uint32_t before=memory_free_pages();
        uint64_t frame=page_alloc();
        int page_ok=frame && memory_free_pages()==before-1 && page_free(frame) && memory_free_pages()==before;
        uint64_t start=interrupt_ticks();
        uint64_t target=start+10;
        while ((int64_t)(interrupt_ticks()-target)<0) thread_yield();
        int clock_ok=(int64_t)(interrupt_ticks()-target)>=0;
        console_write("Physical page alloc/free: "); console_write(page_ok?"PASS\n":"FAIL\n");
        console_write("PIT advanced by >=10 ticks: "); console_write(clock_ok?"PASS\n":"FAIL\n");
        console_write(page_ok && clock_ok ? "Selftest passed\n" : "Selftest failed\n");
    }
    else if (same(cmd,"vm")) console_write("Bootstrap virtual mapping and heap initialized\n");
    else if (same(cmd,"history")) { for(unsigned i=0;i<hist_count;i++) { number(i+1); console_write("  "); console_write(history[i]); console_putc('\n'); } }
    else if (same(cmd,"mount")) console_write(fat32_mount()?"FAT32 mounted read-only\n":"FAT32 mount failed (attach a FAT32 IDE disk)\n");
    else if (same(cmd,"fsinfo")) fat32_info();
    else if (same(cmd,"ls")) fat32_ls();
    else if (same(cmd,"cat")) fat32_cat(arg);
    else if (same(cmd,"halt")) { console_write("CPU halted. Close QEMU to exit.\n"); halt(); }
    else console_write("Unknown command. Type help.\n");
}
static void save_history(const char *line) {
    if (!*trim((char *)line)) return;
    if (hist_count==HISTORY_MAX) {
        for(unsigned i=1;i<HISTORY_MAX;i++) for(unsigned j=0;j<LINE_MAX;j++) history[i-1][j]=history[i][j];
        hist_count--;
    }
    unsigned i=0;
    while(line[i] && i<LINE_MAX-1) { history[hist_count][i]=line[i]; i++; }
    history[hist_count][i]=0; hist_count++;
}
static void replace_line(char *line,unsigned *len,const char *new_line) {
    while(*len) { console_putc('\b'); (*len)--; }
    while(new_line[*len] && *len<LINE_MAX-1) { line[*len]=new_line[*len]; console_putc(line[*len]); (*len)++; }
}
void debug_shell_run(void) {
    console_write("kilons: interactive kernel debug shell (US PS/2)\n");
    console_write("Type help. This is NOT a POSIX shell.\nkilons> ");
    char line[LINE_MAX]={0}; unsigned len=0;
    unsigned history_pos=0;
    for(;;) {
        char ch=keyboard_poll();
        if (!ch) { thread_yield(); continue; }
        if (ch=='\b') { if(len) { len--; console_putc('\b'); } }
        else if (ch=='\n') {
            console_putc('\n'); line[len]=0; save_history(line); run(line);
            len=0; history_pos=hist_count; console_write("kilons> ");
        } else if (ch==17) { /* Up */
            if (history_pos) { history_pos--; replace_line(line,&len,history[history_pos]); }
        } else if (ch==18) { /* Down */
            if (history_pos<hist_count) {
                history_pos++;
                replace_line(line,&len,history_pos==hist_count ? "" : history[history_pos]);
            }
        } else if(ch>=32 && ch<=126 && len<LINE_MAX-1) {
            line[len++]=ch; console_putc(ch);
        }
    }
}

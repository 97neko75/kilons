#include <stdint.h>
#include <stddef.h>
#include <kilons/shell.h>
#include <kilons/version.h>
#include <kilons/console.h>
#include <kilons/serial.h>
#include <kilons/keyboard.h>
#include <kilons/interrupt.h>
#include <kilons/memory.h>
#include <kilons/heap.h>
#include <kilons/scheduler.h>
#include <kilons/fat32.h>
#include <kilons/ramfs.h>
#include <kilons/power.h>
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
    if (same(cmd,"help")) console_write("help clear echo uname uptime ticks mem free heap ps threads sched vm history sleep status threadstats selftest ramtest ramfiles mount fsinfo ls cat about halt shutdown poweroff\n");
    else if (same(cmd,"clear")) console_clear();
    else if (same(cmd,"echo")) { console_write(arg); console_putc('\n'); }
    else if (same(cmd,"uname")) { console_write("kilons " KILONS_VERSION_STRING " amd64 (early kernel)\n"); }
    else if (same(cmd,"about")) { console_write("kilons " KILONS_VERSION_STRING ": kernel debug shell, not a POSIX shell\n"); }
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
    else if (same(cmd,"ramtest")) console_write(ramfs_selftest()?"RAMFS open/write/seek/read/close/unlink: PASS\n":"RAMFS selftest: FAIL\n");
    else if (same(cmd,"ramfiles")) { console_write("RAMFS files: "); number(ramfs_file_count()); console_write(" / 16 (volatile)\n"); }
    else if (same(cmd,"vm")) console_write("Bootstrap virtual mapping and heap initialized\n");
    else if (same(cmd,"history")) { for(unsigned i=0;i<hist_count;i++) { number(i+1); console_write("  "); console_write(history[i]); console_putc('\n'); } }
    else if (same(cmd,"mount")) console_write(fat32_mount()?"FAT32 mounted read-only\n":"FAT32 mount failed (attach a FAT32 IDE disk)\n");
    else if (same(cmd,"fsinfo")) fat32_info();
    else if (same(cmd,"ls")) fat32_ls();
    else if (same(cmd,"cat")) fat32_cat(arg);
    else if (same(cmd,"shutdown") || same(cmd,"poweroff")) {
        if (*arg) { console_write("Usage: shutdown\n"); return; }
        console_write("kilons: requesting ACPI S5 power off...\n");
        if (!power_shutdown()) console_write("kilons: ACPI power off unavailable or failed; CPU halted.\n");
        halt();
    }
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
/* The VGA backspace operation ERASES a cell. Never use it for cursor motion.
 * Repaint only the editable input, preserving the prompt to its left. */
static void move_left(unsigned count) {
    while (count--) console_cursor_left();
}
static void move_right(unsigned count) {
    while (count--) console_cursor_right();
}
static void redraw_line(const char *line, unsigned len, unsigned cursor,
                        unsigned old_len, unsigned old_cursor) {
    move_left(old_cursor);
    for (unsigned i=0; i<len; ++i) console_screen_putc(line[i]);
    for (unsigned i=len; i<old_len; ++i) console_screen_putc(' ');
    move_left((len>old_len ? len : old_len) - cursor);
}
static void replace_line(char *line,unsigned *len,unsigned *cursor,const char *new_line) {
    unsigned old_len=*len, old_cursor=*cursor;
    unsigned n=0;
    while(new_line[n] && n<LINE_MAX-1) { line[n]=new_line[n]; n++; }
    line[n]=0;
    *len=n; *cursor=n;
    redraw_line(line,n,n,old_len,old_cursor);
}
void debug_shell_run(void) {
    console_write("kilons: interactive kernel debug shell (US PS/2)\n");
    console_write("Type help. This is NOT a POSIX shell.\nkilons> ");
    char line[LINE_MAX]={0}; unsigned len=0, cursor=0;
    unsigned history_pos=0;
    for (;;) {
        char ch=keyboard_poll();
        if (!ch) { thread_yield(); continue; }
        if (ch=='\n') {
            move_right(len-cursor);
            line[len]=0;
            /* Emit the completed command exactly once on the serial log. */
            serial_write(line);
            console_putc('\n'); save_history(line); run(line);
            len=0; cursor=0; line[0]=0; history_pos=hist_count;
            console_write("kilons> ");
        } else if (ch==17 || ch==18) {
            if (ch==17 && history_pos) {
                history_pos--; replace_line(line,&len,&cursor,history[history_pos]);
            } else if (ch==18 && history_pos<hist_count) {
                history_pos++;
                replace_line(line,&len,&cursor,history_pos==hist_count ? "" : history[history_pos]);
            }
        } else if (ch==19 && cursor) { console_cursor_left(); cursor--; }
        else if (ch==20 && cursor<len) { console_cursor_right(); cursor++; }
        else if (ch==21) { move_left(cursor); cursor=0; }
        else if (ch==22) { move_right(len-cursor); cursor=len; }
        else if (ch=='\b' && cursor) {
            unsigned old_len=len, old_cursor=cursor;
            for (unsigned i=cursor-1;i<len;i++) line[i]=line[i+1];
            cursor--; len--;
            redraw_line(line,len,cursor,old_len,old_cursor);
        } else if (ch==23 && cursor<len) {
            unsigned old_len=len, old_cursor=cursor;
            for (unsigned i=cursor;i<len;i++) line[i]=line[i+1];
            len--;
            redraw_line(line,len,cursor,old_len,old_cursor);
        } else if(ch>=32 && ch<=126 && len<LINE_MAX-1) {
            unsigned old_len=len, old_cursor=cursor;
            for (unsigned i=len;i>cursor;i--) line[i]=line[i-1];
            line[cursor]=ch; len++; cursor++; line[len]=0;
            redraw_line(line,len,cursor,old_len,old_cursor);
        }
    }
}

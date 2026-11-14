#include <kilons/file_time.h>
#include <kilons/interrupt.h>
#include <kilons/io.h>
#include <kilons/irq_guard.h>

/* PC CMOS calendar is interpreted as UTC. Read two matching snapshots
 * outside the update window before anchoring the clock to PIT time. */
static int64_t epoch;
static uint64_t start_tick;
static unsigned reg(unsigned index) {
    outb(0x70,(uint8_t)index);
    return inb(0x71);
}
static unsigned decode(unsigned raw,unsigned binary) {
    return binary?raw:10*(raw>>4)+(raw&15);
}
static int64_t snapshot(void) {
    if(reg(10)&128)return -1;
    unsigned mode=reg(11),binary=mode&4;
    unsigned second=decode(reg(0),binary),minute=decode(reg(2),binary);
    unsigned hour=reg(4),pm=hour&128;
    hour=decode(hour&127,binary);
    if(!(mode&2))hour=hour%12+(pm?12:0);
    unsigned day=decode(reg(7),binary),month=decode(reg(8),binary),year=decode(reg(9),binary);
    if((reg(10)&128)||month<1||month>12||day<1||day>31||hour>23||minute>59||second>59)return -1;
    year+=year<70?2000:1900;
    unsigned days=0;
    static const unsigned lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    for(unsigned y=1970;y<year;y++)days+=365+(y%4==0);
    for(unsigned m=1;m<month;m++)days+=lengths[m-1]+(m==2 && year%4==0);
    return ((int64_t)(days+day-1)*24+hour)*3600+minute*60+second;
}
void wall_clock_init(void) {
    uint64_t flags=kilons_irq_save();
    int64_t last=-1;
    for(unsigned i=0;i<4096;i++) {
        int64_t value=snapshot();
        if(value>=0 && value==last){epoch=value;break;}
        last=value;
    }
    start_tick=interrupt_ticks();
    kilons_irq_restore(flags);
}
int64_t file_now(void) { return epoch+(int64_t)((interrupt_ticks()-start_tick)/100); }

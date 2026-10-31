#include <kilons/io.h>
#include <kilons/serial.h>
#define COM1 0x3f8
static int ready;
void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x0b);
    ready = 1;
}
void serial_write(const char *text) {
    if (!ready) return;
    for (; *text; ++text) {
        if (*text == '\n') {
            while (!(inb(COM1 + 5) & 0x20)) {}
            outb(COM1, '\r');
        }
        while (!(inb(COM1 + 5) & 0x20)) {}
        outb(COM1, (uint8_t)*text);
    }
}

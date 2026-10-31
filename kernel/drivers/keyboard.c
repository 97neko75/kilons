#include <stdint.h>
#include <kilons/io.h>
#include <kilons/keyboard.h>
/* Set-1 scancodes, US keyboard layout. QEMU i8042/PS2 only. */
static const char normal[128] = {
    [0x01]=27,[0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',
    [0x07]='6',[0x08]='7',[0x09]='8',[0x0a]='9',[0x0b]='0',[0x0c]='-',
    [0x0d]='=',[0x0e]='\b',[0x0f]='\t',[0x10]='q',[0x11]='w',
    [0x12]='e',[0x13]='r',[0x14]='t',[0x15]='y',[0x16]='u',[0x17]='i',
    [0x18]='o',[0x19]='p',[0x1a]='[',[0x1b]=']',[0x1c]='\n',
    [0x1e]='a',[0x1f]='s',[0x20]='d',[0x21]='f',[0x22]='g',[0x23]='h',
    [0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',[0x28]='\'',
    [0x29]='`',[0x2b]='\\',[0x2c]='z',[0x2d]='x',[0x2e]='c',
    [0x2f]='v',[0x30]='b',[0x31]='n',[0x32]='m',[0x33]=',',
    [0x34]='.',[0x35]='/',[0x39]=' '
};
static const char shifted_digits[13] = {0}; /* shifted punctuation handled explicitly */
static uint8_t shift, caps, extended;
static char decode(uint8_t sc) {
    if (sc == 0xe0 || sc == 0xe1) { extended = 1; return 0; }
    if (extended) { extended = 0; if (sc == 0x48) return 17; if (sc == 0x50) return 18; return 0; }
    if ((sc & 0x7f) == 0x2a || (sc & 0x7f) == 0x36) {
        shift = (sc & 0x80) ? 0 : 1; return 0;
    }
    if (sc == 0x3a) { caps ^= 1; return 0; }
    if (sc & 0x80) return 0;
    char ch = normal[sc];
    if (ch >= 'a' && ch <= 'z') { if (shift ^ caps) ch = (char)(ch - 'a' + 'A'); }
    else if (shift) {
        switch (ch) {
            case '1': ch='!'; break; case '2': ch='@'; break;
            case '3': ch='#'; break; case '4': ch='$'; break;
            case '5': ch='%'; break; case '6': ch='^'; break;
            case '7': ch='&'; break; case '8': ch='*'; break;
            case '9': ch='('; break; case '0': ch=')'; break;
            case '-': ch='_'; break; case '=': ch='+'; break;
            case '[': ch='{'; break; case ']': ch='}'; break;
            case ';': ch=':'; break; case '\'': ch='"'; break;
            case '`': ch='~'; break; case '\\': ch='|'; break;
            case ',': ch='<'; break; case '.': ch='>'; break;
            case '/': ch='?'; break;
        }
    }
    (void)shifted_digits;
    return ch;
}

/* Single producer (IRQ1), single consumer (kernel monitor). */
#define KB_SIZE 128u
static volatile unsigned head, tail;
static char buffer[KB_SIZE];
void keyboard_irq(void) {
    uint8_t sc = inb(0x60);
    char ch = decode(sc);
    if (!ch) return;
    unsigned next = (head + 1u) % KB_SIZE;
    if (next != tail) { buffer[head] = ch; head = next; }
}
char keyboard_poll(void) {
    if (tail == head) return 0;
    char ch = buffer[tail];
    tail = (tail + 1u) % KB_SIZE;
    return ch;
}

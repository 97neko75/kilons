#include <stdint.h>
#include <kilons/io.h>
#include <kilons/keyboard.h>
/* Controller translation turns keyboard set 2 into the set 1 stream below. */
enum {
    DATA = 0x60, STATUS = 0x64, COMMAND = 0x64,
    OUTPUT_FULL = 1, INPUT_FULL = 2, AUX_DATA = 0x20, RX_ERROR = 0xc0
};

/* IRQs and PIT are still disabled. Bound every hardware handshake. */
static int send_byte(unsigned port, uint8_t value) {
    for (unsigned poll = 0; poll < 100000; ++poll) {
        uint8_t status = inb(STATUS);
        if (status == 0xff) return 0;
        if (!(status & INPUT_FULL)) {
            outb(port, value);
            return 1;
        }
        __asm__ volatile("pause");
    }
    return 0;
}

static int receive_byte(uint8_t *value) {
    for (unsigned poll = 0; poll < 100000; ++poll) {
        uint8_t status = inb(STATUS);
        if (status == 0xff) return 0;
        if (status & OUTPUT_FULL) {
            uint8_t byte = inb(DATA);
            if (!(status & (AUX_DATA | RX_ERROR))) {
                *value = byte;
                return 1;
            }
        }
        __asm__ volatile("pause");
    }
    return 0;
}

static int keyboard_command(uint8_t command) {
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        uint8_t reply;
        if (!send_byte(DATA, command) || !receive_byte(&reply)) return 0;
        if (reply == 0xfa) return 1;
        if (reply != 0xfe) return 0;
    }
    return 0;
}

static int write_config(uint8_t config) {
    return send_byte(COMMAND, 0x60) && send_byte(DATA, config);
}

static int port_ready;

const char *keyboard_init(void) {
    port_ready = 0;
    if (amd64_i8042_presence() == 0) return "absent according to firmware";
    if (!send_byte(COMMAND, 0xad) || !send_byte(COMMAND, 0xa7))
        return "controller did not accept port disable";
    for (unsigned i = 0; i < 64 && (inb(STATUS) & OUTPUT_FULL); ++i)
        (void)inb(DATA);
    if (inb(STATUS) & OUTPUT_FULL) return "output buffer did not drain";

    uint8_t config;
    if (!send_byte(COMMAND, 0x20) || !receive_byte(&config))
        return "cannot read controller configuration";
    /* Preserve unrelated flags. Port 2 stays disabled until it has a driver. */
    config = (config | 0x30u) & ~0x43u;
    if (!write_config(config) || !send_byte(COMMAND, 0xae))
        return "cannot enable keyboard port";
    if (!keyboard_command(0xf5) || !keyboard_command(0xf0) || !keyboard_command(2))
        return "keyboard did not acknowledge scan-set selection";
    if (!write_config((config & ~0x10u) | 0x41u) || !keyboard_command(0xf4))
        return "cannot enable translation, IRQ1 or keyboard scanning";
    port_ready = 1;
    return 0;
}

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
static uint8_t shift, caps, extended, ctrl;
/* Consume typematic D repeats until the physical D key is released. */
static uint8_t eof_latched;
static char decode(uint8_t sc) {
    if (sc == 0xe0 || sc == 0xe1) { extended = 1; return 0; }
    /* Each Control key owns one bit, including the E0-prefixed right key. */
    if ((sc & 0x7f) == 0x1d) {
        unsigned bit = extended ? 2u : 1u;
        if (sc & 0x80) ctrl &= ~bit;
        else ctrl |= bit;
        extended = 0;
        return 0;
    }
    if (extended) { extended = 0; if (sc & 0x80) return 0; if (sc == 0x48) return 17; if (sc == 0x50) return 18; if (sc == 0x4b) return 19; if (sc == 0x4d) return 20; if (sc == 0x47) return 21; if (sc == 0x4f) return 22; if (sc == 0x53) return 23; return 0; }
    if ((sc & 0x7f) == 0x2a || (sc & 0x7f) == 0x36) {
        shift = (sc & 0x80) ? 0 : 1; return 0;
    }
    if (sc == 0x3a) { caps ^= 1; return 0; }
    if (sc == 0xa0) {
        eof_latched = 0;
        return 0;
    }
    if (sc & 0x80) return 0;
    if (sc == 0x20) {
        if (eof_latched) return 0;
        if (ctrl) {
            eof_latched = 1;
            return 4;
        }
    }
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

/* Both callers use interrupt gates, so the producer cannot reenter on this UP kernel. */
#define KB_SIZE 128u
static volatile unsigned head, tail;
static char buffer[KB_SIZE];
void keyboard_service(void) {
    if (!port_ready) return;
    /* Always consume pending hardware bytes, even if the software queue fills.
     * A stuck controller must not monopolize an interrupt handler. */
    for (unsigned count = 0; count < 16; ++count) {
        uint8_t status = inb(STATUS);
        if (status == 0xff || !(status & OUTPUT_FULL)) break;
        uint8_t sc = inb(DATA);
        if (status & AUX_DATA) continue;
        if (status & RX_ERROR) {
            extended = shift = ctrl = eof_latched = 0;
            continue;
        }
        char ch = decode(sc);
        if (!ch) continue;
        unsigned next = (head + 1u) % KB_SIZE;
        if (next == tail) continue;
        buffer[head] = ch;
        head = next;
    }
}
int keyboard_available(void){return tail!=head;}
char keyboard_poll(void) {
    if (tail == head) return 0;
    char ch = buffer[tail];
    tail = (tail + 1u) % KB_SIZE;
    return ch;
}

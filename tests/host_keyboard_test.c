#include <assert.h>
#include <stdio.h>
#include "../kernel/drivers/keyboard.c"

int amd64_i8042_presence(void) { return 0; }

int main(void) {
    decode(0x2a);
    decode(0x36);
    assert(decode(0x1e) == 'A');
    decode(0xaa);
    assert(decode(0x1e) == 'A');
    decode(0xb6);
    assert(decode(0x1e) == 'a');
    decode(0x3a);
    decode(0x36);
    assert(decode(0x1e) == 'a');
    decode(0xb6);
    assert(decode(0x1e) == 'A');
    puts("PS/2 independent Shift keys and Caps Lock PASS");
}

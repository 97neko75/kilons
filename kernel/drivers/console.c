#include <stdint.h>
#include <stddef.h>
#include <kilons/console.h>
#include <kilons/serial.h>
#include <kilons/io.h>
#define WIDTH 80u
#define HEIGHT 25u
static volatile uint16_t *const vga = (volatile uint16_t *)0xb8000;
static size_t row, col;
static uint8_t color = 0x0f;
static void cursor_update(void) {
    uint16_t pos = (uint16_t)(row * WIDTH + col);
    outb(0x3d4, 0x0f); outb(0x3d5, (uint8_t)pos);
    outb(0x3d4, 0x0e); outb(0x3d5, (uint8_t)(pos >> 8));
}
void console_clear(void) {
    for (size_t i = 0; i < WIDTH * HEIGHT; ++i) vga[i] = (uint16_t)((color << 8) | ' ');
    row = col = 0;
    cursor_update();
}
void console_init(void) {
    /* VGA cursor shape: scanlines 13..15, explicitly enable bit 5=0. */
    outb(0x3d4, 0x0a); outb(0x3d5, 13);
    outb(0x3d4, 0x0b); outb(0x3d5, 15);
    console_clear();
}
static void newline(void) {
    col = 0;
    if (++row < HEIGHT) return;
    for (size_t y = 1; y < HEIGHT; ++y)
        for (size_t x = 0; x < WIDTH; ++x) vga[(y - 1) * WIDTH + x] = vga[y * WIDTH + x];
    for (size_t x = 0; x < WIDTH; ++x) vga[(HEIGHT - 1) * WIDTH + x] = (uint16_t)((color << 8) | ' ');
    row = HEIGHT - 1;
}
static void putc_screen(char ch) {
    if (ch == '\n') newline();
    else if (ch == '\r') col = 0;
    else if (ch == '\b') {
        if (col) --col;
        else if (row) { --row; col = WIDTH - 1; }
        vga[row * WIDTH + col] = (uint16_t)((color << 8) | ' ');
    } else if (ch == '\t') {
        do { putc_screen(' '); } while (col % 4);
    } else {
        if (col == WIDTH) newline();
        vga[row * WIDTH + col++] = (uint16_t)((color << 8) | (uint8_t)ch);
    }
    cursor_update();
}
/* A small ANSI CSI interpreter keeps the VGA view synchronized with the
 * serial terminal. PKS uses cursor-left/right and erase-to-end-of-line for
 * in-place editing. Ordinary output still passes through unchanged. */
static unsigned csi_state, csi_count, csi_value;
static void cursor_left_n(unsigned n) {
    while (n--) {
        if (col) --col;
        else if (row) { --row; col = WIDTH - 1; }
    }
    cursor_update();
}
static void cursor_right_n(unsigned n) {
    while (n--) {
        if (col < WIDTH - 1) ++col;
        else if (row < HEIGHT - 1) { ++row; col = 0; }
    }
    cursor_update();
}
void console_putc(char ch) {
    /* Serial receives complete ANSI escapes for a standard terminal. */
    if (ch == '\b') serial_write("\b \b");
    else { char s[2] = {ch, 0}; serial_write(s); }
    if (csi_state == 1) {
        csi_state = ch == '[' ? 2u : 0u;
        csi_value = 0; csi_count = 0;
        return;
    }
    if (csi_state == 2) {
        if (ch >= '0' && ch <= '9') {
            if (csi_value < 1000) csi_value = csi_value * 10u + (unsigned)(ch - '0');
            ++csi_count;
            return;
        }
        unsigned n = csi_count ? csi_value : 1u;
        if (ch == 'D') cursor_left_n(n);
        else if (ch == 'C') cursor_right_n(n);
        else if (ch == 'K' && (csi_value == 0 || csi_count == 0)) {
            for (size_t x = col; x < WIDTH; ++x)
                vga[row * WIDTH + x] = (uint16_t)((color << 8) | ' ');
            cursor_update();
        }
        csi_state = 0;
        return;
    }
    if (ch == 27) { csi_state = 1; return; }
    putc_screen(ch);
}
void console_write(const char *text) { while (*text) console_putc(*text++); }
/* VGA-only painting: never mirror interactive redraws to serial. */
void console_screen_putc(char ch) { putc_screen(ch); }

/* Shell line editing must never use destructive backspace for navigation. */
void console_cursor_left(void) {
    if (col) --col;
    else if (row) { --row; col = WIDTH - 1; }
    cursor_update();
}
void console_cursor_right(void) {
    if (col < WIDTH) ++col;
    else if (row < HEIGHT - 1) { ++row; col = 0; }
    cursor_update();
}

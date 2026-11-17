#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <kilons/console.h>
#include <kilons/serial.h>
#include <kilons/limine.h>
#include "console_font.h"
#define WIDTH 80u
#define HEIGHT 25u
/* The Limine framebuffer address is already mapped in the higher half. */
struct fb_view {
    volatile uint8_t *address;
    uint64_t width, height, pitch;
    uint16_t bpp;
    uint8_t model, red_size, red_shift, green_size, green_shift, blue_size, blue_shift;
};
struct fb_response { uint64_t revision, count; struct fb_view **buffers; };
__attribute__((used, aligned(8), section(".limine_requests")))
static volatile struct {
    uint64_t id[4], revision;
    struct fb_response *response;
} fb_request = {{LIMINE_COMMON_MAGIC, 0x9d5827dcd881dd75ULL, 0xa3148604f6fab11bULL}, 0, 0};
static struct fb_view fb;
static unsigned pixel_bytes;
static uint32_t white;
static size_t row, col, cursor;
static char cells[WIDTH * HEIGHT];
static bool screen_ready;

static void paint(size_t index, bool underline) {
    unsigned ch = (unsigned char)cells[index];
    if (ch < 32 || ch > 126) ch = '?';
    for (unsigned y = 0; y < 16; ++y) {
        unsigned bits = underline && y == 15 ? 255 : glyphs[ch - 32][y];
        volatile uint8_t *dst = fb.address + (index / WIDTH * 16 + y) * fb.pitch
                               + index % WIDTH * 8 * pixel_bytes;
        for (unsigned x = 0; x < 8; ++x) {
            uint32_t pixel = bits & (128u >> x) ? white : 0;
            for (unsigned i = 0; i < pixel_bytes; ++i)
                *dst++ = (uint8_t)(pixel >> (8 * i));
        }
    }
}
static void cursor_update(void) {
    paint(cursor, false);
    cursor = row * WIDTH + (col < WIDTH ? col : WIDTH - 1);
    paint(cursor, true);
}
void console_clear(void) {
    if (!screen_ready) return;
    for (size_t i = 0; i < WIDTH * HEIGHT; ++i) { cells[i] = ' '; paint(i, false); }
    row = col = 0;
    cursor_update();
}
void console_init(void) {
    struct fb_response *reply = fb_request.response;
    if (!reply || !reply->count || !reply->buffers || !reply->buffers[0]) return;
    fb = *reply->buffers[0];
    if (!fb.address || fb.width < WIDTH*8 || fb.height < HEIGHT*16 ||
        fb.model != 1 || (fb.bpp != 32 && fb.bpp != 24) ||
        fb.red_size != 8 || fb.green_size != 8 || fb.blue_size != 8 ||
        fb.red_shift > fb.bpp-8 || fb.green_shift > fb.bpp-8 || fb.blue_shift > fb.bpp-8) return;
    pixel_bytes = fb.bpp / 8;
    if (fb.pitch / pixel_bytes < fb.width || fb.pitch > SIZE_MAX / fb.height) return;
    white = (255u << fb.red_shift) | (255u << fb.green_shift) | (255u << fb.blue_shift);
    screen_ready = true;
    console_clear();
}
static void newline(void) {
    col = 0;
    if (++row < HEIGHT) return;
    for (size_t y = 1; y < HEIGHT; ++y)
        for (size_t x = 0; x < WIDTH; ++x) { size_t i = (y - 1) * WIDTH + x; cells[i] = cells[i + WIDTH]; paint(i, false); }
    for (size_t x = 0; x < WIDTH; ++x) { size_t i = (HEIGHT - 1) * WIDTH + x; cells[i] = ' '; paint(i, false); }
    row = HEIGHT - 1;
}
static void putc_screen(char ch) {
    if (ch == '\n') newline();
    else if (ch == '\r') col = 0;
    else if (ch == '\b') {
        if (col) --col;
        else if (row) { --row; col = WIDTH - 1; }
        cells[row * WIDTH + col] = ' '; paint(row * WIDTH + col, false);
    } else if (ch == '\t') {
        do { putc_screen(' '); } while (col % 4);
    } else {
        if (col == WIDTH) newline();
        cells[row * WIDTH + col] = ch; paint(row * WIDTH + col, false); ++col;
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
    if (!screen_ready) return;
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
                { cells[row * WIDTH + x] = ' '; paint(row * WIDTH + x, false); }
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
void console_screen_putc(char ch) { if (screen_ready) putc_screen(ch); }

/* Shell line editing must never use destructive backspace for navigation. */
void console_cursor_left(void) {
    if (!screen_ready) return;
    if (col) --col;
    else if (row) { --row; col = WIDTH - 1; }
    cursor_update();
}
void console_cursor_right(void) {
    if (!screen_ready) return;
    if (col < WIDTH) ++col;
    else if (row < HEIGHT - 1) { ++row; col = 0; }
    cursor_update();
}

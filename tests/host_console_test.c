#include <assert.h>
#include <stdio.h>
#include "../kernel/drivers/console.c"

void serial_write(const char *text) { (void)text; }

int main(void) {
    static uint8_t pixels[400][2568];
    struct fb_view view = {
        &pixels[0][0], 640, 400, 2568, 32, 1, 8, 16, 8, 8, 8, 0
    };
    struct fb_view *views[] = { &view };
    struct fb_response reply = { 0, 1, views };
    fb_request.response = &reply;
    console_init();
    assert(screen_ready);
    console_write("pks$ ");
    unsigned lit = 0;
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 32; ++x)
            lit += pixels[y][4*x] != 0;
    assert(lit && cells[0] == 'p');
    console_write("\033[2J");
    assert(col == 5);
    for (size_t i = 0; i < WIDTH * HEIGHT; ++i) assert(cells[i] == ' ');
    console_write("\033[H");
    assert(row == 0 && col == 0);
    for (unsigned i = 0; i < 30; ++i) console_write("\nabc");
    assert(row == 24 && cells[24 * WIDTH] == 'a');
    for (unsigned y = 0; y < 400; ++y)
        for (unsigned x = 2560; x < 2568; ++x) assert(pixels[y][x] == 0);
    view.bpp = 24;
    console_init();
    assert(screen_ready);
    console_write("24-bit");
    view.green_shift = view.red_shift;
    console_init();
    assert(!screen_ready);
    puts("framebuffer pixels / clear / home / scroll / stride / formats PASS");
}

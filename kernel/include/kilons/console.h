#ifndef KILONS_CONSOLE_H
#define KILONS_CONSOLE_H
void console_init(void);
void console_clear(void);
void console_putc(char ch);
void console_write(const char *text);
/* Interactive editing is VGA-only; serial receives committed lines. */
void console_screen_putc(char ch);
/* Move the VGA cursor without modifying characters on screen. */
void console_cursor_left(void);
void console_cursor_right(void);
#endif

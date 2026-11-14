#ifndef KILONS_KEYBOARD_H
#define KILONS_KEYBOARD_H
/* Nonblocking IRQ-driven keyboard queue, US PS/2 set 1. */
int amd64_i8042_presence(void);
const char *keyboard_init(void);
void keyboard_service(void);
int keyboard_available(void);
char keyboard_poll(void);
#endif

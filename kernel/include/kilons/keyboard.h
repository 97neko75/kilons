#ifndef KILONS_KEYBOARD_H
#define KILONS_KEYBOARD_H
/* Nonblocking IRQ-driven keyboard queue, US PS/2 set 1. */
void keyboard_irq(void);
char keyboard_poll(void);
#endif

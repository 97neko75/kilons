#ifndef KILONS_INTERRUPT_H
#define KILONS_INTERRUPT_H
#include <stdint.h>
void interrupt_init(void);
uint64_t interrupt_ticks(void);
#endif

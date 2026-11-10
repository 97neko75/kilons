#ifndef KILONS_TSS_H
#define KILONS_TSS_H
#include <stdint.h>
void tss_init(void);
void tss_set_rsp0(uint64_t top);
#endif

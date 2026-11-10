#ifndef KILONS_POWER_H
#define KILONS_POWER_H
/* ACPI S5 soft-off. Returns false when firmware support is unavailable. */
int power_shutdown(void);
/* Read-only capability check; does not change machine power state. */
int power_probe(void);
#endif

# Power management (0.3.0)

`shutdown` and `poweroff` request ACPI S5 soft-off from the kernel debug shell.
`halt` stops the CPU but does **not** power off the machine.

The current implementation discovers RSDP in EBDA/BIOS, validates ACPI table checksums, locates FADT/DSDT, reads the simple AML `_S5_` package, enables ACPI if required, and writes the PM1 sleep control registers. This is an early implementation, not a complete ACPI interpreter. It works only with legacy PM1 I/O blocks and tables identity-mapped below 1 GiB; unsupported firmware falls back to CPU halt. No user process or filesystem sync exists yet.

Test in QEMU with `make run`, then type `shutdown`. Expected: the QEMU VM powers off. If firmware does not support the current ACPI path, the CPU halts with a diagnostic instead.

**Never use this early implementation on a real machine with unsaved data.**

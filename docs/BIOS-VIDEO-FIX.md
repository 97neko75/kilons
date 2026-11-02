v0.3.1-3 boot regression fix

Correct Multiboot2 console-flags bit: bit 0 = console required, bit 1 = EGA text supported. Set value 2.
Restore GRUB config to the proven 0.3.1 behavior (no forced terminal_output or gfxpayload=text).
This is a hypothesis-driven boot fix, not verified on QEMU or real hardware here.

/* kilons Ring 3 signal-return entry.
 *
 * This is an OS/libc ABI boundary, so its bytes are written explicitly
 * instead of being emitted from a compiler-generated naked C function.
 * The kernel validates the first seven bytes before accepting a handler.
 */
.section .text.kilons_signal_restore,"ax",@progbits
.global kilons_signal_restore
.type kilons_signal_restore,@function
kilons_signal_restore:
    .byte 0xb8, 0x0f, 0x00, 0x00, 0x00 /* mov $15,%eax */
    .byte 0xcd, 0x80                   /* int $0x80 */
    .byte 0x0f, 0x0b                   /* ud2 if the syscall returns */
.size kilons_signal_restore, .-kilons_signal_restore

.section .note.GNU-stack,"",@progbits

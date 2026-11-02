# kilons 0.2.1

Experimental Unix-like operating system, with POSIX interfaces as a long-term goal. **Not POSIX compliant yet.**

This release continues directly from the assistant-generated kilons 0.1.0 snapshot, preserving existing code, including the experimental FAT32 read-only driver and kernel debug shell.

## New in 0.2.0 (retained)

- `kernel/filesystem/ramfs.c`: bounded, in-memory file store with file handles and offset-based `open`, `read`, `write`, `seek`, `close`, `unlink` operations. This is a kernel-internal API, **not** POSIX syscalls or a mounted VFS.
- Up to 16 files, 16 open handles, 4096 bytes per file; names up to 31 characters, no directories.
- `ramtest` debug command exercises create/write/seek/read/close/unlink.
- `ramfiles` shows current in-memory file count.
- RAMFS initializes during boot and does not touch disks.

## Existing functionality

amd64/Multiboot2, GRUB ISO, basic physical and virtual memory, heap, GDT/TSS/IDT, PIC/PIT, PS/2 input, cooperative kernel threads, interactive kernel debug shell, and unverified experimental FAT32 read-only driver.

## Build and test

Dependencies: GNU GCC/binutils/make, GRUB `grub-mkrescue`, xorriso and QEMU x86-64.

```sh
make clean
make
make run
```

In the kernel debug shell:

```text
kilons> uname
kilons> ramfiles
kilons> ramtest
kilons> ramfiles
kilons> selftest
```

Expected `ramtest`: `RAMFS open/write/seek/read/close/unlink: PASS`.

`make run` boots from ISO only. Optional FAT32 testing: `make run-fat32 DISK=test-fat32.img` (never use a physical disk).

## Limitations

No Ring 3, user ELF loader, POSIX syscalls, process isolation, persistent RAMFS storage or full VFS yet. RAMFS uses fixed static buffers, no concurrency locking, and is intended as a foundation for future file-descriptor/VFS work. QEMU runtime behavior must be tested on the user's machine.

Versioning: new features increment Y (`0.1.0` -> `0.2.0`), fixes increment Z only. Full-source snapshots are archived when a release cycle ends.

## Fixed in 0.2.1

- Corrected history navigation (Up/Down) erasing `kilons>` prompt.
- Added non-destructive VGA cursor movement for Left/Right/Home/End.
- Corrected line redraw on insert, Backspace and Delete.
- No new features; this is a patch release.

### Manual regression test

1. At an empty prompt, press Up and Down repeatedly; `kilons> ` must remain intact.
2. Run `echo abc`, then press Up, Left, Home, End, Down and verify the prompt survives.
3. Recall a long command, edit in its middle, press Backspace/Delete and Enter.
4. Repeat at the bottom of the 25-line VGA display to exercise scrolling.

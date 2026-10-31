# kilons Milestone 7 — FAT32 read-only + visible VGA cursor

Based on M6. `make clean && make run` boots without a disk. The VGA text-mode cursor is explicitly enabled.

## FAT32 disk testing

Create a **separate disposable raw FAT32 image**, e.g. on a Linux host with `mkfs.fat`:

```sh
truncate -s 64M test-fat32.img
mkfs.fat -F 32 -s 1 test-fat32.img
# optionally: mcopy -i test-fat32.img README.TXT ::README.TXT
make run DISK=test-fat32.img
```

In the kernel debug shell:

```
mount
fsinfo
ls
cat README.TXT
```

The image is attached as **primary IDE master**. FAT32 supports superfloppy or first FAT32 MBR partition. The implementation is intentionally read-only: `mount`, root-directory `ls` and `cat` for short 8.3 names, max file size 64 KiB. Long filenames, subdirectories, writes, GPT, AHCI, NVMe, UEFI, user mode and POSIX shell are not implemented. The QEMU ISO is a separate CD-ROM, not the FAT32 data disk. Do not attach your host's real disk.

FAT32 volume must contain >= 65525 data clusters; with 64 MiB and 1 sector/cluster this is normally satisfied. The ATA PIO driver has a finite polling timeout and only supports LBA28. No writes are issued.

Build verification is separate from actual QEMU interaction testing.


## Milestone 7 update: default ISO boot and diagnostic commands

- `make run` boots only the GRUB ISO; no disk image is needed.
- `make run-fat32 DISK=test-fat32.img` explicitly opts into the optional IDE FAT32 disk. Use only a disposable image.
- `status` summarizes PIT, memory, and thread counts.
- `sleep 1` through `sleep 10` provides a timer/interrupt responsiveness check (not a POSIX sleep utility).
- `console_init()` already enables the VGA hardware cursor; visibility depends on QEMU's VGA display, not the serial log.
- FAT32 code and existing commands remain intact.

This update is compiled and linked, but not runtime-tested under QEMU in this environment.


## Milestone 7 next: kernel diagnostics

- `threadstats`: scheduler switch/sleep/wakeup counters and per-slot scheduling counts.
- `selftest`: non-destructive physical page allocation/free test and PIT progression test.
- FAT32 driver remains unchanged and is **not** exercised by default.
- `make run` continues to boot only the ISO.
- This is still a cooperative kernel, **not** Ring 3, ELF user process support or a POSIX shell.
- QEMU boot and interactive behavior need testing on a machine with QEMU and GRUB tools.

CC ?= gcc
LD ?= ld
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -mgeneral-regs-only -mcmodel=kernel -Ikernel/include
ASFLAGS := -m64
BUILD := build
OBJS := $(BUILD)/user_signal.o $(BUILD)/wall_clock.o $(BUILD)/boot.o $(BUILD)/kernel.o $(BUILD)/console.o $(BUILD)/serial.o $(BUILD)/physical.o $(BUILD)/keyboard.o $(BUILD)/interrupt.o $(BUILD)/interrupt_stubs.o $(BUILD)/virtual.o $(BUILD)/heap.o $(BUILD)/tss.o $(BUILD)/scheduler.o $(BUILD)/process.o $(BUILD)/context.o $(BUILD)/block.o $(BUILD)/boot_module.o $(BUILD)/ata_pio.o $(BUILD)/fat32.o $(BUILD)/fat32_vfs.o $(BUILD)/ramfs.o $(BUILD)/vfs.o $(BUILD)/iso9660.o $(BUILD)/power.o $(BUILD)/ring3.o $(BUILD)/exec_args.o $(BUILD)/ring3_entry.o $(BUILD)/posix.o $(BUILD)/record_lock.o
.PHONY: all clean iso run run-fat32
all: $(BUILD)/kilons.elf
$(BUILD):
	mkdir -p $@
$(BUILD)/wall_clock.o: kernel/arch/amd64/wall_clock.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/boot.o: boot/amd64/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/boot_module.o: kernel/devices/boot_module.c kernel/include/kilons/boot_module.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/kernel.o: kernel/core/main.c VERSION kernel/include/kilons/version.h kernel/include/kilons/boot_module.h | $(BUILD)
	$(CC) $(CFLAGS) -DKILONS_VERSION_STRING='"$(strip $(file <VERSION))"' -c $< -o $@
$(BUILD)/console.o: kernel/drivers/console.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/serial.o: kernel/drivers/serial.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/physical.o: kernel/memory/physical.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/keyboard.o: kernel/drivers/keyboard.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/interrupt.o: kernel/arch/interrupt.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/interrupt_stubs.o: boot/amd64/interrupts.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/virtual.o: kernel/memory/virtual.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/heap.o: kernel/memory/heap.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/tss.o: kernel/arch/amd64/tss.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/process.o: kernel/process/process.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/scheduler.o: kernel/process/scheduler.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/fat32_vfs.o: kernel/filesystem/fat32_vfs.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/fat32.o: kernel/filesystem/fat32.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/ramfs.o: kernel/filesystem/ramfs.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/vfs.o: kernel/filesystem/vfs/vfs.c include/dirent.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/iso9660.o: kernel/filesystem/iso9660.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/power.o: kernel/arch/amd64/power.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/context.o: boot/amd64/context.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/kilons.elf: $(OBJS) boot/amd64/linker.ld
	$(LD) -m elf_x86_64 -nostdlib -z max-page-size=0x1000 -T boot/amd64/linker.ld -o $@ $(OBJS)
iso: all rootfs-kilofs
	@command -v grub-mkrescue >/dev/null || (echo 'grub-mkrescue is required' && exit 1)
	mkdir -p $(BUILD)/iso/boot/grub
	cp $(BUILD)/kilons.elf $(BUILD)/iso/boot/kilons.elf
	cp boot/grub/grub.cfg $(BUILD)/iso/boot/grub/grub.cfg
	cp $(ROOTFS) $(BUILD)/iso/boot/rootfs.kilofs
	cp tests/iso-hello.txt $(BUILD)/iso/HELLO.TXT
	grub-mkrescue -o $(BUILD)/kilons.iso $(BUILD)/iso
# Normal development boot: no FAT32 test disk is required.
run: iso
	qemu-system-x86_64 -cdrom $(BUILD)/kilons.iso -m 256M -serial stdio -display gtk -no-reboot

# Optional, explicitly requested FAT32 testing (read-only guest driver).
# Supply DISK=path/to/test-fat32.img. Never use a physical disk device.
COMMA := ,
run-fat32: iso
	@test -n "$(DISK)" || (echo 'Usage: make run-fat32 DISK=test-fat32.img' && exit 1)
	@test -f "$(DISK)" || (echo 'Disk image not found: $(DISK)' && exit 1)
	qemu-system-x86_64 -cdrom $(BUILD)/kilons.iso -m 256M -serial stdio -display gtk -no-reboot -drive file=$(DISK)$(COMMA)format=raw$(COMMA)if=ide$(COMMA)index=0$(COMMA)media=disk$(COMMA)snapshot=on

clean:
	rm -rf $(BUILD)

$(BUILD)/exec_args.o: kernel/process/exec_args.c kernel/include/kilons/exec_args.h include/exec_limits.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/ring3.o: kernel/arch/amd64/ring3.c include/signal_numbers.h kernel/include/kilons/exec_args.h include/exec_limits.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/ring3_entry.o: boot/amd64/ring3.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/record_lock.o: kernel/filesystem/vfs/record_lock.c kernel/include/kilons/record_lock.h include/record_lock.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/posix.o: kernel/arch/amd64/posix.c include/fcntl_flags.h kernel/include/kilons/record_lock.h include/record_lock.h include/sys/stat.h kernel/include/kilons/posix.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

USER_CFLAGS := -ffunction-sections -fdata-sections -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pie -m64 -mno-red-zone -fno-asynchronous-unwind-tables -fno-unwind-tables -mgeneral-regs-only

# Hosted process-state regression. Outputs remain under build/ and are not shipped.
.PHONY: check
check: all
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel/include -DKILONS_HOST_TEST tests/process_state_test.c kernel/process/process.c -o $(BUILD)/process_state_test
	./$(BUILD)/process_state_test
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel/include tests/host_filesystem_block_test.c kernel/filesystem/vfs/vfs.c kernel/filesystem/ramfs.c kernel/filesystem/fat32_vfs.c kernel/devices/block.c $(KILOFS_SRCS) kernel/filesystem/kilofs/block_adapter.c -o $(BUILD)/host_filesystem_block_test
	./$(BUILD)/host_filesystem_block_test
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel/include tests/host_vfs_034_test.c kernel/filesystem/vfs/vfs.c kernel/filesystem/ramfs.c kernel/filesystem/fat32_vfs.c kernel/devices/block.c $(KILOFS_SRCS) kernel/filesystem/kilofs/block_adapter.c -o $(BUILD)/host_vfs_034_test
	./$(BUILD)/host_vfs_034_test
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel/include tests/host_posix_io_test.c kernel/arch/amd64/posix.c kernel/filesystem/vfs/record_lock.c kernel/filesystem/vfs/vfs.c kernel/filesystem/ramfs.c kernel/filesystem/fat32_vfs.c kernel/devices/block.c $(KILOFS_SRCS) kernel/filesystem/kilofs/block_adapter.c -o $(BUILD)/host_posix_io_test
	./$(BUILD)/host_posix_io_test
	python3 tests/check_elf.py
	python3 tests/check_instruction_set.py

$(BUILD)/block.o: kernel/devices/block.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/ata_pio.o: kernel/devices/ata_pio.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: smoke
smoke: iso
	sh tests/qemu_smoke.sh

# kilofs host-only validation; never touches a physical disk.
KILOFS_SRCS := kernel/filesystem/kilofs/disk.c kernel/filesystem/kilofs/alloc.c kernel/filesystem/kilofs/inode.c kernel/filesystem/kilofs/directory.c kernel/filesystem/kilofs/file.c kernel/filesystem/kilofs/vfs_adapter.c kernel/filesystem/kilofs/fsck.c
.PHONY: check-kilofs
check-kilofs:
	mkdir -p $(BUILD)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror -Ikernel/include $(KILOFS_SRCS) tests/host_kilofs_test.c -o $(BUILD)/host_kilofs_test
	./$(BUILD)/host_kilofs_test
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror -Ikernel/include $(KILOFS_SRCS) tests/host_kilofs_indirect_test.c -o $(BUILD)/host_kilofs_indirect_test
	./$(BUILD)/host_kilofs_indirect_test

.PHONY: tools-kilofs
$(BUILD)/mkfs.kilofs: tools/mkfs.kilofs.c tools/kilofs_image.h $(KILOFS_SRCS) | $(BUILD)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror -Ikernel/include $(KILOFS_SRCS) tools/mkfs.kilofs.c -o $@
$(BUILD)/fsck.kilofs: tools/fsck.kilofs.c tools/kilofs_image.h $(KILOFS_SRCS) | $(BUILD)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror -Ikernel/include $(KILOFS_SRCS) tools/fsck.kilofs.c -o $@
tools-kilofs: $(BUILD)/mkfs.kilofs $(BUILD)/fsck.kilofs

# Include kilofs in kernel binary. The normal boot path only mounts existing
# kilofs partitions read-only; no automatic format or disk writes.
KFS_KERNEL_OBJS := $(addprefix $(BUILD)/kfs_,$(addsuffix .o,disk alloc inode directory file vfs_adapter fsck block_adapter))
OBJS += $(KFS_KERNEL_OBJS)
$(BUILD)/kfs_%.o: kernel/filesystem/kilofs/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/kilons.elf: $(KFS_KERNEL_OBJS)
.PHONY: check-vfs-kilofs
check-vfs-kilofs: | $(BUILD)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror -Ikernel/include tests/host_vfs_kilofs_test.c kernel/filesystem/vfs/vfs.c kernel/filesystem/ramfs.c kernel/filesystem/fat32_vfs.c kernel/devices/block.c $(KILOFS_SRCS) kernel/filesystem/kilofs/block_adapter.c -o $(BUILD)/host_vfs_kilofs_test
	./$(BUILD)/host_vfs_kilofs_test

# A preformatted disposable image can be attached read-only. If its size is
# <=128 MiB, the boot path recognizes whole-disk kilofs at block 0.
.PHONY: run-kilofs-ro
run-kilofs-ro: iso
	@test -n "$(DISK)" && test -f "$(DISK)" || (echo 'Set DISK=path/to/disposable-kilofs.img' && exit 1)
	qemu-system-x86_64 -cdrom $(BUILD)/kilons.iso -m 256M -serial stdio -display gtk -no-reboot -drive file=$(DISK)$(COMMA)format=raw$(COMMA)if=ide$(COMMA)index=0$(COMMA)media=disk$(COMMA)snapshot=on
.PHONY: check-posix-example
check-posix-example: check-core-guest
	@echo 'POSIX example guest execution is covered by test19'
.PHONY: check-kilofs-fault
check-kilofs-fault: | $(BUILD)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror -Ikernel/include $(KILOFS_SRCS) tests/host_kilofs_fault_test.c -o $(BUILD)/host_kilofs_fault_test
	./$(BUILD)/host_kilofs_fault_test

# Unix-style installation into a NEW kilofs disk image. The source build/
# artifacts remain separate from filesystem paths. No physical disk writes.
# Existing no-argument probes supply their own entry point. New commands
# use main(argc,argv) and the shared System V AMD64 startup object.
LEGACY := demo second fault probe hog quick forkprobe execprobe mapprobe filter posix_example
INIT_MAIN := signalprobe
UTILITIES := $(basename $(notdir $(wildcard system/bin/*.c)))
USER_ROOTFS_PROGRAMS := $(LEGACY) $(INIT_MAIN) ud divide pks $(UTILITIES)
USER_ROOTFS_ELFS := $(BUILD)/user_init.elf $(addprefix $(BUILD)/user_,$(addsuffix .elf,$(USER_ROOTFS_PROGRAMS)))
USER_LINK := -Wl,--gc-sections -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,system/init/user.ld
USER_LIB := system/libc/signal.c system/libc/unistd.c system/libc/dirent.c system/libc/env.c system/libc/time.c system/libc/exec.c
USER_HEADERS := include/signal_numbers.h include/fcntl_flags.h $(wildcard system/libc/include/bits/*.h) include/dirent.h include/sys/stat.h $(wildcard system/libc/*.h system/libc/include/*.h system/libc/include/sys/*.h)
USER_LIB_OBJS := $(patsubst system/libc/%.c,$(BUILD)/libc/%.o,$(USER_LIB))
USER_LIB_ASM := system/libc/signal_restore.S
USER_LIB_ASM_OBJS := $(patsubst system/libc/%.S,$(BUILD)/libc/%.o,$(USER_LIB_ASM))
$(BUILD)/libc/%.o: system/libc/%.c $(USER_HEADERS) include/exec_limits.h include/record_lock.h | $(BUILD)
	mkdir -p $(BUILD)/libc
	$(CC) $(USER_CFLAGS) -c $< -o $@
$(BUILD)/libc/%.o: system/libc/%.S | $(BUILD)
	mkdir -p $(BUILD)/libc
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/libc.a: $(USER_LIB_OBJS) $(USER_LIB_ASM_OBJS)
	$(AR) rcs $@ $^
$(BUILD)/crt0.o: system/libc/crt0.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

# Standalone AMD64 user development files. Install the actual declarations
# behind the in-tree forwarding headers, so no source checkout is required.
.PHONY: sysroot
sysroot: $(BUILD)/libc.a $(BUILD)/crt0.o
	mkdir -p $(BUILD)/sysroot/usr/include/sys $(BUILD)/sysroot/usr/include/bits $(BUILD)/sysroot/usr/lib
	cp system/libc/include/*.h $(BUILD)/sysroot/usr/include/
	cp system/libc/include/sys/*.h $(BUILD)/sysroot/usr/include/sys/
	cp include/sys/stat.h $(BUILD)/sysroot/usr/include/bits/
	cp include/dirent.h system/libc/time.h $(BUILD)/sysroot/usr/include/
	cp system/libc/env.h $(BUILD)/sysroot/usr/include/stdlib.h
	cp include/signal_numbers.h include/record_lock.h include/fcntl_flags.h $(BUILD)/sysroot/usr/include/bits/
	cp $(BUILD)/libc.a $(BUILD)/crt0.o system/init/user.ld $(BUILD)/sysroot/usr/lib/

.PHONY: check-sysroot
check-sysroot: sysroot
	$(CC) $(USER_CFLAGS) -nostdinc -isystem $(BUILD)/sysroot/usr/include -isystem "$(shell $(CC) -print-file-name=include)" -nostdlib -static -no-pie -Wl,--gc-sections,--build-id=none -Wl,-T,$(BUILD)/sysroot/usr/lib/user.ld system/bin/posixcheck.c $(BUILD)/sysroot/usr/lib/crt0.o $(BUILD)/sysroot/usr/lib/libc.a -o $(BUILD)/sysroot-posixcheck.elf

$(USER_ROOTFS_ELFS): $(USER_HEADERS) system/init/user.ld
$(addprefix $(BUILD)/user_,$(addsuffix .elf,$(LEGACY))): $(BUILD)/user_%.elf: system/init/%.c $(BUILD)/libc.a | $(BUILD)
	$(CC) $(USER_CFLAGS) $(USER_LINK) $< $(BUILD)/libc.a -o $@
$(addprefix $(BUILD)/user_,$(addsuffix .elf,$(INIT_MAIN))): $(BUILD)/user_%.elf: system/init/%.c $(BUILD)/crt0.o $(BUILD)/libc.a | $(BUILD)
	$(CC) $(USER_CFLAGS) -Isystem/libc/include $(USER_LINK) $< $(BUILD)/crt0.o $(BUILD)/libc.a -o $@
$(BUILD)/user_ud.elf: system/init/invalid_opcode.c $(BUILD)/libc.a | $(BUILD)
	$(CC) $(USER_CFLAGS) $(USER_LINK) $< $(BUILD)/libc.a -o $@
$(BUILD)/user_divide.elf: system/init/divide_fault.c $(BUILD)/libc.a | $(BUILD)
	$(CC) $(USER_CFLAGS) $(USER_LINK) $< $(BUILD)/libc.a -o $@
$(addprefix $(BUILD)/user_,$(addsuffix .elf,$(UTILITIES))): $(BUILD)/user_%.elf: system/bin/%.c $(BUILD)/crt0.o $(BUILD)/libc.a | $(BUILD)
	$(CC) $(USER_CFLAGS) -Isystem/libc/include $(USER_LINK) $< $(BUILD)/crt0.o $(BUILD)/libc.a -o $@
$(BUILD)/user_init.elf: system/sbin/init.c $(BUILD)/crt0.o $(BUILD)/libc.a | $(BUILD)
	$(CC) $(USER_CFLAGS) -Isystem/libc/include $(USER_LINK) $< $(BUILD)/crt0.o $(BUILD)/libc.a -o $@
$(BUILD)/user_pks.elf: system/shell/pks.c $(BUILD)/crt0.o $(BUILD)/libc.a | $(BUILD)
	$(CC) $(USER_CFLAGS) -Isystem/libc/include $(USER_LINK) $< $(BUILD)/crt0.o $(BUILD)/libc.a -o $@
check: $(USER_ROOTFS_ELFS)
$(BUILD)/mkrootfs.kilofs: tools/mkrootfs.kilofs.c tools/kilofs_image.h $(KILOFS_SRCS) | $(BUILD)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror -Ikernel/include $(KILOFS_SRCS) tools/mkrootfs.kilofs.c -o $@
.PHONY: rootfs-kilofs
# Use ROOTFS=build/rootfs.kilofs, BLOCKS=4096 (16 MiB) by default.
ROOTFS ?= $(BUILD)/rootfs.kilofs
BLOCKS ?= 4096
rootfs-kilofs: $(BUILD)/mkrootfs.kilofs $(BUILD)/fsck.kilofs $(USER_ROOTFS_ELFS)
	@set -eu; test ! -e "$(ROOTFS)" || test -f "$(ROOTFS)"; \
	tmp=$$(mktemp -d -- "$(ROOTFS).XXXXXX"); \
	trap 'rm -rf -- "$$tmp"' EXIT; \
	./$(BUILD)/mkrootfs.kilofs "$$tmp/image" "$(BLOCKS)" /bin/stress=$(BUILD)/user_kregress.elf /bin/test19=$(BUILD)/user_kregress.elf /sbin/init=$(BUILD)/user_init.elf /bin/sh=$(BUILD)/user_pks.elf $(foreach p,$(USER_ROOTFS_PROGRAMS),/bin/$(p)=$(BUILD)/user_$(p).elf); \
	./$(BUILD)/fsck.kilofs "$$tmp/image"; \
	mv -fT -- "$$tmp/image" "$(ROOTFS)"
.PHONY: run-rootfs-kilofs
run-rootfs-kilofs: iso
	@test -f "$(ROOTFS)" || (echo 'Run make rootfs-kilofs first' && exit 1)
	qemu-system-x86_64 -cdrom $(BUILD)/kilons.iso -m 256M -serial stdio -display gtk -no-reboot -drive file=$(ROOTFS)$(COMMA)format=raw$(COMMA)if=ide$(COMMA)index=0$(COMMA)media=disk$(COMMA)snapshot=on

# 0.3.4-3 P0 hardening: deterministic hosted failure/limit tests.
# These test real allocator and process state code, but DO NOT replace
# QEMU instruction/interrupt/CR3/ELF execution verification.
.PHONY: check-p0
check-p0: | $(BUILD)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror -DKILONS_HOST_TEST -Ikernel/include tests/host_p0_scheduler_test.c kernel/process/scheduler.c -o $(BUILD)/host_p0_scheduler_test
	./$(BUILD)/host_p0_scheduler_test
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror -DKILONS_HOST_TEST -Ikernel/include tests/host_p0_physical_test.c kernel/memory/physical.c -o $(BUILD)/host_p0_physical_test
	./$(BUILD)/host_p0_physical_test
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror -DKILONS_HOST_TEST -Ikernel/include tests/host_p0_vm_test.c kernel/memory/virtual.c kernel/process/user_signal.c -o $(BUILD)/host_p0_vm_test
	./$(BUILD)/host_p0_vm_test
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror -DKILONS_HOST_TEST -Ikernel/include tests/host_p0_heap_test.c kernel/memory/heap.c -o $(BUILD)/host_p0_heap_test
	./$(BUILD)/host_p0_heap_test
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror -DKILONS_HOST_TEST -Ikernel/include tests/host_p0_process_test.c kernel/process/process.c -o $(BUILD)/host_p0_process_test
	./$(BUILD)/host_p0_process_test
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror -Ikernel/include tests/host_p0_syscalls_test.c kernel/arch/amd64/posix.c kernel/filesystem/vfs/record_lock.c kernel/filesystem/vfs/vfs.c kernel/filesystem/ramfs.c kernel/filesystem/fat32_vfs.c kernel/devices/block.c $(KILOFS_SRCS) kernel/filesystem/kilofs/block_adapter.c -o $(BUILD)/host_p0_syscalls_test
	./$(BUILD)/host_p0_syscalls_test

# Optional on a machine with QEMU and grub-mkrescue. The hosted tests above
# cannot verify real interrupts, CR3, ELFs or GRUB module placement.
.PHONY: check-qemu-p0
check-qemu-p0: iso
	sh tests/qemu_p0_smoke.sh

# Optional hosted AddressSanitizer + UndefinedBehaviorSanitizer P0 suite.
# This is not a substitute for QEMU/real-CPU privilege and IRQ checks.
.PHONY: check-p0-sanitize
check-p0-sanitize:
	sh tests/p0_sanitize.sh

# Run the shell entry point against host process and filesystem adapters.
.PHONY: check-pks
check-pks: | $(BUILD)
	$(CC) -D_POSIX_C_SOURCE=200809L -std=c11 -O2 -Wall -Wextra -Werror system/shell/pks.c -o $(BUILD)/host_pks
	python3 tests/host_pks_test.py $(BUILD)/host_pks

.PHONY: check-boot-root
check-boot-root: rootfs-kilofs
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel/include tests/host_boot_root_test.c kernel/filesystem/vfs/vfs.c kernel/filesystem/ramfs.c kernel/filesystem/fat32_vfs.c kernel/devices/block.c $(KILOFS_SRCS) kernel/filesystem/kilofs/block_adapter.c -o $(BUILD)/host_boot_root
	$(BUILD)/host_boot_root "$(ROOTFS)" /bin/sh /bin/stress /bin/test19 $(addprefix /bin/,$(USER_ROOTFS_PROGRAMS))

.PHONY: check-exec
check-exec: | $(BUILD)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel/include tests/host_exec_args_test.c kernel/process/exec_args.c -o $(BUILD)/host_exec_args
	$(BUILD)/host_exec_args
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Derrno=target_errno -Dexecv=target_execv -Dexecl=target_execl -Dexecle=target_execle -Dexeclp=target_execlp -Dexecve=target_execve -Dexecvp=target_execvp -Denviron=target_environ -Dgetenv=target_getenv -Dsetenv=target_setenv -Dunsetenv=target_unsetenv tests/host_exec_search_test.c system/libc/exec.c system/libc/env.c -o $(BUILD)/host_exec_search
	$(BUILD)/host_exec_search

$(addprefix $(BUILD)/user_,$(addsuffix .elf,$(USER_ROOTFS_PROGRAMS) init)): include/exec_limits.h include/record_lock.h

.PHONY: check-core check-core-guest
check-core: all rootfs-kilofs
	$(MAKE) check check-exec check-pks check-p0 check-kilofs check-kilofs-fault check-vfs-kilofs check-boot-root check-sysroot
check-core-guest: iso
	python3 tests/qemu_core_test.py $(BUILD)/kilons.iso

$(BUILD)/user_signal.o: kernel/process/user_signal.c kernel/include/kilons/user_signal.h include/signal_numbers.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/ring3.o: kernel/include/kilons/user_signal.h

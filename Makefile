CC ?= gcc
LD ?= ld
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -mgeneral-regs-only -mcmodel=kernel -Ikernel/include
ASFLAGS := -m64
BUILD := build
OBJS := $(BUILD)/boot.o $(BUILD)/kernel.o $(BUILD)/console.o $(BUILD)/serial.o $(BUILD)/physical.o $(BUILD)/keyboard.o $(BUILD)/interrupt.o $(BUILD)/interrupt_stubs.o $(BUILD)/virtual.o $(BUILD)/heap.o $(BUILD)/tss.o $(BUILD)/scheduler.o $(BUILD)/process.o $(BUILD)/context.o $(BUILD)/block.o $(BUILD)/ata_pio.o $(BUILD)/fat32.o $(BUILD)/ramfs.o $(BUILD)/vfs.o $(BUILD)/iso9660.o $(BUILD)/power.o $(BUILD)/ring3.o $(BUILD)/ring3_entry.o $(BUILD)/posix.o $(BUILD)/user_demo_embed.o $(BUILD)/user_second_embed.o $(BUILD)/user_pks_embed.o $(BUILD)/user_fault_embed.o $(BUILD)/user_probe_embed.o $(BUILD)/user_ud_embed.o $(BUILD)/user_divide_embed.o $(BUILD)/user_hog_embed.o $(BUILD)/user_quick_embed.o $(BUILD)/user_forkprobe_embed.o $(BUILD)/user_execprobe_embed.o $(BUILD)/user_mapprobe_embed.o $(BUILD)/user_filter_embed.o
.PHONY: all clean iso run run-fat32
all: $(BUILD)/kilons.elf
$(BUILD):
	mkdir -p $@
$(BUILD)/boot.o: boot/amd64/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/kernel.o: kernel/core/main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
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
$(BUILD)/fat32.o: kernel/filesystem/fat32.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/ramfs.o: kernel/filesystem/ramfs.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/vfs.o: kernel/filesystem/vfs.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/iso9660.o: kernel/filesystem/iso9660.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/power.o: kernel/arch/amd64/power.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/context.o: boot/amd64/context.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/kilons.elf: $(OBJS) boot/amd64/linker.ld
	$(LD) -m elf_x86_64 -nostdlib -z max-page-size=0x1000 -T boot/amd64/linker.ld -o $@ $(OBJS)
iso: all
	@command -v grub-mkrescue >/dev/null || (echo 'grub-mkrescue is required' && exit 1)
	mkdir -p $(BUILD)/iso/boot/grub
	cp $(BUILD)/kilons.elf $(BUILD)/iso/boot/kilons.elf
	cp boot/grub/grub.cfg $(BUILD)/iso/boot/grub/grub.cfg
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
	qemu-system-x86_64 -cdrom $(BUILD)/kilons.iso -m 256M -serial stdio -display gtk -no-reboot -drive file=$(DISK)$(COMMA)format=raw$(COMMA)if=ide$(COMMA)index=0$(COMMA)media=disk

clean:
	rm -rf $(BUILD)

$(BUILD)/ring3.o: kernel/arch/amd64/ring3.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/ring3_entry.o: boot/amd64/ring3.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/posix.o: kernel/arch/amd64/posix.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

USER_CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pie -m64 -mno-red-zone -fno-asynchronous-unwind-tables -fno-unwind-tables -mgeneral-regs-only
$(BUILD)/user_demo.elf: system/init/demo.c system/libc/unistd.c system/libc/unistd.h system/init/user.ld | $(BUILD)
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,system/init/user.ld system/init/demo.c system/libc/unistd.c -o $@
$(BUILD)/user_demo_embed.o: $(BUILD)/user_demo.elf
	$(LD) -r -b binary -o $@ $<

$(BUILD)/user_second.elf: system/init/second.c system/libc/unistd.c system/libc/unistd.h system/init/user.ld | $(BUILD)
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,system/init/user.ld system/init/second.c system/libc/unistd.c -o $@
$(BUILD)/user_second_embed.o: $(BUILD)/user_second.elf
	$(LD) -r -b binary -o $@ $<

$(BUILD)/user_pks.elf: system/shell/pks.c system/libc/unistd.c system/libc/unistd.h system/init/user.ld | $(BUILD)
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,system/init/user.ld system/shell/pks.c system/libc/unistd.c -o $@
$(BUILD)/user_pks_embed.o: $(BUILD)/user_pks.elf
	$(LD) -r -b binary -o $@ $<
$(BUILD)/user_fault.elf: system/init/fault.c system/libc/unistd.c system/libc/unistd.h system/init/user.ld | $(BUILD)
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,system/init/user.ld system/init/fault.c system/libc/unistd.c -o $@
$(BUILD)/user_fault_embed.o: $(BUILD)/user_fault.elf
	$(LD) -r -b binary -o $@ $<

$(BUILD)/user_probe.elf: system/init/probe.c system/libc/unistd.c system/libc/unistd.h system/init/user.ld | $(BUILD)
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,system/init/user.ld system/init/probe.c system/libc/unistd.c -o $@
$(BUILD)/user_probe_embed.o: $(BUILD)/user_probe.elf
	$(LD) -r -b binary -o $@ $<

$(BUILD)/user_ud.elf: system/init/invalid_opcode.c system/libc/unistd.c system/libc/unistd.h system/init/user.ld | $(BUILD)
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,system/init/user.ld system/init/invalid_opcode.c system/libc/unistd.c -o $@
$(BUILD)/user_ud_embed.o: $(BUILD)/user_ud.elf
	$(LD) -r -b binary -o $@ $<

$(BUILD)/user_divide.elf: system/init/divide_fault.c system/libc/unistd.c system/libc/unistd.h system/init/user.ld | $(BUILD)
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,system/init/user.ld system/init/divide_fault.c system/libc/unistd.c -o $@
$(BUILD)/user_divide_embed.o: $(BUILD)/user_divide.elf
	$(LD) -r -b binary -o $@ $<

# Hosted process-state regression. Outputs remain under build/ and are not shipped.
.PHONY: check
check: all
	$(CC) -std=c11 -Wall -Wextra -Werror -Ikernel/include tests/process_state_test.c kernel/process/process.c -o $(BUILD)/process_state_test
	./$(BUILD)/process_state_test
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel/include tests/host_filesystem_block_test.c kernel/filesystem/vfs.c kernel/filesystem/ramfs.c kernel/devices/block.c -o $(BUILD)/host_filesystem_block_test
	./$(BUILD)/host_filesystem_block_test
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Ikernel/include tests/host_posix_io_test.c kernel/arch/amd64/posix.c kernel/filesystem/vfs.c kernel/filesystem/ramfs.c -o $(BUILD)/host_posix_io_test
	./$(BUILD)/host_posix_io_test
	python3 tests/check_elf.py
	python3 tests/check_instruction_set.py

$(BUILD)/user_hog.elf: system/init/hog.c system/libc/unistd.c system/libc/unistd.h system/init/user.ld | $(BUILD)
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,system/init/user.ld system/init/hog.c system/libc/unistd.c -o $@
$(BUILD)/user_hog_embed.o: $(BUILD)/user_hog.elf
	$(LD) -r -b binary -o $@ $<

$(BUILD)/user_quick.elf: system/init/quick.c system/libc/unistd.c system/libc/unistd.h system/init/user.ld | $(BUILD)
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,system/init/user.ld system/init/quick.c system/libc/unistd.c -o $@
$(BUILD)/user_quick_embed.o: $(BUILD)/user_quick.elf
	$(LD) -r -b binary -o $@ $<

$(BUILD)/user_forkprobe.elf: system/init/forkprobe.c system/libc/unistd.c system/libc/unistd.h system/init/user.ld | $(BUILD)
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,system/init/user.ld system/init/forkprobe.c system/libc/unistd.c -o $@
$(BUILD)/user_forkprobe_embed.o: $(BUILD)/user_forkprobe.elf
	$(LD) -r -b binary -o $@ $<

$(BUILD)/user_execprobe.elf: system/init/execprobe.c system/libc/unistd.c system/libc/unistd.h system/init/user.ld | $(BUILD)
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,system/init/user.ld system/init/execprobe.c system/libc/unistd.c -o $@
$(BUILD)/user_execprobe_embed.o: $(BUILD)/user_execprobe.elf
	$(LD) -r -b binary -o $@ $<

$(BUILD)/user_mapprobe.elf: system/init/mapprobe.c system/libc/unistd.c system/libc/unistd.h system/init/user.ld | $(BUILD)
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,system/init/user.ld system/init/mapprobe.c system/libc/unistd.c -o $@
$(BUILD)/user_mapprobe_embed.o: $(BUILD)/user_mapprobe.elf
	$(LD) -r -b binary -o $@ $<

$(BUILD)/block.o: kernel/devices/block.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/ata_pio.o: kernel/devices/ata_pio.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: smoke
smoke: iso
	sh tests/qemu_smoke.sh

$(BUILD)/user_filter.elf: system/init/filter.c system/libc/unistd.c system/libc/unistd.h system/init/user.ld | $(BUILD)
	$(CC) $(USER_CFLAGS) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,system/init/user.ld system/init/filter.c system/libc/unistd.c -o $@
$(BUILD)/user_filter_embed.o: $(BUILD)/user_filter.elf
	$(LD) -r -b binary -o $@ $<

CC ?= gcc
LD ?= ld
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -mgeneral-regs-only -mcmodel=kernel -Ikernel/include
ASFLAGS := -m64
BUILD := build
OBJS := $(BUILD)/boot.o $(BUILD)/kernel.o $(BUILD)/console.o $(BUILD)/serial.o $(BUILD)/physical.o $(BUILD)/keyboard.o $(BUILD)/interrupt.o $(BUILD)/interrupt_stubs.o $(BUILD)/virtual.o $(BUILD)/heap.o $(BUILD)/tss.o $(BUILD)/scheduler.o $(BUILD)/context.o $(BUILD)/shell.o $(BUILD)/fat32.o
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
$(BUILD)/scheduler.o: kernel/process/scheduler.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/shell.o: kernel/debug/shell.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/fat32.o: kernel/filesystem/fat32.c | $(BUILD)
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

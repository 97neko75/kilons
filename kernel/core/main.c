#include <stdint.h>
#include <kilons/console.h>
#include <kilons/version.h>
#include <kilons/serial.h>
#include <kilons/memory.h>
#include <kilons/keyboard.h>
#include <kilons/interrupt.h>
#include <kilons/vm.h>
#include <kilons/heap.h>
#include <kilons/tss.h>
#include <kilons/scheduler.h>
#include <kilons/ring3.h>
#include <kilons/ramfs.h>
#include <kilons/vfs.h>
#include <kilons/block.h>
#include <kilons/ata_pio.h>
#include <kilons/boot_module.h>
static void print_number(uint64_t n) {
    char buf[21]; unsigned i = sizeof(buf)-1; buf[i]=0;
    do { buf[--i]=(char)('0'+n%10); n/=10; } while (n);
    console_write(buf+i);
}
static void halt(void) { for (;;) __asm__ volatile ("cli; hlt"); }
static void worker_a(void) { for (;;) { thread_sleep(10); } }
static void worker_b(void) { for (;;) { thread_sleep(25); } }
void kernel_main(uint32_t multiboot_info_phys) {
    serial_init(); console_init();
    console_write("kilons " KILONS_VERSION_STRING ": amd64 long mode entered\n");
    if (!memory_init(multiboot_info_phys)) {
        console_write("kilons: ERROR invalid Multiboot2 memory map\n"); halt();
    }
    uint32_t before = memory_free_pages();
    console_write("kilons: usable free 4KiB frames: "); print_number(before); console_write("\n");
    uint64_t a = page_alloc(), b = page_alloc();
    if (!a || !b || a == b || memory_free_pages() != before-2 ||
        !page_free(a) || !page_free(b) || memory_free_pages() != before || page_free(a)) {
        console_write("kilons: ERROR physical allocator self-test failed\n"); halt();
    }
    if(!memory_validate()) {
        console_write("kilons: ERROR physical bitmap invariant violated\n");halt();
    }
    console_write("kilons: physical page allocator self-test passed\n");
    tss_init();
    console_write("kilons: GDT/TSS and IST initialized\n");
    if (!vm_init()) { console_write("kilons: ERROR VM init\n"); halt(); }
    uint64_t frame=page_alloc();
    if (!frame || !vm_map(VM_TEST_BASE,frame,VM_WRITABLE) || vm_translate(VM_TEST_BASE)!=frame) {
        console_write("kilons: ERROR VM map test\n"); halt();
    }
    volatile uint64_t *test=(volatile uint64_t *)(uintptr_t)VM_TEST_BASE;
    *test=0x4b494c4f4e53ULL;
    if (*test!=0x4b494c4f4e53ULL || !vm_unmap(VM_TEST_BASE) || vm_translate(VM_TEST_BASE) || !page_free(frame)) {
        console_write("kilons: ERROR VM unmap test\n"); halt();
    }
    console_write("kilons: virtual mapping self-test passed\n");
    if (!heap_init()) { console_write("kilons: ERROR heap init\n"); halt(); }
    uint64_t *heap_sample=kmalloc(sizeof(uint64_t)*8);
    if (!heap_sample) { console_write("kilons: ERROR kmalloc\n"); halt(); }
    for (unsigned i=0;i<8;i++) heap_sample[i]=i+100;
    for (unsigned i=0;i<8;i++) if (heap_sample[i]!=i+100) { console_write("kilons: ERROR heap test\n"); halt(); }
    if(!kfree(heap_sample) || kfree(heap_sample) || heap_used_bytes()!=0) {
        console_write("kilons: ERROR heap reclaim test\n");halt();
    }
    console_write("kilons: bootstrap heap self-test passed\n");
    interrupt_init();
    console_write("kilons: IDT/PIC/PIT initialized (100 Hz nominal)\n");
    scheduler_init();
    if (thread_create(worker_a)<0 || thread_create(worker_b)<0) {
        console_write("kilons: ERROR creating worker threads\n"); halt();
    }
    console_write("kilons: kernel cooperative threads and timed sleeping initialized\n");
    /* Do not drive experimental cooperative workers in the bootstrap
     * path. The P0-max change introduced active kernel context switching
     * before the first userspace prompt and caused a boot regression in
     * QEMU. Keep the workers registered, but defer dispatch until a proper
     * independent kernel idle/scheduling integration test exists. */
    console_write("kilons: kernel worker dispatch deferred until Ring 3 boot is verified\n");
    block_init();
    if(!block_selftest()) {console_write("kilons: ERROR block layer self-test failed\n");halt();}
    console_write("kilons: block RAM device self-test passed\n");
    ramfs_init();
    vfs_init();
    console_write("kilons: experimental RAMFS initialized (volatile memory only)\n");
    /* The GRUB module lives in reserved, identity-mapped physical memory.
     * Expose it as a read-only block device, never copy or write it. */
    int iso_rootfs=boot_module_register_rootfs(multiboot_info_phys);
    if(iso_rootfs>=0){
        if(vfs_mkdir("/mnt/disk")==0 && vfs_mount_kilofs("/mnt/disk",iso_rootfs,0)==0)
            console_write("kilons: ISO rootfs module mounted read-only at /mnt/disk\n");
        else console_write("kilons: ERROR ISO rootfs mount failed\n");
    }
    /* ATAPI/ISO9660 experimental probing is suspended for this milestone. */
    /* Optional primary-master ATA disk. Never issues a sector write.
     * Mount the first valid FAT32 MBR partition at /mnt/disk read-only. */
    int ata=ata_pio_register_block();
    if(ata>=0 && iso_rootfs<0) {
        console_write("kilons: ATA PIO disk registered (read-only)\n");
        /* Detect an explicitly formatted whole-disk kilofs test image first.
         * No sector is written to ATA hardware on normal boot. */
        if(vfs_mkdir("/mnt/disk")==0){
            int mounted=vfs_mount_kilofs("/mnt/disk",ata,0)==0;
            if(mounted)console_write("kilons: kilofs whole disk mounted read-only at /mnt/disk\n");
            for(unsigned part=0;part<4&&!mounted;part++){
                int device=block_register_mbr_partition(ata,part);
                if(device<0)continue;
                if(vfs_mount_kilofs("/mnt/disk",device,0)==0){
                    console_write("kilons: kilofs partition mounted read-only at /mnt/disk\n");
                    mounted=1;break;
                }
                if(vfs_mount_fat32("/mnt/disk",device)==0){
                    console_write("kilons: FAT32 partition mounted read-only at /mnt/disk\n");
                    mounted=1;break;
                }
            }
            if(!mounted)(void)vfs_rmdir("/mnt/disk");
        }
    }
    console_write("kilons: VFS regression ");
    console_write(vfs_selftest() ? "PASS\n" : "FAIL\n");
    console_write("kilons: RAMFS regression ");
    console_write(ramfs_selftest() ? "PASS\n" : "FAIL\n");
    if(vfs_use_boot_root()<0){console_write("kilons: rootfs requires executable /sbin/init\n");halt();}
    ring3_boot();
}

#!/bin/sh
# 0.3.4-4 boot acceptance gate (NOT an exhaustive guest stress test).
# Requires make iso, grub-mkrescue and QEMU. Nothing writes to a physical disk.
set -eu
command -v qemu-system-x86_64 >/dev/null || { echo 'QEMU required for guest validation' >&2; exit 77; }
command -v timeout >/dev/null || { echo 'timeout command required' >&2; exit 77; }
[ -f build/kilons.iso ] || { echo 'make iso before guest validation' >&2; exit 2; }
log="${TMPDIR:-/tmp}/kilons-p0-qemu-$$.log"
trap 'rm -f "$log"' EXIT HUP INT TERM
status=0
timeout 20 qemu-system-x86_64 -cdrom build/kilons.iso -m 256M \
  -serial file:"$log" -display none -monitor none -no-reboot || status=$?
if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
  cat "$log"; echo "QEMU exited with $status" >&2; exit 1
fi
for required in \
  "kilons $(cat VERSION): amd64 long mode entered" \
  'physical page allocator self-test passed' \
  'virtual mapping self-test passed' \
  'bootstrap heap self-test passed' \
  'kernel worker dispatch deferred until Ring 3 boot is verified' \
  'ISO rootfs module mounted read-only at /mnt/disk' \
  'VFS regression PASS' \
  'RAMFS regression PASS' \
  'independent CR3 mappings selftest PASS' \
  'kilons: P0 BOOT first user syscall nr=' \
  'kilons PKS: Ring 3 interactive shell' \
  'pks$ '; do
  if ! grep -Fq "$required" "$log"; then
    cat "$log";echo "MISSING BOOT MARKER: $required" >&2;exit 1
  fi
done
if grep -Eq 'CPU exception|selftest FAIL|P0 BOOT EFAULT|P0 BOOT ERROR|VFS regression FAIL' "$log"; then
  cat "$log";echo 'BOOT ERROR in guest log' >&2;exit 1
fi
echo 'QEMU P0 boot/CR3/VM/RAMFS/ISO-kilofs smoke: PASS'

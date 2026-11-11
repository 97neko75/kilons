#!/bin/sh
# Legacy name retained: ISO-embedded kilofs auto-mount smoke for stage-1 rootfs.
# No separate -drive is needed or permitted by this acceptance gate.
set -eu
command -v qemu-system-x86_64 >/dev/null || { echo 'QEMU missing' >&2;exit 77; }
command -v timeout >/dev/null || { echo 'timeout missing' >&2;exit 77; }
[ -f build/kilons.iso ] || { echo 'make iso first' >&2;exit 2; }
log="${TMPDIR:-/tmp}/kilons-kilofs-$$.log"
trap 'rm -f "$log"' EXIT HUP INT TERM
status=0
timeout 15 qemu-system-x86_64 -cdrom build/kilons.iso -m 256M \
    -display none -serial file:"$log" -monitor none -no-reboot || status=$?
if [ "$status" -ne 0 ] && [ "$status" -ne 124 ];then cat "$log";exit 1;fi
for marker in \
    'ISO rootfs module mounted read-only at /mnt/disk' \
    'VFS regression PASS' \
    'independent CR3 mappings selftest PASS';do
    if ! grep -Fq "$marker" "$log";then cat "$log";exit 1;fi
done
if grep -Eq 'CPU exception|selftest FAIL|regression FAIL' "$log";then cat "$log";exit 1;fi
printf 'QEMU ISO kilofs auto-mount smoke: PASS\n'

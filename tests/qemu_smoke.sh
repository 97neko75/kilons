#!/bin/sh
# Optional runtime smoke test on hosts with QEMU + grub-mkrescue installed.
set -eu
command -v qemu-system-x86_64 >/dev/null || { echo 'SKIP: no QEMU installed' >&2; exit 77; }
command -v grub-mkrescue >/dev/null || { echo 'SKIP: no GRUB ISO tooling' >&2; exit 77; }
make iso >/dev/null
log="${TMPDIR:-/tmp}/kilons-qemu-$$.log"
trap 'rm -f "$log"' EXIT HUP INT TERM
# Termination by timeout is expected: the interactive PKS does not exit.
timeout 12 qemu-system-x86_64 -cdrom build/kilons.iso -m 256M \
    -serial file:"$log" -display none -monitor none -no-reboot || status=$?
if [ "${status:-0}" -ne 0 ] && [ "${status:-0}" -ne 124 ]; then
    cat "$log";exit 1
fi
for marker in 'VFS regression PASS' 'RAMFS regression PASS' \
    'block RAM device self-test passed' 'started /sbin/init' \
    'independent CR3 mappings selftest PASS' 'Ring 3 interactive shell'; do
    if ! grep -q "$marker" "$log";then cat "$log";exit 1;fi
done
if grep -q 'CPU exception' "$log";then cat "$log";exit 1;fi
printf 'QEMU serial boot smoke PASS\n'

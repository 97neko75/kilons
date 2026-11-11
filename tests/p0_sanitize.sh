#!/bin/sh
# Hosted P0 sanitizers: exercises core C code, not CPU privilege transitions.
set -eu
CC=${SANITIZER_CC:-clang}
command -v "$CC" >/dev/null 2>&1 || { echo "Need $CC for P0 sanitizers" >&2; exit 77; }
mkdir -p build/sanitize
FLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -fno-omit-frame-pointer -fsanitize=address,undefined -DKILONS_HOST_TEST -Ikernel/include'
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1}
"$CC" $FLAGS tests/host_p0_physical_test.c kernel/memory/physical.c -o build/sanitize/physical
build/sanitize/physical
"$CC" $FLAGS tests/host_p0_vm_test.c kernel/memory/virtual.c -o build/sanitize/vm
build/sanitize/vm
"$CC" $FLAGS tests/host_p0_heap_test.c kernel/memory/heap.c -o build/sanitize/heap
build/sanitize/heap
"$CC" $FLAGS tests/host_p0_process_test.c kernel/process/process.c -o build/sanitize/process
build/sanitize/process
"$CC" $FLAGS tests/host_p0_syscalls_test.c kernel/arch/amd64/posix.c \
  kernel/filesystem/vfs.c kernel/filesystem/ramfs.c kernel/filesystem/fat32_vfs.c \
  kernel/devices/block.c kernel/filesystem/kilofs/disk.c \
  kernel/filesystem/kilofs/alloc.c kernel/filesystem/kilofs/inode.c \
  kernel/filesystem/kilofs/directory.c kernel/filesystem/kilofs/file.c \
  kernel/filesystem/kilofs/vfs_adapter.c kernel/filesystem/kilofs/fsck.c \
  kernel/filesystem/kilofs/block_adapter.c -o build/sanitize/syscalls
build/sanitize/syscalls
echo "P0 $CC ASan/UBSan hosted suite: PASS"

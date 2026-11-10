#!/usr/bin/env python3
"""Validate the bounded kilons amd64 ELF64 loader's assumptions."""
import glob
import struct
import sys
from pathlib import Path
ROOT = 0x32000000
IMAGE_END = ROOT + 16 * 4096
all_elf = sorted(glob.glob('build/user_*.elf'))
if not all_elf:
    raise SystemExit('FAIL: no ELF images built')
for filename in all_elf:
    data = Path(filename).read_bytes()
    if len(data) > 32768:
        raise SystemExit(f'FAIL: {filename}: exceeds RAMFS executable capacity')
    if data[:6] != b'\x7fELF\x02\x01':
        raise SystemExit(f'FAIL: {filename}: not ELF64 little-endian')
    kind, machine, version = struct.unpack_from('<HHI', data, 16)
    entry, phoff = struct.unpack_from('<QQ', data, 24)
    phentsize, phnum = struct.unpack_from('<HH', data, 54)
    if (kind, machine, version) != (2, 62, 1) or phentsize != 56:
        raise SystemExit(f'FAIL: {filename}: unexpected ELF header')
    if phoff + phnum * phentsize > len(data) or not phnum:
        raise SystemExit(f'FAIL: {filename}: bad headers')
    executable = False
    intervals = []
    for i in range(phnum):
        flags_type, flags, offset, address, _, filesize, memsize, _ = struct.unpack_from(
            '<IIQQQQQQ', data, phoff+i*phentsize)
        if flags_type != 1 or memsize == filesize == 0:
            continue
        if (flags & 3) == 3 or address < ROOT or address+memsize > IMAGE_END:
            raise SystemExit(f'FAIL: {filename}: W^X violation or VA overflow')
        if filesize > memsize or offset+filesize > len(data):
            raise SystemExit(f'FAIL: {filename}: filesz/memsz out of bounds')
        for begin, end in intervals:
            if begin < address+memsize and address < end:
                raise SystemExit(f'FAIL: {filename}: overlapping segments')
        intervals.append((address,address+memsize))
        if flags&1 and address <= entry < address+memsize:
            executable = True
    if not executable:
        raise SystemExit(f'FAIL: {filename}: entry is not in executable segment')
print(f'ELF64 W^X/VA/size bounds PASS ({len(all_elf)} programs)')

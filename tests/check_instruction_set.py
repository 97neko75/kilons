#!/usr/bin/env python3
"""Check function instructions only, not rodata embedded in .text."""
import glob
import re
import subprocess
files = sorted(glob.glob('build/user_*.elf'))
if not files:
    raise SystemExit('FAIL: no user ELF64 images')
for path in files:
    symbols = subprocess.check_output(['nm','-S','--defined-only',path],text=True)
    ranges = []
    for line in symbols.splitlines():
        match = re.fullmatch(r'([0-9a-f]+)\s+([0-9a-f]+)\s+[Tt]\s+\S+',line)
        if match:
            address,size = int(match[1],16),int(match[2],16)
            if size:ranges.append((address,address+size))
    code = subprocess.check_output(['objdump','-d',path],text=True)
    for line in code.splitlines():
        match = re.match(r'\s*([0-9a-f]+):',line)
        if not match or not re.search(r'%(?:xmm|ymm|zmm|mm[0-7]|st\()',line):
            continue
        address = int(match[1],16)
        if any(lo <= address < hi for lo,hi in ranges):
            raise SystemExit(f'FAIL: user SIMD/FPU instruction in {path}: {line.strip()}')
print(f'User ELF64 function instruction set PASS ({len(files)} programs)')

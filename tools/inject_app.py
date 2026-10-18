#!/usr/bin/env python3
"""Add our own code to OP1_vdk.ldr and hook the phase engine to it.

Takes an image linked for the free SDRAM region (ports/rings/op1/link.ld) and:
  1. adds its load segments as plain LDR blocks, and its .bss as a zero FILL
     block, inserted just before the FINAL block (so every existing block keeps
     its file offset; only FINAL and the trailing IGNORE block move);
  2. optionally fills the rest of the region with a pattern (--fill-pattern),
     which the probe build checks to catch other users of the region;
  3. redirects the phase engine's vtable entries (render, note on, note off)
     to the image's entry points, read from its header at the region start;
  4. applies the image's patch table, if its header points to one (header
     words 5 and 6: table address and entry count). Each entry is
     {address, kind, stock, value}: kind 0 replaces a 32-bit word, kinds 1
     and 2 replace the 16-bit immediate of an instruction with the low or high
     half of `value` (an address in the image; the linker can't emit masked
     halves in data). Patches must land in the app's own blocks.
Every patch checks the stock value first, like tools/patch_app.py.

The image must stay inside 0x02c00000-0x02ffffff (docs/memory-map.md) and
must not overlap anything the app loads.

Usage:
    python tools/inject_app.py IN.ldr OUT.ldr IMAGE.elf [--fill-pattern 0xa5a5a5a5]
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ldr_dump import LdrError, memory_runs, parse, serialize  # noqa: E402
from patch_app import apply_patch  # noqa: E402

REGION_START = 0x02c00000
REGION_END = 0x03000000
HEADER_MAGIC = 0x5831504f  # "OP1X"
DMA_CODE = 6               # as every other block in OP1_vdk.ldr

# Phase engine voice vtable (0x191113c): slot address -> stock function.
PHASE_RENDER = (0x01911150, 0x01a0bc5c)
PHASE_NOTE_ON = (0x01911158, 0x01a0be82)
PHASE_NOTE_OFF = (0x01911168, 0x01a1b5ec)

PT_LOAD = 1

PATCH_WORD32, PATCH_IMM16_LO, PATCH_IMM16_HI, PATCH_CALL24 = 0, 1, 2, 3
PATCH_KIND_NAMES = {PATCH_WORD32: 'word', PATCH_IMM16_LO: 'imm16 lo', PATCH_IMM16_HI: 'imm16 hi',
                    PATCH_CALL24: 'call'}
MAX_PATCHES = 64


def read_elf_segments(path):
    """Return [(vaddr, file_bytes, memsz)] for the PT_LOAD segments of an ELF32 LE file."""
    data = open(path, 'rb').read()
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        raise LdrError('{}: not a little-endian ELF32 file'.format(path))
    (e_phoff,) = struct.unpack_from('<I', data, 28)
    e_phentsize, e_phnum = struct.unpack_from('<HH', data, 42)
    segments = []
    for i in range(e_phnum):
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = \
            struct.unpack_from('<8I', data, e_phoff + i * e_phentsize)
        if p_type != PT_LOAD or p_memsz == 0:
            continue
        segments.append((p_vaddr, data[p_offset:p_offset + p_filesz], p_memsz))
    return sorted(segments)


def read_image(segments, addr, size):
    """Bytes of the image's initialised data at addr (must be in one segment)."""
    for vaddr, payload, _ in segments:
        if vaddr <= addr and addr + size <= vaddr + len(payload):
            return payload[addr - vaddr:addr - vaddr + size]
    raise LdrError('{:#x}: not in the initialised data of the image'.format(addr))


def read_patch_table(segments, table, count):
    """[(address, kind, stock, value)] from the image's patch table."""
    if count > MAX_PATCHES:
        raise LdrError('patch table has {} entries (max {})'.format(count, MAX_PATCHES))
    raw = read_image(segments, table, 16 * count)
    entries = [struct.unpack_from('<4I', raw, 16 * i) for i in range(count)]
    addresses = [e[0] for e in entries]
    if len(set(addresses)) != len(addresses):
        raise LdrError('patch table patches an address twice')
    return entries


def patch_bytes(entry):
    """(old, new) bytes for a patch table entry."""
    address, kind, stock, value = entry
    if kind == PATCH_WORD32:
        return struct.pack('<I', stock), struct.pack('<I', value)
    if kind in (PATCH_IMM16_LO, PATCH_IMM16_HI):
        if address & 1 or stock > 0xffff:
            raise LdrError('{:#x}: bad imm16 patch'.format(address))
        half = value & 0xffff if kind == PATCH_IMM16_LO else value >> 16
        return struct.pack('<H', stock), struct.pack('<H', half)
    if kind == PATCH_CALL24:
        return call24(address, stock), call24(address, value)
    raise LdrError('{:#x}: unknown patch kind {}'.format(address, kind))


def call24(address, target):
    """Blackfin 'CALL pcrel24' at address to target: two halfwords,
    0xE300 | offset[23:16], offset[15:0], offset = (target - address) / 2."""
    if address & 1 or target & 1:
        raise LdrError('{:#x}: CALL to {:#x}: odd address'.format(address, target))
    offset = (target - address) >> 1
    if not -(1 << 23) <= offset < (1 << 23):
        raise LdrError('{:#x}: CALL to {:#x}: out of range'.format(address, target))
    offset &= 0xffffff
    return struct.pack('<HH', 0xe300 | (offset >> 16), offset & 0xffff)


def plain_block(target, payload):
    if len(payload) % 4:
        payload = payload + b'\0' * (4 - len(payload) % 4)
    return {'flags': 0, 'dmacode': DMA_CODE, 'target': target, 'count': len(payload),
            'argument': 0, 'payload': bytes(payload)}


def fill_block(target, count, value):
    return {'flags': 0x0100, 'dmacode': DMA_CODE, 'target': target, 'count': count,
            'argument': value, 'payload': b''}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('ldr_in')
    parser.add_argument('ldr_out')
    parser.add_argument('image')
    parser.add_argument('--fill-pattern', type=lambda s: int(s, 0),
                        help='fill the rest of the region with this 32-bit value')
    args = parser.parse_args()

    dxes = parse(open(args.ldr_in, 'rb').read())
    if len(dxes) != 1:
        raise LdrError('expected a single-DXE app LDR, found {}'.format(len(dxes)))
    dxe = dxes[0]
    loaded = [(start, start + len(run)) for start, run in memory_runs(dxe)]

    segments = read_elf_segments(args.image)
    if not segments:
        raise LdrError('{}: no load segments'.format(args.image))
    new_blocks = []
    image_end = REGION_START
    header = None
    for vaddr, payload, memsz in segments:
        end = vaddr + memsz
        if vaddr < REGION_START or end > REGION_END:
            raise LdrError('segment {:#x}-{:#x} is outside {:#x}-{:#x}'.format(
                vaddr, end, REGION_START, REGION_END))
        for lo, hi in loaded:
            if vaddr < hi and lo < end:
                raise LdrError('segment {:#x}-{:#x} overlaps app data {:#x}-{:#x}'.format(
                    vaddr, end, lo, hi))
        if payload:
            new_blocks.append(plain_block(vaddr, payload))
            if vaddr == REGION_START:
                header = struct.unpack_from('<8I', payload, 0)
        filled = (len(payload) + 3) & ~3
        if memsz > filled:
            new_blocks.append(fill_block(vaddr + filled, (memsz - filled + 3) & ~3, 0))
        image_end = max(image_end, end)

    if header is None or header[0] != HEADER_MAGIC:
        raise LdrError('no OP1X header at {:#x}'.format(REGION_START))
    _, version, render, note_on, note_off, patch_table, patch_count = header[:7]
    patches = read_patch_table(segments, patch_table, patch_count) if patch_table else []

    if args.fill_pattern is not None:
        fill_start = (image_end + 4095) & ~4095
        new_blocks.append(fill_block(fill_start, REGION_END - fill_start, args.fill_pattern))

    final = [i for i, b in enumerate(dxe) if b['flags'] & 0x8000]
    if len(final) != 1:
        raise LdrError('expected exactly one FINAL block')
    dxe[final[0]:final[0]] = new_blocks

    hooks = [(PHASE_RENDER, render, 'render'), (PHASE_NOTE_ON, note_on, 'note on'),
             (PHASE_NOTE_OFF, note_off, 'note off')]
    for (slot, stock), target, name in hooks:
        if not target:
            continue
        if not REGION_START <= target < image_end:
            raise LdrError('{} entry {:#x} is outside the image'.format(name, target))
        apply_patch(dxes, slot, struct.pack('<I', stock), struct.pack('<I', target))
    for entry in patches:
        address = entry[0]
        if REGION_START <= address < REGION_END:
            raise LdrError('patch at {:#x} is inside our own region'.format(address))
        old, new = patch_bytes(entry)
        apply_patch(dxes, address, old, new)

    out = serialize(dxes)
    parse(out)  # re-check headers, checksums and the FIRST block's length
    with open(args.ldr_out, 'wb') as f:
        f.write(out)

    print('Image {} (header version {}): {:#x}-{:#x}, {} bytes'.format(
        os.path.basename(args.image), version, REGION_START, image_end, image_end - REGION_START))
    for b in new_blocks:
        print('  + {:<5} {:#010x} {:#9x} bytes{}'.format(
            'FILL' if b['flags'] & 0x0100 else 'block', b['target'], b['count'],
            ' value {:#010x}'.format(b['argument']) if b['flags'] & 0x0100 else ''))
    for (slot, stock), target, name in hooks:
        if target:
            print('  vtable {:#010x} ({}): {:#010x} -> {:#010x}'.format(slot, name, stock, target))
    for entry in patches:
        old, new = patch_bytes(entry)
        print('  patch  {:#010x} ({}): {} -> {}'.format(
            entry[0], PATCH_KIND_NAMES[entry[1]], old[::-1].hex(), new[::-1].hex()))
    print('{}: {} -> {} bytes'.format(args.ldr_out, os.path.getsize(args.ldr_in), len(out)))


if __name__ == '__main__':
    try:
        main()
    except LdrError as e:
        print('ERROR: {}'.format(e))
        sys.exit(1)

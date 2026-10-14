#!/usr/bin/env python3
"""Add our own code to OP1_vdk.ldr and hook the phase engine to it.

Takes an image linked for the free SDRAM region (ports/rings/op1/link.ld) and:
  1. adds its load segments as plain LDR blocks, and its .bss as a zero FILL
     block, inserted just before the FINAL block (so every existing block keeps
     its file offset; only FINAL and the trailing IGNORE block move);
  2. optionally fills the rest of the region with a pattern (--fill-pattern),
     which the probe build checks to catch other users of the region;
  3. redirects the phase engine's vtable entries (render, note on, note off)
     to the image's entry points, read from its header at the region start.
     Each patch checks the stock value first, like tools/patch_app.py.

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
    _, version, render, note_on, note_off = header[:5]

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
    print('{}: {} -> {} bytes'.format(args.ldr_out, os.path.getsize(args.ldr_in), len(out)))


if __name__ == '__main__':
    try:
        main()
    except LdrError as e:
        print('ERROR: {}'.format(e))
        sys.exit(1)

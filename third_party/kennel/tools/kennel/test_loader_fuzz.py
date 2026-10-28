"""Structured fuzz tests for the C loader (synth_sdk/kennel/kn_load.c) against the reference loader reloc.py.

    python3 -m unittest tools.kennel.test_loader_fuzz
    python3 -m unittest discover -s tools/kennel -p 'test_loader_fuzz.py'

test_loader.py already compares both loaders on random byte flips. This file mutates the file the way a broken or
hostile plugin would: boundary values in every header word, damaged relocation / import / string / extension
tables and SVG prefixes, then random combinations of those. Every case is deterministic (fixed seeds) and must
satisfy: (a) the C runner never crashes (exit 0 = accepted, 1 = refused with "error: ..."; a signal, a sanitizer
report or any other exit code fails), (b) C and reloc.py accept or refuse alike, (c) accepted images are equal.
The C side is built with -fsanitize=address,undefined from a private runner that allocates the file and the image
buffer at their exact sizes, so any read or write past either one aborts. Needs a host C compiler and the Blackfin
toolchain (for packing); skipped without them.
Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE).
"""
import itertools
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pack
import reloc

SDK = pack.SDK
BASES = (0x02980000, 0x0299F000)
IMPORT_BASE = 0x01E00100
HOST_CAP = 1 << 24                         # largest image the runner allocates
CC = shutil.which('cc')
try:
    pack.find_toolchain()
    HAVE_BFIN = True
except pack.PackError:
    HAVE_BFIN = False

PLUGINS = ('supersaw', 'tape', 'clouds', 'chord', 'reverb', 'quad', 'granular')
WORKERS = 6
ASAN_ENV = {'ASAN_OPTIONS': 'exitcode=77:detect_leaks=0:abort_on_error=0',
            'UBSAN_OPTIONS': 'exitcode=78:halt_on_error=1:print_stacktrace=1'}

# The sanitized runner: like harness/kennel_load_host.c, but the file and the image buffer are malloc'ed at their
# exact sizes (the stock runner uses 1 MiB static buffers, where an overread stays inside the array).
RUNNER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kn_load.h"
int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    long fl = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc(fl ? (size_t)fl : 1);
    uint32_t n = (uint32_t)fread(file, 1, (size_t)fl, f);
    fclose(f);
    uint32_t base = (uint32_t)strtoul(argv[2], 0, 0);
    kn_export ex[64];
    int ne = 0;
    for (int i = 4; i < argc && ne < 64; i++) {
        char *eq = strchr(argv[i], '=');
        if (!eq) continue;
        *eq = 0;
        ex[ne].name = argv[i];
        ex[ne++].addr = (uint32_t)strtoul(eq + 1, 0, 0);
    }
    kn_info info;
    const char *err;
    if (kn_parse(file, n, 1, &info, &err)) { fprintf(stderr, "error: %s\n", err); return 1; }
    if (info.load_size > (1u << 24)) { fprintf(stderr, "error: host cap\n"); return 1; }
    uint8_t *img = malloc(info.load_size ? info.load_size : 1);
    if (kn_relocate(file, &info, img, base, info.load_size, ex, ne, &err)) { fprintf(stderr, "error: %s\n", err); return 1; }
    FILE *o = fopen(argv[3], "wb");
    fwrite(img, 1, info.load_size, o);
    fclose(o);
    printf("ok %u\n", info.load_size);
    return 0;
}
'''

WORD_NAMES = (['magic', 'version_hdrsize', 'file_size', 'crc32', 'api_kind_order', 'name0', 'name1', 'name2',
               'name3', 'flags', 'defaults01', 'defaults23', 'page2_01', 'page2_23'] +
              reloc.FIELDS[reloc.FIELDS.index('text_size'):])
assert len(WORD_NAMES) == 36


def u32(d, off):
    return struct.unpack_from('<I', d, off)[0]


def put32(d, off, v):
    if 0 <= off and off + 4 <= len(d):
        struct.pack_into('<I', d, off, v & 0xFFFFFFFF)


def hoff_of(d):
    try:
        return reloc.header_offset(bytes(d))
    except reloc.PluginError:
        return None


def seal(d, size_from=None):
    """Fix up the CRC (and, when the length changed from size_from, the file_size field) of a mutated file."""
    h = hoff_of(d)
    if h is None or len(d) < h + 16:
        return d
    if size_from is not None and len(d) != size_from:
        put32(d, h + 8, len(d))
    put32(d, h + 12, reloc.crc32(bytes(d), h + 12))
    return d


class Env:
    """Built once: the sanitized runner, the packed plugins and the pool that runs it."""
    def __init__(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.work = Path(self.tmp.name)
        src = self.work / 'asan_host.c'
        src.write_text(RUNNER)
        self.exe = self.work / 'kennel_load_asan'
        subprocess.run([CC, '-O1', '-g', '-Wall', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-fno-omit-frame-pointer', '-I', str(SDK / 'kennel'), '-o', str(self.exe), str(src),
                        str(SDK / 'kennel/kn_load.c')], check=True)
        self.files, self.info, self.ex = {}, {}, {}
        for name in PLUGINS:
            d = SDK / 'plugins' / name
            if not (d / 'plugin.json').exists() or not pack.generated_ready(d):
                continue
            out = self.work / name
            out.mkdir()
            pack.pack_dir(d, out / (name + '.knl'))
            data = (out / (name + '.knl')).read_bytes()
            self.files[name] = data
            self.info[name] = reloc.parse(data, 1)
            self.ex[name] = {n: IMPORT_BASE + 0x40 * i for i, n in enumerate(self.info[name]['imports'])}
        self.pool = ThreadPoolExecutor(WORKERS)
        self.counter = itertools.count()
        self.stats = {}

    def run_c(self, item):
        data, base, ex = item
        k = next(self.counter)
        f, o = self.work / f'in{k}.knl', self.work / f'out{k}.bin'
        f.write_bytes(data)
        r = subprocess.run([str(self.exe), str(f), hex(base), str(o)] + [f'{n}={a:#x}' for n, a in ex.items()],
                           capture_output=True, text=True, env={'PATH': os.environ.get('PATH', ''), **ASAN_ENV})
        img = o.read_bytes() if r.returncode == 0 and o.exists() else None
        f.unlink()
        if o.exists():
            o.unlink()
        return r.returncode, r.stderr.strip(), img

    def batch(self, items):
        return list(self.pool.map(self.run_c, items))


ENV = None


def setUpModule():
    global ENV
    if CC and HAVE_BFIN:
        ENV = Env()


def tearDownModule():
    if ENV:
        ENV.pool.shutdown()
        ENV.tmp.cleanup()


def load_py(data, base, ex):
    """-> (image | None, message, load_size | None); only PluginError counts as a refusal."""
    try:
        p = reloc.parse(data, 1)
    except reloc.PluginError as e:
        return None, str(e), None
    if p['load_size'] > HOST_CAP:
        return 'big', 'load size over the host cap', p['load_size']
    try:
        return reloc.relocate(p, base, ex), '', p['load_size']
    except reloc.PluginError as e:
        return None, str(e), p['load_size']


# ---- mutation generators: each returns [(label, fn)], fn(bytearray) -> bytearray (CRC is sealed by the caller) ----

def header_values(h, fsize, orig):
    vals = [0, 1, 2, 3, 4, fsize - 1, fsize, fsize + 1, fsize - 4, fsize - 8, 0x7FFFFFFF, 0xFFFFFFFF, 0x80000000,
            0xFFFF, 0x10000, h + 8, h + 12, h + 140, h + 143, h + 144, h + 148,
            orig + 1, orig - 1, orig + 4, orig - 4]
    return sorted({v & 0xFFFFFFFF for v in vals})


def gen_header(P, fsize):
    h = P['header_offset']
    out = []
    for w in range(36):
        woff = 4 * w
        if woff == 12:
            continue                                           # the CRC is recomputed
        for v in header_values(h, fsize, 0):
            out.append((f'hdr[{WORD_NAMES[w]}@{woff}]={v:#x}',
                        lambda d, woff=woff, v=v: (put32(d, h + woff, v), d)[1]))
    return out


def gen_header_orig_rel(P, data):
    """Values relative to the pristine value of each word (+-1, +-4)."""
    h = P['header_offset']
    out = []
    for w in range(36):
        woff = 4 * w
        if woff == 12:
            continue
        o = u32(data, h + woff)
        for dv in (1, -1, 4, -4, 2, -2):
            out.append((f'hdr[{WORD_NAMES[w]}]={o:#x}{dv:+d}',
                        lambda d, woff=woff, v=o + dv: (put32(d, h + woff, v), d)[1]))
    return out


def gen_reloc(P, data, rnd, per_type=2, extra=4):
    """Damaged relocation entries: the first `per_type` of every type, plus `extra` random ones."""
    n = P['reloc_count']
    if not n:
        return []
    ro, isz, total, ic = P['reloc_offset'], P['image_size'], P['load_size'], P['import_count']
    by_type = {}
    for i, (off, typ, sym, add) in enumerate(P['relocs']):
        by_type.setdefault((typ, sym == reloc.SYM_LOCAL), []).append(i)
    idx = []
    for lst in by_type.values():
        idx += lst[:per_type]
    idx += [rnd.randrange(n) for _ in range(extra)]
    out = []
    for i in sorted(set(idx)):
        e = ro + 12 * i
        off0, typ0, sym0, add0 = P['relocs'][i]
        muts = []
        for v in (0, 1, 2, isz - 4, isz - 2, isz - 1, isz, isz + 2, isz + 4, off0 + 1, off0 | 1, off0 - 2,
                  0x7FFFFFFE, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF):
            muts.append((f'offset={v & 0xFFFFFFFF:#x}', lambda d, v=v: (put32(d, e, v), d)[1]))
        for v in (0, 5, 6, 255, 1, 2, 3, 4):
            muts.append((f'type={v}', lambda d, v=v: (d.__setitem__(e + 4, v), d)[1]))
        muts.append(('reserved=1', lambda d: (d.__setitem__(e + 5, 1), d)[1]))
        muts.append(('reserved=255', lambda d: (d.__setitem__(e + 5, 255), d)[1]))
        for v in (ic, ic + 1, 0xFFFE, 0xFFFF, 0, max(ic - 1, 0), 0x8000):
            muts.append((f'sym={v:#x}', lambda d, v=v: (struct.pack_into('<H', d, e + 6, v), d)[1]))
        # local PCREL24 / import PCREL24 combinations
        muts.append(('type=4,sym=local', lambda d: (d.__setitem__(e + 4, 4), struct.pack_into('<H', d, e + 6, 0xFFFF), d)[2]))
        muts.append(('type=1,sym=local,addend=0',
                     lambda d: (d.__setitem__(e + 4, 1), struct.pack_into('<H', d, e + 6, 0xFFFF), put32(d, e + 8, 0), d)[3]))
        for v in (total - 1, total, total + 1, total + 4, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFFFC, 1 << 24,
                  (1 << 24) + 2, -(1 << 24) - 4 & 0xFFFFFFFF, 0, 2, 3):
            muts.append((f'addend={v & 0xFFFFFFFF:#x}', lambda d, v=v: (put32(d, e + 8, v), d)[1]))
        for lbl, fn in muts:
            out.append((f'reloc#{i}({reloc.R_NAMES.get(typ0)},sym={sym0:#x}) {lbl}', fn))
    return out


def gen_strings(P, data):
    """Import table / string table damage."""
    h = P['header_offset']
    so, sn, io, ic = P['strings_offset'], P['strings_size'], P['import_offset'], P['import_count']
    out = []
    if ic:
        for k in sorted({0, ic - 1}):
            e = io + 4 * k
            nm = u32(data, e)
            for v in (sn - 1, sn, sn + 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFFFE, nm + 1, nm - 1, 0, 1):
                out.append((f'import#{k} name_off={v & 0xFFFFFFFF:#x}', lambda d, e=e, v=v: (put32(d, e, v), d)[1]))
            out.append((f'import#{k} name empty', lambda d, so=so, nm=nm: (d.__setitem__(so + nm, 0), d)[1]))
            out.append((f'import#{k} name non-ASCII', lambda d, so=so, nm=nm: (d.__setitem__(so + nm, 0xC3), d)[1]))
            out.append((f'import#{k} name unknown', lambda d, so=so, nm=nm: (d.__setitem__(so + nm + 1, ord('Z')), d)[1]))
            out.append((f'import#{k} non-ASCII at end of name', lambda d, so=so, nm=nm: (
                d.__setitem__(max(d.find(b'\0', so + nm) - 1, so + nm), 0x80), d)[1]))
            out.append((f'import#{k} NUL cut at name+1', lambda d, so=so, nm=nm: (d.__setitem__(so + nm + 1, 0), d)[1]))
    if sn:
        out.append(('strings last NUL -> A', lambda d: (d.__setitem__(so + sn - 1, ord('A')), d)[1]))
        out.append(('strings all non-NUL', lambda d: (d.__setitem__(slice(so, so + sn), b'A' * sn), d)[1]))
        out.append(('strings all NUL', lambda d: (d.__setitem__(slice(so, so + sn), bytes(sn)), d)[1]))
        out.append(('strings all high bytes', lambda d: (d.__setitem__(slice(so, so + sn - 1), b'\xff' * (sn - 1)), d)[1]))
    for v in (0, 1, sn - 1, sn + 1, sn + 4, 0xFFFFFFFF, 0x80000000):
        out.append((f'strings_size={v & 0xFFFFFFFF:#x}', lambda d, v=v: (put32(d, h + 124, v), d)[1]))
    for v in (0, 1, 3, so - 4, so + 1, so + 4, 0xFFFFFFFF, len(data) - 1, len(data)):
        out.append((f'strings_offset={v & 0xFFFFFFFF:#x}', lambda d, v=v: (put32(d, h + 120, v), d)[1]))
    for v in (0, 1, ic + 1, ic - 1, 0x40000000, 0x3FFFFFFF, 0x80000000, 0xFFFFFFFF, len(data) // 4,
              len(data) // 4 + 1):
        out.append((f'import_count={v & 0xFFFFFFFF:#x}', lambda d, v=v: (put32(d, h + 116, v), d)[1]))
    for v in (0, 2, io + 1, io + 2, io + 4, io - 4, len(data) - 4, len(data), len(data) + 4, 0xFFFFFFFC, 0xFFFFFFFF):
        out.append((f'import_offset={v & 0xFFFFFFFF:#x}', lambda d, v=v: (put32(d, h + 112, v), d)[1]))
    return out


def ext_blob(*entries, end=True):
    """entries: (tag, length_field | None, payload). The payload is padded to 4; length defaults to len(payload)."""
    b = b''
    for tag, n, pay in entries:
        n = len(pay) if n is None else n
        b += struct.pack('<HH', tag, n) + pay + bytes(-len(pay) % 4)
    return b + (struct.pack('<HH', 0, 0) if end else b'')


EXT_BLOBS = {
    'valid all three': ext_blob((1, None, b'a.b\0\0'), (2, None, b'me\0'), (3, None, b'1.0\0')),
    'no terminator': ext_blob((2, None, b'me\0'), end=False),
    'terminator cut to 2 bytes': ext_blob((2, None, b'me\0'), end=False) + b'\0\0',
    'claims empty': ext_blob((1, 0, b'')),
    'author empty': ext_blob((2, 0, b'')),
    'claims unterminated': ext_blob((1, None, b'abcd')),
    'author unterminated, padded': ext_blob((2, None, b'abc')),
    'version non-ASCII': ext_blob((3, None, b'a\xc3\0')),
    'author len beyond file': ext_blob((2, 100, b'abc\0')),
    'author len 0xFFFF': ext_blob((2, 0xFFFF, b'abc\0')),
    'author len 0xFFFC': ext_blob((2, 0xFFFC, b'abc\0')),
    'unknown tag 77': ext_blob((77, None, b'zz'), (2, None, b'me\0')),
    'unknown tag 0xFFFF': ext_blob((0xFFFF, None, b'zzzz')),
    'unknown tag 4 non-ASCII': ext_blob((4, None, b'\xff\xff\xff\xff')),
    'tag 99 len 1,2,3,5': ext_blob((99, None, b'a'), (99, None, b'ab'), (99, None, b'abc'), (99, None, b'abcde')),
    'claims odd len 5': ext_blob((1, None, b'abcd\0')),
    'repeated author': ext_blob((2, None, b'a\0'), (2, None, b'b\0'), (2, None, b'c\0')),
    'tag 0 with nonzero length': struct.pack('<HH', 0, 7) + b'AAAA',
    'length overshoots by one': ext_blob((99, 5, b'abcd'), end=False),
    'n so that next lands exactly at EOF': ext_blob((99, None, b'abcd'), end=False),
    'one entry, no room for a header': ext_blob((99, None, b'abcd'), end=False) + b'\1',
    'tags only 1,2,3 empty then end': ext_blob((1, 0, b''), (2, 0, b''), (3, 0, b'')),
}


def gen_ext(P, data):
    h = P['header_offset']
    fs = len(data)
    out = []

    def append(blob, mode):
        def fn(d, blob=blob, mode=mode):
            while len(d) % 4:
                d.append(0)
            pos = len(d)
            d.extend(blob)
            put32(d, h + 140, pos + mode)
            return d
        return fn
    for name, blob in EXT_BLOBS.items():
        for mode in (0, 2, 4):
            out.append((f'ext "{name}" +{mode}', append(blob, mode)))
    for v in (0, 1, 2, 3, 4, 8, h + 140, h + 143, h + 144, h + 148, fs - 4, fs - 3, fs - 1, fs, fs + 4,
              0x7FFFFFFC, 0x80000000, 0xFFFFFFFC, 0xFFFFFFFF, P['image_file_offset'], P['reloc_offset'],
              P['import_offset'], P['strings_offset'], P['image_file_offset'] + 2):
        out.append((f'ext_offset={v & 0xFFFFFFFF:#x} (existing bytes)', lambda d, v=v: (put32(d, h + 140, v), d)[1]))
    return out


def gen_svg(P, data):
    h = P['header_offset']
    ss = P['svg_size']
    out = []
    if h:
        for n in (1, 5, ss - 2, ss - 1, ss, ss + 1, h - 1, h, h + 3, h + 4, h + 7, h + 8, h + 20):
            out.append((f'truncate to {n}', lambda d, n=n: d[:n]))
        out.append(('remove the SVG NUL', lambda d: (d.__setitem__(ss - 1, ord(' ')), d)[1]))
        out.append(('remove NUL and padding', lambda d: (d.__setitem__(slice(ss - 1, h), b' ' * (h - ss + 1)), d)[1]))
        out.append(('NUL early in SVG', lambda d: (d.__setitem__(3, 0), d)[1]))
        out.append(('SVG starts with a space', lambda d: (d.__setitem__(0, ord(' ')), d)[1]))
        out.append(('SVG starts with KNPL', lambda d: (d.__setitem__(slice(0, 4), b'KNPL'), d)[1]))
        out.append(('KNPL inside the SVG', lambda d: (d.__setitem__(slice(8, 12), b'KNPL'), d)[1]))
        out.append(('KNPL at the SVG NUL', lambda d: (d.__setitem__(slice(ss - 1, ss + 3), b'KNPL'), d)[1]))
        out.append(('header magic broken', lambda d: (d.__setitem__(h, 0), d)[1]))
        out.append(('padding byte after NUL set', lambda d: (d.__setitem__(ss, 1) if ss < h else None, d)[1]))
        out.append(('insert 1 byte before the header', lambda d: d[:h] + b'\0' + d[h:]))
        out.append(('insert 4 bytes before the header', lambda d: d[:h] + bytes(4) + d[h:]))
        out.append(('insert 2 bytes in the SVG', lambda d: d[:5] + b'  ' + d[5:]))
        out.append(('insert 4 bytes in the SVG', lambda d: d[:5] + b'    ' + d[5:]))
        out.append(('delete 1 byte in the SVG', lambda d: d[:5] + d[6:]))
        out.append(('header at odd offset (shifted by 1, magic only)',
                    lambda d: d[:h] + b'\0' + d[h:]))
        out.append(('only SVG + NUL, no header', lambda d: d[:ss]))
        out.append(('SVG text of 3 bytes + header copy', lambda d: b'<a\0' + d[h:]))
        out.append(('SVG text of 4 bytes + header copy', lambda d: b'<ab\0' + d[h:]))
        out.append(('SVG text of 7 bytes + header copy', lambda d: b'<abcdef\0' + d[h:]))
        for v in (0, 1, ss - 1, ss + 1, h - 1, h, h + 1, h + 4, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF):
            out.append((f'svg_size={v & 0xFFFFFFFF:#x}', lambda d, v=v: (put32(d, h + 132, v), d)[1]))
    else:
        for v in (1, 4, 8, 144, 0x7FFFFFFF, 0xFFFFFFFF):
            out.append((f'no SVG, svg_size={v:#x}', lambda d, v=v: (put32(d, h + 132, v), d)[1]))
        out.append(('magic prefix inside a plain file: "<" in front', lambda d: b'<\0\0\0' + d))
        out.append(('prefix "<" NUL pad, size field 0', lambda d: b'<\0\0\0' + d))
        out.append(('prefix 8 bytes of "<" with NUL', lambda d: b'<ab\0\0\0\0\0' + d))
    for n in (0, 1, 3, 4, 5, 6, 7, 8, 11, 12, 15, 16, 20, 143, 144, 147, 148):
        out.append((f'truncate to {n} (cut file)', lambda d, n=n: d[:n]))
    return out


def seal_for(P, pristine_len):
    return lambda d: seal(d, pristine_len)


class FuzzCase:
    __slots__ = ('label', 'data', 'ex', 'base')

    def __init__(self, label, data, ex, base=BASES[0]):
        self.label, self.data, self.ex, self.base = label, bytes(data), ex, base


class FuzzBase(unittest.TestCase):
    CLASS = ''

    def check_cases(self, cases, plugin=''):
        """Run every case in the sanitized C runner (in parallel), then compare with reloc.py case by case."""
        res = ENV.batch([(c.data, c.base, c.ex) for c in cases])
        ENV.stats[self.CLASS] = ENV.stats.get(self.CLASS, 0) + len(cases)
        ENV.stats[self.CLASS + ' (accepted)'] = ENV.stats.get(self.CLASS + ' (accepted)', 0) + sum(r[0] == 0 for r in res)
        for c, (rc, err, img) in zip(cases, res):
            with self.subTest(case=f'{plugin}: {c.label}'):
                self.assertIn(rc, (0, 1), f'C runner crashed/sanitizer: rc={rc} {err[:600]!r}')
                self.assertFalse('runtime error' in err or 'Sanitizer' in err, err[:600])
                if rc == 1:
                    self.assertTrue(err.startswith('error:'), err[:300])
                try:
                    py, perr, lsz = load_py(c.data, c.base, c.ex)
                except Exception as e:                       # reloc.py must only ever raise PluginError
                    self.fail(f'reloc.py raised {type(e).__name__}: {e} (C: rc={rc} {err!r})')
                if py == 'big':                              # beyond the runner's buffer
                    if err != 'error: host cap':
                        self.fail(f'reloc.py accepts load size {lsz:#x} (parse OK); C: rc={rc} {err!r}')
                    continue
                self.assertEqual(rc == 1, py is None, f'C: rc={rc} {err!r} / reloc.py: {perr!r}')
                if rc == 0:
                    self.assertEqual(img, py)


def make(label, data, fn, P, ex, sealed=True, base=BASES[0], fix_size=True):
    d = bytearray(fn(bytearray(data)))
    if sealed:
        seal(d, len(data) if fix_size else None)
    return FuzzCase(label, d, ex, base)


@unittest.skipUnless(CC and HAVE_BFIN, 'needs a host C compiler and the Blackfin toolchain')
class HeaderFieldFuzz(FuzzBase):
    CLASS = '1 header fields'

    def test_boundary_values(self):
        for name in ('supersaw', 'clouds', 'tape'):
            if name not in ENV.files:
                continue
            data, P, ex = ENV.files[name], ENV.info[name], ENV.ex[name]
            gens = gen_header(P, len(data)) + gen_header_orig_rel(P, data)
            cases = [make(lbl, data, fn, P, ex, fix_size=False) for lbl, fn in gens]
            self.check_cases(cases, name)


@unittest.skipUnless(CC and HAVE_BFIN, 'needs a host C compiler and the Blackfin toolchain')
class RelocTableFuzz(FuzzBase):
    CLASS = '2 relocation entries'

    def test_entries(self):
        rnd = random.Random(2)
        for name in ('clouds', 'supersaw', 'tape'):
            if name not in ENV.files:
                continue
            data, P, ex = ENV.files[name], ENV.info[name], ENV.ex[name]
            cases = [make(lbl, data, fn, P, ex, fix_size=False) for lbl, fn in gen_reloc(P, data, rnd)]
            self.check_cases(cases, name)

    def test_import_reach(self):
        """Import addresses far from the plugin: PCREL24 reach checks on both sides, several addends."""
        data, P = ENV.files['clouds'], ENV.info['clouds']
        names = P['imports']
        for far in (0x00000000, 0x01000000, 0x01E00100, 0x03F00000, 0x04000000, 0x7FFFFFF0, 0xFFFFFFF0, 0x02980001):
            ex = {n: (far + 0x40 * i) & 0xFFFFFFFF for i, n in enumerate(names)}
            for base in BASES:
                self.check_cases([FuzzCase(f'imports at {far:#x}', data, ex, base)], 'clouds')
        for base in (0x02980002, 0x02980001, 0, 0xFFFFFFFC, 0xFFFFF000):
            self.check_cases([FuzzCase(f'base {base:#x}', data, ENV.ex['clouds'], base)], 'clouds')


@unittest.skipUnless(CC and HAVE_BFIN, 'needs a host C compiler and the Blackfin toolchain')
class StringTableFuzz(FuzzBase):
    CLASS = '3 import/string tables'

    def test_strings(self):
        for name in ('clouds', 'tape', 'chord'):
            if name not in ENV.files:
                continue
            data, P, ex = ENV.files[name], ENV.info[name], ENV.ex[name]
            if not P['import_count']:
                continue
            cases = [make(lbl, data, fn, P, ex, fix_size=False) for lbl, fn in gen_strings(P, data)]
            self.check_cases(cases, name)

    def test_empty_name_exported(self):
        """An empty import name that the core does export resolves in both loaders."""
        data, P = ENV.files['clouds'], ENV.info['clouds']
        so = P['strings_offset']
        nm = u32(data, P['import_offset'])
        d = bytearray(data)
        d[so + nm] = 0
        seal(d)
        ex = dict(ENV.ex['clouds'])
        ex[''] = IMPORT_BASE
        self.check_cases([FuzzCase('first import name empty, "" exported', d, ex)], 'clouds')


@unittest.skipUnless(CC and HAVE_BFIN, 'needs a host C compiler and the Blackfin toolchain')
class ExtensionTableFuzz(FuzzBase):
    CLASS = '4 extension table'

    def test_ext(self):
        for name in ('supersaw', 'clouds', 'chord'):
            if name not in ENV.files:
                continue
            data, P, ex = ENV.files[name], ENV.info[name], ENV.ex[name]
            cases = [make(lbl, data, fn, P, ex, fix_size=True) for lbl, fn in gen_ext(P, data)]
            self.check_cases(cases, name)


@unittest.skipUnless(CC and HAVE_BFIN, 'needs a host C compiler and the Blackfin toolchain')
class SvgPrefixFuzz(FuzzBase):
    CLASS = '5 SVG prefix / short files'

    def test_svg(self):
        withsvg = [n for n in ENV.files if ENV.info[n]['header_offset']]
        plain = [n for n in ENV.files if not ENV.info[n]['header_offset']]
        assert withsvg, 'no packed plugin has an SVG prefix'
        for name in withsvg[:3] + plain[:1]:
            data, P, ex = ENV.files[name], ENV.info[name], ENV.ex[name]
            cases = []
            for lbl, fn in gen_svg(P, data):
                cases.append(make(lbl + ' [sealed]', data, fn, P, ex, fix_size=True))
                cases.append(make(lbl + ' [CRC only]', data, fn, P, ex, fix_size=False))
            self.check_cases(cases, name)

    def test_tiny_files(self):
        hdr = b'KNPL' + struct.pack('<HH', 1, 144)
        blobs = {'empty': b'', '1 byte': b'K', '3 bytes': b'KNP', 'magic only': b'KNPL',
                 'magic + 1': b'KNPL\1', 'magic + version': b'KNPL\1\0', 'magic + 7': hdr[:7],
                 'magic + version + hsize': hdr, 'hdr + 4': hdr + bytes(4), 'zeros 144': bytes(144),
                 'magic then zeros 144': b'KNPL' + bytes(140), 'hdr then zeros 144': hdr + bytes(140),
                 '< only': b'<', '<\\0': b'<\0', '<\\0 x4': b'<\0\0\0', '< NUL KNPL': b'<\0\0\0KNPL',
                 '< NUL KNPL +4': b'<\0\0\0KNPL\1\0', '< NUL KNPL +8': b'<\0\0\0' + hdr,
                 '< NUL KNPL header only': b'<\0\0\0' + hdr + bytes(140), 'unterminated SVG': b'<svg>' * 4}
        ex = {}
        self.check_cases([FuzzCase(k, v, ex) for k, v in blobs.items()], 'tiny')


@unittest.skipUnless(CC and HAVE_BFIN, 'needs a host C compiler and the Blackfin toolchain')
class RandomComboFuzz(FuzzBase):
    CLASS = '6 random combos'

    def test_combos(self):
        rnd = random.Random(6)
        names = [n for n in PLUGINS if n in ENV.files]
        pools = {}
        for n in names:
            data, P = ENV.files[n], ENV.info[n]
            pools[n] = {
                'header': gen_header(P, len(data)),
                'reloc': gen_reloc(P, data, random.Random(n), per_type=3, extra=8),
                'string': gen_strings(P, data),
                'ext': gen_ext(P, data),
                'svg': gen_svg(P, data),
            }

        def flip(d):
            for _ in range(rnd.randint(1, 4)):
                d[rnd.randrange(len(d))] = rnd.randrange(256)
            return d

        def flip_head(d):
            for _ in range(rnd.randint(1, 3)):
                d[rnd.randrange(min(len(d), 400))] = rnd.randrange(256)
            return d

        def trunc(d):
            return d[:rnd.randrange(len(d))]
        cases = []
        for i in range(300):
            n = names[i % len(names)]
            data, P, ex = ENV.files[n], ENV.info[n], ENV.ex[n]
            kinds = [rnd.choice(('header', 'reloc', 'string', 'ext', 'svg', 'flip', 'flip_head', 'trunc'))
                     for _ in range(rnd.randint(2, 4))]
            d = bytearray(data)
            lbls = []
            for k in kinds:
                if k in pools[n] and pools[n][k]:
                    lbl, fn = rnd.choice(pools[n][k])
                    lbls.append(f'{k}:{lbl}')
                    try:
                        d = bytearray(fn(d))
                    except (IndexError, struct.error):       # an earlier op cut the file short of this field
                        lbls[-1] += '(skipped)'
                else:
                    lbls.append(k)
                    d = {'flip': flip, 'flip_head': flip_head, 'trunc': trunc}.get(k, lambda x: x)(d)
                if not d:
                    d = bytearray(b'\0')
            r = rnd.random()
            if r < 0.9:
                seal(d, len(data) if r < 0.7 else None)
            base = BASES[i % 2] if i % 10 else 0x02980002
            cases.append(FuzzCase(f'#{i} ' + ' + '.join(lbls), d, ex, base))
        by_plugin = {}
        for i, c in enumerate(cases):
            by_plugin.setdefault(names[i % len(names)], []).append(c)
        for n, cs in by_plugin.items():
            self.check_cases(cs, n)


@unittest.skipUnless(CC and HAVE_BFIN, 'needs a host C compiler and the Blackfin toolchain')
class ZZSummary(unittest.TestCase):
    def test_counts(self):
        print('\nfuzz cases per class: ' + ', '.join(f'{k}: {v}' for k, v in sorted(ENV.stats.items())),
              file=sys.stderr)


if __name__ == '__main__':
    unittest.main()

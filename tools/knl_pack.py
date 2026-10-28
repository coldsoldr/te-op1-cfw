#!/usr/bin/env python3
"""Pack one of our plugin folders (ports/knl/<id>/) into a kennel .knl file.

    python3 tools/knl_pack.py ports/knl/crush -o build/knl/crush.knl [--keep build/knl/crush-objects]

Run inside WSL (the Blackfin toolchain lives there: $BFIN_BIN or ~/op1-toolchain/bfin/bin).
Python 3.8 is enough.

Why a driver of our own: hundekraft's packer (third_party/kennel/tools/kennel/pack.py) compiles C
with its own fixed flags and refuses sources outside the plugin folder. Our modules are C++ with
assembly kernels under ports/fx, ports/clouds, ports/rings. So this script compiles the sources
its way (g++ -std=c++17 -fno-exceptions -fno-rtti ..., gcc for .c, gcc for .S), links them with
`bfin-elf-ld -r -T merge.ld` plus libgcc exactly as pack.py does, and hands the relocatable object
to pack.pack(), unmodified, so the file format stays theirs. Then it re-reads the file with their
reference loader (reloc.py) at two load addresses and checks every import against the core's
export table (synth_sdk/kennel/kn_exports.c): an import the core lacks makes the loader skip the
file at boot, better to know here.

plugin.json is hundekraft's format (PLUGIN_GUIDE.md) plus:
  "sources"       may point outside the folder (relative paths, e.g. "../../fx/src/fold/fold.cc")
  "include_dirs"  relative to the folder, may point outside (e.g. "../.." for the ports tree)
  "cxxflags"      extra flags for C++ sources, "cflags" for C, "asflags" for .S
The screen's "svg" may also be a relative path outside the folder.

Refuses a plugin with a static initialiser (a global object with a constructor: _GLOBAL__sub_I in
the object): the loader never runs .init_array. Zero-initialise globals and construct in init().
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
KENNEL = REPO / 'third_party' / 'kennel'
KTOOLS = KENNEL / 'tools' / 'kennel'
SDK = KENNEL / 'synth_sdk'
EXPORTS_C = SDK / 'kennel' / 'kn_exports.c'
sys.path.insert(0, str(KTOOLS))
import pack as kpack  # noqa: E402  (hundekraft's packer, unmodified)
import reloc as kreloc  # noqa: E402

COMMON = ['-O2', '-mcpu=bf524', '-ffreestanding', '-fno-builtin', '-nostdlib', '-fno-common',
          '-fno-tree-loop-distribute-patterns', '-ffunction-sections', '-fdata-sections', '-Wall']
CFLAGS = COMMON + ['-std=gnu11']
CXXFLAGS = COMMON + ['-std=c++17', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
                     '-fno-use-cxa-atexit', '-Wno-unused-function']
ASFLAGS = ['-mcpu=bf524']


class Error(Exception):
    pass


def toolchain(arg):
    bin_dir = Path(arg or os.environ.get('BFIN_BIN') or Path.home() / 'op1-toolchain' / 'bfin' / 'bin')
    tools = {t: bin_dir / ('bfin-elf-' + t) for t in ('gcc', 'g++', 'ld', 'nm')}
    missing = [str(p) for p in tools.values() if not p.exists()]
    if missing:
        raise Error('toolchain not found: ' + ', '.join(missing) + ' (set --bfin-bin or $BFIN_BIN)')
    return {k: str(v) for k, v in tools.items()}


def run(cmd):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
    if r.returncode:
        raise Error(' '.join(cmd) + '\n' + r.stdout)
    return r.stdout


def exports():
    """The names kn_exports.c lists (with the C underscore), as the loader resolves them."""
    return set(re.findall(r'\{\s*"(_[A-Za-z0-9_]+)"', EXPORTS_C.read_text()))


def compile_all(plugin_dir, meta, work, tc):
    includes = ['-I' + str(SDK), '-I' + str(plugin_dir)]
    for rel in meta.get('include_dirs', []):
        includes.append('-I' + str((plugin_dir / rel).resolve()))
    objs = []
    for i, src in enumerate(meta['sources']):
        path = (plugin_dir / src).resolve()
        if not path.exists():
            raise Error('source not found: ' + str(path))
        o = work / ('%02d_%s.o' % (i, path.stem))
        ext = path.suffix.lower()
        if ext in ('.cc', '.cpp', '.cxx'):
            cmd = [tc['g++'], *CXXFLAGS, *meta.get('cxxflags', [])]
        elif ext == '.c':
            cmd = [tc['gcc'], *CFLAGS, *meta.get('cflags', [])]
        elif ext == '.s':
            cmd = [tc['gcc'], *ASFLAGS, *meta.get('asflags', [])]
        else:
            raise Error('unknown source type: ' + src)
        out = run(cmd + includes + ['-c', str(path), '-o', str(o)])
        if out.strip():
            print(out.rstrip())
        objs.append(str(o))
    return objs


def link(objs, out, tc, roots):
    """ld -r as pack.py does, plus --gc-sections rooted at the descriptors: a
    plugin pulls whole source files in (a core's tables, the Clouds halfband
    filters, the firmware's memcpy), and keeps only what its callbacks reach."""
    libgcc = run([tc['gcc'], '-mcpu=bf524', '-print-libgcc-file-name']).strip()
    keep = []
    for r in roots:
        keep += ['-u', '_' + r]
    # --force-group-allocation: C++ inline variables and out-of-line inline
    # functions sit in COMDAT groups, which a relocatable link keeps as
    # separate sections; this places them in the output sections as a final
    # link would, so merge.ld's four sections are all there is.
    run([tc['ld'], '-r', '--gc-sections', '--force-group-allocation', *keep, '-T', str(KTOOLS / 'merge.ld'),
         *objs, libgcc, '-o', str(out)])
    syms = run([tc['nm'], str(out)])
    bad = sorted({l.split()[-1] for l in syms.splitlines() if '_GLOBAL__sub_I' in l or '__cxa_guard' in l})
    if bad:
        raise Error('static initialiser or guarded static in the plugin: ' + ', '.join(bad)
                    + ' (the loader never runs .init_array; construct in init())')
    return out


def self_check(data, meta):
    """Their reference loader: parse, then relocate at two bases (the two must differ only where
    relocations are). Imports resolve to dummy core addresses."""
    plugin = kreloc.parse(data, kpack.api_version())
    fake = {name: 0x01E00000 + 16 * i for i, name in enumerate(plugin['imports'])}
    a = kreloc.relocate(plugin, 0x02980000, fake)
    b = kreloc.relocate(plugin, 0x02A00000, fake)
    if len(a) != len(b):
        raise Error('self-check: image sizes differ between bases')
    return plugin


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('plugin_dir')
    ap.add_argument('-o', '--output', required=True)
    ap.add_argument('--bfin-bin', help='directory of bfin-elf-gcc (default $BFIN_BIN or ~/op1-toolchain/bfin/bin)')
    ap.add_argument('--keep', help='keep the objects and the ld -r output here')
    a = ap.parse_args()
    try:
        plugin_dir = Path(a.plugin_dir).resolve()
        meta = json.loads((plugin_dir / 'plugin.json').read_text())
        kpack.check_id(meta, plugin_dir.name)
        out = Path(a.output)
        if out.name != meta['name'] + '.knl':
            raise Error('output must be named %s.knl' % meta['name'])
        tc = toolchain(a.bfin_bin)
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(a.keep or tmp)
            work.mkdir(parents=True, exist_ok=True)
            objs = compile_all(plugin_dir, meta, work, tc)
            roots = [meta['symbol']] + ([meta['screen']['symbol']] if meta.get('screen') else [])
            ro = link(objs, work / (meta['name'] + '.ro'), tc, roots)
            data, info = kpack.pack(meta, ro.read_bytes(), plugin_dir)
        known = exports()
        unknown = [n for n in info['imports'] if n not in known]
        if unknown:
            raise Error('imports the 3.0.3 core does not export (the loader would skip the file): '
                        + ', '.join(unknown))
        plugin = self_check(data, meta)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(data)
    except (Error, kpack.PackError, kreloc.PluginError) as e:
        sys.exit('error: %s' % e)
    lay = info['layout']
    print('%s: %d bytes; text %d rodata %d data %d bss %d (load %d); %d load-time relocations; '
          'imports: %s; svg %d bytes; self-check ok'
          % (out, len(data), lay['.text'][1], lay['.rodata'][1], lay['.data'][1], lay['.bss'][1],
             plugin['load_size'], info['relocs'], ', '.join(info['imports']) or 'none',
             max(0, plugin.get('svg_size', 0) - 1)))


if __name__ == '__main__':
    main()

"""Host test for the kennel plugin format: pack.py + reloc.py against a plain bfin-elf-ld link.

    python3 -m unittest discover -s tools/kennel -p 'test_*.py'

Each plugin in synth_sdk/plugins is packed, relocated by reloc.py to two load addresses and compared byte for byte
with `bfin-elf-ld` linking the same objects (the `ld -r` output: plugin objects + libgcc) at those addresses, with the core imports given fixed test
addresses. Needs the Blackfin toolchain (OP1-Emulator toolchain/ or KENNEL_TOOLCHAIN); skipped without it.
Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE).
"""
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pack
import reloc

PLUGINS = pack.SDK / 'plugins'
# 0x0299F000 puts the image across a 64 KB boundary (hi halves differ inside one plugin); 0x02A40000 moves every
# hi half. Both are in the planned SDRAM block and within CALL range of the test import addresses.
BASES = (0x0299F000, 0x02A40000)
IMPORT_BASE = 0x01E00100
BASE_LD = """SECTIONS {{
  . = {base:#x};
  .text : {{ *(.text .text.*) }}
  .rodata ALIGN(4) : {{ *(.rodata .rodata.*) }}
  .data ALIGN(4) : {{ *(.data .data.*) }}
  .bss ALIGN(4) : {{ *(.bss .bss.*) }}
  /DISCARD/ : {{ *(.comment) *(.note .note.*) *(.eh_frame) }}
}}
"""

try:
    GCC, LD = pack.find_toolchain()
except pack.PackError:
    GCC = LD = None


def tool(name):
    return str(Path(LD).with_name('bfin-elf-' + name))


@unittest.skipUnless(LD, 'Blackfin toolchain not found')
class PluginFormatTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.work = Path(cls.tmp.name)
        cls.packed = {}
        for d in sorted(p.parent for p in PLUGINS.glob('*/plugin.json')):
            if not pack.generated_ready(d):                # build-time data without a firmware tree: skip
                continue
            out = cls.work / d.name
            out.mkdir()
            objs = pack.compile_plugin(d, out)
            ro = pack.link_relocatable(objs, out / 'plugin.ro')
            meta = json.loads((d / 'plugin.json').read_text())
            data, info = pack.pack(meta, Path(ro).read_bytes(), d)
            cls.packed[d.name] = (meta, [str(ro)], data, info)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def exports(self, plugin):
        return {n: IMPORT_BASE + 0x40 * i for i, n in enumerate(plugin['imports'])}

    def ld_link(self, name, objs, base, exports):
        """Plain final link of the plugin's `ld -r` object at base -> (text..data bytes, symbol table).

        The reference links the merged object rather than the .o files: a final link of the .o files would also
        merge duplicate strings across .rodata.str* sections, which `ld -r` leaves alone, so only the layout
        would differ (palme: 4 bytes). libgcc is already in the merged object."""
        d = self.work / name
        script = d / f'base_{base:x}.ld'
        script.write_text(BASE_LD.format(base=base))
        elf, binf = d / f'ref_{base:x}.elf', d / f'ref_{base:x}.bin'
        defsyms = [f'--defsym={n}={a:#x}' for n, a in exports.items()]
        pack.run([LD, '-T', str(script), *objs, *defsyms, '-o', str(elf)])
        pack.run([tool('objcopy'), '-O', 'binary', '-j', '.text', '-j', '.rodata', '-j', '.data', str(elf), str(binf)])
        syms = {}
        for line in pack.run([tool('nm'), str(elf)]).splitlines():
            parts = line.split()
            if len(parts) == 3:
                syms[parts[2]] = int(parts[0], 16)
        return binf.read_bytes(), syms

    def test_matches_ld_at_two_bases(self):
        for name, (meta, objs, data, info) in self.packed.items():
            p = reloc.parse(data, api_version=pack.api_version())
            exports = self.exports(p)
            for base in BASES:
                with self.subTest(plugin=name, base=hex(base)):
                    img = reloc.relocate(p, base, exports)
                    ref, syms = self.ld_link(name, objs, base, exports)
                    self.assertEqual(len(img), p['load_size'])
                    self.assertLessEqual(p['image_size'] - len(ref), 3)      # objcopy stops at the last byte
                    self.assertEqual(img[:len(ref)], ref)
                    self.assertEqual(base + p['entry_offset'], syms['_' + meta['symbol']])
                    if meta.get('screen'):
                        self.assertEqual(base + p['screen_offset'], syms['_' + meta['screen']['symbol']])
                    self.assertFalse(any(img[p['image_size']:]), 'bss not zero')

    def test_svg_long_decimals_refused(self):
        """R246's SVG parser panics at boot on Python float noise such as 1.6500000000000001."""
        meta, objs, _, _ = self.packed['supersaw']
        d = self.work / 'svgnoise'
        d.mkdir(exist_ok=True)
        svg = (pack.SDK / 'plugins/supersaw' / meta['screen']['svg']).read_text()
        (d / meta['screen']['svg']).write_text(svg.replace('>', ' stroke-width="1.6500000000000001">', 1))
        with self.assertRaisesRegex(pack.PackError, 'more than 4 decimals'):
            pack.pack(meta, Path(objs[0]).read_bytes(), d)

    def test_file_name_rule(self):
        """plugin file names follow the SysEx update's path rule (devupd du_path_ok): "in a room.knl" cannot be sent"""
        for ok in ('in_a_room.knl', 'my_saw-2.knl', 'x.op1p'):
            self.assertTrue(pack.file_name_ok(ok), ok)
        for bad in ('in a room.knl', 'a/b.knl', '..knl', 'sitting.txt', 'x' * 40 + '.knl', 'ø.knl'):
            self.assertFalse(pack.file_name_ok(bad), bad)
        with self.assertRaisesRegex(pack.PackError, 'plugin file name'):
            pack.pack_dir(pack.SDK / 'plugins/in_a_room', self.work / 'in a room.knl')

    def test_names(self):
        """docs/plugin-format.md "Names": id = folder = plugin.json name, the saved name derived from it, one frozen
        exception list"""
        self.assertEqual(pack.SAVED_NAMES, {'drumplus': 'drum+'})          # frozen: grows only for names already saved
        for pj in sorted(pack.SDK.glob('plugins/*/plugin.json')):
            meta = json.loads(pj.read_text())
            with self.subTest(plugin=pj.parent.name):
                saved = pack.check_id(meta, pj.parent.name)
                self.assertEqual(saved, pack.SAVED_NAMES.get(meta['name'], meta['name'].replace('_', ' ')))
                self.assertEqual('saved_name' in meta, meta['name'] in pack.SAVED_NAMES)
        self.assertEqual(pack.check_id({'name': 'in_a_room'}), 'in a room')
        self.assertEqual(reloc.parse(self.packed['in_a_room'][2])['type_name'], 'in a room')
        self.assertEqual(reloc.parse(self.packed['drumplus'][2])['type_name'], 'drum+')
        for bad in ({'name': 'in a room'}, {'name': 'Supersaw'}, {'name': 'x' * 16}, {'name': ''}, {'name': 'a+b'},
                    {'name': 'supersaw', 'saved_name': 'super saw'}, {'name': 'drumplus', 'saved_name': 'drum plus'}):
            with self.subTest(bad=bad), self.assertRaises(pack.PackError):
                pack.check_id(bad)
        with self.assertRaisesRegex(pack.PackError, 'folder'):
            pack.check_id({'name': 'supersaw'}, 'saw')

    def test_svg_turn_back_refused(self):
        """R246's stroke join draws a polyline point where the line turns back on itself as a spike to the screen edge."""
        meta, objs, _, _ = self.packed['supersaw']
        d = self.work / 'svgturn'
        d.mkdir(exist_ok=True)
        svg = (pack.SDK / 'plugins/supersaw' / meta['screen']['svg']).read_text()
        (d / meta['screen']['svg']).write_text(svg.replace('</svg>', '<polyline points="10,10 50,10 12,10.2"/></svg>'))
        with self.assertRaisesRegex(pack.PackError, 'turns back'):
            pack.pack(meta, Path(objs[0]).read_bytes(), d)

    def test_header(self):
        meta, _, data, _ = self.packed['supersaw']
        p = reloc.parse(data)
        self.assertEqual((p['type_name'], p['kind'], p['legacy_order']), ('supersaw', reloc.KIND_SYNTH, 1))
        self.assertEqual(p['defaults'], meta['defaults'])
        self.assertEqual(p['svg'], (PLUGINS / 'supersaw' / meta['screen']['svg']).read_bytes())
        self.assertTrue(data.startswith(p['svg'] + b'\0'))   # the file is its own screen SVG
        self.assertEqual(len(data) % 4, 0)
        clouds = reloc.parse(self.packed['clouds'][2])
        self.assertEqual(clouds['page2_defaults'], self.packed['clouds'][0]['page2_defaults'])
        self.assertEqual(clouds['kind'], reloc.KIND_FX)
        gran = reloc.parse(self.packed['granular'][2])
        self.assertTrue(gran['flags'] & reloc.FLAG_USES_SAMPLE)
        self.assertIn('_read_fx_param_safe', reloc.parse(self.packed['tape'][2])['imports'])
        self.assertEqual(reloc.parse(self.packed['supersaw'][2])['imports'], [])   # libgcc is linked in
        chord = reloc.parse(self.packed['chord'][2])            # extension table: claims, author, version
        self.assertEqual(chord['claims'], ['page:t4'])
        self.assertEqual(chord['ext'][reloc.EXT_AUTHOR], b'Hundekraft\0')
        self.assertEqual(reloc.parse(self.packed['supersaw'][2])['ext'], {})

    def test_relocation_types(self):
        """The ELF types the plugins use, all handled; PCREL24 to an import stays in the table."""
        seen = set()
        for _, _, data, info in self.packed.values():
            seen |= set(info['elf_reloc_types'])
        self.assertEqual(seen, set(pack.ELF_TO_FILE))
        tape = reloc.parse(self.packed['tape'][2])
        self.assertIn(reloc.R_PCREL24, {t for _, t, s, _ in tape['relocs'] if s != reloc.SYM_LOCAL})

    def test_call_out_of_range(self):
        """A CALL into the core that a load address puts out of reach: the loader must skip the plugin."""
        p = reloc.parse(self.packed['tape'][2])
        exports = self.exports(p)
        far = IMPORT_BASE + (1 << 24)
        with self.assertRaisesRegex(reloc.PluginError, 'out of reach'):
            reloc.relocate(p, far, exports)
        with self.assertRaises(pack.PackError):           # ld refuses the same link
            self.ld_link('tape', self.packed['tape'][1], far, exports)

    def test_broken_files_are_refused(self):
        data = self.packed['tape'][2]
        bad = bytearray(data)
        bad[200] ^= 1
        h = reloc.parse(data)['header_offset']           # the screen SVG comes first
        self.assertGreater(h, 0)
        cases = {'magic': data[:h] + b'XXXX' + data[h + 4:], 'CRC': bytes(bad), 'size': data[:-4],
                 'format version': data[:h + 4] + struct.pack('<H', 2) + data[h + 6:]}
        for what, d in cases.items():
            with self.subTest(what), self.assertRaisesRegex(reloc.PluginError, what):
                reloc.parse(d)
        with self.assertRaisesRegex(reloc.PluginError, 'plugin API'):
            reloc.parse(data, api_version=pack.api_version() + 1)
        with self.assertRaisesRegex(reloc.PluginError, 'not exported'):
            reloc.relocate(reloc.parse(data), BASES[0], {})
        # a header that passes the CRC but points a relocation outside the image
        p = reloc.parse(data)
        forged = bytearray(data)
        struct.pack_into('<I', forged, p['reloc_offset'], p['image_size'])
        struct.pack_into('<I', forged, h + 12, reloc.crc32(bytes(forged), h + 12))
        with self.assertRaisesRegex(reloc.PluginError, 'outside the image'):
            reloc.parse(bytes(forged))

    def test_unsupported_relocation_fails(self):
        """A reloc type the format does not know (here a PCREL10 conditional branch across objects) is an error."""
        d = self.work / 'bad'
        d.mkdir(exist_ok=True)
        (d / 'a.s').write_text('.text\n.global _f\n_f:\nIF CC JUMP _g;\nRTS;\n')
        (d / 'b.s').write_text('.text\n.global _g\n_g:\nRTS;\n')
        objs = []
        for s in ('a', 'b'):
            pack.run([tool('as'), '-mcpu=bf524', str(d / f'{s}.s'), '-o', str(d / f'{s}.o')])
            objs.append(str(d / f'{s}.o'))
        # no ld -r here (it may resolve the branch); the unresolved reloc sits in a.o
        with self.assertRaisesRegex(pack.PackError, 'not supported'):
            pack.pack({'name': 'bad', 'kind': 'synth', 'symbol': 'f'}, Path(objs[0]).read_bytes())


if __name__ == '__main__':
    unittest.main()

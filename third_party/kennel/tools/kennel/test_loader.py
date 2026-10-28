"""Host test for the C loader (synth_sdk/kennel/kn_load.c) against the reference loader reloc.py.

    python3 -m unittest discover -s tools/kennel -p 'test_*.py'

Every plugin in synth_sdk/plugins is packed, then loaded by both at two addresses: the images must be equal. Then
mutated files (bytes changed with the CRC fixed up, so the structural checks run, and truncated files) must be
accepted or refused by both alike, with equal images when accepted. Needs a host C compiler and the Blackfin
toolchain (for packing); skipped without them.
Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE).
"""
import json
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pack
import reloc

SDK = pack.SDK
BASES = (0x02980000, 0x0299F000)
IMPORT_BASE = 0x01E00100
HOST_CAP = 1 << 20                         # image buffer of harness/kennel_load_host.c
CC = shutil.which('cc')
try:
    pack.find_toolchain()
    HAVE_BFIN = True
except pack.PackError:
    HAVE_BFIN = False


@unittest.skipUnless(CC and HAVE_BFIN, 'needs a host C compiler and the Blackfin toolchain')
class LoaderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.work = Path(cls.tmp.name)
        cls.exe = cls.work / 'kennel_load_host'
        subprocess.run([CC, '-O2', '-Wall', '-Werror', '-o', str(cls.exe), str(SDK / 'harness/kennel_load_host.c'),
                        str(SDK / 'kennel/kn_load.c')], check=True)
        cls.files = {}
        for d in sorted(p.parent for p in (SDK / 'plugins').glob('*/plugin.json')):
            if not pack.generated_ready(d):                # build-time data without a firmware tree: skip
                continue
            out = cls.work / d.name
            out.mkdir()
            pack.pack_dir(d, out / (d.name + '.knl'))
            cls.files[d.name] = (out / (d.name + '.knl')).read_bytes()

    def load_c(self, data, base, exports, api=1):
        f = self.work / 'in.knl'
        o = self.work / 'out.bin'
        f.write_bytes(data)
        if o.exists():
            o.unlink()
        r = subprocess.run([str(self.exe), str(f), hex(base), str(o)] + [f'{n}={a:#x}' for n, a in exports.items()],
                           capture_output=True, text=True, env={'KN_API': str(api)})
        return (o.read_bytes() if r.returncode == 0 else None), r.stderr.strip()

    def load_py(self, data, base, exports, api=1):
        try:
            return reloc.relocate(reloc.parse(data, api), base, exports), ''
        except reloc.PluginError as e:
            return None, str(e)

    def exports(self, data):
        try:
            names = reloc.parse(data)['imports']
        except reloc.PluginError:
            names = []
        return {n: IMPORT_BASE + 0x40 * i for i, n in enumerate(names)}

    def test_same_image_as_reloc(self):
        for name, data in self.files.items():
            ex = self.exports(data)
            for base in BASES:
                with self.subTest(plugin=name, base=hex(base)):
                    c, cerr = self.load_c(data, base, ex)
                    py, _ = self.load_py(data, base, ex)
                    self.assertIsNotNone(c, cerr)
                    self.assertEqual(c, py)

    def test_missing_import_and_api(self):
        data = next(d for d in self.files.values() if reloc.parse(d)['imports'])
        self.assertIsNone(self.load_c(data, BASES[0], {})[0])
        self.assertIsNone(self.load_c(self.files['supersaw'], BASES[0], {}, api=2)[0])
        self.assertIsNotNone(self.load_c(self.files['supersaw'], BASES[0], {})[0])

    def test_mutations_agree(self):
        rnd = random.Random(1)
        for name in ('supersaw', 'tape', 'clouds', 'chord'):
            base = self.files[name]
            ex = self.exports(base)
            for i in range(150):
                d = bytearray(base)
                if i % 10 == 9:
                    d = d[:rnd.randrange(len(d))]
                else:
                    for _ in range(rnd.randint(1, 3)):
                        pos = rnd.randrange(min(len(d), 400) if i % 2 else len(d))
                        d[pos] = rnd.randrange(256)
                    if len(d) >= 16:
                        struct.pack_into('<I', d, 12, reloc.crc32(bytes(d)))
                with self.subTest(plugin=name, case=i):
                    c, cerr = self.load_c(bytes(d), BASES[0], ex)
                    py, perr = self.load_py(bytes(d), BASES[0], ex)
                    if py is not None and len(py) > HOST_CAP:     # the host runner's buffer, not a format rule
                        continue
                    self.assertEqual(c is None, py is None, f'C: {cerr!r} / reloc.py: {perr!r}')
                    if c is not None:
                        self.assertEqual(c, py)


if __name__ == '__main__':
    unittest.main()

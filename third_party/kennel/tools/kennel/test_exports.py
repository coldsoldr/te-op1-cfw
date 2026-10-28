"""Every plugin's imports are core exports: pack each synth_sdk/plugins folder and check its import names against
synth_sdk/kennel/kn_exports.c (the loader skips a file with an import the core does not export).

    python3 -m unittest discover -s tools/kennel -p 'test_*.py'

Needs the Blackfin toolchain (OP1-Emulator toolchain/ or KENNEL_TOOLCHAIN); skipped without it.
Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE).
"""
import re
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pack
import reloc

try:
    pack.find_toolchain()
    HAVE = True
except pack.PackError:
    HAVE = False


@unittest.skipUnless(HAVE, 'Blackfin toolchain not found')
class ExportsTest(unittest.TestCase):
    def test_plugin_imports_are_exported(self):
        exports = set(re.findall(r'\{\s*"(_\w+)"', (pack.SDK / 'kennel' / 'kn_exports.c').read_text()))
        with tempfile.TemporaryDirectory() as tmp:
            for d in sorted(p.parent for p in (pack.SDK / 'plugins').glob('*/plugin.json')):
                with self.subTest(plugin=d.name):
                    out = Path(tmp) / (d.name + '.knl')
                    pack.pack_dir(d, out)
                    imports = reloc.parse(out.read_bytes())['imports']
                    self.assertEqual(sorted(set(imports) - exports), [], d.name)


if __name__ == '__main__':
    unittest.main()

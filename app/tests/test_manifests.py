"""The feature manifests and the resolver.

    .venv/Scripts/python -m unittest app.tests.test_manifests
"""

import os
import unittest

from app.backend import manifests, paths


class Manifests(unittest.TestCase):
    def test_bits_match_the_header(self):
        """app/manifests/image.json's bit layout must be ports/fx/op1/modules.h's."""
        header = open(os.path.join(paths.REPO, 'ports', 'fx', 'op1', 'modules.h'), encoding='utf-8').read()
        bits = manifests.image()['mask_bits']
        for fid, bit in bits.items():
            name = 'MODULE_' + fid.upper().replace('-', '_')
            self.assertIn('{} = {},'.format(name, bit), header, name)
        self.assertIn('kNumModules = {}'.format(len(bits)), header)
        modules = [f for f in manifests.features() if f['kind'] == 'module']
        self.assertEqual(sorted(f['mask_bit'] for f in modules), sorted(bits.values()))

    def test_screen_sources_exist(self):
        for f in manifests.features():
            for name, src in f['screens'].items():
                self.assertTrue(os.path.isfile(os.path.join(paths.REPO, *src.split('/'))), src)
                self.assertFalse(os.path.exists(os.path.join(paths.STOCK_DIR, 'content', 'display', name)) if os.path.isdir(paths.STOCK_DIR) else False,
                                 name + ' clashes with a stock screen')

    def test_image_file(self):
        img = manifests.image()
        elf = os.path.join(paths.REPO, *img['elf'].split('/'))
        self.assertTrue(os.path.isfile(elf))

    def test_resolve_all(self):
        ids = [f['id'] for f in manifests.features() if f['id'] not in ('gfx-cwo-dog', 'gfx-cwo-cat')]
        r = manifests.resolve(ids)
        self.assertTrue(r['ok'], r['errors'])
        self.assertEqual(r['mask'], manifests.image()['mask_all'])
        self.assertEqual(r['preset_rows'], 11)
        self.assertEqual(r['budget']['non_sampler'], 171)
        self.assertTrue(any('Clouds' in w for w in r['warnings']))
        self.assertEqual(r['options'][:3], ['iter', 'presets-iter', 'gfx-iter-lab'])

    def test_resolve_none(self):
        r = manifests.resolve([])
        self.assertTrue(r['ok'])
        self.assertEqual(r['mask'], 0)
        self.assertEqual(r['options'], [])
        self.assertEqual(len(r['screens']), 13)

    def test_dependencies_and_conflicts(self):
        r = manifests.resolve(['presets-iter'])
        self.assertFalse(r['ok'])
        self.assertTrue(any('needs iter' in e for e in r['errors']))
        r = manifests.resolve(['gfx-cwo-dog', 'gfx-cwo-cat'])
        self.assertFalse(r['ok'])
        r = manifests.resolve(['iter', 'presets-iter'])
        self.assertTrue(r['ok'])
        self.assertTrue(any('filter' in w for w in r['warnings']))
        r = manifests.resolve(['iter', 'presets-iter', 'filter'])
        self.assertFalse(any('filter' in w for w in r['warnings']))
        self.assertFalse(manifests.resolve(['nosuch'])['ok'])

    def test_names_follow_modules(self):
        img = manifests.image()
        r = manifests.resolve([])
        self.assertEqual(r['query'], img['query_stock'])
        self.assertFalse(any(n['active'] for n in r['names']))
        r = manifests.resolve(['hall', 'drive'])
        shown = {n['stock']: n['active'] for n in r['names']}
        self.assertEqual(shown, {'phase': False, 'spring': True, 'cwo': False, 'punch': True})
        self.assertEqual(r['query'], "SELECT id, CASE type WHEN 'spring' THEN 'reverb' WHEN 'punch' THEN 'dirt' ELSE type END FROM %s")
        r = manifests.resolve(['rings', 'plate', 'ensemble', 'crush'])
        self.assertTrue(all(n['active'] for n in r['names']))
        # The longest form must fit the image's buffer (the firmware's full string).
        full = ("SELECT id, CASE type WHEN 'phase' THEN 'multi' WHEN 'spring' THEN 'reverb' WHEN 'cwo' THEN 'modulation' "
                "WHEN 'punch' THEN 'dirt' ELSE type END FROM %s")
        self.assertEqual(r['query'], full)
        for name in ('reverb', 'dirt', 'modulation', 'multi'):
            self.assertIn(name, r['query'])

    def test_query_symbol_in_image(self):
        import inject_app   # tools/, on the path via app.backend.paths
        img = manifests.image()
        symbols = inject_app.read_elf_symbols(os.path.join(paths.REPO, *img['elf'].split('/')))
        _, size = inject_app.symbol_address(symbols, img['query_symbol'])
        for ids in ([], ['rings', 'plate', 'ensemble', 'crush']):
            self.assertLess(len(manifests.resolve(ids)['query']), size)
        _, mask_size = inject_app.symbol_address(symbols, img['mask_symbol'])
        self.assertEqual(mask_size, 4)

    def test_budget_check(self):
        b = manifests.resolve([])['budget']
        b['non_sampler'] = 177
        self.assertTrue(manifests.check_budget(b, []))


if __name__ == '__main__':
    unittest.main()

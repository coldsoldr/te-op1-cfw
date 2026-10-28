"""Preset formats: every factory row round-trips through the canonical
formatter byte for byte and passes the validator; every .aif preset we have
round-trips through the AIFF reader/writer byte for byte; the readable
description names things; the drum unit holds.

    .venv/Scripts/python -m unittest app.tests.test_presets
"""

import glob
import os
import unittest

from app.backend import aiff, factorydb, knobs, patch, paths

STOCK_DB = os.path.join(paths.STOCK_DIR, 'content', 'op1_factory.db')
ITER_PRESETS = os.path.join(paths.REPACKER, 'op1repacker', 'assets', 'presets', 'iter')


@unittest.skipUnless(os.path.isfile(STOCK_DB), 'needs the stock firmware unpacked in op1 firmware/op1_246')
class FactoryRows(unittest.TestCase):
    def setUp(self):
        self.db = factorydb.FactoryDb(STOCK_DB)

    def tearDown(self):
        self.db.close()

    def test_counts(self):
        c = self.db.counts()
        self.assertEqual((c['synth'], c['sampler'], c['drum']), (169, 9, 24))

    def test_every_row_round_trips_and_validates(self):
        for table in factorydb.TABLES:
            for row in self.db.rows(table):
                self.assertTrue(patch.same_text(patch.canonical(row['patch']), row['raw']), '{} {}'.format(table, row['id']))
                errs, _ = patch.validate(row['patch'])
                self.assertEqual(errs, [], '{} {}: {}'.format(table, row['id'], errs))
                d = patch.describe(row['patch'])
                self.assertIn(d['kind'], ('synth', 'drum'))
                self.assertEqual(d['custom'], [], 'stock rows need no custom firmware')

    def test_validate_all_is_clean(self):
        self.assertEqual(self.db.validate_all(), [])

    def test_budget(self):
        b = factorydb.budget_from_counts(self.db.counts())
        self.assertEqual(b['non_sampler'], 160)
        self.assertEqual(b['total'], 193)


class Validator(unittest.TestCase):
    def good(self):
        return {'adsr': [64, 12352, 10239, 3008, 2048, 64, 4000, 4000], 'fx_active': True,
                'fx_params': [500, 15360, 15660, 10880, 0, 0, 0, 0], 'fx_type': 'nitro',
                'knobs': [17408, 24320, 1535, 10240, 0, 0, 0, 0], 'lfo_active': True,
                'lfo_params': [4688, -17976, 2000, 4880, 0, 0, 0, 0], 'lfo_type': 'element',
                'name': 'auto rptr', 'octave': 1, 'synth_version': 1, 'type': 'digital'}

    def test_rules(self):
        p = self.good()
        self.assertEqual(patch.validate(p)[0], [])
        p['name'] = 'fourteen chars'
        self.assertTrue(any('longer' in w for w in patch.validate(p)[1]))   # a warning: the OP-1 loads such names
        p['name'] = 'Caps'
        self.assertTrue(any('a-z' in e for e in patch.validate(p)[0]))
        p = self.good(); p['octave'] = 5
        self.assertTrue(any('octave' in e for e in patch.validate(p)[0]))
        p = self.good(); p['knobs'] = [0] * 7
        self.assertTrue(any('knobs' in e for e in patch.validate(p)[0]))
        p = self.good(); p['fx_type'] = 'nope'
        self.assertTrue(any('effect' in e for e in patch.validate(p)[0]))
        p = self.good(); p['fx_params'][4] = -1
        self.assertTrue(any('negative' in e for e in patch.validate(p)[0]))

    def test_marker_and_rings(self):
        p = self.good()
        p['fx_type'] = 'spring'
        p['fx_params'] = [8000, 8000, 8000, 8000, 0x6100, 0, 0, 0]
        d = patch.describe(p)
        self.assertEqual((d['effect']['category'], d['effect']['engine'], d['effect']['module']), ('reverb', 'plate', 'plate'))
        self.assertIn('effect reverb -> plate', d['custom'])
        p['fx_params'][4] = 0x6300
        self.assertEqual(patch.describe(p)['effect']['engine'], 'clouds')
        p['fx_params'][4] = 8000
        self.assertEqual(patch.describe(p)['effect']['engine'], 'spring')
        p['type'] = 'phase'
        patch.rings_settings(p, engine='rings', model=3, polyphony=2)
        d = patch.describe(p)
        self.assertEqual(d['engine']['multi']['model_name'], 'FM voice')
        self.assertEqual(d['engine']['multi']['polyphony_name'], '2')
        self.assertIn('multi -> Rings', d['custom'])
        self.assertEqual(d['engine']['knobs'][1]['name'], 'brightness')

    def test_drum_unit(self):
        # neoprene: end[23] = 1028893014 for a 253549-sample file (within a few samples)
        self.assertAlmostEqual(1028893014 / knobs.DRUM_UNIT, 253549, delta=4)


@unittest.skipUnless(os.path.isdir(ITER_PRESETS), 'needs third_party/op1repacker')
class AiffFiles(unittest.TestCase):
    def test_round_trip(self):
        files = sorted(glob.glob(os.path.join(ITER_PRESETS, '*.aif')))
        self.assertTrue(files)
        for f in files:
            data = open(f, 'rb').read()
            p = aiff.Preset(data)
            self.assertEqual(p.patch['type'], 'iter')
            # Rewriting the same patch reproduces the file (its APPL was canonical).
            again = p.with_patch(p.patch)
            self.assertEqual(again, data, os.path.basename(f))
            self.assertEqual(aiff.Preset(again).patch, p.patch)
            audio = p.audio()
            self.assertIsNotNone(audio)
            pcm, channels, rate, frames = audio
            self.assertIn(rate, (22050, 44100))
            self.assertEqual(len(pcm), frames * channels * 2)
            self.assertTrue(aiff.peaks(pcm, channels, 50))

    def test_edit_and_new(self):
        f = sorted(glob.glob(os.path.join(ITER_PRESETS, '*.aif')))[0]
        p = aiff.Preset.load(f)
        q = dict(p.patch)
        q['name'] = 'renamed'
        out = aiff.Preset(p.with_patch(q))
        self.assertEqual(out.patch['name'], 'renamed')
        self.assertEqual(out.audio()[0], p.audio()[0])
        fresh = aiff.Preset(aiff.new_preset(q, p.audio()[0], 1, 44100))
        self.assertEqual(fresh.patch, q)
        self.assertEqual(fresh.audio()[0], p.audio()[0])


if __name__ == '__main__':
    unittest.main()

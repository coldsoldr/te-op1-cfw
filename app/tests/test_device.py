"""The Librarian's writes on a disk laid out like the OP-1's, with undo:
a preset's name is its file name (outside the user slots), so a rename
rewrites the name inside and an edit of the name renames the file.

    .venv/Scripts/python -m unittest app.tests.test_device
"""

import glob
import os
import shutil
import tempfile
import unittest

from app.backend import aiff, device, factorydb, paths

ITER_PRESETS = os.path.join(paths.REPACKER, 'op1repacker', 'assets', 'presets', 'iter')


def _name_inside(path):
    return aiff.Preset.load(path).patch['name']


@unittest.skipUnless(os.path.isdir(ITER_PRESETS), 'needs op1repacker\'s iter presets')
class LibrarianWrites(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='op1lib-')
        self.root = os.path.join(self.tmp, 'disk')
        for d in ('synth/user', 'synth/pads', 'drum/user', 'tape', 'album'):
            os.makedirs(os.path.join(self.root, *d.split('/')))
        src = sorted(glob.glob(os.path.join(ITER_PRESETS, '*.aif')))[0]
        self.stem = os.path.splitext(os.path.basename(src))[0]
        # The folder preset carries its file name inside, as presets on a
        # disk do; the slot and the op1.fun copy keep the file's own name
        # inside ('20180511_1642', the snapshot it came from).
        preset = aiff.Preset.load(src)
        self.op1fun_name = preset.patch['name']
        self.folder_file = os.path.join(self.root, 'synth', 'pads', self.stem + '.aif')
        with open(self.folder_file, 'wb') as f:
            f.write(preset.with_patch(dict(preset.patch, name=self.stem)))
        self.slot_file = os.path.join(self.root, 'synth', 'user', '1.aif')
        shutil.copy2(src, self.slot_file)
        self.stale_file = os.path.join(self.root, 'synth', 'pads', 'wonky.aif')
        shutil.copy2(src, self.stale_file)
        self.lib = device.Librarian(self.root, data_dir=os.path.join(self.tmp, 'undo'))
        self.lib.backed_up = True

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_needs_backup(self):
        self.lib.backed_up = False
        with self.assertRaises(device.DeviceError):
            self.lib.rename('synth/pads/' + self.stem + '.aif', 'other')

    def test_rename_rewrites_the_name_inside(self):
        entry = self.lib.rename('synth/pads/' + self.stem + '.aif', 'glass pad')
        self.assertEqual(entry['to'], 'synth/pads/glass pad.aif')
        self.assertIn('name inside', entry['text'])
        renamed = os.path.join(self.root, 'synth', 'pads', 'glass pad.aif')
        self.assertFalse(os.path.exists(self.folder_file))
        self.assertEqual(_name_inside(renamed), 'glass pad')
        self.lib.undo()
        self.assertFalse(os.path.exists(renamed))
        self.assertEqual(_name_inside(self.folder_file), self.stem)

    def test_rename_rejects_bad_names(self):
        for bad in ('Glass', 'a/b', '', 'pad!'):
            with self.assertRaises(device.DeviceError):
                self.lib.rename('synth/pads/' + self.stem + '.aif', bad)
        self.assertTrue(os.path.isfile(self.folder_file))

    def test_slot_rename_keeps_the_name_inside(self):
        before = _name_inside(self.slot_file)
        entry = self.lib.rename('synth/user/1.aif', '7')
        self.assertEqual(entry['to'], 'synth/user/7.aif')
        self.assertNotIn('stash', entry)
        self.assertEqual(_name_inside(os.path.join(self.root, 'synth', 'user', '7.aif')), before)

    def test_edit_of_the_name_renames_the_file(self):
        p = dict(aiff.Preset.load(self.folder_file).patch)
        p['name'] = 'bells'
        entry = self.lib.save_patch('synth/pads/' + self.stem + '.aif', p)
        self.assertEqual(entry['to'], 'synth/pads/bells.aif')
        moved = os.path.join(self.root, 'synth', 'pads', 'bells.aif')
        self.assertFalse(os.path.exists(self.folder_file))
        self.assertEqual(_name_inside(moved), 'bells')
        self.lib.undo()
        self.assertFalse(os.path.exists(moved))
        self.assertEqual(_name_inside(self.folder_file), self.stem)

    def test_edit_of_the_name_refuses_a_taken_name(self):
        shutil.copy2(self.folder_file, os.path.join(self.root, 'synth', 'pads', 'bells.aif'))
        p = dict(aiff.Preset.load(self.folder_file).patch)
        p['name'] = 'bells'
        with self.assertRaises(device.DeviceError):
            self.lib.save_patch('synth/pads/' + self.stem + '.aif', p)
        self.assertEqual(_name_inside(self.folder_file), self.stem)

    def test_slot_edit_keeps_the_file_name(self):
        p = dict(aiff.Preset.load(self.slot_file).patch)
        p['name'] = 'mine'
        entry = self.lib.save_patch('synth/user/1.aif', p)
        self.assertNotIn('to', entry)
        self.assertTrue(os.path.isfile(self.slot_file))
        self.assertEqual(_name_inside(self.slot_file), 'mine')
        self.lib.undo()
        self.assertEqual(_name_inside(self.slot_file), self.op1fun_name)

    def test_stale_name_inside_is_left_alone(self):
        # An edit that doesn't touch the name never renames the file, and a
        # rename brings the name inside along.
        p = dict(aiff.Preset.load(self.stale_file).patch)
        p['octave'] = 2
        entry = self.lib.save_patch('synth/pads/wonky.aif', p)
        self.assertNotIn('to', entry)
        self.assertTrue(os.path.isfile(self.stale_file))
        self.assertEqual(_name_inside(self.stale_file), self.op1fun_name)
        entry = self.lib.rename('synth/pads/wonky.aif', 'wonkier')
        self.assertEqual(_name_inside(os.path.join(self.root, 'synth', 'pads', 'wonkier.aif')), 'wonkier')
        self.lib.undo()
        self.assertEqual(_name_inside(self.stale_file), self.op1fun_name)

    def test_edit_without_a_name_change_keeps_the_file(self):
        p = dict(aiff.Preset.load(self.folder_file).patch)
        p['octave'] = 1
        entry = self.lib.save_patch('synth/pads/' + self.stem + '.aif', p)
        self.assertNotIn('to', entry)
        self.assertEqual(aiff.Preset.load(self.folder_file).patch['octave'], 1)


@unittest.skipUnless(os.path.isfile(factorydb.stock_db_path()), 'needs the stock unpack (op1 firmware/op1_246/)')
class FactoryToDisk(unittest.TestCase):
    """'Copy the factory presets to the OP-1': every stock row becomes a
    user preset in a folder named after its engine, with its audio, in one
    undoable change; files already there are kept."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='op1fac-')
        self.root = os.path.join(self.tmp, 'disk')
        for d in ('synth/user', 'drum/user', 'tape', 'album'):
            os.makedirs(os.path.join(self.root, *d.split('/')))
        self.lib = device.Librarian(self.root, data_dir=os.path.join(self.tmp, 'undo'))
        self.lib.backed_up = True

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_every_row_lands_in_its_engine_folder(self):
        items = factorydb.preset_files(with_data=False)
        self.assertEqual([i for i in items if 'problem' in i], [])
        entry = self.lib.copy_factory()
        self.assertEqual(len(entry['paths']), len(items))
        self.assertEqual(entry['skipped'], [])
        self.assertEqual(entry['problems'], [])
        # The stock folders: the ten engines and the sampler, drum and dbox.
        self.assertEqual(sorted(os.listdir(os.path.join(self.root, 'synth'))),
                         sorted(['user', 'digital', 'dna', 'dr wave', 'dsynth', 'cluster', 'voltage', 'phase', 'pulse', 'string', 'fm', 'sampler']))
        self.assertEqual(sorted(os.listdir(os.path.join(self.root, 'drum'))), ['dbox', 'drum', 'user'])
        # A sampler row carries its factory audio; a synth row a silent clip
        # the length of the device's own; the settings are the row's.
        sampler = aiff.Preset.load(os.path.join(self.root, 'synth', 'sampler', 'designadrum.aif'))
        self.assertEqual(sampler.patch['type'], 'sampler')
        self.assertEqual(sampler.comm['frames'], os.path.getsize(factorydb.factory_audio_path('synth', 'designadrum')) // 2)
        self.assertEqual(sampler.comm['rate'], 44100)
        kit = aiff.Preset.load(os.path.join(self.root, 'drum', 'drum', 'neoprene.aif'))
        self.assertEqual(kit.patch['name'], 'neoprene')
        self.assertEqual(kit.comm['frames'], os.path.getsize(factorydb.factory_audio_path('drum', 'neoprene')) // 2)
        synth = aiff.Preset.load(os.path.join(self.root, 'synth', 'digital', 'auto rptr.aif'))
        self.assertEqual(synth.comm['frames'], factorydb.SILENT_FRAMES)
        self.assertEqual([c[0] for c in synth.chunks], [b'FVER', b'COMM', b'APPL', b'SSND'])
        self.assertEqual(device.file_entry(self.root, synth and os.path.join(self.root, 'synth', 'digital', 'auto rptr.aif'))['problems'], [])
        # Every file reads back as a valid preset.
        scan = device.scan(self.root)
        self.assertEqual(scan['counts']['synth'] + scan['counts']['drum'], len(items))
        self.assertEqual([e['rel'] for e in scan['synth'] + scan['drum'] if e.get('problems') or e.get('error')], [])

    def test_existing_files_are_kept_and_undo_removes_the_rest(self):
        mine = os.path.join(self.root, 'synth', 'digital', 'auto rptr.aif')
        os.makedirs(os.path.dirname(mine))
        with open(mine, 'wb') as f:
            f.write(b'mine')
        entry = self.lib.copy_factory()
        self.assertEqual(entry['skipped'], ['synth/digital/auto rptr.aif'])
        self.assertNotIn('synth/digital', entry['dirs'])
        with open(mine, 'rb') as f:
            self.assertEqual(f.read(), b'mine')
        self.lib.undo()
        self.assertTrue(os.path.isfile(mine))
        self.assertEqual(sorted(os.listdir(os.path.join(self.root, 'synth'))), ['digital', 'user'])
        self.assertEqual(sorted(os.listdir(os.path.join(self.root, 'drum'))), ['user'])
        self.assertEqual(self.lib.log, [])

    def test_a_second_copy_changes_nothing(self):
        self.lib.copy_factory()
        n = len(self.lib.log)
        entry = self.lib.copy_factory()
        self.assertTrue(entry.get('nothing'))
        self.assertEqual(entry['paths'], [])
        self.assertEqual(len(self.lib.log), n)

    def test_needs_backup(self):
        self.lib.backed_up = False
        with self.assertRaises(device.DeviceError):
            self.lib.copy_factory()


if __name__ == '__main__':
    unittest.main()

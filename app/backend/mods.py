"""op1repacker's mods applied to an unpacked work dir, through its own
modules (op1_db, op1_patches, op1_gfx), in a fixed order, each reported.
Also the factory preset folder rename phase -> multi (display only), which
a build does when Rings is on, because the list then shows the engine as
"multi"."""

import json
import os
import shutil
import sqlite3

from . import patch, paths  # noqa: F401
from op1repacker import op1_db, op1_gfx, op1_patches  # noqa: E402

REPACKER_PKG = os.path.join(paths.REPACKER, 'op1repacker')
ASSETS = os.path.join(REPACKER_PKG, 'assets')
ITER_PRESETS = os.path.join(ASSETS, 'presets', 'iter')
MOD_ROW_ID = 9000   # ids the factory tab shows for rows a mod inserts at build time


def iter_preset_rows():
    """The rows the presets-iter option inserts at build time (as the
    factory tab shows them: the ids are for display, the build assigns its
    own)."""
    if not os.path.isdir(ITER_PRESETS):
        return []
    patches = op1_patches.load_patch_folder(ITER_PRESETS)
    patches.sort(key=lambda p: p['name'])
    return [{'id': MOD_ROW_ID + i, 'folder': 'iter', 'patch': json.loads(patch.canonical(p)),
             'feature': 'presets-iter'} for i, p in enumerate(patches)]
DB_OPTIONS = ('iter', 'presets-iter', 'filter', 'subtle-fx')
GFX_OPTIONS = ('gfx-iter-lab', 'gfx-tape-invert', 'gfx-cwo-moose', 'gfx-cwo-dog', 'gfx-cwo-cat')
ORDER = ('iter', 'filter', 'presets-iter', 'subtle-fx', 'gfx-iter-lab', 'gfx-tape-invert',
         'gfx-cwo-moose', 'gfx-cwo-dog', 'gfx-cwo-cat')


class ModError(Exception):
    pass


def factory_db_path(work_dir):
    return os.path.join(work_dir, 'content', 'op1_factory.db')


def apply(work_dir, options, log=print):
    """Apply the options (any order) to the work dir. Returns the list of
    (option, note) applied."""
    unknown = [o for o in options if o not in ORDER]
    if unknown:
        raise ModError('unknown op1repacker option(s): {}'.format(', '.join(unknown)))
    applied = []
    db_wanted = [o for o in ORDER if o in options and o in DB_OPTIONS]
    if db_wanted:
        db = op1_db.OP1DB()
        db.open(factory_db_path(work_dir))
        for opt in db_wanted:
            if opt == 'iter':
                ok = db.enable_iter()
                note = 'iter row added to synth_types' if ok else 'iter already in synth_types'
            elif opt == 'filter':
                ok = db.enable_filter()
                note = 'filter row added to fx_types' if ok else 'filter already in fx_types'
            elif opt == 'presets-iter':
                if db.synth_preset_folder_exists('iter'):
                    note = 'iter presets already present'
                else:
                    patches = op1_patches.load_patch_folder(os.path.join(ASSETS, 'presets', 'iter'))
                    patches.sort(key=lambda p: p['name'])
                    for p in patches:
                        # In the factory rows' format (op1repacker itself writes
                        # json.dumps's spaced, unsorted text; the firmware reads both).
                        db.insert_synth_preset(patch.canonical(p), 'iter')
                    note = '{} iter presets added'.format(len(patches))
            elif opt == 'subtle-fx':
                db.enable_subtle_fx_defaults()
                note = 'effect defaults made subtle'
            log('  mod {}: {}'.format(opt, note))
            applied.append((opt, note))
        db.commit()
        db.conn.close()
        db.conn = None
    for opt in ORDER:
        if opt not in options or opt not in GFX_OPTIONS:
            continue
        if opt == 'gfx-iter-lab':
            shutil.copyfile(os.path.join(ASSETS, 'display', 'iter-lab.svg'),
                            os.path.join(work_dir, 'content', 'display', 'iter.svg'))
            note = 'iter.svg replaced by the lab drawing'
        else:
            patch_file = os.path.join(ASSETS, 'display', opt[4:] + '.patch.json')
            if not op1_gfx.patch_image_file(work_dir, patch_file):
                raise ModError('gfx patch {} failed (already applied?)'.format(opt))
            note = 'screen patched'
        log('  mod {}: {}'.format(opt, note))
        applied.append((opt, note))
    return applied


def rename_folder(db_path, old, new, expect, kind):
    """tools/rename_preset_folder.py's check and rename, in process."""
    db = sqlite3.connect(db_path, isolation_level=None)
    try:
        rows = db.execute('SELECT id, patch FROM synth_presets WHERE folder = ?', (old,)).fetchall()
        if len(rows) != expect:
            raise ModError('{} rows have folder {!r}, expected {}'.format(len(rows), old, expect))
        for row_id, patch in rows:
            if json.loads(patch).get('type') != kind:
                raise ModError('row {} is not a {} preset'.format(row_id, kind))
        if db.execute('SELECT count(*) FROM synth_presets WHERE folder = ?', (new,)).fetchone()[0]:
            raise ModError('folder {!r} already exists'.format(new))
        db.execute('BEGIN')
        changed = db.execute('UPDATE synth_presets SET folder = ? WHERE folder = ?', (new, old)).rowcount
        if changed != expect:
            db.execute('ROLLBACK')
            raise ModError('updated {} rows, expected {}'.format(changed, expect))
        db.execute('COMMIT')
        check = db.execute('PRAGMA integrity_check').fetchone()[0]
    finally:
        db.close()
    if check != 'ok':
        raise ModError('integrity_check: ' + check)
    return changed


def check_db_format(db_path):
    """The firmware's SQLite reads rollback-journal files only: header bytes
    18/19 = 1, and no journal left behind."""
    with open(db_path, 'rb') as f:
        header = f.read(20)
    problems = []
    if header[:16] != b'SQLite format 3\x00':
        problems.append('op1_factory.db is not an SQLite 3 file')
    elif header[18] != 1 or header[19] != 1:
        problems.append('op1_factory.db left rollback-journal format (WAL?)')
    for suffix in ('-journal', '-wal', '-shm'):
        if os.path.exists(db_path + suffix):
            problems.append('a journal file was left next to op1_factory.db')
    db = sqlite3.connect(db_path)
    try:
        check = db.execute('PRAGMA integrity_check').fetchone()[0]
    finally:
        db.close()
    if check != 'ok':
        problems.append('integrity_check: ' + check)
    return problems

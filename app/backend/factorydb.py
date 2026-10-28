"""content/op1_factory.db: the factory presets the firmware reads in place at
boot (docs/librarian-handover.md Part 3). Rows can be read, replaced in
place (keeping id and position), appended, or deleted, always within the
budget and the validator's rules; the file stays in the rollback-journal
format the firmware's SQLite 3.6.20 reads.

A preset set (app/presets/NAME.json) is a list of changes to apply to a
stock database, so a build is reproducible:
  {"name": "...", "changes": [
     {"table": "synth_presets", "op": "replace", "id": 97, "folder": "multi", "patch": {...}},
     {"table": "synth_presets", "op": "append", "folder": "multi", "patch": {...}},
     {"table": "drum_presets", "op": "delete", "id": 24}]}"""

import json
import os
import sqlite3

from . import manifests, patch, paths

TABLES = ('synth_presets', 'drum_presets')
TYPE_TABLES = ('synth_types', 'fx_types', 'lfo_types', 'drum_types', 'seq_types')


class FactoryError(Exception):
    pass


def budget_from_counts(counts):
    limits = manifests.image()['limits']
    return {
        'non_sampler': counts['synth'] - counts['sampler'],
        'non_sampler_limit': limits['non_sampler'],
        'sampler': counts['sampler'],
        'sampler_limit': limits['sampler'],
        'total': counts['synth'] + counts['drum'],
        'total_limit': limits['total'],
    }


class FactoryDb:
    def __init__(self, path):
        if not os.path.isfile(path):
            raise FactoryError('{} not found'.format(path))
        self.path = path
        self.db = sqlite3.connect(path, isolation_level=None)

    def close(self):
        self.db.close()

    # ---- reading ----
    def rows(self, table):
        if table not in TABLES:
            raise FactoryError('no table {}'.format(table))
        out = []
        for row_id, text, folder in self.db.execute('SELECT id, patch, folder FROM {} ORDER BY id'.format(table)):
            try:
                p = json.loads(text)
            except ValueError:
                p = None
            out.append({'id': row_id, 'folder': folder, 'patch': p, 'raw': text, 'table': table})
        return out

    def types(self):
        return {t: self.db.execute('SELECT * FROM {} ORDER BY id'.format(t)).fetchall() for t in TYPE_TABLES}

    def counts(self):
        synth = self.db.execute('SELECT count(*) FROM synth_presets').fetchone()[0]
        sampler = self.db.execute("SELECT count(*) FROM synth_presets WHERE folder = 'sampler'").fetchone()[0]
        drum = self.db.execute('SELECT count(*) FROM drum_presets').fetchone()[0]
        return {'synth': synth, 'sampler': sampler, 'drum': drum}

    def folders(self, table):
        return [{'folder': f, 'count': n} for f, n in self.db.execute(
            'SELECT folder, count(*) FROM {} GROUP BY folder ORDER BY min(id)'.format(table))]

    def validate_all(self, strict_format=False):
        """Problems with any row: bad JSON, the validator's rules, a name
        used twice in a folder, a type the database doesn't list. The factory
        text format is checked only with strict_format (the firmware reads any
        JSON: beta4 booted with op1repacker's spaced rows)."""
        problems = []
        types = self.types()
        synth_types = {r[1] for r in types['synth_types']} | {'sampler'}
        fx_types = {r[1] for r in types['fx_types']}
        lfo_types = {r[1] for r in types['lfo_types']}
        for table in TABLES:
            seen = {}
            for row in self.rows(table):
                where = '{} id {}'.format(table, row['id'])
                p = row['patch']
                if p is None:
                    problems.append(where + ': not valid JSON')
                    continue
                if strict_format and not patch.same_text(row['raw'], patch.canonical(p)):
                    problems.append(where + ': not in the factory format (compact, sorted keys, newline)')
                errs, _ = patch.validate(p)
                problems += [where + ': ' + e for e in errs]
                if table == 'synth_presets' and p.get('type') not in synth_types:
                    problems.append(where + ': engine {!r} is not in synth_types'.format(p.get('type')))
                if p.get('fx_type') not in fx_types:
                    problems.append(where + ': effect {!r} is not in fx_types'.format(p.get('fx_type')))
                if p.get('lfo_type') not in lfo_types:
                    problems.append(where + ': LFO {!r} is not in lfo_types'.format(p.get('lfo_type')))
                key = (row['folder'], p.get('name'))
                if key in seen:
                    problems.append(where + ': name {!r} is already used in folder {!r} (id {})'.format(
                        p.get('name'), row['folder'], seen[key]))
                seen[key] = row['id']
        return problems

    # ---- writing ----
    def _check_patch(self, table, p, folder):
        if not isinstance(p, dict):
            raise FactoryError('patch must be an object')
        k = patch.kind(p)
        if table == 'synth_presets' and k != 'synth':
            raise FactoryError('synth_presets takes synth presets')
        if table == 'drum_presets' and k != 'drum':
            raise FactoryError('drum_presets takes drum presets')
        errs, _ = patch.validate(p)
        if errs:
            raise FactoryError('; '.join(errs))
        if not folder or not isinstance(folder, str):
            raise FactoryError('a folder is required')

    def replace(self, table, row_id, p, folder=None):
        row = self.db.execute('SELECT folder FROM {} WHERE id = ?'.format(table), (row_id,)).fetchone()
        if row is None:
            raise FactoryError('{} has no row {}'.format(table, row_id))
        folder = folder or row[0]
        self._check_patch(table, p, folder)
        self.db.execute('UPDATE {} SET patch = ?, folder = ? WHERE id = ?'.format(table),
                        (patch.canonical(p), folder, row_id))
        return row_id

    def append(self, table, p, folder):
        self._check_patch(table, p, folder)
        counts = self.counts()
        if table == 'synth_presets':
            counts['synth'] += 1
            if folder == 'sampler' or p.get('type') == 'sampler':
                counts['sampler'] += 1
        else:
            counts['drum'] += 1
        errors = manifests.check_budget(budget_from_counts(counts), [])
        if errors:
            raise FactoryError('; '.join(errors))
        cur = self.db.execute('INSERT INTO {} (patch, folder) VALUES (?, ?)'.format(table),
                              (patch.canonical(p), folder))
        return cur.lastrowid

    def delete(self, table, row_id):
        n = self.db.execute('DELETE FROM {} WHERE id = ?'.format(table), (row_id,)).rowcount
        if n != 1:
            raise FactoryError('{} has no row {}'.format(table, row_id))

    def apply_set(self, preset_set):
        """Apply a preset set (a path, a name under app/presets, or a dict)."""
        if isinstance(preset_set, str):
            path = preset_set
            if not os.path.isfile(path):
                path = os.path.join(paths.PRESET_SETS, preset_set + ('' if preset_set.endswith('.json') else '.json'))
            if not os.path.isfile(path):
                raise FactoryError('preset set {} not found'.format(preset_set))
            with open(path, encoding='utf-8') as f:
                preset_set = json.load(f)
        changes = preset_set.get('changes', [])
        self.db.execute('BEGIN')
        try:
            for i, c in enumerate(changes):
                table, op = c.get('table'), c.get('op')
                if table not in TABLES:
                    raise FactoryError('change {}: bad table {!r}'.format(i, table))
                if op == 'replace':
                    self.replace(table, c['id'], c['patch'], c.get('folder'))
                elif op == 'append':
                    self.append(table, c['patch'], c['folder'])
                elif op == 'delete':
                    self.delete(table, c['id'])
                else:
                    raise FactoryError('change {}: bad op {!r}'.format(i, op))
            problems = self.validate_all()
            if problems:
                raise FactoryError('after the preset set: ' + '; '.join(problems[:5]))
            self.db.execute('COMMIT')
        except Exception:
            self.db.execute('ROLLBACK')
            raise
        return {'name': preset_set.get('name', 'unnamed'), 'changes': len(changes)}

    def diff(self, other):
        """Rows that differ from another FactoryDb: {table: {'changed': [...ids], 'added': [...], 'removed': [...]}}."""
        out = {}
        for table in TABLES:
            mine = {r['id']: (r['raw'], r['folder']) for r in self.rows(table)}
            theirs = {r['id']: (r['raw'], r['folder']) for r in other.rows(table)}
            out[table] = {
                'changed': sorted(i for i in mine if i in theirs and mine[i] != theirs[i]),
                'added': sorted(i for i in mine if i not in theirs),
                'removed': sorted(i for i in theirs if i not in mine),
            }
        return out


def list_sets():
    """The preset sets in app/presets."""
    os.makedirs(paths.PRESET_SETS, exist_ok=True)
    sets = []
    for name in sorted(os.listdir(paths.PRESET_SETS)):
        if name.endswith('.json'):
            try:
                with open(os.path.join(paths.PRESET_SETS, name), encoding='utf-8') as f:
                    data = json.load(f)
                sets.append({'file': name, 'name': data.get('name', name[:-5]), 'changes': len(data.get('changes', []))})
            except (OSError, ValueError):
                sets.append({'file': name, 'name': name[:-5], 'changes': None, 'error': 'unreadable'})
    return sets


def save_set(name, data):
    os.makedirs(paths.PRESET_SETS, exist_ok=True)
    safe = ''.join(c if c.isalnum() or c in '-_ ' else '-' for c in name).strip() or 'set'
    path = os.path.join(paths.PRESET_SETS, safe + '.json')
    data = dict(data)
    data['name'] = name
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=1, ensure_ascii=False)
        f.write('\n')
    return path


# ---- the factory rows as preset files (for the OP-1's disk) ----
# The OP-1 writes every preset file with audio: the sampler's or kit's
# sound, and for the other engines a short clip (28896 frames at 22050 Hz
# in the files it saves). A row with no .raw file gets that much silence,
# so the file looks like the device's own.
AUDIO_RATE = 44100
SILENT_FRAMES = 28896
SILENT_RATE = 22050


def stock_db_path(stock_dir=None):
    return os.path.join(stock_dir or paths.STOCK_DIR, 'content', 'op1_factory.db')


def factory_audio_path(kind, name, stock_dir=None):
    """The stock .raw sample for a sampler or drum row (16-bit LE mono 44.1 kHz), or None."""
    if '/' in name or '\\' in name or '..' in name:
        return None
    p = os.path.join(stock_dir or paths.STOCK_DIR, 'content', 'audio', 'factory_' + kind, name + '.raw')
    return p if os.path.isfile(p) else None


def preset_file(table, p, stock_dir=None):
    """A .aif file (bytes) for a factory row: the OP-1's own layout (FVER,
    COMM, APPL, SSND), with the factory audio when the row has some."""
    from . import aiff
    kind = 'synth' if table == 'synth_presets' else 'drum'
    raw = factory_audio_path(kind, p.get('name', ''), stock_dir) if p.get('type') in ('sampler', 'drum') else None
    if raw:
        with open(raw, 'rb') as f:
            pcm = f.read()
        return aiff.new_preset(p, pcm[:len(pcm) & ~1], 1, AUDIO_RATE)
    return aiff.new_preset(p, bytes(SILENT_FRAMES * 2), 1, SILENT_RATE)


def preset_files(db_path=None, stock_dir=None, with_data=True):
    """Every stock factory row as a file for the OP-1's disk, in a folder
    named after its engine (the row's factory folder): synth/<engine>/<name>.aif,
    drum/drum/<name>.aif, drum/dbox/<name>.aif.

    [{'rel', 'table', 'id', 'folder', 'name', 'size', 'data'?}]; a row the
    validator rejects, or one without a usable name, gets a 'problem' instead."""
    db_path = db_path or stock_db_path(stock_dir)
    fdb = FactoryDb(db_path)
    out = []
    try:
        for table in TABLES:
            top = 'synth' if table == 'synth_presets' else 'drum'
            for row in fdb.rows(table):
                p = row['patch']
                item = {'table': table, 'id': row['id'], 'folder': row['folder'], 'name': (p or {}).get('name')}
                errs = ['not valid JSON'] if p is None else patch.validate(p)[0]
                name = (p or {}).get('name') or ''
                if not errs and (not name.strip() or not patch.NAME_RE.match(name)):
                    errs = ['no usable name']
                if errs:
                    item['problem'] = '; '.join(errs)
                    out.append(item)
                    continue
                item['rel'] = '{}/{}/{}.aif'.format(top, row['folder'], name)
                data = preset_file(table, p, stock_dir)
                item['size'] = len(data)
                if with_data:
                    item['data'] = data
                out.append(item)
    finally:
        fdb.close()
    return out

"""The OP-1 in USB disk mode (docs/librarian-handover.md Part 2): find it,
read its presets, back it up, and change it with an undo log. Reads are
free; every write goes through `Librarian`, which refuses to write before a
backup exists for the session and records each change so it can be undone.

The disk shows synth/, drum/, tape/ and album/. User presets are .aif
files in synth/ and drum/ (subfolders allowed)."""

import datetime
import json
import os
import re
import shutil
import string
import sys
import time

from . import aiff, factorydb, patch, paths

TOP_DIRS = ('synth', 'drum', 'tape', 'album')
PRESET_DIRS = ('synth', 'drum')
# The user slots: the OP-1 names them by number; the name inside is the
# snapshot's or whatever was saved there, and the file never follows it.
SLOT_RE = re.compile(r'^(synth|drum)/user/\d+\.aif$', re.IGNORECASE)


def _stem(rel):
    return os.path.splitext(os.path.basename(rel))[0]


class DeviceError(Exception):
    pass


def looks_like_op1(root):
    """A directory with the OP-1's four top folders (at least synth and drum)."""
    try:
        names = set(n.lower() for n in os.listdir(root))
    except OSError:
        return False
    return 'synth' in names and 'drum' in names and ('tape' in names or 'album' in names)


def find_disks():
    """Candidate OP-1 disks: on Windows every drive letter with the layout."""
    found = []
    if sys.platform.startswith('win'):
        for letter in string.ascii_uppercase:
            root = letter + ':\\'
            if os.path.isdir(root) and looks_like_op1(root):
                found.append(root)
    else:
        for base in ('/Volumes', '/media', '/mnt', '/run/media'):
            if os.path.isdir(base):
                for name in os.listdir(base):
                    root = os.path.join(base, name)
                    if looks_like_op1(root):
                        found.append(root)
                for sub in os.listdir(base) if os.path.isdir(base) else []:
                    p = os.path.join(base, sub)
                    if os.path.isdir(p):
                        for name in os.listdir(p):
                            root = os.path.join(p, name)
                            if looks_like_op1(root):
                                found.append(root)
    return found


def _rel(root, path):
    return os.path.relpath(path, root).replace('\\', '/')


def file_entry(root, path, with_patch=True):
    st = os.stat(path)
    entry = {'path': path, 'rel': _rel(root, path), 'name': os.path.basename(path),
             'size': st.st_size, 'mtime': int(st.st_mtime)}
    if with_patch and path.lower().endswith(('.aif', '.aiff')):
        try:
            p = aiff.Preset.load(path)
            entry['patch'] = p.patch
            entry['summary'] = patch.describe(p.patch)
            entry['has_audio'] = p.has_audio
            if p.comm:
                entry['audio'] = {'rate': p.comm['rate'], 'frames': p.comm['frames'],
                                  'channels': p.comm['channels'], 'bits': p.comm['bits']}
            errs, warns = patch.validate(p.patch)
            entry['problems'] = errs
            entry['warnings'] = warns
        except (aiff.AiffError, OSError, ValueError) as e:
            entry['error'] = str(e)
    return entry


def scan(root, with_patch=True):
    """Everything on the disk: {'synth': [...], 'drum': [...], 'tape': [...], 'album': [...], 'folders': {...}}."""
    if not looks_like_op1(root):
        raise DeviceError('{} does not look like an OP-1 disk'.format(root))
    out = {'root': root, 'folders': {}}
    for top in TOP_DIRS:
        entries = []
        folders = []
        base = os.path.join(root, top)
        if os.path.isdir(base):
            for dirpath, dirnames, filenames in os.walk(base):
                dirnames.sort()
                for d in dirnames:
                    folders.append(_rel(root, os.path.join(dirpath, d)))
                for f in sorted(filenames):
                    if f.startswith('.') or f.startswith('._'):
                        continue
                    entries.append(file_entry(root, os.path.join(dirpath, f), with_patch and top in PRESET_DIRS))
        out[top] = entries
        out['folders'][top] = folders
    out['counts'] = {top: len(out[top]) for top in TOP_DIRS}
    return out


def safe_path(root, rel):
    """An absolute path under root for a relative one; refuses escapes."""
    rel = rel.replace('\\', '/').lstrip('/')
    if '..' in rel.split('/') or not rel:
        raise DeviceError('bad path {!r}'.format(rel))
    full = os.path.normpath(os.path.join(root, *rel.split('/')))
    if not os.path.normcase(full).startswith(os.path.normcase(os.path.normpath(root))):
        raise DeviceError('path {!r} leaves the disk'.format(rel))
    return full


def backup(root, dest_dir, what=TOP_DIRS, log=print):
    """Copy the disk's folders into dest_dir/op1-backup-DATE/. Returns the folder."""
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    target = os.path.join(dest_dir, 'op1-backup-' + stamp)
    os.makedirs(target)
    copied = 0
    for top in what:
        src = os.path.join(root, top)
        if os.path.isdir(src):
            shutil.copytree(src, os.path.join(target, top))
            copied += sum(len(f) for _, _, f in os.walk(src))
            log('  backed up {}'.format(top))
    with open(os.path.join(target, 'backup.json'), 'w', encoding='utf-8') as f:
        json.dump({'root': root, 'when': stamp, 'files': copied}, f, indent=2)
    return target, copied


def list_backups(dest_dir):
    out = []
    if os.path.isdir(dest_dir):
        for name in sorted(os.listdir(dest_dir), reverse=True):
            p = os.path.join(dest_dir, name)
            info = os.path.join(p, 'backup.json')
            if name.startswith('op1-backup-') and os.path.isfile(info):
                with open(info, encoding='utf-8') as f:
                    out.append(dict(json.load(f), path=p))
    return out


class Librarian:
    """Writes to the OP-1 disk with an undo log in app/data/undo/.

    Every operation first saves what it will change (a copy of the file, or
    the old name), appends an entry to the log, then acts. undo() reverses
    the last entry."""

    def __init__(self, root, data_dir=None):
        if not looks_like_op1(root):
            raise DeviceError('{} does not look like an OP-1 disk'.format(root))
        self.root = root
        self.data_dir = data_dir or os.path.join(paths.DATA_DIR, 'undo')
        os.makedirs(self.data_dir, exist_ok=True)
        self.log_path = os.path.join(self.data_dir, 'log.json')
        self.log = []
        if os.path.isfile(self.log_path):
            with open(self.log_path, encoding='utf-8') as f:
                self.log = json.load(f)
        self.backed_up = False

    def _save_log(self):
        with open(self.log_path, 'w', encoding='utf-8') as f:
            json.dump(self.log, f, indent=1)

    def _stash(self, path):
        """Copy a file into the undo store; return the copy's path."""
        name = '{}-{}'.format(int(time.time() * 1000), os.path.basename(path))
        copy = os.path.join(self.data_dir, name)
        shutil.copy2(path, copy)
        return copy

    def _record(self, entry):
        entry['when'] = datetime.datetime.now().isoformat(timespec='seconds')
        entry['root'] = self.root
        self.log.append(entry)
        self._save_log()
        return entry

    def require_backup(self):
        if not self.backed_up:
            raise DeviceError('back up the OP-1 first (this session has no backup yet)')

    def _check_preset_name(self, rel):
        if not rel.lower().endswith('.aif'):
            raise DeviceError('preset files end in .aif')
        top = rel.split('/')[0]
        if top not in PRESET_DIRS:
            raise DeviceError('presets live in synth/ or drum/')

    # ---- operations ----
    # A preset's name is its file name: the OP-1 lists the files, and the
    # name inside agrees everywhere but the user slots (op1.fun's, the
    # factory folders'). So a rename rewrites the name inside, and an edit
    # of the name renames the file.
    def rename(self, rel, new_name):
        self.require_backup()
        src = safe_path(self.root, rel)
        if not os.path.isfile(src):
            raise DeviceError('{} not found'.format(rel))
        new_name = new_name.strip()
        if '/' in new_name or '\\' in new_name or not new_name:
            raise DeviceError('a file name, not a path')
        preset = None
        if rel.lower().endswith('.aif'):
            if not new_name.lower().endswith('.aif'):
                new_name += '.aif'
            if not SLOT_RE.match(rel):
                if not patch.NAME_RE.match(_stem(new_name)):
                    raise DeviceError('a preset name may only use a-z, 0-9, space, - and _')
                try:
                    preset = aiff.Preset.load(src)
                except Exception:
                    preset = None   # not a preset we can read: the file alone
        dst = os.path.join(os.path.dirname(src), new_name)
        if os.path.exists(dst):
            raise DeviceError('{} already exists'.format(new_name))
        entry = {'op': 'rename', 'from': rel, 'to': _rel(self.root, dst),
                 'text': 'renamed {} to {}'.format(os.path.basename(rel), new_name)}
        if preset is not None and preset.patch.get('name') != _stem(new_name):
            entry['stash'] = self._stash(src)
            p = dict(preset.patch)
            p['name'] = _stem(new_name)
            with open(src, 'wb') as f:
                f.write(preset.with_patch(p))
            entry['text'] += ' (the name inside too)'
        os.rename(src, dst)
        return self._record(entry)

    def move(self, rel, folder_rel):
        self.require_backup()
        src = safe_path(self.root, rel)
        folder = safe_path(self.root, folder_rel)
        if not os.path.isfile(src):
            raise DeviceError('{} not found'.format(rel))
        if folder_rel.split('/')[0] != rel.split('/')[0]:
            raise DeviceError('a preset stays in its own top folder (synth or drum)')
        os.makedirs(folder, exist_ok=True)
        dst = os.path.join(folder, os.path.basename(src))
        if os.path.exists(dst):
            raise DeviceError('{} already has {}'.format(folder_rel, os.path.basename(src)))
        shutil.move(src, dst)
        return self._record({'op': 'move', 'from': rel, 'to': _rel(self.root, dst),
                             'text': 'moved {} to {}'.format(os.path.basename(rel), folder_rel)})

    def duplicate(self, rel, new_name=None):
        self.require_backup()
        src = safe_path(self.root, rel)
        if not os.path.isfile(src):
            raise DeviceError('{} not found'.format(rel))
        base, ext = os.path.splitext(os.path.basename(src))
        new_name = new_name or base + ' copy' + ext
        dst = os.path.join(os.path.dirname(src), new_name)
        if os.path.exists(dst):
            raise DeviceError('{} already exists'.format(new_name))
        shutil.copy2(src, dst)
        return self._record({'op': 'create', 'path': _rel(self.root, dst),
                             'text': 'duplicated {} as {}'.format(os.path.basename(rel), new_name)})

    def delete(self, rel):
        self.require_backup()
        src = safe_path(self.root, rel)
        if not os.path.isfile(src):
            raise DeviceError('{} not found'.format(rel))
        stash = self._stash(src)
        os.remove(src)
        return self._record({'op': 'delete', 'path': rel, 'stash': stash,
                             'text': 'deleted {}'.format(os.path.basename(rel))})

    def new_folder(self, folder_rel):
        self.require_backup()
        if folder_rel.split('/')[0] not in PRESET_DIRS:
            raise DeviceError('folders go under synth/ or drum/')
        folder = safe_path(self.root, folder_rel)
        if os.path.exists(folder):
            raise DeviceError('{} already exists'.format(folder_rel))
        os.makedirs(folder)
        return self._record({'op': 'mkdir', 'path': folder_rel, 'text': 'created folder ' + folder_rel})

    def save_patch(self, rel, new_patch):
        """Write new settings into a preset file (its audio untouched)."""
        self.require_backup()
        src = safe_path(self.root, rel)
        if not os.path.isfile(src):
            raise DeviceError('{} not found'.format(rel))
        errs, _ = patch.validate(new_patch)
        if errs:
            raise DeviceError('; '.join(errs))
        preset = aiff.Preset.load(src)
        if patch.kind(new_patch) != patch.kind(preset.patch):
            raise DeviceError('a {} preset cannot become a {} one'.format(patch.kind(preset.patch), patch.kind(new_patch)))
        # A changed name renames the file (not a user slot). A name left as
        # it was renames nothing, even one that differs from the file name
        # (op1.fun's files carry their snapshot name inside).
        dst = None
        new_name = (new_patch.get('name') or '').strip()
        if new_name and new_name != preset.patch.get('name') and new_name != _stem(rel) and not SLOT_RE.match(rel):
            dst = os.path.join(os.path.dirname(src), new_name + '.aif')
            if os.path.exists(dst):
                raise DeviceError('{}.aif already exists here: pick another name'.format(new_name))
        stash = self._stash(src)
        data = preset.with_patch(new_patch)
        with open(src, 'wb') as f:
            f.write(data)
        entry = {'op': 'edit', 'path': rel, 'stash': stash, 'text': 'edited ' + os.path.basename(rel)}
        if dst:
            os.rename(src, dst)
            entry['to'] = _rel(self.root, dst)
            entry['text'] += ', now ' + os.path.basename(dst)
        return self._record(entry)

    def import_file(self, source, folder_rel, new_name=None):
        """Copy a .aif preset from the computer onto the disk."""
        self.require_backup()
        if not os.path.isfile(source):
            raise DeviceError('{} not found'.format(source))
        preset = aiff.Preset.load(source)   # must be a preset we can read
        errs, _ = patch.validate(preset.patch)
        if errs:
            raise DeviceError('not a valid preset: ' + '; '.join(errs))
        kind = patch.kind(preset.patch)
        top = folder_rel.split('/')[0]
        if (kind == 'synth' and top != 'synth') or (kind == 'drum' and top != 'drum'):
            raise DeviceError('a {} preset goes under {}/'.format(kind, kind))
        folder = safe_path(self.root, folder_rel)
        os.makedirs(folder, exist_ok=True)
        name = new_name or os.path.basename(source)
        if not name.lower().endswith('.aif'):
            name = os.path.splitext(name)[0] + '.aif'
        dst = os.path.join(folder, name)
        if os.path.exists(dst):
            raise DeviceError('{} already exists in {}'.format(name, folder_rel))
        shutil.copy2(source, dst)
        return self._record({'op': 'create', 'path': _rel(self.root, dst),
                             'text': 'imported {} into {}'.format(name, folder_rel)})

    def add_files(self, files, text):
        """Write several new files in one change (one undo entry).
        files = [(rel, bytes)]; a file that already exists is left alone and
        listed in the entry's 'skipped'. Folders made on the way are removed
        again by undo when they are empty."""
        self.require_backup()
        written, skipped, dirs = [], [], []
        for rel, data in files:
            self._check_preset_name(rel)
            dst = safe_path(self.root, rel)
            if os.path.exists(dst):
                skipped.append(rel)
                continue
            folder = os.path.dirname(dst)
            if not os.path.isdir(folder):
                os.makedirs(folder)
                dirs.append(_rel(self.root, folder))
            with open(dst, 'wb') as f:
                f.write(data)
            written.append(rel)
        entry = {'op': 'create_many', 'paths': written, 'dirs': dirs, 'skipped': skipped,
                 'text': text.format(n=len(written))}
        if not written:
            entry['nothing'] = True   # nothing changed: nothing to undo
            return entry
        return self._record(entry)

    def copy_factory(self, db_path=None, stock_dir=None):
        """Put every stock factory preset on the disk as a user preset, in a
        folder named after its engine (synth/digital/..., drum/dbox/...), so
        it can be changed or removed in disk mode. Files already there are
        kept. One undo entry."""
        items = factorydb.preset_files(db_path, stock_dir)
        files = [(i['rel'], i['data']) for i in items if 'rel' in i]
        entry = self.add_files(files, 'copied {n} factory presets to the disk')
        entry['problems'] = ['{} id {} ({}): {}'.format(i['table'], i['id'], i['name'], i['problem']) for i in items if 'problem' in i]
        return entry

    def export_file(self, rel, dest_dir):
        """Copy a preset off the disk (no undo needed)."""
        src = safe_path(self.root, rel)
        os.makedirs(dest_dir, exist_ok=True)
        dst = os.path.join(dest_dir, os.path.basename(src))
        shutil.copy2(src, dst)
        return dst

    def undo(self):
        if not self.log:
            raise DeviceError('nothing to undo')
        entry = self.log[-1]
        if entry.get('root') != self.root:
            raise DeviceError('the last change was on another disk ({})'.format(entry.get('root')))
        op = entry['op']
        if op in ('rename', 'move'):
            src = safe_path(self.root, entry['to'])
            dst = safe_path(self.root, entry['from'])
            if os.path.exists(dst):
                raise DeviceError('{} exists again; cannot undo'.format(entry['from']))
            shutil.move(src, dst)
            if entry.get('stash'):   # the rename rewrote the name inside
                shutil.copy2(entry['stash'], dst)
        elif op == 'create':
            p = safe_path(self.root, entry['path'])
            if os.path.isfile(p):
                os.remove(p)
        elif op in ('delete', 'edit'):
            p = safe_path(self.root, entry['path'])
            if entry.get('to'):   # the edit renamed the file too
                if os.path.exists(p):
                    raise DeviceError('{} exists again; cannot undo'.format(entry['path']))
                moved = safe_path(self.root, entry['to'])
                if os.path.isfile(moved):
                    os.remove(moved)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            shutil.copy2(entry['stash'], p)
        elif op == 'create_many':
            for rel in entry['paths']:
                p = safe_path(self.root, rel)
                if os.path.isfile(p):
                    os.remove(p)
            for rel in reversed(entry.get('dirs', [])):
                p = safe_path(self.root, rel)
                if os.path.isdir(p) and not os.listdir(p):
                    os.rmdir(p)
        elif op == 'mkdir':
            p = safe_path(self.root, entry['path'])
            if os.path.isdir(p) and not os.listdir(p):
                os.rmdir(p)
        self.log.pop()
        self._save_log()
        return entry

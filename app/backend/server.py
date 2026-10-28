"""The local app: a small HTTP server (standard library only) that serves
app/ui and a JSON API over the backend.

    python -m app.backend.server [--port 8765] [--no-browser]

The server listens on 127.0.0.1 only. Builds run in a thread; the UI polls
/api/build/status. Writes to the OP-1 go through device.Librarian (backup
first, undo log)."""

import argparse
import io
import json
import mimetypes
import os
import sys
import threading
import traceback
import urllib.parse
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from . import aiff, build, device, factorydb, firmware, knobs, manifests, mods, patch, paths

REPACKER_ASSETS = os.path.join(paths.REPACKER, 'op1repacker', 'assets')


class ApiError(Exception):
    def __init__(self, message, status=400):
        super().__init__(message)
        self.status = status


class State:
    def __init__(self):
        self.lock = threading.Lock()
        self.build_thread = None
        self.build_log = []
        self.build_report = None
        self.build_error = None
        self.librarians = {}

    def librarian(self, root):
        root = os.path.normpath(root)
        if root not in self.librarians:
            self.librarians[root] = device.Librarian(root)
        return self.librarians[root]


STATE = State()


# ---------------------------------------------------------------- API ----
def api_state(q, body):
    settings = paths.load_settings()
    img = manifests.image()
    return {
        'settings': settings,
        'image': {k: img[k] for k in ('elf', 'elf_sha256', 'built_with', 'mask_bits', 'limits', 'stock')},
        'disks': device.find_disks(),
        'backups': device.list_backups(settings['backup_dir']),
        'preset_sets': factorydb.list_sets(),
    }


def api_features(q, body):
    return {'features': manifests.features(), 'groups': manifests.GROUPS, 'group_titles': manifests.GROUP_TITLES,
            'category_titles': manifests.CATEGORY_TITLES}


def api_resolve(q, body):
    return manifests.resolve(body.get('selected', []))


def api_stock(q, body):
    path = q.get('path') or paths.load_settings()['stock_package']
    return firmware.verify_stock(path, manifests.image())


def api_build(q, body):
    with STATE.lock:
        if STATE.build_thread and STATE.build_thread.is_alive():
            raise ApiError('a build is running', 409)
        settings = paths.load_settings()
        stock = body.get('stock') or settings['stock_package']
        name = body.get('out_name') or build.default_output_name(body.get('selected', []))
        if os.path.sep in name or '/' in name:
            raise ApiError('the output is a file name; the folder comes from the settings')
        out = os.path.join(body.get('out_dir') or settings['output_dir'], name)
        selected = body.get('selected', [])
        preset_set = body.get('preset_set') or None
        STATE.build_log = []
        STATE.build_report = None
        STATE.build_error = None

        def run():
            try:
                STATE.build_report = build.build(stock, selected, out, preset_set=preset_set,
                                                 log=lambda s: STATE.build_log.append(s))
            except Exception as e:  # reported to the UI, never swallowed
                STATE.build_error = str(e)
                STATE.build_log.append('ERROR: ' + str(e))

        STATE.build_thread = threading.Thread(target=run, daemon=True)
        STATE.build_thread.start()
    return {'started': True, 'out': out}


def api_build_status(q, body):
    running = bool(STATE.build_thread and STATE.build_thread.is_alive())
    return {'running': running, 'log': STATE.build_log, 'report': STATE.build_report, 'error': STATE.build_error}


def api_screen(q, body):
    name = q.get('name', '')
    screens = manifests.resolve([])['screens']
    if name in screens:
        path = os.path.join(paths.REPO, *screens[name].split('/'))
    elif name == 'iter-lab.svg':
        path = os.path.join(REPACKER_ASSETS, 'display', 'iter-lab.svg')
    else:
        raise ApiError('no such screen', 404)
    with open(path, 'rb') as f:
        return ('image/svg+xml', f.read())


def api_disks(q, body):
    return {'disks': device.find_disks()}


def api_scan(q, body):
    root = q.get('root') or ''
    if not root:
        raise ApiError('no disk')
    return device.scan(root)


def api_backup(q, body):
    root = body.get('root')
    settings = paths.load_settings()
    target, n = device.backup(root, settings['backup_dir'], log=lambda s: None)
    STATE.librarian(root).backed_up = True
    return {'backup': target, 'files': n, 'backups': device.list_backups(settings['backup_dir'])}


def api_preset(q, body):
    root, rel = q.get('root'), q.get('rel')
    path = device.safe_path(root, rel)
    entry = device.file_entry(root, path)
    if 'patch' in entry:
        preset = aiff.Preset.load(path)
        audio = preset.audio()
        if audio:
            pcm, channels, rate, frames = audio
            entry['peaks'] = aiff.peaks(pcm, channels, 800)
            entry['audio'] = {'rate': rate, 'frames': frames, 'channels': channels, 'seconds': frames / rate}
    return entry


def api_audio(q, body):
    root, rel = q.get('root'), q.get('rel')
    preset = aiff.Preset.load(device.safe_path(root, rel))
    audio = preset.audio()
    if not audio:
        raise ApiError('this preset has no audio', 404)
    pcm, channels, rate, frames = audio
    return ('audio/wav', aiff.wav_bytes(pcm, channels, rate))


def api_device_op(q, body):
    root = body.get('root')
    lib = STATE.librarian(root)
    op = body.get('op')
    if op == 'rename':
        entry = lib.rename(body['rel'], body['name'])
    elif op == 'move':
        entry = lib.move(body['rel'], body['folder'])
    elif op == 'duplicate':
        entry = lib.duplicate(body['rel'], body.get('name'))
    elif op == 'delete':
        entry = lib.delete(body['rel'])
    elif op == 'mkdir':
        entry = lib.new_folder(body['folder'])
    elif op == 'save':
        entry = lib.save_patch(body['rel'], body['patch'])
    elif op == 'import':
        entry = lib.import_file(body['source'], body['folder'], body.get('name'))
    elif op == 'export':
        return {'exported': lib.export_file(body['rel'], body['dest'])}
    elif op == 'factory':
        entry = lib.copy_factory()
    elif op == 'undo':
        entry = lib.undo()
        entry = dict(entry, undone=True)
    else:
        raise ApiError('unknown op {!r}'.format(op))
    return {'entry': entry, 'log': lib.log[-20:], 'backed_up': lib.backed_up}


def api_factory_plan(q, body):
    """What 'copy the factory presets to the OP-1' would write: every stock
    row's file, its size, and whether the disk already has it."""
    root = q.get('root') or ''
    if not root:
        raise ApiError('no disk')
    items = factorydb.preset_files(with_data=False)
    files = []
    for i in items:
        if 'rel' not in i:
            continue
        exists = os.path.exists(device.safe_path(root, i['rel']))
        files.append({'rel': i['rel'], 'size': i['size'], 'exists': exists, 'folder': i['folder'],
                      'name': i['name'], 'table': i['table']})
    return {'files': files, 'problems': [i for i in items if 'problem' in i],
            'total': sum(f['size'] for f in files if not f['exists']),
            'new': sum(1 for f in files if not f['exists']), 'existing': sum(1 for f in files if f['exists'])}


def api_undo_log(q, body):
    root = q.get('root')
    if not root:
        return {'log': [], 'backed_up': False}
    lib = STATE.librarian(root)
    return {'log': lib.log[-20:], 'backed_up': lib.backed_up}


def api_factory(q, body):
    db_path = q.get('db') or os.path.join(paths.STOCK_DIR, 'content', 'op1_factory.db')
    fdb = factorydb.FactoryDb(db_path)
    try:
        out = {'db': db_path, 'counts': fdb.counts(), 'types': fdb.types()}
        out['budget'] = factorydb.budget_from_counts(out['counts'])
        for table in factorydb.TABLES:
            rows = fdb.rows(table)
            for r in rows:
                r['summary'] = patch.describe(r['patch']) if r['patch'] else None
                del r['raw']
            out[table] = rows
            out[table + '_folders'] = fdb.folders(table)
        # The rows a mod adds at build time (the iter presets), shown read-only.
        mod_rows = mods.iter_preset_rows()
        for r in mod_rows:
            r['summary'] = patch.describe(r['patch'])
        out['synth_presets_mods'] = mod_rows
        out['drum_presets_mods'] = []
        out['problems'] = fdb.validate_all()
    finally:
        fdb.close()
    return out


def api_factory_audio(q, body):
    kind, name = q.get('kind'), q.get('name', '')
    if kind not in ('synth', 'drum') or '/' in name or '\\' in name or '..' in name:
        raise ApiError('bad request')
    path = os.path.join(paths.STOCK_DIR, 'content', 'audio', 'factory_' + kind, name + '.raw')
    if not os.path.isfile(path):
        raise ApiError('no factory audio for {!r}'.format(name), 404)
    return ('audio/wav', aiff.raw_to_wav(path))


def api_factory_peaks(q, body):
    kind, name = q.get('kind'), q.get('name', '')
    path = os.path.join(paths.STOCK_DIR, 'content', 'audio', 'factory_' + kind, name + '.raw')
    if not os.path.isfile(path):
        raise ApiError('no factory audio for {!r}'.format(name), 404)
    with open(path, 'rb') as f:
        pcm = f.read()
    pcm = pcm[:len(pcm) & ~1]
    return {'peaks': aiff.peaks(pcm, 1, 800), 'frames': len(pcm) // 2, 'rate': 44100, 'seconds': len(pcm) / 2 / 44100}


def api_sets(q, body):
    return {'sets': factorydb.list_sets()}


def api_set_get(q, body):
    name = q.get('file', '')
    if '/' in name or '\\' in name or not name.endswith('.json'):
        raise ApiError('bad set name')
    with open(os.path.join(paths.PRESET_SETS, name), encoding='utf-8') as f:
        return json.load(f)


def api_set_save(q, body):
    name = body.get('name') or 'my set'
    data = body.get('data') or {'changes': []}
    # Validate every change's patch now, so a saved set is always buildable.
    for i, c in enumerate(data.get('changes', [])):
        if c.get('op') in ('replace', 'append'):
            errs, _ = patch.validate(c.get('patch'))
            if errs:
                raise ApiError('change {}: {}'.format(i + 1, '; '.join(errs)))
    return {'path': factorydb.save_set(name, data), 'sets': factorydb.list_sets()}


def api_set_preview(q, body):
    """Apply a set to a copy of the stock database and report the result."""
    import shutil
    import tempfile
    data = body.get('data') or {'changes': []}
    src = os.path.join(paths.STOCK_DIR, 'content', 'op1_factory.db')
    with tempfile.TemporaryDirectory() as temp:
        copy = os.path.join(temp, 'op1_factory.db')
        shutil.copyfile(src, copy)
        fdb = factorydb.FactoryDb(copy)
        try:
            summary = fdb.apply_set(data)
            counts = fdb.counts()
            problems = fdb.validate_all()
        finally:
            fdb.close()
        problems += mods.check_db_format(copy)
    budget = factorydb.budget_from_counts(counts)
    problems = manifests.check_budget(budget, problems)
    return {'summary': summary, 'counts': counts, 'budget': budget, 'problems': problems}


def api_validate(q, body):
    p = body.get('patch')
    errs, warns = patch.validate(p)
    return {'problems': errs, 'warnings': warns, 'summary': patch.describe(p) if not errs and patch.kind(p) else None,
            'custom': patch.needs_custom_firmware(p) if isinstance(p, dict) else []}


def api_knobs(q, body):
    return {'synth_engines': {k: {'name': v[0], 'knobs': v[1]} for k, v in knobs.SYNTH_ENGINES.items()},
            'rings': {'knobs': knobs.RINGS_KNOBS, 'models': knobs.RINGS_MODELS, 'polyphony': knobs.RINGS_POLYPHONY},
            'fx_types': {k: {'name': v[0], 'knobs': v[1]} for k, v in knobs.FX_TYPES.items()},
            'fx_categories': {k: {'category': v[0], 'engines': {str(i): {'name': e[0], 'knobs': e[1], 'module': e[2]}
                                                                for i, e in v[1].items()}}
                              for k, v in knobs.FX_CATEGORIES.items()},
            'lfo_types': {k: {'name': v[0], 'knobs': v[1]} for k, v in knobs.LFO_TYPES.items()},
            'adsr': knobs.ADSR, 'dyna_env': knobs.DYNA_ENV, 'drum_types': {k: {'name': v[0], 'knobs': v[1]} for k, v in knobs.DRUM_TYPES.items()},
            'drum_keys': knobs.DRUM_KEYS, 'drum_unit': knobs.DRUM_UNIT, 'sample_rate': knobs.SAMPLE_RATE,
            'marker': {'value': knobs.MARKER, 'shift': knobs.ENGINE_SHIFT}}


def api_settings_get(q, body):
    return paths.load_settings()


def api_settings_save(q, body):
    settings = paths.load_settings()
    for key in paths.DEFAULT_SETTINGS:
        if key in body:
            settings[key] = body[key]
    paths.save_settings(settings)
    return settings


def api_file_preset(q, body):
    """A preset file anywhere on the computer (for imports and factory rows)."""
    path = q.get('path') or ''
    if not os.path.isfile(path):
        raise ApiError('file not found', 404)
    preset = aiff.Preset.load(path)
    errs, warns = patch.validate(preset.patch)
    out = {'path': path, 'patch': preset.patch, 'problems': errs, 'warnings': warns, 'has_audio': preset.has_audio,
           'summary': patch.describe(preset.patch) if patch.kind(preset.patch) else None}
    audio = preset.audio()
    if audio:
        pcm, channels, rate, frames = audio
        out['peaks'] = aiff.peaks(pcm, channels, 800)
        out['audio'] = {'rate': rate, 'frames': frames, 'channels': channels, 'seconds': frames / rate}
    return out


def api_file_audio(q, body):
    preset = aiff.Preset.load(q.get('path') or '')
    audio = preset.audio()
    if not audio:
        raise ApiError('this preset has no audio', 404)
    pcm, channels, rate, frames = audio
    return ('audio/wav', aiff.wav_bytes(pcm, channels, rate))


def api_browse(q, body):
    """List a folder on the computer (for the import and settings pickers)."""
    path = q.get('path') or os.path.expanduser('~')
    if not os.path.isdir(path):
        raise ApiError('not a folder', 404)
    dirs, files = [], []
    try:
        for name in sorted(os.listdir(path), key=str.lower):
            full = os.path.join(path, name)
            if name.startswith('.'):
                continue
            if os.path.isdir(full):
                dirs.append(name)
            elif name.lower().endswith(('.aif', '.aiff', '.op1', '.json')):
                files.append({'name': name, 'size': os.path.getsize(full)})
    except OSError as e:
        raise ApiError(str(e))
    drives = []
    if sys.platform.startswith('win'):
        import string
        drives = [d + ':\\' for d in string.ascii_uppercase if os.path.isdir(d + ':\\')]
    return {'path': os.path.abspath(path), 'parent': os.path.dirname(os.path.abspath(path)), 'dirs': dirs,
            'files': files, 'drives': drives}


ROUTES = {
    ('GET', '/api/state'): api_state,
    ('GET', '/api/features'): api_features,
    ('POST', '/api/resolve'): api_resolve,
    ('GET', '/api/stock'): api_stock,
    ('POST', '/api/build'): api_build,
    ('GET', '/api/build/status'): api_build_status,
    ('GET', '/api/screen'): api_screen,
    ('GET', '/api/device/disks'): api_disks,
    ('GET', '/api/device/scan'): api_scan,
    ('POST', '/api/device/backup'): api_backup,
    ('GET', '/api/device/preset'): api_preset,
    ('GET', '/api/device/audio'): api_audio,
    ('POST', '/api/device/op'): api_device_op,
    ('GET', '/api/device/undo'): api_undo_log,
    ('GET', '/api/device/factory-plan'): api_factory_plan,
    ('GET', '/api/factory'): api_factory,
    ('GET', '/api/factory/audio'): api_factory_audio,
    ('GET', '/api/factory/peaks'): api_factory_peaks,
    ('GET', '/api/presets/sets'): api_sets,
    ('GET', '/api/presets/set'): api_set_get,
    ('POST', '/api/presets/set'): api_set_save,
    ('POST', '/api/presets/preview'): api_set_preview,
    ('POST', '/api/validate'): api_validate,
    ('GET', '/api/knobs'): api_knobs,
    ('GET', '/api/settings'): api_settings_get,
    ('POST', '/api/settings'): api_settings_save,
    ('GET', '/api/browse'): api_browse,
    ('GET', '/api/file/preset'): api_file_preset,
    ('GET', '/api/file/audio'): api_file_audio,
}

BACKEND_ERRORS = (ApiError, device.DeviceError, aiff.AiffError, factorydb.FactoryError, firmware.FirmwareError,
                  mods.ModError, build.BuildError, FileNotFoundError, KeyError, ValueError)


class Handler(BaseHTTPRequestHandler):
    server_version = 'op1app/0.1'

    def log_message(self, fmt, *args):
        if '/api/build/status' in (args[0] if args else ''):
            return
        sys.stderr.write('%s %s\n' % (self.address_string(), fmt % args))

    def _send(self, status, content_type, data):
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(data)

    def _json(self, status, obj):
        self._send(status, 'application/json; charset=utf-8', json.dumps(obj, default=str).encode('utf-8'))

    def _handle(self, method):
        url = urllib.parse.urlsplit(self.path)
        q = {k: v[-1] for k, v in urllib.parse.parse_qs(url.query).items()}
        body = {}
        if method == 'POST':
            n = int(self.headers.get('Content-Length') or 0)
            raw = self.rfile.read(n) if n else b''
            if raw:
                try:
                    body = json.loads(raw.decode('utf-8'))
                except ValueError:
                    return self._json(400, {'error': 'bad JSON body'})
        route = ROUTES.get((method, url.path))
        if route:
            try:
                result = route(q, body)
            except BACKEND_ERRORS as e:
                status = getattr(e, 'status', 400)
                return self._json(status, {'error': str(e) or e.__class__.__name__})
            except Exception as e:
                traceback.print_exc()
                return self._json(500, {'error': '{}: {}'.format(e.__class__.__name__, e)})
            if isinstance(result, tuple):
                return self._send(200, result[0], result[1])
            return self._json(200, result)
        if method == 'GET':
            return self._static(url.path)
        self._json(404, {'error': 'no such route'})

    def _static(self, path):
        if path == '/':
            path = '/index.html'
        rel = urllib.parse.unquote(path).lstrip('/')
        if '..' in rel.split('/'):
            return self._json(400, {'error': 'bad path'})
        full = os.path.join(paths.UI_DIR, *rel.split('/'))
        if not os.path.isfile(full):
            return self._json(404, {'error': 'not found'})
        ctype = mimetypes.guess_type(full)[0] or 'application/octet-stream'
        if ctype.startswith('text/') or ctype in ('application/javascript', 'application/json'):
            ctype += '; charset=utf-8'
        with open(full, 'rb') as f:
            self._send(200, ctype, f.read())

    def do_GET(self):
        self._handle('GET')

    def do_POST(self):
        self._handle('POST')


def main(argv=None):
    parser = argparse.ArgumentParser(description='OP-1 builder and librarian: local server')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--no-browser', action='store_true')
    args = parser.parse_args(argv)
    mimetypes.add_type('application/javascript', '.js')
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    url = 'http://127.0.0.1:{}/'.format(args.port)
    print('OP-1 builder and librarian at ' + url)
    if not args.no_browser:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == '__main__':
    sys.exit(main())

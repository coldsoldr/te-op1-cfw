"""The preset ("patch") JSON: its canonical text, the validator's rules
(boot validation 0x1a037a8, docs/next-projects.md §6; applied to every edit
because a bad factory row hangs the boot), and a readable description with
the names the device shows."""

import json
import re

from . import knobs

SYNTH_KEYS = ('adsr', 'fx_active', 'fx_params', 'fx_type', 'knobs', 'lfo_active', 'lfo_params',
              'lfo_type', 'name', 'octave', 'synth_version', 'type')
DRUM_KEYS = ('drum_version', 'dyna_env', 'end', 'fx_active', 'fx_params', 'fx_type', 'lfo_active',
             'lfo_params', 'lfo_type', 'name', 'octave', 'pitch', 'playmode', 'reverse', 'start',
             'type', 'volume')
DBOX_KEYS = ('dbox_data', 'dyna_env', 'fx_active', 'fx_params', 'fx_type', 'lfo_active', 'lfo_params',
             'lfo_type', 'name', 'octave', 'synth_version', 'type')
FLOAT_ZEROS = re.compile(r'(\d\.\d*?)0+(?=[,\]}])')
NAME_RE = re.compile(r'^[a-z0-9 _-]*$')
NAME_MAX = 13
VALUE_MIN, VALUE_MAX = -32768, 32767


def canonical(patch):
    """The factory rows' formatting: compact, sorted keys, trailing newline."""
    return json.dumps(patch, separators=(',', ':'), sort_keys=True, ensure_ascii=False) + '\n'


def same_text(a, b):
    """Whether two patch texts are the same bar trailing zeros in floats (the
    stock rows print base_freq with 17 digits, Python with the shortest) and
    a space after a colon (stock row 169 has one)."""
    a = FLOAT_ZEROS.sub(r'\1', a.replace(': ', ':'))
    b = FLOAT_ZEROS.sub(r'\1', b.replace(': ', ':'))
    return a == b


def kind(patch):
    """'synth', 'drum' or None."""
    t = patch.get('type')
    if t in knobs.DRUM_TYPES:
        return 'drum'
    if t in knobs.SYNTH_ENGINES:
        return 'synth'
    return None


def _ints(patch, key, n, problems, lo=VALUE_MIN, hi=VALUE_MAX):
    v = patch.get(key)
    if not isinstance(v, list) or len(v) != n:
        problems.append('{} must be a list of {} values'.format(key, n))
        return None
    for i, x in enumerate(v):
        if not isinstance(x, int) or isinstance(x, bool):
            problems.append('{}[{}] is not an integer'.format(key, i))
        elif not lo <= x <= hi:
            problems.append('{}[{}] = {} is outside {}..{}'.format(key, i, x, lo, hi))
    return v


def validate(patch):
    """Problems (errors) and warnings for a patch dict."""
    problems, warnings = [], []
    if not isinstance(patch, dict):
        return ['not a JSON object'], warnings
    k = kind(patch)
    if k is None:
        problems.append('unknown type {!r}'.format(patch.get('type')))
        return problems, warnings
    dbox = k == 'drum' and patch.get('type') == 'dbox'
    keys = SYNTH_KEYS if k == 'synth' else (DBOX_KEYS if dbox else DRUM_KEYS)
    # A dbox kit carries synth_version (the factory rows) or drum_version
    # (kits the OP-1 saves): either.
    versions = ('synth_version', 'drum_version')
    for key in keys:
        if key not in patch and not (dbox and key in versions):
            problems.append('missing {}'.format(key))
    if k == 'synth' and patch.get('type') == 'sampler' and 'base_freq' not in patch:
        problems.append('a sampler preset needs base_freq')
    for key in patch:
        if key not in keys and not (key == 'base_freq' and k == 'synth') and not (dbox and key in versions):
            warnings.append('unexpected key {}'.format(key))

    name = patch.get('name')
    if not isinstance(name, str):
        problems.append('name must be a string')
    else:
        if len(name) > NAME_MAX:
            # The OP-1 loads longer names (op1.fun's "acoustic-drumkit-4865"
            # plays from the disk); the factory rows keep to 13, the display's width.
            warnings.append('name {!r} is longer than {} characters: the OP-1 shows it cut'.format(name, NAME_MAX))
        if not NAME_RE.match(name):
            problems.append('name {!r} may only use a-z, 0-9, space, - and _'.format(name))
        if not name.strip():
            warnings.append('empty name')
    octave = patch.get('octave')
    if not isinstance(octave, int) or isinstance(octave, bool) or not -4 <= octave <= 4:
        problems.append('octave must be an integer from -4 to 4')
    for key in ('fx_active', 'lfo_active'):
        if not isinstance(patch.get(key), bool):
            problems.append('{} must be true or false'.format(key))
    if patch.get('fx_type') not in knobs.FX_TYPES:
        problems.append('unknown effect {!r}'.format(patch.get('fx_type')))
    if patch.get('lfo_type') not in knobs.LFO_TYPES:
        problems.append('unknown LFO {!r}'.format(patch.get('lfo_type')))
    fx = _ints(patch, 'fx_params', 8, problems)
    if fx and all(isinstance(x, int) for x in fx):
        for i in range(4, 8):
            if fx[i] < 0:
                problems.append('fx_params[{}] is negative: the patch check rejects it (error 24)'.format(i))
    _ints(patch, 'lfo_params', 8, problems)
    if k == 'synth':
        kn = _ints(patch, 'knobs', 8, problems)
        _ints(patch, 'adsr', 8, problems)
        if not isinstance(patch.get('synth_version'), int):
            problems.append('synth_version must be an integer')
        if kn and patch.get('type') == 'phase' and all(isinstance(x, int) for x in kn):
            if knobs.decode_setting(kn[6], 2) == 1 and kn[1] > knobs.RINGS_BRIGHTNESS_MAX:
                warnings.append('Rings brightness above {} shows past the end of its scale'.format(knobs.RINGS_BRIGHTNESS_MAX))
    elif patch.get('type') == 'dbox':
        _ints(patch, 'dyna_env', 8, problems)
        data = patch.get('dbox_data')
        if not isinstance(data, list) or len(data) != 24:
            problems.append('dbox_data must be a list of 24 keys')
        else:
            for i, key in enumerate(data):
                if not isinstance(key, list) or len(key) != 8 or not all(isinstance(x, int) and not isinstance(x, bool) and VALUE_MIN <= x <= VALUE_MAX for x in key):
                    problems.append('dbox_data[{}] must be 8 values in {}..{}'.format(i, VALUE_MIN, VALUE_MAX))
        if not any(isinstance(patch.get(v), int) for v in versions):
            problems.append('a dbox kit needs synth_version or drum_version, an integer')
    else:
        _ints(patch, 'dyna_env', 8, problems)
        for key in ('pitch', 'playmode', 'reverse', 'volume'):
            _ints(patch, key, 24, problems)
        start = _ints(patch, 'start', 24, problems, 0, 2147483647)
        end = _ints(patch, 'end', 24, problems, 0, 2147483647)
        if start and end and all(isinstance(x, int) for x in start + end):
            for i in range(24):
                if end[i] < start[i]:
                    warnings.append('key {} ends before it starts'.format(i + 1))
        if not isinstance(patch.get('drum_version'), int):
            problems.append('drum_version must be an integer')
    return problems, warnings


def needs_custom_firmware(patch):
    """Why a preset needs this firmware (a hosted effect engine, or Rings), or []."""
    reasons = []
    fx = patch.get('fx_params')
    if isinstance(fx, list) and len(fx) == 8 and isinstance(fx[4], int):
        category, engine, eid, module, _ = knobs.effect_name(patch.get('fx_type'), fx)
        if eid:
            reasons.append('effect {} -> {}'.format(category, engine))
    if patch.get('fx_type') == 'filter':
        reasons.append('the filter effect (hidden in stock)')
    if patch.get('type') == 'iter':
        reasons.append('the iter engine (hidden in stock)')
    kn = patch.get('knobs')
    if patch.get('type') == 'phase' and isinstance(kn, list) and len(kn) == 8 and isinstance(kn[6], int):
        if knobs.decode_setting(kn[6], 2) == 1:
            reasons.append('multi -> Rings')
    return reasons


def _knob_rows(names, values):
    return [{'color': knobs.COLORS[i], 'name': names[i], 'value': values[i]} for i in range(4)]


def describe(patch):
    """A readable view of a patch for the editor and the browser."""
    k = kind(patch)
    d = {'kind': k, 'name': patch.get('name', ''), 'type': patch.get('type'), 'octave': patch.get('octave')}
    fx = patch.get('fx_params') or [0] * 8
    category, engine, eid, module, fx_names = knobs.effect_name(patch.get('fx_type'), fx)
    d['effect'] = {'type': patch.get('fx_type'), 'category': category, 'engine': engine, 'engine_id': eid,
                   'module': module, 'active': patch.get('fx_active'), 'knobs': _knob_rows(fx_names, fx),
                   'label': engine if engine == category else '{} → {}'.format(category, engine)}
    lfo_type = patch.get('lfo_type')
    lfo_names = knobs.LFO_TYPES.get(lfo_type, (lfo_type, ('knob 1', 'knob 2', 'knob 3', 'knob 4')))[1]
    lp = patch.get('lfo_params') or [0] * 8
    d['lfo'] = {'type': lfo_type, 'active': patch.get('lfo_active'), 'knobs': _knob_rows(lfo_names, lp)}
    d['custom'] = needs_custom_firmware(patch)
    if k == 'synth':
        t = patch['type']
        shown, names = knobs.SYNTH_ENGINES[t]
        kn = patch.get('knobs') or [0] * 8
        engine = {'type': t, 'name': shown, 'knobs': _knob_rows(names, kn), 'extra': kn[4:]}
        if t == 'phase':
            rings = knobs.decode_setting(kn[6], 2) == 1
            engine['multi'] = {
                'engine': 'rings' if rings else 'phase',
                'model': knobs.decode_setting(kn[4], len(knobs.RINGS_MODELS)),
                'model_name': knobs.RINGS_MODELS[knobs.decode_setting(kn[4], len(knobs.RINGS_MODELS))],
                'polyphony': knobs.decode_setting(kn[5], len(knobs.RINGS_POLYPHONY)),
                'polyphony_name': knobs.RINGS_POLYPHONY[knobs.decode_setting(kn[5], len(knobs.RINGS_POLYPHONY))],
            }
            if rings:
                engine['name'] = 'multi: Rings'
                engine['knobs'] = _knob_rows(knobs.RINGS_KNOBS, kn)
            else:
                engine['name'] = 'multi: phase'
        d['engine'] = engine
        adsr = patch.get('adsr') or [0] * 8
        d['envelope'] = {'knobs': _knob_rows(knobs.ADSR, adsr), 'extra': adsr[4:]}
        if t == 'sampler':
            d['base_freq'] = patch.get('base_freq')
    elif k == 'drum':
        t = patch['type']
        shown, names = knobs.DRUM_TYPES[t]
        d['engine'] = {'type': t, 'name': shown, 'knob_names': names}
        env = patch.get('dyna_env') or [0] * 8
        d['envelope'] = {'knobs': _knob_rows(knobs.DYNA_ENV, env), 'extra': env[4:]}
        keys = []
        if t == 'dbox':
            d['keys'] = [{'index': i, 'key': knobs.DRUM_KEYS[i], 'data': row}
                         for i, row in enumerate(patch.get('dbox_data') or [[0] * 8] * 24)]
            return d
        for i in range(24):
            start = patch['start'][i] if 'start' in patch else 0
            end = patch['end'][i] if 'end' in patch else 0
            keys.append({
                'index': i, 'key': knobs.DRUM_KEYS[i],
                'start': start, 'end': end,
                'start_s': start / knobs.DRUM_UNIT / knobs.SAMPLE_RATE,
                'end_s': end / knobs.DRUM_UNIT / knobs.SAMPLE_RATE,
                'pitch': patch.get('pitch', [0] * 24)[i],
                'playmode': patch.get('playmode', [0] * 24)[i],
                'reverse': patch.get('reverse', [0] * 24)[i],
                'volume': patch.get('volume', [0] * 24)[i],
            })
        d['keys'] = keys
    return d


def rings_settings(patch, engine=None, model=None, polyphony=None):
    """Set the MULTI settings of a phase patch (knobs[6], [4], [5])."""
    kn = list(patch['knobs'])
    if engine is not None:
        kn[6] = knobs.encode_setting(1 if engine == 'rings' else 0)
    if model is not None:
        kn[4] = knobs.encode_setting(model)
    if polyphony is not None:
        kn[5] = knobs.encode_setting(polyphony)
    patch['knobs'] = kn
    return patch

"""Feature manifests (app/manifests/*.json) and the resolver that turns a
chosen set into a module mask, op1repacker options, screens to ship, and
warnings. Nothing here touches files beyond reading the manifests."""

import json
import os

from . import paths

GROUPS = ['synths', 'effects', 'sequencers', 'mods']
GROUP_TITLES = {'synths': 'Synths', 'effects': 'Effects', 'sequencers': 'Sequencers', 'mods': 'Screens and mods'}
CATEGORY_TITLES = {'multi': 'multi', 'iter': 'iter', 'reverb': 'reverb', 'dirt': 'dirt', 'modulation': 'modulation',
                   'delay': 'delay', 'other': 'other effects', 'synth': 'synth', 'drum': 'drums',
                   'screens': 'screens'}

_features = None
_image = None


def _read(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def image():
    """The shipped image's facts (app/manifests/image.json)."""
    global _image
    if _image is None:
        _image = _read(os.path.join(paths.MANIFESTS, 'image.json'))
    return _image


def features():
    """Every feature manifest, in display order."""
    global _features
    if _features is None:
        found = []
        for name in sorted(os.listdir(paths.MANIFESTS)):
            if name.endswith('.json') and name != 'image.json':
                found.append(_read(os.path.join(paths.MANIFESTS, name)))
        found.sort(key=lambda f: (GROUPS.index(f['group']), f['order']))
        _features = found
    return _features


def by_id():
    return {f['id']: f for f in features()}


def module_ids():
    return [f['id'] for f in features() if f['kind'] == 'module']


def resolve(selected):
    """Resolve a set of feature ids.

    Returns a dict with: ok, errors, warnings, mask (int), options (op1repacker
    options in application order), screens ({file name: source path}, every
    module's screen, enabled or not: they all ship), names (the list names:
    [{stock, shown, active}], a stock type shown as its category's name only
    while one of the category's modules is on), query (the engine browser's
    query for those names; the stock query when none is renamed), preset_rows,
    budget, selected (the ids as validated)."""
    table = by_id()
    errors = []
    warnings = []
    chosen = []
    for fid in selected:
        if fid not in table:
            errors.append('unknown feature {!r}'.format(fid))
        elif fid not in chosen:
            chosen.append(fid)
    chosen_set = set(chosen)
    for fid in chosen:
        f = table[fid]
        for need in f['requires']:
            if need not in chosen_set:
                errors.append('{} needs {}'.format(f['name'], table[need]['name']))
        for other in f['conflicts']:
            if other in chosen_set and fid < other:
                errors.append('{} and {} cannot both be chosen'.format(f['name'], table[other]['name']))
        for key, text in f['warnings'].items():
            if key.startswith('!'):
                if key[1:] not in chosen_set and text not in warnings:
                    warnings.append(text)
            elif key in chosen_set and text not in warnings:
                warnings.append(text)

    mask = 0
    options = []
    rows = 0
    for f in features():        # manifest order, so the options apply in a fixed order
        if f['id'] not in chosen_set:
            continue
        if f['mask_bit'] is not None:
            mask |= 1 << f['mask_bit']
        if f['option']:
            options.append(f['option'])
        rows += f['preset_rows']
    screens = {}
    for f in features():
        for name, src in f['screens'].items():
            screens[name] = src

    img = image()
    names = []
    for entry in img['names']:
        names.append({'stock': entry['stock'], 'shown': entry['shown'],
                      'active': any(m in chosen_set for m in entry['modules'])})
    cases = ''.join("WHEN '{}' THEN '{}' ".format(n['stock'], n['shown']) for n in names if n['active'])
    query = 'SELECT id, CASE type {}ELSE type END FROM %s'.format(cases) if cases else img['query_stock']

    limits = img['limits']
    stock = img['stock']
    budget = {
        'non_sampler': stock['synth_presets_non_sampler'] + rows,
        'non_sampler_limit': limits['non_sampler'],
        'sampler': stock['sampler_presets'],
        'sampler_limit': limits['sampler'],
        'total': stock['synth_presets'] + stock['drum_presets'] + rows,
        'total_limit': limits['total'],
    }
    return {
        'ok': not errors,
        'errors': errors,
        'warnings': warnings,
        'selected': chosen,
        'mask': mask,
        'mask_hex': '0x{:04x}'.format(mask),
        'options': options,
        'screens': screens,
        'names': names,
        'query': query,
        'preset_rows': rows,
        'budget': budget,
    }


def check_budget(budget, errors):
    """Append errors for a preset budget over its limits (a boot hang on the device)."""
    if budget['non_sampler'] > budget['non_sampler_limit']:
        errors.append('{} non-sampler factory presets, over the limit of {} (the OP-1 would not boot)'.format(
            budget['non_sampler'], budget['non_sampler_limit']))
    if budget['sampler'] > budget['sampler_limit']:
        errors.append('{} sampler factory presets, over the limit of {}'.format(
            budget['sampler'], budget['sampler_limit']))
    if budget['total'] > budget['total_limit']:
        errors.append('{} factory presets in total, over the limit of {}'.format(
            budget['total'], budget['total_limit']))
    return errors

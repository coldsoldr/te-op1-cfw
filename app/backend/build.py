"""The build pipeline: stock op1_246.op1 + chosen features -> a verified
custom .op1 (docs/librarian-handover.md Part 1).

    python -m app.backend.build --stock "op1 firmware/op1_246.op1" \
        --features rings,plate,clouds --out "op1 firmware/my.op1"
    python -m app.backend.build --all --out ...          # every feature
    python -m app.backend.build --list                   # the features
    python -m app.backend.build --features ... --dry-run # resolve only

Steps: verify the stock package; unpack it; inject the shipped ELF with the
module mask; copy every module screen (refusing stock names); apply the
op1repacker mods; rename the phase preset folder to multi (with Rings on); apply the factory
preset set; check the preset budget and the database; pack without
te-boot.ldr; verify_bootloader; compare the package with the work dir; diff
against stock. The report says what happened at every step; `ok` is true
only if every check passed."""

import argparse
import datetime
import json
import os
import shutil
import sys
import tempfile

from . import factorydb, firmware, manifests, mods, paths
import inject_app  # noqa: E402  (tools/, via paths)

STOCK_TEBOOT = 'te-boot.ldr'


class BuildError(Exception):
    pass


def hook_check(report, symbols):
    """Every hook target (vtable entries and patch values inside our region)
    must be a symbol of the image: a wrong hook is a crash at boot."""
    names = {}
    for name, (value, _) in symbols.items():
        names.setdefault(value, []).append(name)
    targets = set(t for _, _, _, t in report['hooks'])
    for address, kind, old, new, value in report['patches']:
        if kind in ('word', 'imm16 lo', 'imm16 hi', 'call') and inject_app.REGION_START <= value < inject_app.REGION_END:
            targets.add(value)
    good, bad = [], []
    for t in sorted(targets):
        (good if t in names else bad).append('0x{:08x}'.format(t))
    return {'good': len(good), 'bad': bad}


def default_output_name(selected):
    stamp = datetime.date.today().strftime('%Y%m%d')
    return 'op1_246_custom_{}.op1'.format(stamp)


def build(stock_package, selected, out_path, preset_set=None, work_dir=None, keep_work=False, log=print,
          image=None):
    """Run the pipeline. Returns the report dict (and raises BuildError for
    anything that stops the build before a package is written). `image`: an
    ELF to inject instead of the shipped one (a test build; its sha256 isn't
    checked against the manifest, the report warns)."""
    img = manifests.image()
    report = {'ok': False, 'steps': [], 'warnings': [], 'errors': [], 'out': os.path.abspath(out_path)}

    def step(name, **info):
        info['step'] = name
        report['steps'].append(info)
        log('- ' + name + ('' if not info.get('note') else ': ' + str(info['note'])))

    # 0. The choice.
    res = manifests.resolve(selected)
    report['resolve'] = res
    report['warnings'] += res['warnings']
    if not res['ok']:
        raise BuildError('; '.join(res['errors']))
    step('features', note=', '.join(res['selected']) or 'none', mask=res['mask_hex'], options=res['options'])

    # 1. The stock package.
    stock = firmware.verify_stock(stock_package, img)
    report['stock'] = stock
    if not stock['ok']:
        raise BuildError('stock package: ' + '; '.join(stock['problems']))
    step('stock verified', note=os.path.basename(stock_package), sha256=stock['vdk_sha256'][:16])

    # The output must not exist (a stale package once shipped by mistake).
    out_path = os.path.abspath(out_path)
    if os.path.exists(out_path):
        raise BuildError('{} already exists; choose another name or remove it'.format(out_path))
    for problem in firmware.verify_bootloader.check_file_name(out_path):
        raise BuildError(problem)

    temp = None
    if work_dir is None:
        temp = tempfile.mkdtemp(prefix='op1build-')
        work_dir = os.path.join(temp, 'work')
    stock_dir = os.path.join(os.path.dirname(work_dir), 'stock') if temp else work_dir + '_stock'
    try:
        # 2. Unpack (twice: the work dir and a stock copy to diff against).
        firmware.unpack(stock_package, work_dir)
        firmware.unpack(stock_package, stock_dir)
        step('unpacked', note=work_dir)

        # 3. Inject the image with the mask.
        if image:
            elf = os.path.abspath(image)
            elf_sha = firmware.sha256_file(elf)
            report['warnings'].append('test image {} (sha256 {}…) injected instead of the shipped one'.format(elf, elf_sha[:12]))
        else:
            elf = os.path.join(paths.REPO, *img['elf'].split('/'))
            elf_sha = firmware.sha256_file(elf)
            if elf_sha != img['elf_sha256']:
                raise BuildError('the shipped image {} does not match its manifest (sha256 {}…)'.format(elf, elf_sha[:12]))
        vdk_path = os.path.join(work_dir, 'OP1_vdk.ldr')
        with open(vdk_path, 'rb') as f:
            vdk = f.read()
        new_vdk, inj = inject_app.inject(vdk, elf, {img['mask_symbol']: res['mask']},
                                         strings={img['query_symbol']: res['query']})
        with open(vdk_path, 'wb') as f:
            f.write(new_vdk)
        hooks = hook_check(inj, inject_app.read_elf_symbols(elf))
        report['inject'] = {'lines': inj['lines'], 'image_end': '0x{:08x}'.format(inj['image_end']),
                            'hooks': hooks, 'words': [(n, '0x{:08x}'.format(a), '0x{:08x}'.format(o), '0x{:08x}'.format(v))
                                                      for n, a, o, v in inj['words']],
                            'strings': [(n, '0x{:08x}'.format(a), o, v) for n, a, o, v in inj['strings']]}
        if hooks['bad']:
            raise BuildError('hook targets that are not symbols of the image: ' + ', '.join(hooks['bad']))
        if inj['image_end'] > inject_app.REGION_END:
            raise BuildError('the image ends past the region')
        shown = ', '.join(n['shown'] if n['active'] else n['stock'] for n in res['names'])
        step('image injected', note='mask {}, {} hooks checked, lists: {}'.format(res['mask_hex'], hooks['good'], shown))

        # 4. Screens: every module's, never over a stock name.
        display = os.path.join(work_dir, 'content', 'display')
        stock_display = os.path.join(stock_dir, 'content', 'display')
        copied = []
        for name, src in sorted(res['screens'].items()):
            if os.path.exists(os.path.join(stock_display, name)):
                raise BuildError('screen {} would replace a stock file'.format(name))
            shutil.copyfile(os.path.join(paths.REPO, *src.split('/')), os.path.join(display, name))
            copied.append(name)
        step('screens copied', note=', '.join(copied), screens=copied)

        # 5. op1repacker mods.
        applied = mods.apply(work_dir, res['options'], log=log)
        step('mods applied', note=', '.join(o for o, _ in applied) or 'none', mods=applied)

        # 6. The phase folder shows as multi, the list's name for the engine
        # while Rings is on; with Rings off the list reads phase and so does
        # the folder.
        db_path = mods.factory_db_path(work_dir)
        if any(n['stock'] == 'phase' and n['active'] for n in res['names']):
            mods.rename_folder(db_path, 'phase', 'multi', expect=16, kind='phase')
            step('preset folder renamed', note='phase -> multi (display only)')
        else:
            step('preset folder kept', note='phase (Rings is off)')

        # 7. The factory preset set.
        if preset_set:
            fdb = factorydb.FactoryDb(db_path)
            summary = fdb.apply_set(preset_set)
            fdb.close()
            report['preset_set'] = summary
            step('factory presets applied', note='{} changes from {}'.format(summary['changes'], summary['name']))

        # 8. The database: budget, rows, format.
        fdb = factorydb.FactoryDb(db_path)
        counts = fdb.counts()
        problems = fdb.validate_all()
        fdb.close()
        report['factory'] = {'counts': counts, 'problems': problems}
        errors = manifests.check_budget(factorydb.budget_from_counts(counts), [])
        errors += mods.check_db_format(db_path)
        errors += problems
        if errors:
            raise BuildError('factory database: ' + '; '.join(errors))
        step('factory database checked', note='{} synth ({} sampler), {} drum rows'.format(
            counts['synth'], counts['sampler'], counts['drum']), counts=counts)

        # 9. Pack and verify.
        size = firmware.pack(work_dir, out_path)
        ok, problems, lines = firmware.verify(out_path)
        report['verify'] = {'ok': ok, 'problems': problems, 'lines': lines}
        if not ok:
            os.remove(out_path)
            raise BuildError('verify_bootloader FAILED: ' + '; '.join(problems))
        step('packaged and verified', note='{} ({} bytes), bootloader check PASS'.format(os.path.basename(out_path), size), size=size)

        # 10. The package equals the work dir; the diff against stock is only what we meant.
        n, mismatches = firmware.compare_with_dir(out_path, work_dir)
        report['contents'] = {'files': n, 'mismatches': mismatches}
        if mismatches:
            os.remove(out_path)
            raise BuildError('package contents differ from the work dir: ' + '; '.join(mismatches))
        diff = firmware.diff_dirs(stock_dir, work_dir)
        report['diff'] = diff
        allowed_changed = {'OP1_vdk.ldr', 'content/op1_factory.db'}
        for f in manifests.features():
            if f['id'] in res['selected']:
                allowed_changed.update('content/display/' + s for s in f['replaces_stock'])
        allowed_added = {'content/display/' + s for s in res['screens']}
        unexpected = [p for p in diff['changed'] if p not in allowed_changed] + \
                     [p for p in diff['added'] if p not in allowed_added] + diff['removed']
        if unexpected:
            os.remove(out_path)
            raise BuildError('unexpected differences from stock: ' + ', '.join(unexpected))
        step('contents compared', note='{} files, {} changed, {} added, none removed'.format(
            n, len(diff['changed']), len(diff['added'])))
        report['ok'] = True
        report['next'] = ('Copy {} to the OP-1 in te-boot upload mode: switch off, hold COM while switching on, '
                          'choose "1. Upload firmware", copy the file to the disk that appears, eject.').format(
                              os.path.basename(out_path))
    finally:
        if temp and not keep_work:
            shutil.rmtree(temp, ignore_errors=True)
        elif temp:
            report['work_dir'] = work_dir
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--stock', default=paths.load_settings()['stock_package'], help='the stock op1_246.op1')
    parser.add_argument('--features', default='', help='comma-separated feature ids')
    parser.add_argument('--all', action='store_true', help='every feature (one cwo graphic: moose)')
    parser.add_argument('--out', help='the package to write (must not exist)')
    parser.add_argument('--preset-set', help='a factory preset set (app/presets/NAME.json or a path)')
    parser.add_argument('--image', help='inject this ELF instead of the shipped app/image one (a test build)')
    parser.add_argument('--keep-work', action='store_true', help='keep the temporary work dir')
    parser.add_argument('--list', action='store_true', help='list the features and exit')
    parser.add_argument('--dry-run', action='store_true', help='resolve the features and exit')
    parser.add_argument('--json', action='store_true', help='print the report as JSON')
    args = parser.parse_args(argv)

    if args.list:
        for f in manifests.features():
            print('{:<16} {:<11} {:<10} {}'.format(f['id'], f['group'], f['category'],
                                                   'bit {}'.format(f['mask_bit']) if f['mask_bit'] is not None
                                                   else 'option ' + str(f['option'])))
        return 0
    selected = [s.strip() for s in args.features.split(',') if s.strip()]
    if args.all:
        selected = [f['id'] for f in manifests.features() if f['id'] not in ('gfx-cwo-dog', 'gfx-cwo-cat')]
    if args.dry_run:
        res = manifests.resolve(selected)
        print(json.dumps(res, indent=2))
        return 0 if res['ok'] else 1
    if not args.out:
        parser.error('--out is required')
    try:
        report = build(args.stock, selected, args.out, preset_set=args.preset_set, keep_work=args.keep_work,
                       image=args.image)
    except (BuildError, firmware.FirmwareError, mods.ModError, factorydb.FactoryError) as e:
        print('ERROR: {}'.format(e))
        return 1
    if args.json:
        print(json.dumps(report, indent=2, default=str))
    else:
        for w in report['warnings']:
            print('warning: ' + w)
        print('OK: ' + report['out'])
        print(report['next'])
    return 0


if __name__ == '__main__':
    sys.exit(main())

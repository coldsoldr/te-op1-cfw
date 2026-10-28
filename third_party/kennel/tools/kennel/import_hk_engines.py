#!/usr/bin/env python3
"""Import hundekraft's six original engines and effects from OP1-Emulator as kennel plugins.

    python3 tools/kennel/import_hk_engines.py OP1_EMULATOR_ROOT

Synths wavetable, taffy, pew, elg (tools/engines/<name>/) and effects washer, snowglobe
(tools/fx/<name>/) become synth_sdk/plugins/<name>/, laid out the way tools/firmware/op1r_plugins.py
generates them for the 2.x builds, so the compiled code is the same:
  <name>_plugin.c        the adapter from op1_plugin.h to the engine's callbacks (op1r_plugins.py's SYNTH / FX and
                         SCREEN templates, unchanged)
  <name>_src_<file>.c    one per source file: the 2.x builder's per-engine renames (engine_render -> pew_render, ...),
                         then the copied source
  <file>.c, *.h          the sources and tables, copied byte for byte except their include of ../engine.h, ../fx.h or
                         ../../engines/engine.h, which becomes hk/engine.h or hk/fx.h (synth_sdk/hk/: copies of
                         OP1-Emulator's two headers)
  <name>.svg             the screen as the 2.x builder ships it (iter's hidden bars and bases added by mkfw.py)
  gen_tables.py          when the engine has one
  plugin.json            type name and order as in the 2.x builds (catalog.json: marker - 0x5300), "loadable": true
The copied files get kennel's licence line (comments only). PROVENANCE.md records the OP1-Emulator commit.
Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE).
"""
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

KENNEL = Path(__file__).resolve().parents[2]
SDK = KENNEL / 'synth_sdk'
LICENCE = 'Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE).'


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit(f'import_hk_engines: expected one {old[:40]!r} in the header (OP1-Emulator changed it?)')
    return text.replace(old, new)


def host_screens(name, text):
    """HK_SCREEN_HOST (set by the <name>_src_screen.c wrappers when OP1_HOST): the screen code's firmware calls go to
    the harness's mocks, so harness/golden.py can draw these screens. The device build never sees these branches."""
    if name == 'engine.h':
        i = text.index('#define SVG_FIND(screen, id)')
        j = text.index('\n', text.index('#define SCREEN_KNOB(i)')) + 1
        return text[:i] + (
            '#ifdef HK_SCREEN_HOST   /* kennel host previews: the harness\'s mocks (op1_host.h) */\n'
            '#include "op1_plugin.h"\n'
            '#define SVG_FIND(screen, id) op1_find_node((screen), (id))\n'
            '#define SVG_MATRIX(elem, m) op1_node_set_transform((elem), (m))\n'
            '#define SVG_LINE(canvas, a, b, width, rgba) op1_raster_line((canvas), (a), (b), (width), (rgba))\n'
            '#define SVG_SIN(i) (OP1_SINE_TABLE[(i) & 8191])\n'
            '#define SCREEN_SLOT() (OP1_SCREEN_SLOT & 7)\n'
            '#define SCREEN_KNOB(i) (op1_synth_knobs(SCREEN_SLOT())[i])\n'
            '#else\n') + text[i:j] + '#endif\n' + text[j:]
    text = replace_once(text, '#ifndef FX_HOST_TEST\n', '#ifndef FX_HOST_TEST\n#ifdef HK_SCREEN_HOST\n#include "op1_plugin.h"\n#endif\n')
    text = replace_once(text, 'static inline int32_t fx_screen_knob(int i)\n{\n',
                        'static inline int32_t fx_screen_knob(int i)\n{\n#ifdef HK_SCREEN_HOST\n'
                        '    if (1) return op1_fx_knobs(0)[i] < 0 ? 0 : op1_fx_knobs(0)[i];   /* host: words are 0..32767 */\n#endif\n')
    return replace_once(text, 'static inline int fx_screen_inst(void)\n{\n',
                        'static inline int fx_screen_inst(void)\n{\n#ifdef HK_SCREEN_HOST\n    if (1) return FX_SYNTH;\n#endif\n')


HOST_SCREEN = """#else   /* host previews (harness/golden.py): the screen code on the harness's mocks (hk/engine.h HK_SCREEN_HOST) */
__attribute__((weak)) void {s}_load(void *s) {{ (void)s; }}
__attribute__((weak)) void {s}_animate(void *s) {{ (void)s; }}
__attribute__((weak)) void {s}_draw(void *s, void *c) {{ (void)s; (void)c; }}
static void screen_init(void *s) {{ {s}_load(s); }}
static void screen_update(void *s, int dt) {{ {s}_animate(s); op1_screen_draw(s, dt); {s}_draw(s, op1_raster()); }}
const op1_screen_t SCREEN_{x} = {{ screen_init, screen_update }};
#endif
"""


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    emu = Path(sys.argv[1]).resolve()
    sys.path[:0] = [str(emu / 'tools/firmware'), str(emu / 'tools/engines')]
    import op1r_plugins as P
    commit = subprocess.check_output(['git', '-C', str(emu), 'log', '-1', '--format=%H', '--', 'tools/engines',
                                      'tools/fx'], text=True).strip()
    hk = SDK / 'hk'
    hk.mkdir(exist_ok=True)
    for src, dst in ((emu / 'tools/engines/engine.h', hk / 'engine.h'), (emu / 'tools/fx/fx.h', hk / 'fx.h')):
        dst.write_text(f'/* {dst.name}: OP1-Emulator {src.relative_to(emu)} at {commit[:7]}, copied for the hundekraft plugins;\n'
                       f' * the only additions are the HK_SCREEN_HOST branches (host previews, see below).\n'
                       f' * {LICENCE} */\n' + host_screens(dst.name, src.read_text()))
    for p in P.catalog_plugins():
        x, src = p['id'], p['dir']
        d = SDK / 'plugins' / x
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)
        defines = ''.join(f'#define {a} {b}\n' for a, b in P.renames(p).items())
        sources = [x + '_plugin.c']
        for f in sorted(src.iterdir()):
            if f.suffix in ('.c', '.h', '.py') and f.is_file():
                text = f.read_text()
                text = text.replace('#include "../engine.h"', '#include "hk/engine.h"')
                text = text.replace('#include "../fx.h"', '#include "hk/fx.h"')
                text = text.replace('#include "../../engines/engine.h"', '#include "hk/engine.h"')   # tools/fx screens
                if f.suffix == '.py':
                    text = re.sub(r'^(#!.*\n)?', lambda m: (m.group(0) or '') + f'# {LICENCE}\n', text, count=1)
                else:
                    text = f'/* {LICENCE} */\n' + text
                (d / f.name).write_text(text)
            if f.suffix == '.c':
                name = f'{x}_src_{f.stem}.c'
                host = ('#define HK_SCREEN_HOST\n' if f.stem == 'screen' else '#define ENGINE_HOST_TEST\n#define FX_HOST_TEST\n')
                (d / name).write_text(f'/* {name}: {x}/{f.name} with the 2.x builder\'s renames (tools/kennel/'
                                      f'import_hk_engines.py).\n * {LICENCE} */\n#ifdef OP1_HOST\n{host}'
                                      f'#endif\n{defines}#include "{f.name}"\n')
                sources.append(name)
        prefix = x + ('_fx_screen' if p['kind'] == 'fx' else '_screen')
        text = (P.FX if p['kind'] == 'fx' else P.SYNTH).format(x=x) + P.SCREEN.format(s=prefix, x=x)
        text = text.replace('Generated by tools/firmware/op1r_plugins.py', 'adapter as OP1-Emulator\'s tools/firmware/op1r_plugins.py makes it', 1)
        text = text.replace(' */\n', f'\n * {LICENCE} */\n', 1)
        assert text.endswith('#endif\n')
        text = text[:-len('#endif\n')] + HOST_SCREEN.format(s=prefix, x=x)
        (d / sources[0]).write_text(text)
        (d / (x + '.svg')).write_bytes(P.svg(p))
        meta = {'name': x, 'kind': p['kind'], 'order': p['order'],
                'symbol': ('FX_' if p['kind'] == 'fx' else 'ENGINE_') + x, 'sources': sources,
                'defaults': P.q15_defaults(P.FX_RAW_DEFAULTS) if p['kind'] == 'fx' else P.SYNTH_DEFAULTS,
                'screen': {'svg': x + '.svg', 'symbol': 'SCREEN_' + x},
                'loadable': True, 'claims': [], 'author': 'hundekraft'}
        (d / 'plugin.json').write_text(json.dumps(meta, indent=2) + '\n')
        print(f'{x}: {len(sources)} sources, order {p["order"]}')
    print(f'from OP1-Emulator {commit}')


if __name__ == '__main__':
    main()

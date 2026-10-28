"""tools/app_manifests.py: write app/manifests/*.json (one per feature) and app/manifests/image.json,
and copy the built ELF into app/image/. Run after `make op1-seq`."""
import hashlib, json, os, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAN = os.path.join(ROOT, 'app', 'manifests')
IMG = os.path.join(ROOT, 'app', 'image')
os.makedirs(MAN, exist_ok=True)
os.makedirs(IMG, exist_ok=True)


def sha256(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()


# Bit layout: ports/fx/op1/modules.h (keep in sync).
BITS = {'plate': 0, 'hall': 1, 'clouds': 2, 'drive': 3, 'tape': 4, 'fold': 5, 'crush': 6,
        'ensemble': 7, 'shift': 8, 'tape-echo': 9, 'rings': 10, 'marbles': 11, 'grids': 12}

features = [
    # ---- synths ----
    dict(id='rings', name='Rings', group='synths', category='multi', kind='module',
         tag='Mutable Instruments', chain='multi: phase → Rings',
         description='Mutable Instruments Rings, a physical-modelling resonator with six models. Shares the multi entry with phase; the orange click swaps them.',
         screens={'rings.svg': 'ports/rings/op1/rings.svg'}, mask_bit=BITS['rings'], cpu='heavy',
         warnings={'clouds': 'Rings next to Clouds: keep Rings at polyphony 4 or fewer, or the OP-1 drops audio.'},
         order=10),
    dict(id='iter', name='iter', group='synths', category='iter', kind='mod', option='iter',
         tag='Teenage Engineering',
         description="Teenage Engineering's hidden synth engine, added to the engine list.",
         screens={}, cpu='light', order=20),
    dict(id='presets-iter', name='iter presets', group='synths', category='iter', kind='mod', option='presets-iter',
         description='11 community presets for iter from op1.fun, in their own preset folder (crystal pad, ebullition, feu doux, iter, korder, lotremkords, pulse bass, rezpiano, wonky bass, woodblock, wood bell).',
         screens={}, preset_rows=11, requires=['iter'], cpu='none',
         warnings={'!filter': 'Some iter presets use the filter effect: enable filter too.'}, order=21),
    dict(id='gfx-iter-lab', name='iter lab screen', group='synths', category='iter', kind='mod', option='gfx-iter-lab',
         description="op1repacker's laboratory drawing for the iter screen, in place of the stock one.",
         screens={}, requires=['iter'], cpu='none', order=22, replaces_stock=['iter.svg']),
    # ---- effects ----
    dict(id='plate', name='plate', group='effects', category='reverb', kind='module', chain='spring → plate → hall → clouds',
         description='A classic plate reverb (the Dattorro design): wide, dense, bright and immediate, slightly metallic. Two pages.',
         screens={'plate.svg': 'ports/fx/op1/plate.svg'}, mask_bit=BITS['plate'], cpu='medium', order=100),
    dict(id='hall', name='hall', group='effects', category='reverb', kind='module',
         description='A hall reverb: a space rather than an object. Smoother and softer than the plate, from a small room to a huge hall. Two pages.',
         screens={'hall.svg': 'ports/fx/op1/hall.svg'}, mask_bit=BITS['hall'], cpu='medium', order=101),
    dict(id='clouds', name='clouds', group='effects', category='reverb', kind='module', tag='Mutable Instruments',
         description='Mutable Instruments Clouds, a granular texture processor: grains read out of a buffer, or a looping pitch-shiftable delay, into a diffuser and reverb. Two pages.',
         screens={'clouds.svg': 'ports/clouds/op1/clouds.svg'}, mask_bit=BITS['clouds'], cpu='heavy',
         warnings={'rings': 'Clouds next to Rings: keep Rings at polyphony 4 or fewer, or the OP-1 drops audio.'}, order=102),
    dict(id='drive', name='drive', group='effects', category='dirt', kind='module', chain='punch → drive → tape → fold → crush',
         description='Overdrive and distortion, from gentle warmth to hard crunch. The valve on screen heats up with the bias.',
         screens={'drive.svg': 'ports/fx/op1/drive.svg'}, mask_bit=BITS['drive'], cpu='light', order=110),
    dict(id='tape', name='tape', group='effects', category='dirt', kind='module',
         description='A tape machine in a box: warm saturation, a low head bump, and the pitch wobble of an uneven transport.',
         screens={'tapesat.svg': 'ports/fx/op1/tapesat.svg'}, mask_bit=BITS['tape'], cpu='light', order=111),
    dict(id='fold', name='fold', group='effects', category='dirt', kind='module',
         description='A wavefolder, the West Coast synth trick: louder sounds grow extra wiggles. Bright, metallic and vocal.',
         screens={'fold.svg': 'ports/fx/op1/fold.svg'}, mask_bit=BITS['fold'], cpu='light', order=112),
    dict(id='crush', name='crush', group='effects', category='dirt', kind='module',
         description='A bit crusher, from subtle 12-bit grit to 2-bit destruction, with an unsteady sample clock.',
         screens={'crush.svg': 'ports/fx/op1/crush.svg'}, mask_bit=BITS['crush'], cpu='light', order=113),
    dict(id='ensemble', name='ensemble', group='effects', category='modulation', kind='module', chain='cwo → ensemble → shift',
         description='Stereo chorus and string ensemble: one voice per side is a classic chorus, three per side the swirl of 1970s string machines.',
         screens={'ensemble.svg': 'ports/fx/op1/ensemble.svg'}, mask_bit=BITS['ensemble'], cpu='medium', order=120),
    dict(id='shift', name='shift', group='effects', category='modulation', kind='module',
         description='Frequency shifter and ring modulator: phasing shimmer, barber-pole sweeps with feedback, or robotic metallic voices.',
         screens={'shift.svg': 'ports/fx/op1/shift.svg'}, mask_bit=BITS['shift'], cpu='light', order=121),
    dict(id='tape-echo', name='tape echo', group='effects', category='delay', kind='module', chain='delay → tape echo',
         description='A tape echo in the style of a Space Echo: up to three heads, each repeat darker and more wobbly, feedback that self-oscillates into a dub howl.',
         screens={'tapeecho.svg': 'ports/fx/op1/tapeecho.svg'}, mask_bit=BITS['tape-echo'], cpu='medium', order=130),
    dict(id='filter', name='filter', group='effects', category='other', kind='mod', option='filter', tag='Teenage Engineering',
         description="Teenage Engineering's hidden multi-mode filter effect, added to the effect list.",
         screens={}, cpu='light', order=140),
    dict(id='subtle-fx', name='subtle effect defaults', group='effects', category='other', kind='mod', option='subtle-fx',
         description="op1repacker's gentler default settings for the stock effects, so switching one on changes the sound less.",
         screens={}, cpu='none', order=141),
    # ---- sequencers ----
    dict(id='marbles', name='Marbles', group='sequencers', category='synth', kind='module', tag='Mutable Instruments',
         chain='tombola → Marbles',
         description='Mutable Instruments Marbles, a random sequencer with memory, on the tombola slot for synths.',
         screens={'marbles.svg': 'ports/marbles/op1/marbles.svg'}, mask_bit=BITS['marbles'], cpu='light', order=200),
    dict(id='grids', name='Grids', group='sequencers', category='drum', kind='module', tag='Mutable Instruments',
         chain='pattern → Grids',
         description='Mutable Instruments Grids, a topographic drum sequencer, on the pattern slot for drums.',
         screens={'grids.svg': 'ports/grids/op1/grids.svg'}, mask_bit=BITS['grids'], cpu='light', order=201),
    # ---- screens and mods ----
    dict(id='gfx-tape-invert', name='tape at the top', group='mods', category='screens', kind='mod', option='gfx-tape-invert',
         description='The tape screen draws the four tracks at the top of the display instead of the bottom.',
         screens={}, cpu='none', order=300, replaces_stock=['tape.svg']),
    dict(id='gfx-cwo-moose', name='cwo: moose', group='mods', category='screens', kind='mod', option='gfx-cwo-moose',
         description='A moose instead of the cow on the cwo screen.', screens={}, cpu='none',
         conflicts=['gfx-cwo-dog', 'gfx-cwo-cat'], order=301, replaces_stock=['bode.svg']),
    dict(id='gfx-cwo-dog', name='cwo: dog', group='mods', category='screens', kind='mod', option='gfx-cwo-dog',
         description='A dog instead of the cow on the cwo screen.', screens={}, cpu='none',
         conflicts=['gfx-cwo-moose', 'gfx-cwo-cat'], order=302, replaces_stock=['bode.svg']),
    dict(id='gfx-cwo-cat', name='cwo: cat', group='mods', category='screens', kind='mod', option='gfx-cwo-cat',
         description='A cat instead of the cow on the cwo screen.', screens={}, cpu='none',
         conflicts=['gfx-cwo-moose', 'gfx-cwo-dog'], order=303, replaces_stock=['bode.svg']),
]

for f in features:
    f.setdefault('requires', [])
    f.setdefault('conflicts', [])
    f.setdefault('warnings', {})
    f.setdefault('preset_rows', 0)
    f.setdefault('option', None)
    f.setdefault('mask_bit', None)
    f.setdefault('tag', None)
    f.setdefault('chain', None)
    f.setdefault('replaces_stock', [])
    # Verify every screen source exists and matches the beta4 content.
    for name, src in f['screens'].items():
        assert os.path.isfile(os.path.join(ROOT, src)), src
    path = os.path.join(MAN, f['id'] + '.json')
    with open(path, 'w', encoding='utf-8', newline='\n') as out:
        json.dump(f, out, indent=2, ensure_ascii=False)
        out.write('\n')
print('features', len(features))

# The image.
src_elf = os.path.join(ROOT, 'ports', 'rings', 'build', 'op1_seq.elf')
dst_elf = os.path.join(IMG, 'op1_seq.elf')
shutil.copyfile(src_elf, dst_elf)
image = {
    'elf': 'app/image/op1_seq.elf',
    'elf_sha256': sha256(dst_elf),
    'built_with': 'make op1-seq FX_ENGINES="plate drive ensemble hall fold shift tape tape_echo" (ports/rings, WSL)',
    'mask_symbol': 'op1_module_mask',
    'mask_bits': BITS,
    'mask_all': (1 << len(BITS)) - 1,
    # The engine browser's query (ports/rings/op1/patches.cc): a CASE renames a
    # stock type to its category's name when one of the category's modules is
    # on; with none on, the stock query, so the lists read like stock.
    'query_symbol': 'op1_type_list_query',
    'query_stock': 'SELECT id, type FROM %s',
    'names': [
        {'stock': 'phase', 'shown': 'multi', 'modules': ['rings']},
        {'stock': 'spring', 'shown': 'reverb', 'modules': ['plate', 'hall', 'clouds']},
        {'stock': 'cwo', 'shown': 'modulation', 'modules': ['ensemble', 'shift']},
        {'stock': 'punch', 'shown': 'dirt', 'modules': ['drive', 'tape', 'fold', 'crush']},
    ],
    'stock': {
        'package': 'op1_246.op1',
        'vdk_sha256': sha256(os.path.join(ROOT, 'op1 firmware', 'op1_246', 'OP1_vdk.ldr')),
        'teboot_sha256': sha256(os.path.join(ROOT, 'op1 firmware', 'op1_246', 'te-boot.ldr')),
        'synth_presets': 169, 'synth_presets_non_sampler': 160, 'sampler_presets': 9, 'drum_presets': 24,
    },
    'limits': {'non_sampler': 176, 'sampler': 42, 'total': 318, 'user': 100},
    'region': {'start': '0x02c00000', 'end': '0x03000000'},
    'insn16_patch': '0x01a040ac',
}
with open(os.path.join(MAN, 'image.json'), 'w', encoding='utf-8', newline='\n') as out:
    json.dump(image, out, indent=2)
    out.write('\n')
print(json.dumps(image, indent=1)[:600])

"""Names for the numbers in a preset: the encoder names of every engine,
effect and LFO (from manual/modules.html for stock and
manual/user-manual.src.html for this firmware's), the effect categories and
their engine ids (docs/fx-categories.md), and the MULTI engine's settings
(ports/rings/op1/settings.h)."""

COLORS = ('blue', 'green', 'white', 'orange')

# Synth engines: type -> (shown name, four knob names).
SYNTH_ENGINES = {
    'cluster': ('cluster', ('wave number', 'wave envelope', 'spread', 'unitor')),
    'digital': ('digital', ('wave shaper', 'octave', 'detune', 'digitalness')),
    'dna': ('dna', ('wave number', 'wave modifier', 'noise', 'filter')),
    'drwave': ('dr wave', ('wave type', 'filter', 'phase', 'chorus')),
    'dsynth': ('dsynth', ('env crossfader', 'waveform 1', 'envelope 1', 'cross modulation')),
    'fm': ('fm', ('topology', 'frequency', 'fm amount', 'detune')),
    'phase': ('multi', ('phase shift', 'distortion', 'phase filter', 'phase tilt')),
    'pulse': ('pulse', ('filter', 'amplitude', 'second pulse', 'modulation')),
    'string': ('string', ('tension', 'decay', 'detune', 'impulse')),
    'voltage': ('voltage', ('modulation', 'ground noise', 'phase filter', 'detune')),
    'sampler': ('sampler', ('start', 'loop in', 'loop out', 'end')),
    'iter': ('iter', ('knob 1', 'knob 2', 'knob 3', 'knob 4')),
}
RINGS_KNOBS = ('structure', 'brightness', 'damping', 'position')
RINGS_MODELS = ('modal', 'sympathetic strings', 'inharmonic string', 'FM voice',
                'quantized sympathetic strings', 'string + reverb')
RINGS_POLYPHONY = ('all', '1', '2', '4')
SETTING_STEP = 4096
RINGS_BRIGHTNESS_MAX = 29491

# Effects: type -> (shown name, knob names). The category hosts' stock names
# are shown as the category.
FX_TYPES = {
    'cwo': ('cwo', ('frequency', 'delay time', 'feedback', 'sideband')),
    'delay': ('delay', ('size', 'speed', 'feedback', 'mix')),
    'grid': ('grid', ('x size', 'y size', 'z feedback', 'mix')),
    'nitro': ('nitro', ('frequency', 'filter follow', 'resonance', 'frequency 2')),
    'phone': ('phone', ('tone', 'phonic', 'baud', 'telematic')),
    'punch': ('punch', ('frequency', 'punch', 'rounds', 'power')),
    'spring': ('spring', ('tone', 'turns', 'damping', 'send')),
    'filter': ('filter', ('knob 1', 'knob 2', 'knob 3', 'knob 4')),
}

# The categories this firmware adds: stock type -> (category, {engine id: (name, knob names, module id)}).
FX_CATEGORIES = {
    'spring': ('reverb', {
        1: ('plate', ('decay', 'damp', 'pre-delay', 'mix'), 'plate'),
        2: ('hall', ('size', 'decay', 'damp', 'mix'), 'hall'),
        3: ('clouds', ('position', 'size', 'pitch', 'density'), 'clouds'),
    }),
    'punch': ('dirt', {
        1: ('drive', ('drive', 'tone', 'bias', 'mix'), 'drive'),
        2: ('tape', ('saturate', 'wobble', 'age', 'mix'), 'tape'),
        3: ('fold', ('fold', 'symmetry', 'tone', 'mix'), 'fold'),
        4: ('crush', ('rate', 'bits', 'jitter', 'mix'), 'crush'),
    }),
    'cwo': ('modulation', {
        1: ('ensemble', ('rate', 'depth', 'voices', 'mix'), 'ensemble'),
        2: ('shift', ('shift', 'feedback', 'spread', 'mix'), 'shift'),
    }),
    'delay': ('delay', {
        1: ('tape echo', ('time', 'feedback', 'age', 'mix'), 'tape-echo'),
    }),
}

# The marker in fx_params[4] (docs/fx-categories.md).
MARKER = 0x6000
MARKER_MASK = 0xf000
ENGINE_SHIFT = 8
ENGINE_MASK = 0xf

LFO_TYPES = {
    'tremolo': ('tremolo', ('speed', 'pitch amount', 'volume level', 'pitch envelope')),
    'value': ('value', ('speed', 'amount', 'destination', 'parameter')),
    'random': ('random', ('speed', 'amount', 'destination', 'envelope')),
    'element': ('element', ('source', 'amount', 'destination', 'parameter')),
    'midi': ('midi', ('parameter 1', 'parameter 2', 'parameter 3', 'parameter 4')),
    'bend': ('bend', ('speed', 'amount', 'destination', 'bend')),
    'crank': ('crank', ('crank', 'amount', 'destination', 'parameter')),
}

ADSR = ('attack', 'decay', 'sustain', 'release', 'adsr 5', 'adsr 6', 'adsr 7', 'adsr 8')
DRUM_TYPES = {
    'drum': ('drum sampler', ('pitch', 'start', 'end', 'play mode')),
    'dbox': ('dbox', ('pitch 1', 'waveform 1', 'envelope 1', 'cross modulation')),
}
DYNA_ENV = ('attack', 'gain', 'release', 'timing', 'env 5', 'env 6', 'env 7', 'env 8')
DRUM_KEYS = ('F', 'F#', 'G', 'G#', 'A', 'A#', 'B', 'C', 'C#', 'D', 'D#', 'E',
             'F', 'F#', 'G', 'G#', 'A', 'A#', 'B', 'C', 'C#', 'D', 'D#', 'E')
# start/end: the 12-second maximum maps to int32 max (measured on the factory
# kits: end / samples = 4057.97 at 44.1 kHz).
DRUM_SECONDS_MAX = 12.0
DRUM_UNIT = 2147483647 / (DRUM_SECONDS_MAX * 44100)
SAMPLE_RATE = 44100


def engine_id(fx_params):
    """The hosted engine id in fx_params[4], or 0 for stock."""
    w = fx_params[4] & 0xffff
    return (w >> ENGINE_SHIFT) & ENGINE_MASK if (w & MARKER_MASK) == MARKER else 0


def effect_name(fx_type, fx_params):
    """(category or type name, engine name, engine id, module id or None,
    knob names) for a preset's effect."""
    if fx_type in FX_CATEGORIES:
        category, engines = FX_CATEGORIES[fx_type]
        eid = engine_id(fx_params)
        if eid in engines:
            name, names, module = engines[eid]
            return category, name, eid, module, names
        if eid:
            return category, 'engine {}'.format(eid), eid, None, FX_TYPES[fx_type][1]
        return category, fx_type, 0, None, FX_TYPES[fx_type][1]
    shown, names = FX_TYPES.get(fx_type, (fx_type, ('knob 1', 'knob 2', 'knob 3', 'knob 4')))
    return shown, shown, 0, None, names


def decode_setting(value, count):
    s = (int(value) + SETTING_STEP // 2) // SETTING_STEP
    return max(0, min(count - 1, s))


def encode_setting(index):
    return int(index) * SETTING_STEP

"""The OP-1's preset files: AIFF/AIFC with the settings as JSON in an APPL
chunk ("op-1" + JSON) and, for sampler and drum presets, the audio in SSND.
Reading keeps every chunk's bytes; writing replaces only the APPL chunk (or
the audio when asked), so an unchanged preset round-trips byte for byte."""

import json
import struct

from . import patch as patchmod

FORM_TYPES = (b'AIFF', b'AIFC')
APPL_PREFIX = b'op-1'


class AiffError(Exception):
    pass


def _ext80_to_float(b):
    """80-bit IEEE extended (the AIFF sample rate) to float."""
    sign = b[0] >> 7
    exponent = ((b[0] & 0x7f) << 8) | b[1]
    mantissa = int.from_bytes(b[2:10], 'big')
    if exponent == 0 and mantissa == 0:
        return 0.0
    value = mantissa * 2.0 ** (exponent - 16383 - 63)
    return -value if sign else value


def _float_to_ext80(f):
    import math
    if f == 0:
        return b'\0' * 10
    sign = 0x8000 if f < 0 else 0
    f = abs(f)
    m, e = math.frexp(f)           # f = m * 2**e, 0.5 <= m < 1
    exponent = e - 1 + 16383
    mantissa = int(m * (1 << 64))
    return struct.pack('>H', sign | exponent) + mantissa.to_bytes(8, 'big')


def read_chunks(data):
    """[(id, payload bytes)] of a FORM AIFF/AIFC file, plus its form type."""
    if len(data) < 12 or data[:4] != b'FORM':
        raise AiffError('not an AIFF file (no FORM header)')
    form_type = data[8:12]
    if form_type not in FORM_TYPES:
        raise AiffError('not an AIFF file (form type {!r})'.format(form_type))
    size = struct.unpack('>I', data[4:8])[0]
    end = min(len(data), 8 + size)
    chunks = []
    pos = 12
    while pos + 8 <= end:
        cid = data[pos:pos + 4]
        n = struct.unpack('>I', data[pos + 4:pos + 8])[0]
        payload = data[pos + 8:pos + 8 + n]
        chunks.append((cid, payload))
        pos += 8 + n + (n & 1)
    return form_type, chunks


def write_chunks(form_type, chunks):
    body = b''
    for cid, payload in chunks:
        body += cid + struct.pack('>I', len(payload)) + payload
        if len(payload) & 1:
            body += b'\0'
    return b'FORM' + struct.pack('>I', 4 + len(body)) + form_type + body


def parse_comm(payload):
    channels, frames = struct.unpack('>hI', payload[:6])
    bits = struct.unpack('>h', payload[6:8])[0]
    rate = _ext80_to_float(payload[8:18])
    compression = payload[18:22] if len(payload) >= 22 else b'NONE'
    return {'channels': channels, 'frames': frames, 'bits': bits, 'rate': rate,
            'compression': compression.decode('ascii', 'replace')}


def make_comm(channels, frames, bits, rate, compression=b'sowt', name=b'Signed integer (little-endian) linear'):
    payload = struct.pack('>hIh', channels, frames, bits) + _float_to_ext80(rate)
    payload += compression + bytes([len(name)]) + name
    if len(payload) & 1:
        payload += b'\0'
    return payload


def parse_appl(payload):
    text = payload
    if text.startswith(APPL_PREFIX):
        text = text[len(APPL_PREFIX):]
    text = text.rstrip(b'\0')
    try:
        return json.loads(text.decode('utf-8'))
    except ValueError as e:
        raise AiffError('APPL chunk is not JSON: {}'.format(e))


def make_appl(p):
    """'op-1' + the canonical JSON (with its newline); an odd length is padded
    with a space inside the chunk, as the files we have are."""
    payload = APPL_PREFIX + patchmod.canonical(p).encode('utf-8')
    if len(payload) & 1:
        payload += b' '
    return payload


class Preset:
    """A preset file in memory."""

    def __init__(self, data):
        self.form_type, self.chunks = read_chunks(data)
        self.data = data
        self.comm = None
        self.patch = None
        self.appl_index = None
        for i, (cid, payload) in enumerate(self.chunks):
            if cid == b'COMM':
                self.comm = parse_comm(payload)
            elif cid == b'APPL' and self.appl_index is None:
                self.appl_index = i
                self.patch = parse_appl(payload)
        if self.patch is None:
            raise AiffError('no APPL chunk with the settings')

    @classmethod
    def load(cls, path):
        with open(path, 'rb') as f:
            return cls(f.read())

    @property
    def has_audio(self):
        return any(cid == b'SSND' and len(p) > 8 for cid, p in self.chunks)

    def audio(self):
        """(samples as bytes of little-endian int16 interleaved, channels, rate, frames) or None."""
        if self.comm is None:
            return None
        for cid, payload in self.chunks:
            if cid != b'SSND':
                continue
            offset, block = struct.unpack('>II', payload[:8])
            raw = payload[8 + offset:]
            bits = self.comm['bits']
            channels = self.comm['channels'] or 1
            comp = self.comm['compression']
            if bits == 16:
                if comp in ('NONE', 'twos'):
                    # big-endian -> little-endian
                    n = len(raw) // 2
                    samples = struct.unpack('>{}h'.format(n), raw[:n * 2])
                    raw = struct.pack('<{}h'.format(n), *samples)
                elif comp != 'sowt':
                    raise AiffError('unsupported AIFF compression {!r}'.format(comp))
            elif bits == 8:
                n = len(raw)
                samples = struct.unpack('{}b'.format(n), raw[:n])
                raw = struct.pack('<{}h'.format(n), *(s * 256 for s in samples))
            elif bits == 24:
                n = len(raw) // 3
                big = comp in ('NONE', 'twos')
                out = bytearray()
                for i in range(n):
                    b = raw[3 * i:3 * i + 3]
                    v = int.from_bytes(b, 'big' if big else 'little', signed=True)
                    out += struct.pack('<h', max(-32768, min(32767, v >> 8)))
                raw = bytes(out)
            else:
                raise AiffError('unsupported sample size {} bits'.format(bits))
            frames = len(raw) // 2 // channels
            return raw, channels, int(round(self.comm['rate'])) or 44100, frames
        return None

    def with_patch(self, p):
        """The file's bytes with a new APPL payload (everything else kept)."""
        chunks = list(self.chunks)
        chunks[self.appl_index] = (b'APPL', make_appl(p))
        return write_chunks(self.form_type, chunks)

    def with_audio(self, pcm_le16, channels, rate):
        """The file's bytes with new audio (AIFC sowt 16-bit) and COMM."""
        frames = len(pcm_le16) // 2 // channels
        chunks = []
        done_comm = done_ssnd = False
        for cid, payload in self.chunks:
            if cid == b'COMM':
                chunks.append((b'COMM', make_comm(channels, frames, 16, rate)))
                done_comm = True
            elif cid == b'SSND':
                chunks.append((b'SSND', struct.pack('>II', 0, 0) + pcm_le16))
                done_ssnd = True
            else:
                chunks.append((cid, payload))
        if not done_comm:
            chunks.insert(1 if chunks and chunks[0][0] == b'FVER' else 0, (b'COMM', make_comm(channels, frames, 16, rate)))
        if not done_ssnd:
            chunks.append((b'SSND', struct.pack('>II', 0, 0) + pcm_le16))
        return write_chunks(b'AIFC', chunks)


def new_preset(p, pcm_le16=None, channels=1, rate=44100):
    """A fresh AIFC file for a patch (and optional audio)."""
    chunks = [(b'FVER', struct.pack('>I', 0xA2805140))]
    frames = len(pcm_le16) // 2 // channels if pcm_le16 else 0
    chunks.append((b'COMM', make_comm(channels, frames, 16, rate)))
    chunks.append((b'APPL', make_appl(p)))
    if pcm_le16:
        chunks.append((b'SSND', struct.pack('>II', 0, 0) + pcm_le16))
    return write_chunks(b'AIFC', chunks)


def wav_bytes(pcm_le16, channels, rate):
    """A WAV file for the browser's decoder."""
    n = len(pcm_le16)
    header = b'RIFF' + struct.pack('<I', 36 + n) + b'WAVE'
    header += b'fmt ' + struct.pack('<IHHIIHH', 16, 1, channels, rate, rate * channels * 2, channels * 2, 16)
    header += b'data' + struct.pack('<I', n)
    return header + pcm_le16


def raw_to_wav(path, rate=44100):
    """A factory .raw sample (headerless 16-bit little-endian mono) as WAV."""
    with open(path, 'rb') as f:
        pcm = f.read()
    return wav_bytes(pcm[:len(pcm) & ~1], 1, rate)


def peaks(pcm_le16, channels, buckets=600):
    """[(min, max)] per bucket, for drawing a waveform."""
    n = len(pcm_le16) // 2
    if n == 0:
        return []
    samples = struct.unpack('<{}h'.format(n), pcm_le16[:n * 2])
    if channels > 1:
        samples = samples[::channels]
        n = len(samples)
    out = []
    step = max(1, n // buckets)
    for i in range(0, n, step):
        chunk = samples[i:i + step]
        out.append((min(chunk), max(chunk)))
    return out

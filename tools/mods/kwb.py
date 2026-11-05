"""DOAX3 Scarlet's short voice sounds (shouts, laughs, reactions): 00/sound/char/SND_CHAR_<GIRL>[_CASINO]_JP_XWS.xws.

XWSFILE: a Koei Tecmo container (u32 total size at 0x10, chunk count at 0x14, u32 chunk offsets at 0x30, chunks 16-byte
aligned) of wave banks, each two chunks: a header "_HBW0000" and its data "_DBW0000".

_HBW0000: u32 size at 0x08, then "KWBN" at 0x0c (offsets below are from there):
    0x04 u16 version (0x3200), u16 sound count   0x08 u32 the data chunk's data size   0x18 u32 sound offsets
    sound: u16 kind, u8, u8 variants (the game picks one); 0x2c u16 variant offset (0x34), u16 variant size (0xac)
    variant: u16 sample rate, u8 codec (0x90: Nintendo DSP ADPCM), u8 channels; 0x0c u32 samples, 0x10 u32 data
             offset, 0x14 u32 data size; 0x4c a standard DSP header, little-endian (samples, nibbles, rate, loop,
             start / end nibble, current address, 16 coefficients, gain, predictor/scale, history, loop context)
_DBW0000: u32 size at 0x08, the data from 0x0c (variants' data offsets count from there; some share data).

With replaced variants the data is laid out again (Bank.build), so a bank stays about its original size.
"""
import struct

DSP = 0x90


def _clamp16(v):
    return -32768 if v < -32768 else 32767 if v > 32767 else v


# ------------------------------------------------------------------ Nintendo DSP ADPCM
def dsp_decode(data, samples, coefs, hist1=0, hist2=0):
    out = []
    o = 0
    while len(out) < samples:
        ps = data[o]
        c1, c2 = coefs[(ps >> 4) & 7]
        scale = 1 << (ps & 0xF)
        for k in range(14):
            if len(out) >= samples:
                break
            b = data[o + 1 + k // 2]
            n = (b >> 4) if k % 2 == 0 else (b & 0xF)
            if n >= 8:
                n -= 16
            s = _clamp16(((n * scale) << 11) + 1024 + c1 * hist1 + c2 * hist2 >> 11)
            hist2, hist1 = hist1, s
            out.append(s)
        o += 8
    return out


def dsp_coefs(pcm):
    """8 predictor pairs for pcm: each 14-sample frame's least-squares 2nd-order predictor, clustered (k-means,
    weighted by the frame's energy) into 8, as 11-bit fixed point."""
    frames = []
    h1 = h2 = 0
    for f in range(0, len(pcm), 14):
        x = pcm[f:f + 14]
        r11 = r22 = r12 = r01 = r02 = 0.0
        for s in x:
            r11 += h1 * h1; r22 += h2 * h2; r12 += h1 * h2; r01 += s * h1; r02 += s * h2
            h2, h1 = h1, s
        det = r11 * r22 - r12 * r12
        if det > 1e-6 and r11 > 100:
            a1 = (r01 * r22 - r02 * r12) / det
            a2 = (r02 * r11 - r01 * r12) / det
            a1, a2 = max(-1.99, min(1.99, a1)), max(-0.99, min(0.99, a2))
            frames.append((a1, a2, r11))
    cent = [(0.0, 0.0), (1.0, 0.0), (1.8, -0.83), (1.5, -0.6), (0.6, 0.2), (1.9, -0.95), (0.3, -0.3), (-0.5, 0.0)]
    for _ in range(12 if frames else 0):
        acc = [[0.0, 0.0, 0.0] for _ in cent]
        for a1, a2, w in frames:
            k = min(range(8), key=lambda i: (a1 - cent[i][0]) ** 2 + (a2 - cent[i][1]) ** 2)
            acc[k][0] += a1 * w; acc[k][1] += a2 * w; acc[k][2] += w
        cent = [(a / w, b / w) if w else cent[i] for i, (a, b, w) in enumerate(acc)]
    return [(int(round(max(-16, min(15.999, a)) * 2048)), int(round(max(-16, min(15.999, b)) * 2048))) for a, b in cent]


def dsp_encode(pcm, coefs):
    """DSP ADPCM frames (8 bytes: predictor/scale, 14 nibbles) of pcm: each frame takes the predictor and scale with
    the least error, encoded against the decoder's own history."""
    out = bytearray()
    h1 = h2 = 0
    for f in range(0, len(pcm), 14):
        x = pcm[f:f + 14]
        x = x + [0] * (14 - len(x))
        best = None
        for ci, (c1, c2) in enumerate(coefs):
            # the scale the open-loop residual needs, then that one and the next (the closed loop can need more)
            a, b, peak = h1, h2, 0
            for s in x:
                r = s - ((c1 * a + c2 * b + 1024) >> 11)
                peak = max(peak, r if r >= 0 else -r - 1)
                b, a = a, s
            sc = 0
            while sc < 12 and (peak >> sc) > 7:
                sc += 1
            for scale in (sc, sc + 1) if sc < 12 else (sc,):
                step = 1 << scale
                a, b, err, nib = h1, h2, 0, []
                for s in x:
                    pred = c1 * a + c2 * b + 1024
                    n = (((s << 11) - pred) // (step << 10) + 1) >> 1  # (round to nearest)
                    n = -8 if n < -8 else 7 if n > 7 else n
                    y = _clamp16(((n * step) << 11) + pred >> 11)
                    err += (s - y) * (s - y)
                    nib.append(n & 0xF)
                    b, a = a, y
                if best is None or err < best[0]:
                    best = (err, ci, scale, nib, a, b)
        err, ci, scale, nib, h1, h2 = best
        out.append((ci << 4) | scale)
        for k in range(0, 14, 2):
            out.append((nib[k] << 4) | nib[k + 1])
    return bytes(out)


def dsp_nibbles(samples):
    return samples // 14 * 16 + (samples % 14 + 2 if samples % 14 else 0)


# ------------------------------------------------------------------ banks
class Variant:
    def __init__(self, bank, off):
        self.bank, self.off = bank, off  # (off: in the bank's header)
        h = bank.head
        self.rate, self.codec, self.channels = struct.unpack_from('<HBB', h, off)
        self.samples, self.data_off, self.data_size = struct.unpack_from('<III', h, off + 0x0c)

    def coefs(self):
        c = struct.unpack_from('<16h', self.bank.head, self.off + 0x4c + 0x1c)
        return [(c[2 * i], c[2 * i + 1]) for i in range(8)]


class Bank:
    def __init__(self, head, data):
        assert head[:8] == b'_HBW0000' and head[0xc:0x10] == b'KWBN', 'not a KWBN wave bank'
        assert data[:8] == b'_DBW0000', 'not a wave bank data chunk'
        self.head, self.data = bytearray(head), bytearray(data)
        self.kw = 0xc
        self.count = struct.unpack_from('<H', head, self.kw + 6)[0]
        self.new = {}  # (variant header offset -> its replacement's data, until build)

    def sound(self, i):
        """[Variant] of sound i (None: no such sound / an empty slot)."""
        if i >= self.count:
            return None
        so = struct.unpack_from('<I', self.head, self.kw + 0x18 + 4 * i)[0]
        if not so:
            return None
        e = self.kw + so
        n = self.head[e + 3]
        sub_off, sub_size = struct.unpack_from('<HH', self.head, e + 0x2c)
        return [Variant(self, e + sub_off + j * sub_size) for j in range(n)]

    def sounds(self):
        for i in range(self.count):
            v = self.sound(i)
            if v:
                yield i, v

    def stream(self, v):
        """Variant v's encoded data."""
        if v.off in self.new:
            return self.new[v.off]
        return bytes(self.data[0xc + v.data_off:0xc + v.data_off + v.data_size])

    def decode(self, v):
        assert v.codec == DSP and v.channels == 1, f'codec {v.codec:#x} / {v.channels} channels not handled'
        ps, h1, h2 = struct.unpack_from('<Hhh', self.head, v.off + 0x4c + 0x3e)
        return struct.pack(f'<{v.samples}h', *dsp_decode(self.stream(v), v.samples, v.coefs(), h1, h2))

    def replace(self, v, pcm):
        """Variant v plays pcm (16-bit mono at its rate) from now on (its data is placed by build)."""
        s = list(struct.unpack(f'<{len(pcm) // 2}h', pcm))
        if not s:
            s = [0]
        coefs = dsp_coefs(s)
        enc = dsp_encode(s, coefs)
        self.new[v.off] = enc
        h, o = self.head, v.off
        struct.pack_into('<I', h, o + 0x0c, len(s))
        struct.pack_into('<I', h, o + 0x14, len(enc))
        d = o + 0x4c
        struct.pack_into('<IIIHHIII', h, d, len(s), dsp_nibbles(len(s)), v.rate, 0, 0, 2, dsp_nibbles(len(s)) - 1, 2)
        struct.pack_into('<16h', h, d + 0x1c, *[c for pair in coefs for c in pair])
        struct.pack_into('<HHhhHhh', h, d + 0x3c, 0, enc[0], 0, 0, 0, 0, 0)  # (gain, ps, hist, loop context)
        v.samples, v.data_size = len(s), len(enc)

    def build(self):
        """(head, data) chunks; the sizes in them are without the container's padding. With replaced variants the data
        is laid out again, in the original order, each stream once (variants share identical data) and 32-byte
        aligned: the bank stays about its original size (the game keeps each bank in memory)."""
        if self.new:
            data = bytearray(self.data[:0xc])
            placed = {}
            variants = sorted((v for _, vs in self.sounds() for v in vs), key=lambda v: (v.data_off, v.off))
            streams = [(v, self.stream(v)) for v in variants]
            for v, blob in streams:
                at = placed.get(blob)
                if at is None:
                    at = (len(data) - 0xc + 0x1f) & ~0x1f
                    data += bytes(0xc + at - len(data)) + blob
                    placed[blob] = at
                struct.pack_into('<I', self.head, v.off + 0x10, at)
                v.data_off = at
            self.data, self.new = data, {}
        struct.pack_into('<I', self.data, 8, len(self.data))
        struct.pack_into('<I', self.head, self.kw + 8, len(self.data) - 0xc)
        return bytes(self.head), bytes(self.data)


class Xws:
    def __init__(self, data):
        assert data[:8] == b'XWSFILE\0', 'not an XWSFILE'
        self.raw = data
        n = struct.unpack_from('<I', data, 0x14)[0]
        offs = list(struct.unpack_from(f'<{n}I', data, 0x30))
        ends = offs[1:] + [len(data)]
        chunks = [data[o:e] for o, e in zip(offs, ends)]
        self.banks = [Bank(chunks[i][:struct.unpack_from('<I', chunks[i], 8)[0]],
                           chunks[i + 1][:struct.unpack_from('<I', chunks[i + 1], 8)[0]]) for i in range(0, n, 2)]

    def build(self):
        chunks = []
        for b in self.banks:
            chunks += list(b.build())
        head = bytearray(self.raw[:0x30])
        table_end = 0x30 + 4 * len(chunks)
        o = (table_end + 0xF) & ~0xF
        offs, body = [], bytearray()
        for c in chunks:
            offs.append(o)
            body += c + b'\0' * ((-len(c)) % 0x10)
            o = (o + len(c) + 0xF) & ~0xF
        out = head + struct.pack(f'<{len(chunks)}I', *offs)
        out += b'\0' * (offs[0] - len(out)) + body
        struct.pack_into('<I', out, 0x10, len(out))
        return bytes(out)

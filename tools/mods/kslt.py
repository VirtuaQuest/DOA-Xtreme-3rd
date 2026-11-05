"""DOAX3 Scarlet's UI texture packs (00/ui/**/*_KSLT.kslt): character portraits, icons, menus.

KSLT ("TLSK3100"):
    0x08 u32 texture count   0x0c u32 file size   0x10 u32 size of table A (count * 0x38)   0x14 u32 size of the names
    0x40 table A: 0x38 bytes per texture (drawing parameters; kept as they are)
    then table B: 0x14 bytes per texture, u32 offset of its data (from the file's start)
    then the names, NUL-terminated, in texture order ("chara_kas", "chara_icon_kas_00", ...)
    each texture: a 0x48-byte header, then its pixels
        0x00 u32 format (3: BC3 / DXT5, 0: RGBA8)   0x04 u16 width, u16 height   0x0c u32 mip levels (1)
        0x1c u32 data size   0x20 u32 row pitch in bytes
    The pixels are linear (rows of 4x4 blocks for BC3), not the Switch GPU's block-linear layout.
A replaced texture keeps its size and format, so the file's layout doesn't change.
"""
import struct

BC3, RGBA8 = 3, 0


class Texture:
    def __init__(self, kslt, index, name, off):
        self.kslt, self.index, self.name, self.off = kslt, index, name, off
        d = kslt.data
        self.format, self.width, self.height = struct.unpack_from('<IHH', d, off)
        self.levels = struct.unpack_from('<I', d, off + 0x0c)[0]
        self.size = struct.unpack_from('<I', d, off + 0x1c)[0]

    @property
    def supported(self):
        return self.format in (BC3, RGBA8) and self.levels <= 1

    def pixels(self):
        return bytes(self.kslt.data[self.off + 0x48:self.off + 0x48 + self.size])

    def rgba(self):
        if self.format == RGBA8:
            return self.pixels()[:self.width * self.height * 4]
        if self.format == BC3:
            return bc3_decode(self.pixels(), self.width, self.height)
        raise ValueError(f'{self.name}: format {self.format} not handled')

    def set_rgba(self, rgba):
        data = rgba if self.format == RGBA8 else bc3_encode(rgba, self.width, self.height)
        assert len(data) == self.size, (self.name, len(data), self.size)
        self.kslt.data[self.off + 0x48:self.off + 0x48 + self.size] = data


class Kslt:
    def __init__(self, data):
        assert data[:8] == b'TLSK3100', 'not a KSLT texture pack'
        self.data = bytearray(data)
        n, size, asz, nsz = struct.unpack_from('<4I', data, 8)
        b = 0x40 + asz
        names = bytes(data[b + n * 0x14:b + n * 0x14 + nsz]).split(b'\0')
        self.textures = [Texture(self, i, names[i].decode('latin1'), struct.unpack_from('<I', data, b + i * 0x14)[0])
                         for i in range(n)]
        self.by_name = {t.name: t for t in self.textures}


# ------------------------------------------------------------------ BC3 (DXT5)
def _rgb565(c):
    return ((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31


def bc3_decode(data, w, h):
    bw, bh = (w + 3) // 4, (h + 3) // 4
    out = bytearray(w * h * 4)
    for by in range(bh):
        for bx in range(bw):
            o = (by * bw + bx) * 16
            a0, a1 = data[o], data[o + 1]
            ai = int.from_bytes(data[o + 2:o + 8], 'little')
            if a0 > a1:
                al = [a0, a1] + [(a0 * (7 - i) + a1 * i) // 7 for i in range(1, 7)]
            else:
                al = [a0, a1] + [(a0 * (5 - i) + a1 * i) // 5 for i in range(1, 5)] + [0, 255]
            c0, c1, ci = struct.unpack_from('<HHI', data, o + 8)
            p0, p1 = _rgb565(c0), _rgb565(c1)
            cols = [p0, p1, tuple((2 * x + y) // 3 for x, y in zip(p0, p1)), tuple((x + 2 * y) // 3 for x, y in zip(p0, p1))]
            for i in range(16):
                x, y = bx * 4 + (i & 3), by * 4 + (i >> 2)
                if x < w and y < h:
                    r, g, b = cols[(ci >> (2 * i)) & 3]
                    p = (y * w + x) * 4
                    out[p:p + 4] = bytes((r, g, b, al[(ai >> (3 * i)) & 7]))
    return bytes(out)


def _to565(r, g, b):
    return (min(31, max(0, round(r * 31 / 255))) << 11) | (min(63, max(0, round(g * 63 / 255))) << 5) | \
        min(31, max(0, round(b * 31 / 255)))


def _color_block(px):
    """8 bytes: endpoints along the colours' principal axis (a few power iterations), 4-colour mode."""
    n = len(px)
    mr = sum(p[0] for p in px) / n
    mg = sum(p[1] for p in px) / n
    mb = sum(p[2] for p in px) / n
    crr = cgg = cbb = crg = crb = cgb = 0.0
    for r, g, b, _ in px:
        r, g, b = r - mr, g - mg, b - mb
        crr += r * r; cgg += g * g; cbb += b * b; crg += r * g; crb += r * b; cgb += g * b
    ax = (1.0, 1.0, 1.0)
    for _ in range(4):
        x = crr * ax[0] + crg * ax[1] + crb * ax[2]
        y = crg * ax[0] + cgg * ax[1] + cgb * ax[2]
        z = crb * ax[0] + cgb * ax[1] + cbb * ax[2]
        m = max(abs(x), abs(y), abs(z))
        if m < 1e-9:
            break
        ax = (x / m, y / m, z / m)
    proj = [(p[0] - mr) * ax[0] + (p[1] - mg) * ax[1] + (p[2] - mb) * ax[2] for p in px]
    lo, hi = px[proj.index(min(proj))], px[proj.index(max(proj))]
    c0, c1 = _to565(*hi[:3]), _to565(*lo[:3])
    if c0 < c1:
        c0, c1 = c1, c0
    if c0 == c1:
        return struct.pack('<HHI', c0, c1, 0)
    p0, p1 = _rgb565(c0), _rgb565(c1)
    pal = [p0, p1, tuple((2 * x + y) / 3 for x, y in zip(p0, p1)), tuple((x + 2 * y) / 3 for x, y in zip(p0, p1))]
    idx = 0
    for i, (r, g, b, _) in enumerate(px):
        best = min(range(4), key=lambda k: (r - pal[k][0]) ** 2 + (g - pal[k][1]) ** 2 + (b - pal[k][2]) ** 2)
        idx |= best << (2 * i)
    return struct.pack('<HHI', c0, c1, idx)


def _alpha_block(alphas):
    a0, a1 = max(alphas), min(alphas)
    if a0 == a1:
        return bytes((a0, a1, 0, 0, 0, 0, 0, 0))
    pal = [a0, a1] + [(a0 * (7 - i) + a1 * i) / 7 for i in range(1, 7)]
    bits = 0
    for i, a in enumerate(alphas):
        bits |= min(range(8), key=lambda k: abs(a - pal[k])) << (3 * i)
    return bytes((a0, a1)) + bits.to_bytes(6, 'little')


def bc3_encode(rgba, w, h):
    bw, bh = (w + 3) // 4, (h + 3) // 4
    out = bytearray()
    for by in range(bh):
        for bx in range(bw):
            px = []
            for i in range(16):
                x, y = min(bx * 4 + (i & 3), w - 1), min(by * 4 + (i >> 2), h - 1)
                p = (y * w + x) * 4
                px.append(tuple(rgba[p:p + 4]))
            out += _alpha_block([p[3] for p in px])
            out += _color_block(px)
    return bytes(out)

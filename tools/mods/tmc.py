"""DOAX3 Scarlet models: <name>_TMC.tmc (the model) + <name>_TMCL.tmcl (its textures, vertices and indices).

Both are Koei Tecmo containers (the DOA5 family). A container: tag (8 bytes), u32 version, u32 header size (0x30),
u32 size, u32 chunk count, u32 valid count, u32, u32 offset of the chunk offset table, u32 offset of the chunk size
table, u32 offset of a sub-header; offsets are from the container's start.

TMC chunks used here:
    MdlGeo   one ObjGeo per object ("WGT_face", "WGT_hair", "*T_SHADOW_MODEL", ...): the object's name at +0x60, a
             GeoDecl (its sub-header) and one record per submesh. GeoDecl: one entry per vertex buffer:
               +0x0c buffer (VtxLay / IdxLay index), +0x10 index count, +0x14 vertex count, +0x44 stride,
               +0x48 element count, +0x58 elements of 8 bytes: u16 stream, u16 offset, u16 type, u8 usage, u8 index
               types: 2 float3, 3 float4, 5 ubyte4, 10 half2, 11 half4; usages: 0 position, 1 blend weight,
               2 blend indices (node numbers), 3 normal, 5 texcoord, 6 tangent
             submesh record: +0x00 number, +0x04 material, +0x10 texture slots (0x90 bytes each from +0xe8, the
               texture's number at +8), +0x3c its GeoDecl entry, +0x80 index start, +0x84 index count,
               +0x88 vertex start, +0x8c vertex count (triangle lists, u16 indices from the buffer's start)
    TTSM     textures: TTSL (in it) gives each texture's offset and size in the TMCL's first section; each is a G1T
    VtxLay   vertex buffers: count and total at +0x40, then the offset and size tables (in the TMCL's 2nd section)
    IdxLay   index buffers, likewise (3rd section)
    MdlInfo  one ObjInfo per object: bounding sphere / box / box axes, for the object (+0xe0) and each submesh (+0x50)
    HieLay   the node tree: per node a local matrix (row vectors, translation in the last row) then the parent
    LHeader  the TMCL's layout: +0x40 its header, +0x80 its sections' offsets, +0x90 their sizes
    NodeLay  per node a NodeObj: +0x38 its number, its name from +0x50 to its end ("MOT01_Head", "OPT_Face_jaw",
             "OPT_acs_hair_a1_bone12": unique)
    GlblMtx  per node its world matrix in the bind pose; BnOfsMtx its inverse
TMCL: a 0x80-byte header (u32 3, u32 size), then the sections, 0x100-aligned. Each section starts with u32 count,
u32 size; vertex buffers are packed from +0x10, index buffers from +0x10, 16-byte aligned.

G1T ("GT1G0600"): u32 size at 0x08, u32 table offset at 0x0c, count at 0x10; per texture: u8 mips << 4 | ?, u8 format
(0x59 BC1, 0x5f BC7), u8 log2 height << 4 | log2 width, u8 layers, u32 flags (bit 28: a u32-sized extra header
follows), then the levels, linear (rows of 4x4 blocks), largest first.
"""
import json
import math
import os
import re
import struct

TYPE_SIZE = {2: 12, 3: 16, 5: 4, 10: 4, 11: 8}
U_POS, U_WEIGHT, U_JOINT, U_NORMAL, U_UV, U_TANGENT = 0, 1, 2, 3, 5, 6


# ------------------------------------------------------------------ containers
def kt_tag(d, base):
    return bytes(d[base:base + 8]).split(b'\0')[0].decode('latin1')


def kt_offsets(d, base):
    cnt = struct.unpack_from('<I', d, base + 0x14)[0]
    oo = struct.unpack_from('<I', d, base + 0x20)[0]
    return [base + o if o else 0 for o in struct.unpack_from(f'<{cnt}I', d, base + oo)] if oo and cnt else []


def kt_sizes_off(d, base):
    return struct.unpack_from('<I', d, base + 0x24)[0]


def kt_sub(d, base):
    s = struct.unpack_from('<I', d, base + 0x28)[0]
    return base + s if s else 0


# ------------------------------------------------------------------ the model
class Element:
    def __init__(self, off, typ, usage, index):
        self.off, self.type, self.usage, self.index = off, typ, usage, index


class Decl:
    def __init__(self, d, at):
        self.at = at
        _, _, _, self.buffer, self.index_count, self.vertex_count = struct.unpack_from('<6I', d, at)
        self.stride = struct.unpack_from('<I', d, at + 0x44)[0]
        n = struct.unpack_from('<I', d, at + 0x48)[0]
        self.elements = []
        for k in range(n):
            _, off, typ, usage, idx = struct.unpack_from('<HHHBB', d, at + 0x58 + 8 * k)
            if typ not in TYPE_SIZE:
                raise ValueError(f'vertex element type {typ} (usage {usage}) not handled')
            self.elements.append(Element(off, typ, usage, idx))

    def find(self, usage, index=0):
        for e in self.elements:
            if e.usage == usage and e.index == index:
                return e
        return None


class Submesh:
    def __init__(self, d, at, obj):
        self.at, self.obj = at, obj
        self.number, self.material = struct.unpack_from('<II', d, at)
        self.decl = struct.unpack_from('<I', d, at + 0x3c)[0]
        self.index_start, self.index_count, self.vertex_start, self.vertex_count = struct.unpack_from('<4I', d, at + 0x80)
        nslots = struct.unpack_from('<I', d, at + 0x10)[0]
        self.textures = []
        for k in range(min(nslots, 4)):
            so = struct.unpack_from('<I', d, at + 0x14 + 4 * k)[0]
            self.textures.append(struct.unpack_from('<I', d, at + so + 8)[0] if so else None)

    @property
    def name(self):
        return f'{self.obj.name}#{self.number}'


class Obj:
    def __init__(self, d, at, index):
        self.at, self.index = at, index
        self.name = bytes(d[at + 0x60:at + 0x70]).split(b'\0')[0].decode('latin1')
        gd = kt_sub(d, at)
        assert kt_tag(d, gd) == 'GeoDecl', f'object {index}: no GeoDecl'
        self.decls = [Decl(d, e) for e in kt_offsets(d, gd)]
        self.submeshes = [Submesh(d, s, self) for s in kt_offsets(d, at)]


class Model:
    """A TMC + TMCL pair, parsed; rebuild() writes them back with new geometry / textures."""

    def __init__(self, tmc, tmcl):
        self.tmc, self.tmcl = bytearray(tmc), bytearray(tmcl)
        d = self.tmc
        assert kt_tag(d, 0) == 'TMC', 'not a TMC model'
        self.chunks = {}
        for o in kt_offsets(d, 0):
            if o:
                self.chunks.setdefault(kt_tag(d, o), o)
        lh = self.chunks['LHeader']
        self.sec_off = list(struct.unpack_from('<3I', d, lh + 0x80))
        self.sec_size = list(struct.unpack_from('<3I', d, lh + 0x90))
        self.objects = [Obj(d, o, i) for i, o in enumerate(kt_offsets(d, self.chunks['MdlGeo']))]
        self.vtx = self._layout('VtxLay')
        self.idx = self._layout('IdxLay')
        # nodes
        raw = [bytes(d[o + 0x50:o + struct.unpack_from('<I', d, o + 0x10)[0]]).split(b'\0')[0].decode('latin1')
               for o in kt_offsets(d, self.chunks['NodeLay'])]
        # unique names: a name the model repeats or lacks gets its node number, "node@37"; import reads the number
        # back (the game's own models have none: every node is named, once)
        count = {}
        for nm in raw:
            count[nm] = count.get(nm, 0) + 1
        self.node_names = [nm if nm and count[nm] == 1 else f'{nm or "node"}@{i}' for i, nm in enumerate(raw)]
        hl = kt_offsets(d, self.chunks['HieLay'])
        self.local = [struct.unpack_from('<16f', d, o) for o in hl]
        self.parent = [struct.unpack_from('<i', d, o + 0x40)[0] for o in hl]
        self.world = [struct.unpack_from('<16f', d, o) for o in kt_offsets(d, self.chunks['GlblMtx'])]
        self.inv_bind = [struct.unpack_from('<16f', d, o) for o in kt_offsets(d, self.chunks['BnOfsMtx'])]
        # each object's node: a NodeObj with a block (its offset in the u32 after the name, padded to 16 bytes): u32
        # object, u32 palette size, u32 node, ..., the palette at +0x60. The object's vertices are in that node's space
        # (the face's eyes relative to the head, the hair's depth layer in the face model's space), and their bone
        # numbers index its palette, not the node list
        for i, o in enumerate(kt_offsets(d, self.chunks['NodeLay'])):
            size = struct.unpack_from('<I', d, o + 0x10)[0]
            at = 0x50 + (len(raw[i]) + 1 + 15) // 16 * 16
            blk = struct.unpack_from('<I', d, o + at)[0] if at + 4 <= size else 0
            if blk and blk + 0x60 <= size:
                oi, n = struct.unpack_from('<II', d, o + blk)
                if oi < len(self.objects) and blk + 0x60 + 4 * n <= size:
                    obj = self.objects[oi]
                    obj.node, obj.palette = i, list(struct.unpack_from(f'<{n}I', d, o + blk + 0x60))
        for obj in self.objects:
            if not hasattr(obj, 'node'):
                raise ValueError(f'{obj.name}: no node names it (no bone palette)')
        # textures (TTSM > TTSL: offsets and sizes in the TMCL's first section)
        self.textures = []
        tt = self.chunks.get('TTSM')
        if tt:
            for o in kt_offsets(d, tt) + [kt_sub(d, tt)]:
                if o and kt_tag(d, o) == 'TTSL':
                    n = struct.unpack_from('<I', d, o + 0x14)[0]
                    offs = struct.unpack_from(f'<{n}I', d, o + struct.unpack_from('<I', d, o + 0x20)[0])
                    sizes = struct.unpack_from(f'<{n}I', d, o + kt_sizes_off(d, o))
                    self.textures = [(self.sec_off[0] + a, s) for a, s in zip(offs, sizes)]

    def obj_matrix(self, obj):
        """The object's vertex space -> the model's (row vectors: p' = p * M)."""
        return self.world[obj.node]

    def _layout(self, tag):
        c = self.chunks[tag]
        n = struct.unpack_from('<I', self.tmc, c + 0x14)[0]
        offs = list(struct.unpack_from(f'<{n}I', self.tmc, c + struct.unpack_from('<I', self.tmc, c + 0x20)[0]))
        sizes = list(struct.unpack_from(f'<{n}I', self.tmc, c + kt_sizes_off(self.tmc, c)))
        return {'chunk': c, 'offs': offs, 'sizes': sizes}

    # ---- reading geometry
    def vertex_bytes(self, decl):
        a = self.sec_off[1] + self.vtx['offs'][decl.buffer]
        return self.tmcl[a:a + decl.stride * decl.vertex_count]

    def indices(self, decl):
        a = self.sec_off[2] + self.idx['offs'][decl.buffer]
        return struct.unpack_from(f'<{decl.index_count}H', self.tmcl, a)

    def g1t(self, i):
        a, s = self.textures[i]
        return bytes(self.tmcl[a:a + s])


def xf_point(p, m):
    """p * m (row vector, m 16 floats with the translation in the last row)."""
    return tuple(p[0] * m[c] + p[1] * m[4 + c] + p[2] * m[8 + c] + m[12 + c] for c in range(3))


def xf_dir(v, m):
    return tuple(v[0] * m[c] + v[1] * m[4 + c] + v[2] * m[8 + c] for c in range(3))


def mat_inverse(m):
    """The inverse of an affine 4x4 (row vectors)."""
    a = [[m[r * 4 + c] for c in range(3)] for r in range(3)]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
           a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    inv = [[0.0] * 3 for _ in range(3)]
    for r in range(3):
        for c in range(3):
            rows = [x for x in range(3) if x != c]
            cols = [x for x in range(3) if x != r]
            minor = a[rows[0]][cols[0]] * a[rows[1]][cols[1]] - a[rows[0]][cols[1]] * a[rows[1]][cols[0]]
            inv[r][c] = (-1) ** (r + c) * minor / det
    t = [-sum(m[12 + k] * inv[k][c] for k in range(3)) for c in range(3)]
    return [inv[0][0], inv[0][1], inv[0][2], 0.0, inv[1][0], inv[1][1], inv[1][2], 0.0,
            inv[2][0], inv[2][1], inv[2][2], 0.0, t[0], t[1], t[2], 1.0]


def read_element(buf, o, e):
    t = e.type
    if t == 2:
        return struct.unpack_from('<3f', buf, o + e.off) + (1.0,)
    if t == 3:
        return struct.unpack_from('<4f', buf, o + e.off)
    if t == 5:
        return tuple(buf[o + e.off:o + e.off + 4])
    if t == 10:
        return struct.unpack_from('<2e', buf, o + e.off) + (0.0, 0.0)
    if t == 11:
        return struct.unpack_from('<4e', buf, o + e.off)


def write_element(buf, o, e, v):
    t = e.type
    if t == 2:
        struct.pack_into('<3f', buf, o + e.off, *v[:3])
    elif t == 3:
        struct.pack_into('<4f', buf, o + e.off, *v[:4])
    elif t == 5:
        buf[o + e.off:o + e.off + 4] = bytes(max(0, min(255, int(x))) for x in v[:4])
    elif t == 10:
        struct.pack_into('<2e', buf, o + e.off, *v[:2])
    elif t == 11:
        struct.pack_into('<4e', buf, o + e.off, *v[:4])


# ------------------------------------------------------------------ textures: G1T, BC1 / BC7
G1T_FORMATS = {0x59: ('BC1', 8), 0x5f: ('BC7', 16)}


def g1t_parse(g):
    """(format, width, height, mips, layers, data offset) of a one-texture G1T."""
    assert g[:4] == b'GT1G', 'not a G1T texture'
    toff = struct.unpack_from('<I', g, 0x0c)[0]
    to = toff + struct.unpack_from('<I', g, toff)[0]
    b0, fmt, dxdy, layers, flags = struct.unpack_from('<BBBBI', g, to)
    ext = struct.unpack_from('<I', g, to + 8)[0] if flags & 0x10000000 else 0
    return fmt, 1 << (dxdy & 15), 1 << (dxdy >> 4), max(1, b0 >> 4), max(1, layers), to + 8 + ext


def level_sizes(fmt, w, h, mips):
    bpb = G1T_FORMATS[fmt][1]
    out = []
    for _ in range(mips):
        out.append((w, h, max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * bpb))
        w, h = max(1, w // 2), max(1, h // 2)
    return out


def g1t_decode(g):
    """(w, h, rgba of the largest level, first layer) or None if the format isn't handled."""
    fmt, w, h, mips, layers, at = g1t_parse(g)
    if fmt == 0x59:
        import kslt
        return w, h, bc1_decode(g[at:], w, h)
    if fmt == 0x5f:
        try:
            import texture2ddecoder  # (optional: pip install texture2ddecoder, for the game's BC7 textures)
        except ImportError:
            return None
        bgra = texture2ddecoder.decode_bc7(bytes(g[at:at + level_sizes(fmt, w, h, 1)[0][2]]), w, h)
        rgba = bytearray(bgra)
        rgba[0::4], rgba[2::4] = bgra[2::4], bgra[0::4]
        return w, h, bytes(rgba)
    return None


def bc1_decode(data, w, h):
    import kslt
    bw, bh = (w + 3) // 4, (h + 3) // 4
    out = bytearray(w * h * 4)
    for by in range(bh):
        for bx in range(bw):
            o = (by * bw + bx) * 8
            c0, c1, ci = struct.unpack_from('<HHI', data, o)
            p0, p1 = kslt._rgb565(c0), kslt._rgb565(c1)
            if c0 > c1:
                cols = [p0 + (255,), p1 + (255,), tuple((2 * x + y) // 3 for x, y in zip(p0, p1)) + (255,),
                        tuple((x + 2 * y) // 3 for x, y in zip(p0, p1)) + (255,)]
            else:
                cols = [p0 + (255,), p1 + (255,), tuple((x + y) // 2 for x, y in zip(p0, p1)) + (255,), (0, 0, 0, 0)]
            for i in range(16):
                x, y = bx * 4 + (i & 3), by * 4 + (i >> 2)
                if x < w and y < h:
                    p = (y * w + x) * 4
                    out[p:p + 4] = bytes(cols[(ci >> (2 * i)) & 3])
    return bytes(out)


def bc1_encode(rgba, w, h):
    import kslt
    out = bytearray()
    for by in range((h + 3) // 4):
        for bx in range((w + 3) // 4):
            px = []
            for i in range(16):
                x, y = min(bx * 4 + (i & 3), w - 1), min(by * 4 + (i >> 2), h - 1)
                p = (y * w + x) * 4
                px.append(tuple(rgba[p:p + 4]))
            out += kslt._color_block(px)
    return bytes(out)


_BC7_W4 = [0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64]


def _bc7_mode6_block(px):
    """16 bytes: BC7 mode 6 (one subset, RGBA endpoints 7 bits + a p-bit each, 4-bit indices)."""
    mean = [sum(p[c] for p in px) / 16 for c in range(4)]
    cov = [[sum((p[a] - mean[a]) * (p[b] - mean[b]) for p in px) for b in range(4)] for a in range(4)]
    ax = [1.0, 1.0, 1.0, 1.0]
    for _ in range(6):
        v = [sum(cov[a][b] * ax[b] for b in range(4)) for a in range(4)]
        m = max(abs(x) for x in v)
        if m < 1e-9:
            break
        ax = [x / m for x in v]
    proj = [sum((p[c] - mean[c]) * ax[c] for c in range(4)) for p in px]
    lo, hi = px[proj.index(min(proj))], px[proj.index(max(proj))]

    def quant(col):  # 7 bits + shared p-bit: the p-bit that fits best
        best = None
        for pb in (0, 1):
            q = [max(0, min(127, round((c - pb) / 2))) for c in col]
            err = sum((((x << 1) | pb) - c) ** 2 for x, c in zip(q, col))
            if best is None or err < best[0]:
                best = (err, q, pb)
        return best[1], best[2]
    q0, p0 = quant(lo)
    q1, p1 = quant(hi)
    e0 = [(x << 1) | p0 for x in q0]
    e1 = [(x << 1) | p1 for x in q1]
    pal = [[((64 - w) * a + w * b + 32) >> 6 for a, b in zip(e0, e1)] for w in _BC7_W4]
    idx = [min(range(16), key=lambda k: sum((p[c] - pal[k][c]) ** 2 for c in range(4))) for p in px]
    err = sum(sum((p[c] - pal[i][c]) ** 2 for c in range(4)) for p, i in zip(px, idx))
    if idx[0] >= 8:  # (the anchor's top bit is implied 0: swap the endpoints)
        q0, q1, p0, p1 = q1, q0, p1, p0
        idx = [15 - i for i in idx]
    bits = 1 << 6  # mode 6
    pos = 7
    for c in range(4):
        for q in (q0[c], q1[c]):
            bits |= q << pos
            pos += 7
    bits |= p0 << pos | p1 << (pos + 1)
    pos += 2
    for i, v in enumerate(idx):
        n = 3 if i == 0 else 4
        bits |= v << pos
        pos += n
    return bits.to_bytes(16, 'little'), err


_BC7_W2 = (0, 21, 43, 64)


def _bc7_mode5_block(px):
    """(16 bytes, squared error): BC7 mode 5 - RGB endpoints 7 bits with 2-bit indices, and alpha apart (8-bit
    endpoints, its own 2-bit indices). Alpha that varies within a block (hair strands, lash tips) keeps its shape,
    where mode 6's shared indices make it blocky."""
    mean = [sum(p[c] for p in px) / 16 for c in range(3)]
    cov = [[sum((p[a] - mean[a]) * (p[b] - mean[b]) for p in px) for b in range(3)] for a in range(3)]
    ax = [1.0, 1.0, 1.0]
    for _ in range(6):
        v = [sum(cov[a][b] * ax[b] for b in range(3)) for a in range(3)]
        m = max(abs(x) for x in v)
        if m < 1e-9:
            break
        ax = [x / m for x in v]
    proj = [sum((p[c] - mean[c]) * ax[c] for c in range(3)) for p in px]
    lo, hi = px[proj.index(min(proj))], px[proj.index(max(proj))]
    q0 = [max(0, min(127, round(c / 2))) for c in lo[:3]]
    q1 = [max(0, min(127, round(c / 2))) for c in hi[:3]]
    e0 = [(x << 1) | (x >> 6) for x in q0]
    e1 = [(x << 1) | (x >> 6) for x in q1]
    pal = [[((64 - w) * a + w * b + 32) >> 6 for a, b in zip(e0, e1)] for w in _BC7_W2]
    ci = [min(range(4), key=lambda k: sum((p[c] - pal[k][c]) ** 2 for c in range(3))) for p in px]
    a0, a1 = min(p[3] for p in px), max(p[3] for p in px)
    apal = [((64 - w) * a0 + w * a1 + 32) >> 6 for w in _BC7_W2]
    ai = [min(range(4), key=lambda k: (p[3] - apal[k]) ** 2) for p in px]
    err = sum(sum((p[c] - pal[i][c]) ** 2 for c in range(3)) + (p[3] - apal[j]) ** 2 for p, i, j in zip(px, ci, ai))
    if ci[0] >= 2:  # (each anchor's top bit is implied 0: swap that pair of endpoints)
        q0, q1 = q1, q0
        ci = [3 - i for i in ci]
    if ai[0] >= 2:
        a0, a1 = a1, a0
        ai = [3 - i for i in ai]
    bits = 1 << 5  # mode 5, rotation 0
    pos = 8
    for c in range(3):
        for q in (q0[c], q1[c]):
            bits |= q << pos
            pos += 7
    bits |= a0 << pos | a1 << (pos + 8)
    pos += 16
    for ind in (ci, ai):
        for i, v in enumerate(ind):
            bits |= v << pos
            pos += 1 if i == 0 else 2
    return bits.to_bytes(16, 'little'), err


def bc7_encode(rgba, w, h):
    out = bytearray()
    for by in range((h + 3) // 4):
        for bx in range((w + 3) // 4):
            px = []
            for i in range(16):
                x, y = min(bx * 4 + (i & 3), w - 1), min(by * 4 + (i >> 2), h - 1)
                p = (y * w + x) * 4
                px.append(tuple(rgba[p:p + 4]))
            b6, e6 = _bc7_mode6_block(px)
            if any(p[3] != px[0][3] for p in px):  # (alpha varies: mode 5 too, the closer one)
                b5, e5 = _bc7_mode5_block(px)
                if e5 < e6:
                    b6 = b5
            out += b6
    return bytes(out)


def downsample(rgba, w, h):
    nw, nh = max(1, w // 2), max(1, h // 2)
    out = bytearray(nw * nh * 4)
    for y in range(nh):
        for x in range(nw):
            for c in range(4):
                s = 0
                for dy in (0, 1):
                    for dx in (0, 1):
                        sx, sy = min(w - 1, 2 * x + dx), min(h - 1, 2 * y + dy)
                        s += rgba[(sy * w + sx) * 4 + c]
                out[(y * nw + x) * 4 + c] = (s + 2) // 4
    return bytes(out), nw, nh


def g1t_replace(g, rgba, alpha_ref=None):
    """The G1T g with its image replaced by rgba (its size; all mips and layers rebuilt), same format and size.
    alpha_ref (0-255): each mip's alpha scaled so as much of it passes that cutoff as in the full-size image; averaged
    down as it is, alpha-cut hair thins out and vanishes with distance."""
    fmt, w, h, mips, layers, at = g1t_parse(g)
    if fmt not in G1T_FORMATS:
        raise ValueError(f'texture format {fmt:#x} not handled')
    enc = bc1_encode if fmt == 0x59 else bc7_encode
    data = bytearray()
    img, cw, ch = rgba, w, h
    target = sum(1 for x in rgba[3::4] if x >= alpha_ref) / (w * h) if alpha_ref else 0
    for lw, lh, size in level_sizes(fmt, w, h, mips):
        lvl = enc(img, cw, ch)
        assert len(lvl) == size, (len(lvl), size)
        data += lvl
        img, cw, ch = downsample(img, cw, ch)
        if target:
            al = sorted(img[3::4])
            v = al[min(len(al) - 1, int((1 - target) * len(al)))]
            if 0 < v < alpha_ref:
                table = bytes(min(255, round(x * alpha_ref / v)) for x in range(256))
                img = bytearray(img)
                img[3::4] = img[3::4].translate(table)
                img = bytes(img)
    data = bytes(data) * layers
    out = bytearray(g)
    out[at:at + len(data)] = data
    return bytes(out)


# ------------------------------------------------------------------ glTF export
def _align(b, n=4):
    b += b'\0' * ((-len(b)) % n)


def export_gltf(model, out_dir, base, textures=True):
    """The model as out_dir/base.gltf + base.bin (+ tex_NN.png): every submesh a mesh named "<object>#<number>", the
    node tree as a skin (inverse bind matrices from the model), up to 12 weights a vertex (JOINTS_0-2 / WEIGHTS_0-2)."""
    import imgio
    os.makedirs(out_dir, exist_ok=True)
    binbuf = bytearray()
    views, accessors = [], []

    def view(data, target=None):
        _align(binbuf)
        views.append({'buffer': 0, 'byteOffset': len(binbuf), 'byteLength': len(data), **({'target': target} if target else {})})
        binbuf.extend(data)
        return len(views) - 1

    def accessor(data, ctype, count, typ, target=None, minmax=None):
        a = {'bufferView': view(data, target), 'componentType': ctype, 'count': count, 'type': typ}
        if minmax:
            a['min'], a['max'] = minmax
        accessors.append(a)
        return len(accessors) - 1

    n = len(model.node_names)
    nodes = [{'name': model.node_names[i], 'matrix': [float(x) for x in model.local[i]]} for i in range(n)]
    for i, p in enumerate(model.parent):
        if 0 <= p < n:
            nodes[p].setdefault('children', []).append(i)
    roots = [i for i in range(n) if not (0 <= model.parent[i] < n)]
    ibm = accessor(b''.join(struct.pack('<16f', *m) for m in model.inv_bind), 5126, n, 'MAT4')
    skins = [{'joints': list(range(n)), 'inverseBindMatrices': ibm, 'skeleton': roots[0]}]

    images, textures_json, materials, meshes = [], [], [], []
    tex_index = {}
    if textures:
        for i in range(len(model.textures)):
            dec = g1t_decode(model.g1t(i))
            fmt, w, h, mips, layers, _ = g1t_parse(model.g1t(i))
            if dec:
                imgio.save_png(os.path.join(out_dir, f'tex_{i:02d}.png'), dec[0], dec[1], dec[2])
                images.append({'uri': f'tex_{i:02d}.png', 'name': f'tex_{i:02d}'})
                textures_json.append({'source': len(images) - 1})
                tex_index[i] = len(textures_json) - 1
    mesh_nodes = []
    for obj in model.objects:
        for sm in obj.submeshes:
            if not sm.index_count:
                continue
            decl = obj.decls[sm.decl]
            vb = model.vertex_bytes(decl)
            ix = model.indices(decl)[sm.index_start:sm.index_start + sm.index_count]
            v0, vn = sm.vertex_start, sm.vertex_count
            if min(ix) < v0 or max(ix) >= v0 + vn:
                raise ValueError(f'{sm.name}: indices outside its vertex range')
            pos, nrm, tan, uv0, uv1 = bytearray(), bytearray(), bytearray(), bytearray(), bytearray()
            joints, weights = [bytearray() for _ in range(3)], [bytearray() for _ in range(3)]
            ep, en, et = decl.find(U_POS), decl.find(U_NORMAL), decl.find(U_TANGENT)
            eu0, eu1 = decl.find(U_UV, 0), decl.find(U_UV, 1)
            ej = [decl.find(U_JOINT, k) for k in range(3)]
            ew = [decl.find(U_WEIGHT, k) for k in range(3)]
            om, pal = model.obj_matrix(obj), obj.palette  # (model space, node numbers)
            lo, hi = [1e30] * 3, [-1e30] * 3
            for v in range(v0, v0 + vn):
                o = v * decl.stride
                p = xf_point(read_element(vb, o, ep)[:3], om)
                lo, hi = [min(a, b) for a, b in zip(lo, p)], [max(a, b) for a, b in zip(hi, p)]
                pos += struct.pack('<3f', *p)
                if en:
                    nx, ny, nz = xf_dir(read_element(vb, o, en)[:3], om)
                    ln = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
                    nrm += struct.pack('<3f', nx / ln, ny / ln, nz / ln)
                if et:
                    t4 = read_element(vb, o, et)
                    tx, ty, tz = xf_dir(t4[:3], om)
                    ln = math.sqrt(tx * tx + ty * ty + tz * tz) or 1.0
                    tan += struct.pack('<4f', tx / ln, ty / ln, tz / ln, 1.0 if t4[3] >= 0 else -1.0)
                if eu0:
                    uv0 += struct.pack('<2f', *read_element(vb, o, eu0)[:2])
                if eu1:
                    uv1 += struct.pack('<2f', *read_element(vb, o, eu1)[:2])
                ws = [read_element(vb, o, e) if e else (0, 0, 0, 0) for e in ew]
                total = sum(sum(w) for w in ws) or 1.0
                for k in range(3):
                    if ej[k] and ew[k]:
                        joints[k] += struct.pack('<4H', *[pal[j] if j < len(pal) else 0 for j in read_element(vb, o, ej[k])])
                        weights[k] += struct.pack('<4f', *[w / total for w in ws[k]])
            attrs = {'POSITION': accessor(pos, 5126, vn, 'VEC3', 34962, (lo, hi))}
            if en:
                attrs['NORMAL'] = accessor(nrm, 5126, vn, 'VEC3', 34962)
            if et:
                attrs['TANGENT'] = accessor(tan, 5126, vn, 'VEC4', 34962)
            if eu0:
                attrs['TEXCOORD_0'] = accessor(uv0, 5126, vn, 'VEC2', 34962)
            if eu1:
                attrs['TEXCOORD_1'] = accessor(uv1, 5126, vn, 'VEC2', 34962)
            for k in range(3):
                if joints[k]:
                    attrs[f'JOINTS_{k}'] = accessor(joints[k], 5123, vn, 'VEC4', 34962)
                    attrs[f'WEIGHTS_{k}'] = accessor(weights[k], 5126, vn, 'VEC4', 34962)
            idx = struct.pack(f'<{len(ix)}H', *[i - v0 for i in ix])
            mat = {'name': f'{sm.name} material {sm.material}', 'doubleSided': True}
            if sm.textures and sm.textures[0] in tex_index:
                mat['pbrMetallicRoughness'] = {'baseColorTexture': {'index': tex_index[sm.textures[0]]}, 'metallicFactor': 0}
            materials.append(mat)
            meshes.append({'name': sm.name, 'primitives': [{'attributes': attrs, 'indices': accessor(idx, 5123, len(ix), 'SCALAR', 34963),
                                                             'material': len(materials) - 1}],
                           'extras': {'tmc_object': obj.index, 'tmc_submesh': sm.number, 'textures': sm.textures}})
            nodes.append({'name': sm.name, 'mesh': len(meshes) - 1, 'skin': 0})
            mesh_nodes.append(len(nodes) - 1)
    gltf = {'asset': {'version': '2.0', 'generator': 'DOAX3 Recomp tools/mods/tmc.py'},
            'scene': 0, 'scenes': [{'nodes': roots + mesh_nodes}], 'nodes': nodes, 'skins': skins, 'meshes': meshes,
            'materials': materials, 'accessors': accessors, 'bufferViews': views,
            'buffers': [{'uri': base + '.bin', 'byteLength': len(binbuf)}]}
    if images:
        gltf['images'], gltf['textures'] = images, textures_json
    with open(os.path.join(out_dir, base + '.bin'), 'wb') as f:
        f.write(binbuf)
    with open(os.path.join(out_dir, base + '.gltf'), 'w') as f:
        json.dump(gltf, f, indent=1)
    return len(meshes)


# ------------------------------------------------------------------ glTF import
def _load_gltf(path):
    """(json, binary buffers) of a .gltf (+ .bin) or .glb."""
    raw = open(path, 'rb').read()
    if raw[:4] == b'glTF':
        jl = struct.unpack_from('<I', raw, 12)[0]
        j = json.loads(raw[20:20 + jl])
        bins = [raw[20 + jl + 8:]] if len(raw) > 20 + jl else []
        return j, bins
    j = json.loads(raw)
    bins = []
    for b in j.get('buffers', []):
        uri = b.get('uri', '')
        if uri.startswith('data:'):
            import base64
            bins.append(base64.b64decode(uri.split(',', 1)[1]))
        else:
            bins.append(open(os.path.join(os.path.dirname(path), uri), 'rb').read())
    return j, bins


def _read_accessor(j, bins, i):
    a = j['accessors'][i]
    bv = j['bufferViews'][a['bufferView']]
    comps = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[a['type']]
    fmt = {5126: 'f', 5125: 'I', 5123: 'H', 5121: 'B', 5122: 'h', 5120: 'b'}[a['componentType']]
    size = struct.calcsize('<' + fmt)
    stride = bv.get('byteStride', size * comps)
    base = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    buf = bins[bv['buffer']]
    out = []
    for k in range(a['count']):
        vals = struct.unpack_from(f'<{comps}{fmt}', buf, base + k * stride)
        if a.get('normalized') and fmt != 'f':
            m = {'B': 255, 'H': 65535, 'b': 127, 'h': 32767}[fmt]
            vals = tuple(v / m for v in vals)
        out.append(vals)
    return out


def _mat_mul(a, b):  # column-major 4x4 (glTF)
    return [sum(a[r + 4 * k] * b[k + 4 * c] for k in range(4)) for c in range(4) for r in range(4)]


def _node_matrix(nd):
    if 'matrix' in nd:
        return list(nd['matrix'])
    t = nd.get('translation', [0, 0, 0])
    x, y, z, w = nd.get('rotation', [0, 0, 0, 1])
    s = nd.get('scale', [1, 1, 1])
    r = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
         2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
         2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0, 0, 0, 0, 1]
    for c in range(3):
        for k in range(3):
            r[4 * c + k] *= s[c]
    r[12], r[13], r[14] = t
    return r


def read_gltf_parts(path, model):
    """{(object index, submesh number): [part]}: each part a dict of positions, normals, tangents, uv0, uv1, influences
    (per vertex [(node, weight)]) and triangles; meshes named "<object>#<number>" (anything after is ignored)."""
    j, bins = _load_gltf(path)
    by_name = {o.name: o for o in model.objects}
    node_index = {nm: i for i, nm in enumerate(model.node_names)}
    nodes = j.get('nodes', [])
    parents = {}
    for i, nd in enumerate(nodes):
        for c in nd.get('children', []):
            parents[c] = i

    def world(i):
        m = _node_matrix(nodes[i])
        while i in parents:
            i = parents[i]
            m = _mat_mul(_node_matrix(nodes[i]), m)
        return m
    parts, unknown = {}, []
    for ni, nd in enumerate(nodes):
        if 'mesh' not in nd:
            continue
        mesh = j['meshes'][nd['mesh']]
        name = nd.get('name') or mesh.get('name', '')
        m = re.match(r'^(.+?)#(\d+)', name) or re.match(r'^(.+?)#(\d+)', mesh.get('name', ''))
        if not m or m.group(1) not in by_name:
            unknown.append(name)
            continue
        obj = by_name[m.group(1)]
        key = (obj.index, int(m.group(2)))
        if key[1] not in [s.number for s in obj.submeshes]:
            raise SystemExit(f'{name}: {obj.name} has no submesh {key[1]} (it has {[s.number for s in obj.submeshes]})')
        skin = j['skins'][nd['skin']] if 'skin' in nd else None
        xf = None if skin else world(ni)  # (a skinned mesh's node transform is ignored, as glTF says)
        joint_map, foreign = [], []
        if skin:
            for jn in skin['joints']:
                nm = nodes[jn].get('name', '')
                mi = re.search(r'@(\d+)$', nm)  # ("bone12@37": node 37, a name the model repeats)
                if nm not in node_index and mi and int(mi.group(1)) < len(model.node_names):
                    nm = model.node_names[int(mi.group(1))]
                if nm not in node_index:
                    foreign.append(nm)
                joint_map.append(node_index.get(nm))
        for prim in mesh['primitives']:
            if prim.get('mode', 4) != 4:
                raise SystemExit(f'{name}: only triangles can be imported')
            at = prim['attributes']
            pos = _read_accessor(j, bins, at['POSITION'])
            nv = len(pos)
            get = lambda k: _read_accessor(j, bins, at[k]) if k in at else None
            nrm, tan, uv0, uv1 = get('NORMAL'), get('TANGENT'), get('TEXCOORD_0'), get('TEXCOORD_1')
            col = get('COLOR_0')
            off = nd.get('extras', {}).get('offset')  # (the mesh moved, also when skinned: one mesh, several models)
            if off:
                pos = [(q[0] + off[0], q[1] + off[1], q[2] + off[2]) for q in pos]
            infl = [[] for _ in range(nv)]
            for k in range(8):
                if foreign or f'JOINTS_{k}' not in at:  # (bones the model hasn't: weights copied instead, import_parts)
                    break
                js, ws = get(f'JOINTS_{k}'), get(f'WEIGHTS_{k}')
                for v in range(nv):
                    for jj, w in zip(js[v], ws[v]):
                        if w > 0:
                            infl[v].append((joint_map[int(jj)] if joint_map else int(jj), w))
            if xf:
                tp = lambda p: tuple(sum(xf[r + 4 * c] * (list(p) + [1.0])[c] for c in range(4)) for r in range(3))
                tn = lambda n: tuple(sum(xf[r + 4 * c] * n[c] for c in range(3)) for r in range(3))
                pos = [tp(p) for p in pos]
                if nrm:
                    nrm = [tn(n) for n in nrm]
                if tan:
                    tan = [tn(t[:3]) + (t[3],) for t in tan]
            if 'indices' in prim:
                tri = [i[0] for i in _read_accessor(j, bins, prim['indices'])]
            else:
                tri = list(range(nv))
            parts.setdefault(key, []).append({'name': name, 'pos': pos, 'nrm': nrm, 'tan': tan, 'uv0': uv0, 'uv1': uv1,
                                              'infl': infl, 'tri': tri, 'foreign': foreign, 'model_space': True,
                                              'hold': [c[0] for c in col] if col else None})
    return parts, unknown


class _Nearest:
    """The original vertices of an object (or the model) by position: nearest() gives a vertex's bone influences."""

    def __init__(self, model, objects):
        self.pts = []
        for obj in objects:
            for decl in obj.decls:
                vb = model.vertex_bytes(decl)
                ep = decl.find(U_POS)
                js = sorted([e for e in decl.elements if e.usage == U_JOINT], key=lambda e: e.index)
                ws = sorted([e for e in decl.elements if e.usage == U_WEIGHT], key=lambda e: e.index)
                for v in range(decl.vertex_count):
                    o = v * decl.stride
                    p = read_element(vb, o, ep)[:3]
                    infl = []
                    for ej, ew in zip(js, ws):
                        infl += [(j, w) for j, w in zip(read_element(vb, o, ej), read_element(vb, o, ew)) if w > 0]
                    self.pts.append((p, infl))
        self.tree = self._build(list(range(len(self.pts))), 0)

    def _build(self, idx, depth):  # (a k-d tree: (axis, split, below, above) or a leaf's point list)
        if len(idx) <= 8:
            return idx
        ax = depth % 3
        idx.sort(key=lambda i: self.pts[i][0][ax])
        m = len(idx) // 2
        return ax, self.pts[idx[m]][0][ax], self._build(idx[:m], depth + 1), self._build(idx[m:], depth + 1)

    def _knn(self, p, k):
        import heapq
        best = []  # (-distance, point): the k nearest so far, farthest first

        def visit(n):
            if isinstance(n, list):
                for i in n:
                    d = math.dist(p, self.pts[i][0])
                    if len(best) < k:
                        heapq.heappush(best, (-d, i))
                    elif d < -best[0][0]:
                        heapq.heapreplace(best, (-d, i))
                return
            ax, split, lo, hi = n
            diff = p[ax] - split
            visit(lo if diff < 0 else hi)
            if len(best) < k or abs(diff) < -best[0][0]:
                visit(hi if diff < 0 else lo)
        visit(self.tree)
        return sorted((-d, i) for d, i in best)

    def nearest(self, p, k=4):
        """The influences of the k nearest original vertices, blended by 1 / distance^2 (smoother than the single
        nearest one: no steps where the nearest vertex changes)."""
        if not self.pts:
            return []
        found = self._knn(p, k)
        if found[0][0] < 1e-6:
            return self.pts[found[0][1]][1]
        acc = {}
        for d, i in found:
            for j, w in self.pts[i][1]:
                acc[j] = acc.get(j, 0.0) + w / (d * d)
        total = sum(acc.values()) or 1.0
        return [(j, w / total) for j, w in acc.items()]


def _tangents(pos, nrm, uv, tri):
    acc = [[0.0, 0.0, 0.0] for _ in pos]
    sgn = [[0.0, 0.0, 0.0] for _ in pos]
    for t in range(0, len(tri) - 2, 3):
        a, b, c = tri[t:t + 3]
        e1 = [pos[b][k] - pos[a][k] for k in range(3)]
        e2 = [pos[c][k] - pos[a][k] for k in range(3)]
        du1, dv1 = uv[b][0] - uv[a][0], uv[b][1] - uv[a][1]
        du2, dv2 = uv[c][0] - uv[a][0], uv[c][1] - uv[a][1]
        r = du1 * dv2 - du2 * dv1
        if abs(r) < 1e-12:
            continue
        r = 1.0 / r
        t3 = [(e1[k] * dv2 - e2[k] * dv1) * r for k in range(3)]
        b3 = [(e2[k] * du1 - e1[k] * du2) * r for k in range(3)]
        for v in (a, b, c):
            for k in range(3):
                acc[v][k] += t3[k]
                sgn[v][k] += b3[k]
    out = []
    for v in range(len(pos)):
        n = nrm[v]
        t = acc[v]
        d = sum(t[k] * n[k] for k in range(3))
        t = [t[k] - n[k] * d for k in range(3)]
        ln = math.sqrt(sum(x * x for x in t)) or 1.0
        t = [x / ln for x in t]
        cr = [n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2], n[0] * t[1] - n[1] * t[0]]
        w = 1.0 if sum(cr[k] * sgn[v][k] for k in range(3)) >= 0 else -1.0
        out.append(tuple(t) + (w,))
    return out


def _normals(pos, tri):
    acc = [[0.0, 0.0, 0.0] for _ in pos]
    for t in range(0, len(tri) - 2, 3):
        a, b, c = tri[t:t + 3]
        e1 = [pos[b][k] - pos[a][k] for k in range(3)]
        e2 = [pos[c][k] - pos[a][k] for k in range(3)]
        n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
        for v in (a, b, c):
            for k in range(3):
                acc[v][k] += n[k]
    return [tuple(x / (math.sqrt(sum(y * y for y in a)) or 1.0) for x in a) for a in acc]


def _vertex(decl, part, v, buf, o, stats):
    """Writes vertex v of part into buf at o in decl's layout."""
    for e in decl.elements:
        if e.usage == U_POS:
            write_element(buf, o, e, tuple(part['pos'][v]) + (1.0,))
        elif e.usage == U_NORMAL:
            write_element(buf, o, e, tuple(part['nrm'][v]) + (0.0,))
        elif e.usage == U_TANGENT:
            write_element(buf, o, e, tuple(part['tan'][v]))
        elif e.usage == U_UV:
            uv = part['uv1' if e.index == 1 and part['uv1'] else 'uv0']
            u, w = (uv[v][0], uv[v][1]) if uv else (0.0, 0.0)
            write_element(buf, o, e, (u, w, u, w))
    groups = sorted([e for e in decl.elements if e.usage == U_JOINT], key=lambda e: e.index)
    wgroups = sorted([e for e in decl.elements if e.usage == U_WEIGHT], key=lambda e: e.index)
    cap = 4 * min(len(groups), len(wgroups))
    infl = part['infl'][v]
    if len(infl) > cap:  # (the strongest, renormalized)
        infl = sorted(infl, key=lambda x: -x[1])[:cap]
        stats['trimmed'] += 1
    total = sum(w for _, w in infl) or 1.0
    infl = [(j, w / total) for j, w in infl] + [(0, 0.0)] * (cap - len(infl))
    for k, (ej, ew) in enumerate(zip(groups, wgroups)):
        q = infl[4 * k:4 * k + 4]
        write_element(buf, o, ej, tuple(j for j, _ in q))
        write_element(buf, o, ew, tuple(w for _, w in q))


def main_bone(model, obj):
    """The bone most of an object's weight is on (the hair's head attachment, the face's head)."""
    total = {}
    for decl in obj.decls:
        vb = model.vertex_bytes(decl)
        js = sorted([e for e in decl.elements if e.usage == U_JOINT], key=lambda e: e.index)
        ws = sorted([e for e in decl.elements if e.usage == U_WEIGHT], key=lambda e: e.index)
        for v in range(decl.vertex_count):
            o = v * decl.stride
            for ej, ew in zip(js, ws):
                for j, w in zip(read_element(vb, o, ej), read_element(vb, o, ew)):
                    total[j] = total.get(j, 0) + w
    return max(total, key=total.get) if total else 0


def import_parts(model, parts, renpack_off=True, bounds=True, only_given=False, copy_weights=False, rigid=False,
                 hold=False, copy_k=4):
    """Rebuilds the model's geometry from parts (read_gltf_parts): every submesh named in parts gets their triangles
    (merged); the others keep theirs (only_given: those of an object parts name are emptied). A part without bone
    weights, with bones the model hasn't, or every part with copy_weights, takes each vertex's influences from the
    nearest vertex of the original object (weight transfer). hold: each vertex's colour (red, 0-1) is how much of it
    goes on the head bone instead. copy_k: how many original vertices a copied weight blends (more: smoother).
    Returns (tmc, tmcl, stats)."""
    stats = {'trimmed': 0, 'noinfl': 0, 'copied': [], 'rigid': [], 'reparented': set(), 'held': []}
    nearest = {}
    for (oi, num), plist in parts.items():
        obj = model.objects[oi]
        for part in plist:
            from_gltf = part.get('model_space')
            if from_gltf:  # (into the object's own space)
                _to_object_space(model, obj, part)
            if rigid:  # (every vertex on the object's main bone: follows the head, no physics or expressions)
                b = main_bone(model, obj)
                part['infl'] = [[(b, 1.0)] for _ in part['pos']]
                stats['rigid'].append((part['name'], model.node_names[obj.palette[b]]))
            elif copy_weights or part.get('foreign') or not any(part['infl']):
                if oi not in nearest:
                    nearest[oi] = _Nearest(model, [obj])
                part['infl'] = [list(nearest[oi].nearest(p, copy_k)) for p in part['pos']]
                stats['copied'].append((part['name'], part.get('foreign', [])[:3]))
            elif from_gltf:  # (its own weights: node numbers -> the object's palette)
                part['infl'] = [[(_palette_slot(model, obj, j, stats), w) for j, w in il] for il in part['infl']]
            if hold and part.get('hold'):  # (held to the head by the vertex colour's red: hair roots on the scalp)
                head = model.node_names.index('MOT01_Head') if 'MOT01_Head' in model.node_names else None
                hb = obj.palette.index(head) if head in obj.palette else main_bone(model, obj)
                held = 0
                for v, h in enumerate(part['hold']):
                    if h > 0.001:
                        il = [(j, w * (1 - h)) for j, w in part['infl'][v] if j != hb]
                        part['infl'][v] = il + [(hb, h + sum(w for j, w in part['infl'][v] if j == hb) * (1 - h))]
                        held += 1
                stats['held'].append((part['name'], held, model.node_names[obj.palette[hb]]))
    if only_given:  # (hidden: one zero-area triangle at its first vertex - the game crashes on an empty submesh)
        for obj in model.objects:
            if any(k[0] == obj.index for k in parts):
                for sm in obj.submeshes:
                    if (obj.index, sm.number) not in parts:
                        parts[(obj.index, sm.number)] = [_degenerate(model, obj, sm)]
    d = model.tmc
    # new vertex / index data per buffer
    vbufs, ibufs = {}, {}
    for obj in model.objects:
        for di, decl in enumerate(obj.decls):
            subs = [s for s in obj.submeshes if s.decl == di]
            vb, ib = bytearray(), []
            old_vb, old_ix = model.vertex_bytes(decl), model.indices(decl)
            for sm in sorted(subs, key=lambda s: s.index_start):
                key = (obj.index, sm.number)
                v_start, i_start = len(vb) // decl.stride, len(ib)
                if key not in parts:  # (kept: its old vertices, re-based)
                    vb += old_vb[sm.vertex_start * decl.stride:(sm.vertex_start + sm.vertex_count) * decl.stride]
                    ib += [i - sm.vertex_start + v_start for i in old_ix[sm.index_start:sm.index_start + sm.index_count]]
                    sm.new = (i_start, sm.index_count, v_start, sm.vertex_count)
                    continue
                nverts = 0
                for part in parts[key]:
                    n = len(part['pos'])
                    if part['nrm'] is None:
                        part['nrm'] = _normals(part['pos'], part['tri'])
                    if part['tan'] is None:
                        part['tan'] = _tangents(part['pos'], part['nrm'], part['uv0'] or [(0, 0)] * n, part['tri'])
                    if not any(part['infl']):
                        stats['noinfl'] += 1
                    buf = bytearray(n * decl.stride)
                    for v in range(n):
                        if not part['infl'][v]:  # (no weights: the object's first bone, as the original's mostly are)
                            part['infl'][v] = [(_main_bone(model, decl, old_vb), 1.0)]
                        _vertex(decl, part, v, buf, v * decl.stride, stats)
                    base = v_start + nverts
                    ib += [i + base for i in part['tri']]
                    vb += buf
                    nverts += n
                if v_start + nverts > 65535:
                    raise SystemExit(f'{sm.name}: over 65535 vertices in one buffer')
                sm.new = (i_start, len(ib) - i_start, v_start, nverts)
            vbufs[decl.buffer] = (bytes(vb), len(vb) // decl.stride, decl)
            ibufs[decl.buffer] = ib
    # the TMCL: textures as they are, then the new vertex and index sections
    tex = bytes(model.tmcl[model.sec_off[0]:model.sec_off[0] + model.sec_size[0]])
    nb = len(model.vtx['offs'])
    vsec, voffs, vsizes = bytearray(16), [], []
    for b in range(nb):
        data = vbufs[b][0]
        voffs.append(len(vsec))
        vsizes.append(len(data) + (-len(data)) % 16)
        vsec += data
        _align(vsec, 16)
    isec, ioffs, isizes = bytearray(16), [], []
    for b in range(nb):
        data = struct.pack(f'<{len(ibufs[b])}H', *ibufs[b])
        ioffs.append(len(isec))
        isizes.append(len(data) + (-len(data)) % 16)
        isec += data
        _align(isec, 16)
    struct.pack_into('<II', vsec, 0, nb, len(vsec))
    struct.pack_into('<II', isec, 0, nb, len(isec))
    tmcl = bytearray(0x80)
    offs, sizes = [], []
    for sec in (tex, vsec, isec):
        offs.append(len(tmcl))
        sizes.append(len(sec))
        tmcl += sec
        _align(tmcl, 0x80)
    struct.pack_into('<II', tmcl, 0, 3, len(tmcl))
    # the TMC's tables
    out = bytearray(d)
    for lay, o, s, total in ((model.vtx, voffs, vsizes, len(vsec)), (model.idx, ioffs, isizes, len(isec))):
        c = lay['chunk']
        struct.pack_into('<II', out, c + 0x40, nb, total)
        struct.pack_into(f'<{nb}I', out, c + struct.unpack_from('<I', out, c + 0x20)[0], *o)
        struct.pack_into(f'<{nb}I', out, c + kt_sizes_off(out, c), *s)
    lh = model.chunks['LHeader']
    struct.pack_into('<II', out, lh + 0x40, 3, len(tmcl))
    struct.pack_into('<3I', out, lh + 0x80, *offs)
    struct.pack_into('<3I', out, lh + 0x90, *sizes)
    for obj in model.objects:
        for di, decl in enumerate(obj.decls):
            vdata, vcount, _ = vbufs[decl.buffer]
            struct.pack_into('<II', out, decl.at + 0x10, len(ibufs[decl.buffer]), vcount)
        for sm in obj.submeshes:
            struct.pack_into('<4I', out, sm.at + 0x80, *sm.new)
    if bounds:
        _update_bounds(model, out, vbufs, ibufs, parts)
    if renpack_off and any(model.objects[k[0]].name == 'WGT_face' for k in parts) and 'RENPACK' in model.chunks:
        _renpack_empty(out, model.chunks['RENPACK'])
    return bytes(out), bytes(tmcl), stats


def _to_object_space(model, obj, part):
    """A part's positions, normals and tangents from the model's space into the object's (its node's inverse)."""
    inv = mat_inverse(model.obj_matrix(obj))
    part['pos'] = [xf_point(p, inv) for p in part['pos']]
    if part['nrm']:
        part['nrm'] = [xf_dir(n, inv) for n in part['nrm']]
    if part['tan']:
        part['tan'] = [xf_dir(t[:3], inv) + (t[3] if len(t) > 3 else 1.0,) for t in part['tan']]
    part['model_space'] = False


def _palette_slot(model, obj, node, stats):
    """Node -> its slot in the object's bone palette; a bone not in it moves to its nearest ancestor that is."""
    n = node
    while n is not None and n >= 0 and n not in obj.palette:
        n = model.parent[n] if n < len(model.parent) else None
    if n is None or n < 0:
        n = obj.palette[main_bone(model, obj)]
    if n != node:
        stats['reparented'].add((model.node_names[node] if node is not None and 0 <= node < len(model.node_names)
                                 else str(node), model.node_names[n]))
    return obj.palette.index(n)


def _degenerate(model, obj, sm):
    """A part that draws nothing: one zero-area triangle at the submesh's first vertex, with its bone influences."""
    decl = obj.decls[sm.decl]
    vb = model.vertex_bytes(decl)
    o = sm.vertex_start * decl.stride
    p = read_element(vb, o, decl.find(U_POS))[:3]
    js = sorted([e for e in decl.elements if e.usage == U_JOINT], key=lambda e: e.index)
    ws = sorted([e for e in decl.elements if e.usage == U_WEIGHT], key=lambda e: e.index)
    infl = []
    for ej, ew in zip(js, ws):
        infl += [(j, w) for j, w in zip(read_element(vb, o, ej), read_element(vb, o, ew)) if w > 0]
    return {'name': f'{sm.name} (hidden)', 'pos': [p] * 3, 'nrm': [(0.0, 1.0, 0.0)] * 3,
            'tan': [(1.0, 0.0, 0.0, 1.0)] * 3, 'uv0': [(0.0, 0.0)] * 3, 'uv1': None,
            'infl': [list(infl) for _ in range(3)], 'tri': [0, 1, 2], 'foreign': []}


def _main_bone(model, decl, old_vb):
    ej, ew = decl.find(U_JOINT, 0), decl.find(U_WEIGHT, 0)
    if not ej or not old_vb:
        return 0
    return read_element(old_vb, 0, ej)[0]


def _bounds_block(out, at, pts):
    """A bounds block at `at` (sphere, box centre, half extents, centre again, axes scaled): from pts."""
    if not pts:
        return
    lo = [min(p[k] for p in pts) for k in range(3)]
    hi = [max(p[k] for p in pts) for k in range(3)]
    c = [(a + b) / 2 for a, b in zip(lo, hi)]
    e = [(b - a) / 2 for a, b in zip(lo, hi)]
    r = max(math.dist(c, p) for p in pts)
    struct.pack_into('<4f', out, at, *c, r)
    struct.pack_into('<4f', out, at + 0x10, *c, 1.0)
    struct.pack_into('<4f', out, at + 0x20, *e, 0.0)
    struct.pack_into('<4f', out, at + 0x30, *c, 1.0)
    struct.pack_into('<4f', out, at + 0x40, 0.0, e[1], 0.0, 0.0)
    struct.pack_into('<4f', out, at + 0x50, 0.0, 0.0, e[2], 0.0)
    struct.pack_into('<4f', out, at + 0x60, e[0], 0.0, 0.0, 0.0)


def _looks_like_bounds(out, at):
    try:
        cx, cy, cz, r = struct.unpack_from('<4f', out, at)
        bx, by, bz, bw = struct.unpack_from('<4f', out, at + 0x10)
    except struct.error:
        return False
    return 0 < r < 100 and bw == 1.0 and abs(cy - by) < 1e-3 + r


def _update_bounds(model, out, vbufs, ibufs, parts):
    """The bounds of the objects and submeshes that got new geometry (the others keep the game's own)."""
    infos = kt_offsets(out, model.chunks['MdlInfo'])
    for obj, info in zip(model.objects, infos):
        if not any(k[0] == obj.index for k in parts):
            continue
        all_pts = []
        subs = kt_offsets(out, info)
        for sm, si in zip(obj.submeshes, subs):
            decl = obj.decls[sm.decl]
            vb = vbufs[decl.buffer][0]
            ep = decl.find(U_POS)
            i0, ic, v0, vc = sm.new
            pts = [read_element(vb, v * decl.stride, ep)[:3] for v in range(v0, v0 + vc)]
            all_pts += pts
            if pts and (obj.index, sm.number) in parts and _looks_like_bounds(out, si + 0x50):
                _bounds_block(out, si + 0x50, pts)
        if all_pts and _looks_like_bounds(out, info + 0xe0):
            _bounds_block(out, info + 0xe0, all_pts)


def move_bones(model, pos):
    """Moves bones to new positions in the model's space ({node: (x, y, z)}), in everything that stores them: HieLay
    (local), GlblMtx (world), BnOfsMtx (its inverse) and the ACSCLS physics chains. A bone's parent must be unmoved or
    in pos too, and a moved bone's children in pos as well. Rotations stay.
    ACSCLS: chunk 0 holds one block per physics group. A chain of particles has its table at +0xa0: +0x30 the chain's
    start (x, y, z, 1: its first node relative to its parent), then two blocks, each with its records at its own header
    offset (+0x20):
      nodes, 0xa0 bytes each: a local matrix (the first node's translation 0, the others' the bone's local one), limits,
      +0x80 two stiffnesses, +0x88 the node number;
      links, 0x60 bytes each: +0x00 the rest length, +0x50 two u16 node indices in this chain (neighbours, and every
      second node for bending)."""
    d = model.tmc
    hl = kt_offsets(d, model.chunks['HieLay'])
    gm = kt_offsets(d, model.chunks['GlblMtx'])
    bo = kt_offsets(d, model.chunks['BnOfsMtx'])
    for n, p in pos.items():
        w = list(model.world[n])
        w[12:15] = p
        model.world[n] = tuple(w)
    for n in pos:
        for c, q in enumerate(model.parent):
            if q == n and c not in pos:
                raise ValueError(f'{model.node_names[c]}: its parent moves and it does not')
        q = model.parent[n]
        l = list(model.local[n])
        if q >= 0:
            pw = model.world[q]
            l[12:15] = xf_dir([model.world[n][12 + k] - pw[12 + k] for k in range(3)], mat_inverse(pw))
        else:
            l[12:15] = model.world[n][12:15]
        model.local[n] = tuple(l)
        model.inv_bind[n] = tuple(mat_inverse(model.world[n]))
        struct.pack_into('<3f', d, hl[n] + 0x30, *l[12:15])
        struct.pack_into('<3f', d, gm[n] + 0x30, *model.world[n][12:15])
        struct.pack_into('<16f', d, bo[n], *model.inv_bind[n])
    if 'ACSCLS' not in model.chunks:
        return
    top = kt_offsets(d, model.chunks['ACSCLS'])[0]
    for g in kt_offsets(d, top):
        hs, count = struct.unpack_from('<I', d, g + 0x20)[0], struct.unpack_from('<I', d, g + 0x14)[0]
        if hs != 0xa0 or count != 5:
            continue  # (not a chain: the bangs' groups, which store limits, not links)
        nb = g + struct.unpack_from('<I', d, g + hs)[0]
        lb = g + struct.unpack_from('<I', d, g + hs + 4)[0]
        nn = struct.unpack_from('<I', d, nb + 0x14)[0]
        nrec = nb + struct.unpack_from('<I', d, nb + 0x20)[0]
        nodes = [struct.unpack_from('<I', d, nrec + k * 0xa0 + 0x88)[0] for k in range(nn)]
        if not any(n in pos for n in nodes):
            continue
        struct.pack_into('<3f', d, g + 0x30, *model.local[nodes[0]][12:15])
        for k, n in enumerate(nodes[1:], 1):
            struct.pack_into('<3f', d, nrec + k * 0xa0 + 0x30, *model.local[n][12:15])
        nl = struct.unpack_from('<I', d, lb + 0x14)[0]
        lrec = lb + struct.unpack_from('<I', d, lb + 0x20)[0]
        for k in range(nl):
            a, b = struct.unpack_from('<2H', d, lrec + k * 0x60 + 0x50)
            wa, wb = model.world[nodes[a]], model.world[nodes[b]]
            struct.pack_into('<f', d, lrec + k * 0x60, math.dist(wa[12:15], wb[12:15]))


def chain_swing(model, stiff):
    """Sets a physics chain's per-node values (ACSCLS node records +0x80, +0x84: see move_bones), {node: value}. They fall
    along a chain (Momiji's ponytail: 0.6 at the root, 0.1 at the tip). Found by testing: higher swings more."""
    d = model.tmc
    top = kt_offsets(d, model.chunks['ACSCLS'])[0]
    for g in kt_offsets(d, top):
        hs, count = struct.unpack_from('<I', d, g + 0x20)[0], struct.unpack_from('<I', d, g + 0x14)[0]
        if hs != 0xa0 or count != 5:
            continue
        nb = g + struct.unpack_from('<I', d, g + hs)[0]
        nrec = nb + struct.unpack_from('<I', d, nb + 0x20)[0]
        for k in range(struct.unpack_from('<I', d, nb + 0x14)[0]):
            r = nrec + k * 0xa0
            n = struct.unpack_from('<I', d, r + 0x88)[0]
            if n in stiff:
                struct.pack_into('<2f', d, r + 0x80, stiff[n], stiff[n])


def _renpack_empty(out, at):
    """The face's RENPACK (render segments of its skin material, laid out for its original mesh) emptied, as the hair
    models have it: none of its segments refer to the new mesh's vertices."""
    struct.pack_into('<II', out, at + 0x14, 0, 0)  # (no objects)


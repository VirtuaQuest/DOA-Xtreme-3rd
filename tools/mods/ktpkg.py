"""DOAX3 Scarlet's packages: <dir>/package_data.bin (the index: file paths) and <dir>/data_<id>.lnk (the data).

data_*.lnk ("b000" archive):
    0x00  "b" and 3 digits (b000, b025, ...), 4 zeros, u64 entry count, u64 file size, u64 alignment (0x800)
    0x20  entries of 0x20 bytes: u64 offset, u64 size, u64 stored size, u64 compressed (0 / 1)
A compressed entry is a run of chunks, each 16-byte aligned (from the entry's start): u32 word, then word & 0x7fff
bytes of zlib data when bit 15 is set (each inflates to at most 0x4000 bytes), else word bytes stored as they are.

package_data.bin ("pkginfo"): u32[6] section offsets at 0x30; section 1 is "btif", the file table:
    btif+0x08  u32 record count
    btif+0x20  u32 records' size in bytes (count * 0x2c)
    btif+0x30  records of 0x2c bytes (11 u32): path hash, ?, path offset (in the strings), ?, -1,
               flags (0x40000000: compressed in the .lnk), size, 0, 0, directory hash, .lnk entry index
    then the paths, NUL-terminated ("00/character/KASUMI/..."). Directory hash 0: a loose RomFS file (movies, sound
    banks, .tss), not in the .lnk.
"""
import os
import struct
import zlib

REC = 0x2c
F_COMPRESSED = 0x40000000


class Record:
    __slots__ = ('index', 'path', 'path_hash', 'dir_hash', 'lnk_index', 'size', 'flags', 'rec_off')

    @property
    def loose(self):
        return self.dir_hash == 0

    def __repr__(self):
        return f'<{self.path} lnk#{self.lnk_index} {self.size:#x}{" z" if self.flags & F_COMPRESSED else ""}>'


class Package:
    """One package folder (romfs/00, romfs/05, aoc/<id>/00DLC...): its index and its .lnk."""

    def __init__(self, folder):
        self.folder = folder
        self.pd_path = os.path.join(folder, 'package_data.bin')
        lnks = [n for n in os.listdir(folder) if n.endswith('.lnk')]
        assert len(lnks) == 1, f'{folder}: expected one .lnk, found {lnks}'
        self.lnk_path = os.path.join(folder, lnks[0])
        self.pd = open(self.pd_path, 'rb').read()
        assert self.pd[:7] == b'pkginfo', self.pd_path
        self.btif = struct.unpack_from('<6I', self.pd, 0x30)[1]
        assert self.pd[self.btif:self.btif + 4] == b'btif', self.pd_path
        n = struct.unpack_from('<I', self.pd, self.btif + 8)[0]
        rec_bytes = struct.unpack_from('<I', self.pd, self.btif + 0x20)[0]
        assert rec_bytes == n * REC, (self.pd_path, n, rec_bytes)
        strings = self.btif + 0x30 + rec_bytes
        with open(self.lnk_path, 'rb') as f:
            magic, count, total, self.align = struct.unpack('<8sQQQ', f.read(0x20))
            assert magic[:1] == b'b' and magic[1:4].isdigit(), self.lnk_path
            tab = f.read(count * 0x20)
        self.lnk_entries = [struct.unpack_from('<QQQQ', tab, i * 0x20) for i in range(count)]
        self.records = []
        for i in range(n):
            o = self.btif + 0x30 + i * REC
            v = struct.unpack_from('<11I', self.pd, o)
            r = Record()
            r.index, r.rec_off = i, o
            r.path_hash, r.flags, r.size, r.dir_hash, r.lnk_index = v[0], v[5], v[6], v[9], v[10]
            so = strings + v[2]
            r.path = self.pd[so:self.pd.index(b'\0', so)].decode('utf-8')
            self.records.append(r)
        self.by_path = {r.path: r for r in self.records}

    def check(self):
        """Every record agrees with its .lnk entry (size, compression): the layout is understood."""
        bad = []
        for r in self.records:
            if r.loose:
                continue
            off, size, stored, comp = self.lnk_entries[r.lnk_index]
            if size != r.size or comp != bool(r.flags & F_COMPRESSED):
                bad.append((r, self.lnk_entries[r.lnk_index]))
        return bad

    def read(self, rec):
        """A file's bytes (inflated)."""
        off, size, stored, comp = self.lnk_entries[rec.lnk_index]
        with open(self.lnk_path, 'rb') as f:
            f.seek(off)
            raw = f.read(stored)
        return inflate(raw, size) if comp else raw


def inflate(buf, size):
    out = bytearray()
    o = 0
    while len(out) < size:
        w = struct.unpack_from('<I', buf, o)[0]
        o += 4
        n = w & 0x7FFF if w & 0x8000 else w
        out += zlib.decompress(buf[o:o + n]) if w & 0x8000 else buf[o:o + n]
        o = (o + n + 15) & ~15
    assert len(out) == size, (len(out), size)
    return bytes(out)


def find_packages(game_dir):
    """Every package folder of the game (romfs/00..06) and its DLC (aoc/<id>/*), in load order."""
    out = []
    romfs = os.path.join(game_dir, 'romfs')
    for n in sorted(os.listdir(romfs)):
        if os.path.isfile(os.path.join(romfs, n, 'package_data.bin')):
            out.append(os.path.join(romfs, n))
    aoc = os.path.join(game_dir, 'aoc')
    if os.path.isdir(aoc):
        for a in sorted(os.listdir(aoc)):
            for n in sorted(os.listdir(os.path.join(aoc, a))):
                if os.path.isfile(os.path.join(aoc, a, n, 'package_data.bin')):
                    out.append(os.path.join(aoc, a, n))
    return out

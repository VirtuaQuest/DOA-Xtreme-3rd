"""Extract exefs/romfs from an NSP with the user's own keys (PFS0 > NCA3 > PFS0/RomFS).

Usage: nsp_extract.py <game.nsp> <prod.keys> <outdir> [--base <base game.nsp>] [--no-romfs] [--list]

Writes <outdir>/exefs/* and <outdir>/romfs/** from the program NCA, and <outdir>/control/** from the control NCA.
An update NSP's RomFS is a BKTR patch of the base game's: pass the base game NSP with --base. A DLC NSP's RomFS goes
to <outdir>/aoc/<its data id>/**.
"""
import argparse
import bisect
import os
import struct
import sys

from Crypto.Cipher import AES

MEDIA = 0x200
CONTENT_TYPES = {0: 'program', 1: 'meta', 2: 'control', 3: 'manual', 4: 'data', 5: 'publicdata'}


def load_keys(path):
    keys = {}
    for line in open(path, encoding='utf-8'):
        if '=' in line:
            k, v = line.split('=', 1)
            keys[k.strip()] = bytes.fromhex(v.strip())
    return keys


def pfs0_entries(read, base):
    """[(name, abs_offset, size)] of a PFS0 at `base`, read via read(off, n)."""
    magic, n, strsz, _ = struct.unpack('<4sIII', read(base, 0x10))
    assert magic == b'PFS0', magic
    ents = read(base + 0x10, n * 0x18)
    strs = read(base + 0x10 + n * 0x18, strsz)
    data = base + 0x10 + n * 0x18 + strsz
    out = []
    for i in range(n):
        off, size, so, _ = struct.unpack_from('<QQII', ents, i * 0x18)
        name = strs[so:strs.index(b'\0', so)].decode()
        out.append((name, data + off, size))
    return out


def xts_decrypt_nintendo(key, data, sector0=0, sector_size=0x200):
    """AES-128-XTS with Nintendo's big-endian sector tweak."""
    k1, k2 = AES.new(key[:16], AES.MODE_ECB), AES.new(key[16:32], AES.MODE_ECB)
    out = bytearray()
    for s in range(len(data) // sector_size):
        t = int.from_bytes(k2.encrypt((sector0 + s).to_bytes(16, 'big')), 'little')
        sec = data[s * sector_size:(s + 1) * sector_size]
        for j in range(0, sector_size, 16):
            tb = t.to_bytes(16, 'little')
            x = bytes(a ^ b for a, b in zip(sec[j:j + 16], tb))
            out += bytes(a ^ b for a, b in zip(k1.decrypt(x), tb))
            t = ((t << 1) & ((1 << 128) - 1)) ^ (0x87 if t >> 127 else 0)
    return bytes(out)


class Nca:
    def __init__(self, f, off, size, keys, tickets):
        self.f, self.off, self.size = f, off, size
        f.seek(off)
        hdr = xts_decrypt_nintendo(keys['header_key'], f.read(0xC00))
        assert hdr[0x200:0x204] == b'NCA3', hdr[0x200:0x204]
        self.hdr = hdr
        self.content_type = hdr[0x205]
        self.program_id = struct.unpack_from('<Q', hdr, 0x210)[0]
        rev = max(hdr[0x206], hdr[0x220])
        rev = rev - 1 if rev else 0
        self.keygen = rev
        rights = hdr[0x230:0x240]
        if any(rights):
            enc = tickets[rights]
            self.key = AES.new(keys[f'titlekek_{rev:02x}'], AES.MODE_ECB).decrypt(enc)
        else:
            kak = keys[f'key_area_key_application_{rev:02x}']
            self.key = AES.new(kak, AES.MODE_ECB).decrypt(hdr[0x300 + 2 * 0x10:0x300 + 3 * 0x10])
        self.sections = []
        for i in range(4):
            start, end = struct.unpack_from('<II', hdr, 0x240 + i * 0x10)
            if not end:
                continue
            fsh = hdr[0x400 + i * 0x200:0x600 + i * 0x200]
            self.sections.append(Section(self, i, start * MEDIA, (end - start) * MEDIA, fsh))

    def raw(self, off, n):
        self.f.seek(self.off + off)
        return self.f.read(n)


class Section:
    def __init__(self, nca, idx, start, size, fsh):
        self.nca, self.idx, self.start, self.size = nca, idx, start, size
        self.fs_type, self.hash_type, self.enc = fsh[2], fsh[3], fsh[4]
        self.ctr_hi = fsh[0x140:0x148][::-1]
        if self.fs_type == 1:  # PFS0 under hierarchical sha256: data region is layer 1
            self.data_off = struct.unpack_from('<Q', fsh, 0x40)[0]
        else:  # RomFS under IVFC: data is the last (6th) level
            assert fsh[0x08:0x0C] == b'IVFC', fsh[0x08:0x0C]
            self.data_off = struct.unpack_from('<Q', fsh, 0x18 + 5 * 0x18)[0]
        assert self.enc in (1, 3, 4), f'section {idx}: unsupported encryption {self.enc}'
        # an update's RomFS (BKTR): the indirect table maps the patched image onto the base game's RomFS and this
        # section's own data, which is AES-CTR with a per-range counter generation (the AesCtrEx table)
        self.patch = struct.unpack_from('<QQ16sQQ16s', fsh, 0x100) if self.enc == 4 else None
        self.base = None

    def _ctr(self, a, n, gen=None):
        """n bytes at NCA offset a, decrypted with the section counter (its generation replaced by gen)."""
        lo, hi = a & ~0xF, (a + n + 0xF) & ~0xF
        data = self.nca.raw(lo, hi - lo)
        ctr = self.ctr_hi if gen is None else self.ctr_hi[:4] + gen.to_bytes(4, 'big')
        iv = ctr + (lo >> 4).to_bytes(8, 'big')
        data = AES.new(self.nca.key, AES.MODE_CTR, nonce=b'', initial_value=iv).decrypt(data)
        return data[a - lo:a - lo + n]

    def attach_base(self, base):
        """Make this BKTR section readable as the patched image of `base` (the base game's RomFS section)."""
        ind_off, ind_size, ind_hdr, ctr_off, ctr_size, ctr_hdr = self.patch
        assert ind_hdr[:4] == b'BKTR' and ctr_hdr[:4] == b'BKTR', 'not a BKTR patch section'
        self.base = base
        # (the tables themselves are encrypted with the section's own counter)
        self.indirect, self.virt_size = bucket_tree(self._ctr(self.start + ind_off, ind_size), 0x14,
                                                    lambda d, o: struct.unpack_from('<QQI', d, o))
        self.ctrex, _ = bucket_tree(self._ctr(self.start + ctr_off, ctr_size), 0x10,
                                    lambda d, o: struct.unpack_from('<QB3xI', d, o))
        self.ind_keys = [e[0] for e in self.indirect]
        self.ctr_keys = [e[0] for e in self.ctrex]
        assert struct.unpack_from('<I', ind_hdr, 8)[0] == len(self.indirect), 'indirect table entry count'
        assert struct.unpack_from('<I', ctr_hdr, 8)[0] == len(self.ctrex), 'AesCtrEx table entry count'

    def _patch_data(self, off, n):
        """n bytes of this section's own (AesCtrEx) data at section offset off."""
        out = bytearray()
        while n:
            i = bisect.bisect_right(self.ctr_keys, off) - 1
            _, plain, gen = self.ctrex[i]
            end = self.ctr_keys[i + 1] if i + 1 < len(self.ctrex) else self.size
            k = min(n, end - off)
            out += self.nca.raw(self.start + off, k) if plain else self._ctr(self.start + off, k, gen)
            off, n = off + k, n - k
        return bytes(out)

    def read(self, off, n):
        """Read n plaintext bytes at section-relative offset off."""
        if self.patch:
            assert self.base, 'an update RomFS needs the base game (--base)'
            out = bytearray()
            while n:
                i = bisect.bisect_right(self.ind_keys, off) - 1
                virt, phys, storage = self.indirect[i]
                end = self.ind_keys[i + 1] if i + 1 < len(self.indirect) else self.virt_size
                k = min(n, end - off)
                src = self._patch_data if storage else self.base.read
                out += src(phys + off - virt, k)
                off, n = off + k, n - k
            return bytes(out)
        if self.enc == 3:
            return self._ctr(self.start + off, n)
        return self.nca.raw(self.start + off, n)


def bucket_tree(data, entry_size, parse):
    """(entries, end offset) of a BKTR bucket tree: a 0x4000-byte header node, then 0x4000-byte buckets of entries."""
    _, nbuckets, end = struct.unpack_from('<IIQ', data, 0)
    out = []
    for b in range(nbuckets):
        at = 0x4000 * (b + 1)
        _, n, _ = struct.unpack_from('<IIQ', data, at)
        out += [parse(data, at + 0x10 + i * entry_size) for i in range(n)]
    return out, end


def copy_out(read, off, size, path, chunk=16 << 20):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as o:
        done = 0
        while done < size:
            n = min(chunk, size - done)
            o.write(read(off + done, n))
            done += n


def extract_pfs0(sec, outdir):
    for name, off, size in pfs0_entries(sec.read, sec.data_off):
        copy_out(sec.read, off, size, os.path.join(outdir, name))
        print(f'  {name:24} {size:>12,}')


def extract_romfs(sec, outdir, verbose=False):
    base = sec.data_off
    h = struct.unpack('<10Q', sec.read(base, 0x50))
    dir_meta = sec.read(base + h[3], h[4])
    file_meta = sec.read(base + h[7], h[8])
    data_base = base + h[9]
    count, total = 0, 0

    def name_at(meta, off, hdrlen):
        nlen = struct.unpack_from('<I', meta, off + hdrlen - 4)[0]
        return meta[off + hdrlen:off + hdrlen + nlen].decode('utf-8')

    def walk_files(foff, path):
        nonlocal count, total
        while foff != 0xFFFFFFFF:
            _, sib, doff, dsize, _, _ = struct.unpack_from('<IIQQII', file_meta, foff)
            fpath = os.path.join(path, name_at(file_meta, foff, 0x20))
            copy_out(sec.read, data_base + doff, dsize, fpath)
            count += 1
            total += dsize
            if verbose or count % 500 == 0:
                print(f'  {count:6} files  {total / 1e9:6.2f} GB  {fpath}', flush=True)
            foff = sib

    def walk_dir(doff, path):
        while doff != 0xFFFFFFFF:
            _, sib, child, files, _, _ = struct.unpack_from('<IIIIII', dir_meta, doff)
            here = os.path.join(path, name_at(dir_meta, doff, 0x18)) if doff else path
            os.makedirs(here, exist_ok=True)
            walk_files(files, here)
            walk_dir(child, here)
            doff = sib if doff else 0xFFFFFFFF

    walk_dir(0, outdir)
    print(f'  romfs: {count} files, {total:,} bytes')


def open_nsp(path, keys):
    """[(name, Nca)] of an NSP's content NCAs (the meta NCA left out)."""
    f = open(path, 'rb')
    rd = lambda off, n: (f.seek(off), f.read(n))[1]
    ents = pfs0_entries(rd, 0)
    tickets = {}
    for name, off, size in ents:
        if name.endswith('.tik'):
            t = rd(off, size)
            assert struct.unpack_from('<I', t, 0)[0] == 0x10004, 'expected RSA-2048 ticket'
            assert t[0x281] == 0, f'{name}: a personalized ticket (its title key is encrypted for one console)'
            tickets[t[0x2A0:0x2B0]] = t[0x180:0x190]
    return [(name, Nca(f, off, size, keys, tickets)) for name, off, size in ents
            if name.endswith('.nca') and not name.endswith('.cnmt.nca')]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('nsp')
    ap.add_argument('keys')
    ap.add_argument('outdir')
    ap.add_argument('--base', help="an update's base game NSP (its RomFS is patched by the update's)")
    ap.add_argument('--no-romfs', action='store_true', help='leave the RomFS out (exefs and control only)')
    ap.add_argument('--list', action='store_true', help='describe the NCAs only')
    a = ap.parse_args()
    keys = load_keys(a.keys)
    ncas = open_nsp(a.nsp, keys)
    base = None
    for name, nca in ncas:
        kind = CONTENT_TYPES.get(nca.content_type, '?')
        print(f'{name}  {kind:10} id {nca.program_id:016x}  keygen {nca.keygen}  '
              + '  '.join(f's{s.idx}:{"pfs0" if s.fs_type == 1 else "romfs"}{"(patch)" if s.patch else ""} '
                          f'{s.size:,}' for s in nca.sections))
        if a.list:
            continue
        if kind == 'program':
            for s in nca.sections:
                if s.idx == 0:
                    print(' exefs:')
                    extract_pfs0(s, os.path.join(a.outdir, 'exefs'))
                elif s.idx == 1 and not a.no_romfs:
                    if s.patch:
                        if not a.base:
                            sys.exit('this is an update: pass the base game NSP with --base')
                        base = base or next(n for _, n in open_nsp(a.base, keys) if n.content_type == 0)
                        assert base.program_id == nca.program_id, 'the base NSP is another game'
                        s.attach_base(next(b for b in base.sections if b.idx == s.idx))
                    print(' romfs:')
                    extract_romfs(s, os.path.join(a.outdir, 'romfs'))
        elif kind == 'control':
            extract_romfs(nca.sections[0], os.path.join(a.outdir, 'control'), verbose=True)
        elif kind == 'publicdata' and not a.no_romfs:  # add-on content (DLC): its RomFS, under aoc/<its id>
            extract_romfs(nca.sections[0], os.path.join(a.outdir, 'aoc', f'{nca.program_id:016x}'))


if __name__ == '__main__':
    sys.exit(main())

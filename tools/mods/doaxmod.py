"""DOAX3 Recomp mod tool (docs/MODDING.md). Run from the repo root with the venv's Python:

    .venv/Scripts/python -I tools/mods/doaxmod.py <command> ...

  girls                                   the girls' codes and what each one's files are
  list [TEXT]                             the game's files (paths as mods name them) containing TEXT
  girl-files GIRL                         every file that makes up a girl's look (models, textures, UI, voice)
  extract PATTERN [--out DIR]             game files matching PATTERN (* ? wildcards) to DIR/<path>, unpacked
  copy SRC DST MOD                        put game file SRC into mod MOD as DST (e.g. another girl's face)
  swap-look TARGET SOURCE MOD [--parts]   TARGET's face / hair / body skin replaced by SOURCE's, in mod MOD
  voice-unpack GIRL DIR                   GIRL's voice lines to DIR/000.wav ... (and DIR/lines.txt)
  voice-pack GIRL DIR MOD                 GIRL's voice bank with every DIR/NNN.<audio> as line NNN, into mod MOD
  reactions-unpack GIRL DIR               GIRL's short voice sounds (shouts, laughs: the .xws banks) to WAVs
  reactions-pack GIRL DIR MOD             those banks with DIR/<bank>/NNN.<audio> replacing sound NNN, into MOD
  portraits-unpack GIRL DIR [--all]       GIRL's portraits, icons, profile and name plates as DIR/<texture>.png
  portraits-pack GIRL DIR MOD [--keep-alpha]  those textures from DIR/<texture>.<image> (any size), into MOD
  kslt-unpack PATTERN DIR                 any UI texture pack's textures as PNGs (PATTERN: game paths, * ?)
  kslt-pack PATTERN DIR MOD [--keep-alpha]  those packs with DIR/<texture>.<image> replacing textures, into MOD
  model-export PATH DIR                   a model (face, hair, costume: its _TMC.tmc path) as DIR/<name>.gltf + textures
  model-import PATH MOD [--gltf F] [--textures DIR]  that model rebuilt from a .gltf / .glb and/or tex_NN images
  tmc-info FILE|PATH                      the chunk tree of a TMC / TMCL / other Koei Tecmo container
  check MOD                               a mod's files against the game's: paths, kinds, sizes, voice banks

Mods live in mods/<MOD>/romfs/<game path>; the runtime loads every mods/* folder whose name doesn't start with "_".
"""
import argparse
import fnmatch
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ktpkg  # noqa: E402
import ktss  # noqa: E402

GIRLS = {
    'KAS': 'Kasumi', 'MAR': 'Marie Rose', 'HON': 'Honoka', 'AYA': 'Ayane', 'NYO': 'Nyotengu', 'KOK': 'Kokoro',
    'HTM': 'Hitomi', 'MOM': 'Momiji', 'HEL': 'Helena', 'MIS': 'Misaki', 'LEI': 'Leifang',
}
GAME = 'extracted'
MODS = 'mods'


class Game:
    """Every file of the game and its DLC, by path."""

    def __init__(self, game_dir=GAME):
        if not os.path.isdir(os.path.join(game_dir, 'romfs')):
            raise SystemExit(f'{game_dir}/romfs not found: run from the repo root, after extracting the game')
        self.dir = game_dir
        self.files = {}  # path -> (Package, Record)
        for p in ktpkg.find_packages(game_dir):
            pk = ktpkg.Package(p)
            for r in pk.records:
                self.files[r.path] = (pk, r)

    def read(self, path):
        pk, r = self.files[path]
        if r.loose:  # (in the RomFS itself, next to the package folder)
            return open(os.path.join(os.path.dirname(pk.folder), *path.split('/')), 'rb').read()
        return pk.read(r)

    def match(self, pattern):
        if not any(c in pattern for c in '*?['):
            return [pattern] if pattern in self.files else []
        return sorted(p for p in self.files if fnmatch.fnmatchcase(p, pattern))


def mod_path(mod, game_path):
    return os.path.join(MODS, mod, 'romfs', *game_path.split('/'))


def write_mod_file(mod, game_path, data):
    out = mod_path(mod, game_path)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'wb') as f:
        f.write(data)
    print(f'  {out} ({len(data):,} bytes)')


def girl_code(g):
    g = g.upper()
    for code, name in GIRLS.items():
        if g in (code, name.upper(), name.upper().split()[0]):
            return code
    raise SystemExit(f'unknown girl {g!r}: one of ' + ', '.join(f'{c} ({n})' for c, n in GIRLS.items()))


def girl_files(game, code):
    """{kind: [paths]} of what makes up a girl. Costumes are character/<who wears it>/costume/<design>_<kind>_<n>_<v>/
    (the design prefix names the girl whose design it was: Marie can wear a Kasumi design, fitted to her body); the
    base game has Kasumi's, each character DLC package (00DLC...) one girl's."""
    kinds = {}
    for p in sorted(game.files):
        parts = p.split('/')
        k = None
        if len(parts) > 2 and parts[1] == 'character' and parts[2] == code:
            sub = parts[3] if len(parts) > 4 else 'data'
            k = {'face': 'face', 'hair': 'hair', 'bodybase': 'body skin', 'animation': 'animation',
                 'costume': 'costume (models she wears)'}.get(sub, 'data')
        elif parts[1:3] == ['sound', 'char'] and f'_{code}_' in parts[-1]:
            k = 'voice'
        elif parts[1] == 'voice_msg' and parts[-1].endswith(f'_{code}.bin'):
            k = 'voice text table'
        elif parts[1] == 'ui' and re.search(rf'(^|_){code}(_|$)', parts[3] if len(parts) > 3 else ''):
            k = 'ui: ' + re.sub(rf'_?{code}.*', '', parts[3]).lower()
        if k:
            kinds.setdefault(k, []).append(p)
    return kinds


# ------------------------------------------------------------------ commands
def cmd_girls(a):
    game = Game()
    print('code  name         files: face, hair, costumes she wears, voice banks')
    for code, name in GIRLS.items():
        k = girl_files(game, code)
        n = lambda key: sum(len(v) for kk, v in k.items() if kk.split(' (')[0] == key)
        print(f'{code}   {name:12} {n("face"):3} {n("hair"):3} {n("costume"):5} {n("voice"):3}')


def cmd_list(a):
    game = Game()
    for p in sorted(game.files):
        if a.text.lower() in p.lower():
            pk, r = game.files[p]
            print(f'{r.size:12,}  {"loose " if r.loose else "packed"}  {p}')


def cmd_girl_files(a):
    game = Game()
    code = girl_code(a.girl)
    for k, paths in sorted(girl_files(game, code).items()):
        print(f'== {k} ({len(paths)})')
        for p in paths if a.all or len(paths) <= 12 else paths[:12] + ['  ... (--all to see every one)']:
            print('  ' + p)


def cmd_extract(a):
    game = Game()
    paths = game.match(a.pattern)
    if not paths:
        raise SystemExit(f'no game file matches {a.pattern} (see: list)')
    for p in paths:
        out = os.path.join(a.out, *p.split('/'))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        d = game.read(p)
        open(out, 'wb').write(d)
        print(f'{len(d):12,}  {out}')


def cmd_copy(a):
    game = Game()
    if a.src not in game.files:
        raise SystemExit(f'{a.src} is not a game file')
    if a.dst not in game.files:
        raise SystemExit(f'{a.dst} is not a game file (a mod can only replace the game\'s own files)')
    write_mod_file(a.mod, a.dst, game.read(a.src))


def cmd_swap_look(a):
    """TARGET wears SOURCE's face, hairstyles and body skin. Hairstyles pair up by number (hair_00 to hair_00, ...);
    one TARGET has that SOURCE doesn't gets SOURCE's hair_00."""
    game = Game()
    t, s = girl_code(a.target), girl_code(a.source)
    want = {'face': 'face', 'hair': 'hair', 'body': 'bodybase'}
    kinds = [want[x] for x in a.parts.split(',') if x in want]
    done = 0
    for p in sorted(game.files):
        m = re.match(rf'00/character/{t}/(face|hair|bodybase)/', p)
        if not m or m.group(1) not in kinds:
            continue
        src = p.replace(f'/{t}/', f'/{s}/').replace(f'/{t}_', f'/{s}_')
        if src not in game.files and m.group(1) == 'hair':
            src = re.sub(r'HAIR_\d+', 'HAIR_00', re.sub(r'hair_\d+', 'hair_00', src))
        if src not in game.files:
            print(f'  ({GIRLS[s]} has no {src}: {p} stays)')
            continue
        write_mod_file(a.mod, p, game.read(src))
        done += 1
    print(f"{done} files: {GIRLS[t]} wears {GIRLS[s]}'s {a.parts} in mods/{a.mod}")


def bank_path(code):
    return f'00/sound/char/SND_CHAR_{code}_JP_XHS.xhs'


def cmd_voice_unpack(a):
    game = Game()
    code = girl_code(a.girl)
    streams = ktss.g1l_read(game.read(bank_path(code)))
    os.makedirs(a.dir, exist_ok=True)
    with open(os.path.join(a.dir, 'lines.txt'), 'w') as lst:
        lst.write(f'# {GIRLS[code]}: {len(streams)} voice lines (line number, seconds)\n')
        for i, s in enumerate(streams):
            k = ktss.Ktss(s)
            ktss.save_wav(os.path.join(a.dir, f'{i:03d}.wav'), k.decode(), k.rate, k.channels)
            lst.write(f'{i:03d} {k.samples / k.rate:6.2f}\n')
    print(f'{len(streams)} lines of {GIRLS[code]} in {a.dir}')


def numbered_audio(d):
    """{number: path} of DIR/NNN.<anything> (also NNN_name.ext)."""
    out = {}
    for n in sorted(os.listdir(d)):
        m = re.match(r'(\d+)\D?.*\.(wav|mp3|ogg|flac|m4a|aac|opus|wma)$', n, re.I)
        if m:
            out[int(m.group(1))] = os.path.join(d, n)
    return out


def cmd_voice_pack(a):
    game = Game()
    code = girl_code(a.girl)
    path = bank_path(code)
    streams = ktss.g1l_read(game.read(path))
    repl = numbered_audio(a.dir)
    bad = [n for n in repl if n >= len(streams)]
    if bad:
        raise SystemExit(f'{GIRLS[code]} has lines 0-{len(streams) - 1}: no line {bad}')
    for n, f in sorted(repl.items()):
        t = ktss.Ktss(streams[n])
        pcm = ktss.load_audio(f, t.rate, t.channels)
        streams[n] = ktss.encode_like(streams[n], pcm)
        print(f'  line {n:03d} <- {os.path.basename(f)} ({len(pcm) / 2 / t.rate:.2f} s)')
    write_mod_file(a.mod, path, ktss.g1l_write(streams))
    print(f'{len(repl)} of {len(streams)} lines replaced')


def xws_paths(game, code):
    return sorted(p for p in game.files if re.match(rf'00/sound/char/SND_CHAR_{code}_(\w+_)?JP_XWS\.xws$', p))


def bank_dir(root, path, b, nbanks):
    return os.path.join(root, os.path.basename(path)[:-4] + (f'_bank{b}' if nbanks > 1 else ''))


def cmd_reactions_unpack(a):
    import kwb
    game = Game()
    for path in xws_paths(game, girl_code(a.girl)):
        x = kwb.Xws(game.read(path))
        n = 0
        for b, bank in enumerate(x.banks):
            out = bank_dir(a.dir, path, b, len(x.banks))
            os.makedirs(out, exist_ok=True)
            for i, variants in bank.sounds():
                for j, v in enumerate(variants):
                    ktss.save_wav(os.path.join(out, f'{i:03d}_{j}.wav'), bank.decode(v), v.rate, v.channels)
                    n += 1
        print(f'{n} sounds of {path} in {a.dir} (NNN_V.wav: sound NNN, variant V - the game picks a variant)')


def cmd_reactions_pack(a):
    import kwb
    game = Game()
    for path in xws_paths(game, girl_code(a.girl)):
        x = kwb.Xws(game.read(path))
        n = 0
        for b, bank in enumerate(x.banks):
            d = bank_dir(a.dir, path, b, len(x.banks))
            if not os.path.isdir(d):
                continue
            for name in sorted(os.listdir(d)):
                m = re.match(r'(\d+)(?:_(\d+))?\.(wav|mp3|ogg|flac|m4a|aac|opus|wma)$', name, re.I)
                if not m:
                    continue
                i = int(m.group(1))
                variants = bank.sound(i)
                if not variants:
                    raise SystemExit(f'{d}: there is no sound {i}')
                which = variants if m.group(2) is None else variants[int(m.group(2)):int(m.group(2)) + 1]
                if not which:
                    raise SystemExit(f'{d}/{name}: sound {i} has {len(variants)} variants')
                for v in which:
                    bank.replace(v, ktss.load_audio(os.path.join(d, name), v.rate, v.channels))
                    n += 1
                print(f'  {os.path.basename(d)} sound {i:03d}' + (f' variant {m.group(2)}' if m.group(2) else ' (all variants)')
                      + f' <- {name}')
        if n:
            out = x.build()
            write_mod_file(a.mod, path, out)
            if len(out) > len(x.raw):
                print(f'  WARNING: {XWS_TOO_BIG}')


# (tested: banks 1.8x and 2.1x their size hung the game on the loading screen before a volleyball match; at or below
# their size they play. The dialogue banks (.xhs) are streamed: 1.6x their size played fine.)
XWS_TOO_BIG = ('this bank is bigger than the game\'s. The game keeps these banks in memory, and much bigger ones hang '
               'it on a loading screen: trim or shorten the sounds')


# ------------------------------------------------------------------ UI textures (portraits)
def kslt_index(game):
    """[(path, texture name, format, w, h)] of every UI texture pack, cached in mod_work/kslt_index.json."""
    import json
    import kslt
    cache = os.path.join('mod_work', 'kslt_index.json')
    if os.path.exists(cache):
        return [tuple(e) for e in json.load(open(cache))]
    print('indexing the UI textures (once, ~1 minute)...')
    idx = []
    for p in sorted(game.files):
        if p.endswith('.kslt'):
            for t in kslt.Kslt(game.read(p)).textures:
                idx.append((p, t.name, t.format, t.width, t.height))
    os.makedirs('mod_work', exist_ok=True)
    json.dump(idx, open(cache, 'w'))
    return idx


def portrait_names(idx, code, everything):
    """{texture name: [packs that hold it]} of a girl's UI textures: her portraits, icons, profile and name plates
    (everything: also her cutscene / pose thumbnails and swimsuit icons)."""
    out = {}
    for path, name, fmt, w, h in idx:
        tokens = name.lower().split('_')
        if code.lower() not in tokens:
            continue
        scene = name.split('_')[0] in ('CNT', 'POSE') or '/ITEM_ICON' in path or '/CONTE_THUMBNAIL' in path or \
            '/POSE_THUMBNAIL' in path or '/ISLAND_ADD' in path
        if scene and not everything:
            continue
        out.setdefault(name, []).append(path)
    return out


def pick_pack(paths):
    """The pack to export a texture from: the English one when there are language versions."""
    for p in paths:
        if '_ENG_' in p:
            return p
    return sorted(paths, key=len)[0]


def export_textures(game, wanted, out_dir):
    """wanted: {texture name: [packs]}: each texture as out_dir/<name>.png; returns the lines of its list."""
    import imgio
    import kslt
    os.makedirs(out_dir, exist_ok=True)
    by_pack = {}
    for name, paths in wanted.items():
        by_pack.setdefault(pick_pack(paths), []).append(name)
    lines = []
    for pack, names in sorted(by_pack.items()):
        k = kslt.Kslt(game.read(pack))
        for name in sorted(names):
            t = k.by_name[name]
            if not t.supported:
                lines.append(f'{name}: format {t.format} with {t.levels} levels, not handled')
                continue
            imgio.save_png(os.path.join(out_dir, name + '.png'), t.width, t.height, t.rgba())
            lines.append(f'{name}.png  {t.width}x{t.height}  (in {len(wanted[name])} pack(s), e.g. {pack})')
            print('  ' + lines[-1])
    return lines


def import_textures(game, wanted, in_dir, mod, keep_alpha):
    """Every in_dir/<texture name>.<image> replaces that texture in each pack that holds it, into mod."""
    import imgio
    import kslt
    files = {}
    for n in os.listdir(in_dir):
        base, ext = os.path.splitext(n)
        if ext.lower() in ('.png', '.jpg', '.jpeg', '.webp', '.bmp', '.tga', '.gif', '.tif', '.tiff') and base in wanted:
            files[base] = os.path.join(in_dir, n)
    if not files:
        raise SystemExit(f'no image in {in_dir} is named after one of these textures: {", ".join(sorted(wanted))}')
    packs = {}
    for name in files:
        for p in wanted[name]:
            packs.setdefault(p, []).append(name)
    for pack, names in sorted(packs.items()):
        k = kslt.Kslt(game.read(pack))
        for name in names:
            t = k.by_name[name]
            if not t.supported:
                print(f'  {name}: format {t.format} with {t.levels} levels, not handled: left as it is')
                continue
            rgba = bytearray(imgio.load_rgba(files[name], t.width, t.height))
            if keep_alpha:  # (the original's shape: round icons, cut-out bodies)
                orig = t.rgba()
                for i in range(3, len(rgba), 4):
                    rgba[i] = rgba[i] * orig[i] // 255
            t.set_rgba(bytes(rgba))
            print(f'  {pack}: {name} <- {os.path.basename(files[name])}')
        write_mod_file(mod, pack, bytes(k.data))


def cmd_portraits_unpack(a):
    game = Game()
    code = girl_code(a.girl)
    wanted = portrait_names(kslt_index(game), code, a.all)
    lines = export_textures(game, wanted, a.dir)
    with open(os.path.join(a.dir, 'textures.txt'), 'w') as f:
        f.write(f'# {GIRLS[code]}\'s UI textures. Edit or replace the PNGs (keep the names; any size: they are scaled\n'
                f'# to the size listed), then: doaxmod.py portraits-pack {code} <this folder> <mod>\n')
        f.write('\n'.join(lines) + '\n')
    print(f'{len(lines)} textures of {GIRLS[code]} in {a.dir} (list: textures.txt)')


def cmd_portraits_pack(a):
    game = Game()
    code = girl_code(a.girl)
    import_textures(game, portrait_names(kslt_index(game), code, True), a.dir, a.mod, a.keep_alpha)


def cmd_kslt_unpack(a):
    game = Game()
    import kslt
    paths = game.match(a.pattern)
    if not paths:
        raise SystemExit(f'no game file matches {a.pattern}')
    wanted = {}
    for p in paths:
        for t in kslt.Kslt(game.read(p)).textures:
            wanted.setdefault(t.name, []).append(p)
    export_textures(game, wanted, a.dir)


def cmd_kslt_pack(a):
    game = Game()
    import kslt
    paths = game.match(a.pattern)
    wanted = {}
    for p in paths:
        for t in kslt.Kslt(game.read(p)).textures:
            wanted.setdefault(t.name, []).append(p)
    import_textures(game, wanted, a.dir, a.mod, a.keep_alpha)


def cmd_tmc_info(a):
    if os.path.isfile(a.file):
        d = open(a.file, 'rb').read()
    else:
        d = Game().read(a.file)
    kt_tree(d)


def kt_tree(d, base=0, depth=0, label=''):
    """The chunk tree of a Koei Tecmo container (TMC, TMCL, XWSFILE, ...): each chunk's tag, offset and size."""
    def tag(o):
        t = d[o:o + 8].split(b'\0')[0]
        return t.decode('latin1') if t and all(32 <= c < 127 for c in t) else None
    ver, hsz, size, cnt, valid = struct.unpack_from('<IIIII', d, base + 8)
    offs_off, sizes_off = struct.unpack_from('<II', d, base + 0x20)
    print('  ' * depth + f'{label}{tag(base) or "?"} @{base:#x} size {size:#x}, {cnt} chunks')
    if not offs_off or depth > 4 or cnt > 4096:
        return
    offs = struct.unpack_from(f'<{cnt}I', d, base + offs_off)
    sizes = struct.unpack_from(f'<{cnt}I', d, base + sizes_off) if sizes_off else [0] * cnt
    for i, (o, s) in enumerate(zip(offs, sizes)):
        if not o:
            continue
        t = tag(base + o)
        if t and base + o + 0x14 <= len(d) and struct.unpack_from('<I', d, base + o + 12)[0] in (0x30, 0x40, 0x50):
            kt_tree(d, base + o, depth + 1, f'[{i}] ')
        else:
            print('  ' * (depth + 1) + f'[{i}] data @{base + o:#x}' + (f' size {s:#x}' if s else ''))


# ------------------------------------------------------------------ models (faces, hairs, ...)
def model_paths(game, target):
    """(TMC path, TMCL path) of a model: its _TMC.tmc game path (or the _TMCL one, or the path without the suffix)."""
    base = re.sub(r'_TMCL?\.tmcl?$', '', target)
    tmc_p, tmcl_p = base + '_TMC.tmc', base + '_TMCL.tmcl'
    if tmc_p not in game.files or tmcl_p not in game.files:
        raise SystemExit(f'{target}: not a model of the game (looked for {tmc_p} and {tmcl_p}; see girl-files)')
    return tmc_p, tmcl_p


def cmd_model_export(a):
    import tmc
    game = Game()
    tmc_p, tmcl_p = model_paths(game, a.target)
    m = tmc.Model(game.read(tmc_p), game.read(tmcl_p))
    base = os.path.basename(tmc_p)[:-len('_TMC.tmc')]
    n = tmc.export_gltf(m, a.dir, base, textures=not a.no_textures)
    with open(os.path.join(a.dir, 'model.txt'), 'w') as f:
        f.write(f'# {tmc_p}: {n} meshes (one per submesh, named "<object>#<submesh>"), {len(m.node_names)} bones, '
                f'{len(m.textures)} textures\n')
        for obj in m.objects:
            for sm in obj.submeshes:
                f.write(f'{sm.name:28} {sm.vertex_count:6} vertices  textures {sm.textures}  material {sm.material}\n')
        f.write('\ntextures:\n')
        for i in range(len(m.textures)):
            fmt, w, h, mips, layers, _ = tmc.g1t_parse(m.g1t(i))
            name = tmc.G1T_FORMATS.get(fmt, (hex(fmt),))[0]
            png = os.path.exists(os.path.join(a.dir, f'tex_{i:02d}.png'))
            f.write(f'  tex_{i:02d}.png  {w}x{h} {name}, {mips} levels{", " + str(layers) + " layers" if layers > 1 else ""}'
                    f'{texture_role(m, i) if png else ""}'
                    f'{"" if png or a.no_textures else "  (not exported: pip install texture2ddecoder to decode BC7)"}\n')
        f.write('\nbones:\n' + ''.join(f'  {i:3} {nm}\n' for i, nm in enumerate(m.node_names)))
    print(f'{n} meshes of {tmc_p} in {a.dir}/{base}.gltf (summary: model.txt)')


def texture_role(m, i):
    """A guess at what texture i is, from its look: normal maps are light blue ((128, 128, 255) on average)."""
    import tmc
    dec = tmc.g1t_decode(m.g1t(i))
    if not dec:
        return ''
    px = dec[2]
    n = len(px) // 4
    step = max(1, n // 4096)
    r = sum(px[k * 4] for k in range(0, n, step)) / len(range(0, n, step))
    g = sum(px[k * 4 + 1] for k in range(0, n, step)) / len(range(0, n, step))
    b = sum(px[k * 4 + 2] for k in range(0, n, step)) / len(range(0, n, step))
    if b > 190 and abs(r - 128) < 45 and abs(g - 128) < 45:
        return '  - normal map (replace with your own, or a flat one: colour (128, 128, 255))'
    if max(r, b) > 30 and g * 1.6 < min(r, b):
        return '  - a mask / shading map (purple: keep it, or replace it laid out for your UVs)'
    return '  - colour'


def cmd_model_import(a):
    import imgio
    import tmc
    game = Game()
    tmc_p, tmcl_p = model_paths(game, a.target)
    m = tmc.Model(game.read(tmc_p), game.read(tmcl_p))
    if not a.gltf and not a.textures:
        raise SystemExit('nothing to import: give --gltf FILE and/or --textures DIR')
    if a.textures:  # (tex_NN.<image> in the folder: replace texture NN, same size and format)
        for name in sorted(os.listdir(a.textures)):
            mt = re.match(r'tex_(\d+)\.(png|jpe?g|webp|bmp|tga|tiff?)$', name, re.I)
            if not mt:
                continue
            i = int(mt.group(1))
            if i >= len(m.textures):
                raise SystemExit(f'{name}: the model has textures 0-{len(m.textures) - 1}')
            g = m.g1t(i)
            fmt, w, h, mips, layers, _ = tmc.g1t_parse(g)
            if not a.all_textures and _same_as_export(m, i, os.path.join(a.textures, name)):
                continue  # (the exported texture, unchanged)
            new = tmc.g1t_replace(g, imgio.load_rgba(os.path.join(a.textures, name), w, h),
                                  alpha_ref=round(a.alpha_coverage * 255) if a.alpha_coverage else None)
            at, size = m.textures[i]
            m.tmcl[at:at + size] = new
            print(f'  texture {i:02d} <- {name} ({w}x{h} {tmc.G1T_FORMATS[fmt][0]})')
    parts = {}
    if a.gltf:
        parts, unknown = tmc.read_gltf_parts(a.gltf, m)
        for u in unknown:
            print(f'  (ignored: mesh "{u}" isn\'t named "<object>#<submesh>" after one of this model\'s)')
        for (oi, num), ps in sorted(parts.items()):
            print(f'  {m.objects[oi].name}#{num} <- ' + ', '.join(f'{p["name"]} ({len(p["pos"])} vertices)' for p in ps))
    if getattr(a, 'parts_hook', None):  # (a script calling this: change the parts and the skeleton before the rebuild)
        a.parts_hook(m, parts)
    for name in [h for h in (a.hide or '').split(',') if h]:  # (one zero-area triangle each, as --only-given's)
        hit = [(o, s) for o in m.objects for s in o.submeshes if s.name == name]
        if not hit:
            raise SystemExit(f'--hide {name}: the model has no such submesh (see model.txt)')
        o, s = hit[0]
        parts[(o.index, s.number)] = [tmc._degenerate(m, o, s)]
        print(f'  {name} hidden')
    new_tmc, new_tmcl, stats = tmc.import_parts(m, parts, renpack_off=not a.keep_renpack, only_given=a.only_given,
                                                 copy_weights=a.copy_weights, rigid=a.rigid, hold=a.hold_from_colour,
                                                 copy_k=a.copy_k)
    for name, bone in stats['rigid']:
        print(f'  {name}: every vertex on bone {bone} (--rigid)')
    for name, foreign in stats['copied']:
        why = f'its bones ({", ".join(foreign)}, ...) aren\'t this model\'s' if foreign else \
            'copied as asked' if a.copy_weights else 'it has no bone weights'
        print(f'  {name}: bone weights copied from the nearest original vertex ({why})')
    if stats['trimmed']:
        print(f'  ({stats["trimmed"]} vertices had more bone weights than the game keeps (12): the strongest are used)')
    for name, n, bone in stats['held']:
        print(f'  {name}: {n} vertices held to {bone} by their colour (--hold-from-colour)')
    for bone, to in sorted(stats['reparented']):
        print(f'  (weights on {bone} moved to {to}: the object can\'t use {bone})')
    if a.material_colour:  # (MtrCol: per material 14 rows of 4 floats; a hair material's row 8 is its highlight)
        new_tmc = bytearray(new_tmc)
        mats = tmc.kt_offsets(new_tmc, m.chunks['MtrCol'])
        for spec in a.material_colour:
            mt = re.match(r'^(\d+):(\d+)=([-\d.]+|\*),([-\d.]+|\*),([-\d.]+|\*),([-\d.]+|\*)$', spec)
            if not mt or int(mt.group(1)) >= len(mats) or int(mt.group(2)) >= 14:
                raise SystemExit(f'--material-colour {spec}: give MATERIAL:ROW=R,G,B,A (materials 0-{len(mats) - 1}, '
                                 f'rows 0-13)')
            at = mats[int(mt.group(1))] + 16 * int(mt.group(2))
            print(f'  material {mt.group(1)} row {mt.group(2)}: {[round(x, 3) for x in struct.unpack_from("<4f", new_tmc, at)]}'
                  f' -> {list(mt.groups()[2:])}')
            for k, v in enumerate(mt.groups()[2:]):
                if v != '*':  # (* keeps it: some aren't floats)
                    struct.pack_into('<f', new_tmc, at + 4 * k, float(v))
        new_tmc = bytes(new_tmc)
    write_mod_file(a.mod, tmc_p, new_tmc)
    write_mod_file(a.mod, tmcl_p, new_tmcl)


def _same_as_export(m, i, path):
    """Is the image the texture as model-export wrote it (so it needn't be re-encoded)?"""
    import tmc
    dec = tmc.g1t_decode(m.g1t(i))
    if not dec or not path.lower().endswith('.png'):
        return False
    import imgio
    try:
        return imgio.load_rgba(path, dec[0], dec[1]) == dec[2]
    except SystemExit:
        return False


def cmd_check(a):
    game = Game()
    root = os.path.join(MODS, a.mod, 'romfs')
    if not os.path.isdir(root):
        raise SystemExit(f'{root} not found')
    problems = 0
    for dp, dn, fn in os.walk(root):
        for n in sorted(fn):
            host = os.path.join(dp, n)
            path = os.path.relpath(host, root).replace(os.sep, '/')
            data = open(host, 'rb').read()
            if path not in game.files:
                print(f'NOT A GAME FILE  {path} (a mod can only replace the game\'s own files)')
                problems += 1
                continue
            orig = game.read(path)
            note = ''
            if orig[:8] != data[:8] and orig[:4] != data[:4]:
                note = f'  WARNING: starts with {data[:8]!r}, the game\'s file with {orig[:8]!r}'
                problems += 1
            if path.endswith('.xhs'):
                try:
                    a_n, b_n = len(ktss.g1l_read(orig)), len(ktss.g1l_read(data))
                    for s in ktss.g1l_read(data):
                        ktss.Ktss(s)
                    if a_n != b_n:
                        note += f'  WARNING: {b_n} lines, the game has {a_n}'
                        problems += 1
                except (AssertionError, struct.error) as e:
                    note += f'  ERROR: not a valid voice bank ({e})'
                    problems += 1
            if path.endswith('.xws') and len(data) > len(orig):
                note += f'  WARNING: {XWS_TOO_BIG}'
                problems += 1
            pk, r = game.files[path]
            print(f'ok  {path}  {len(data):,} bytes (game: {len(orig):,}, {"loose" if r.loose else "packed"}){note}')
    print(f'{problems} problem(s)')
    return 1 if problems else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('girls').set_defaults(fn=cmd_girls)
    p = sub.add_parser('list'); p.add_argument('text', nargs='?', default=''); p.set_defaults(fn=cmd_list)
    p = sub.add_parser('girl-files'); p.add_argument('girl'); p.add_argument('--all', action='store_true')
    p.set_defaults(fn=cmd_girl_files)
    p = sub.add_parser('extract'); p.add_argument('pattern'); p.add_argument('--out', default='mod_work/extracted')
    p.set_defaults(fn=cmd_extract)
    p = sub.add_parser('copy'); p.add_argument('src'); p.add_argument('dst'); p.add_argument('mod')
    p.set_defaults(fn=cmd_copy)
    p = sub.add_parser('swap-look'); p.add_argument('target'); p.add_argument('source'); p.add_argument('mod')
    p.add_argument('--parts', default='face,hair,body', help='any of face,hair,body (default: all three)')
    p.set_defaults(fn=cmd_swap_look)
    for name, fn in (('voice-unpack', cmd_voice_unpack), ('reactions-unpack', cmd_reactions_unpack)):
        p = sub.add_parser(name); p.add_argument('girl'); p.add_argument('dir'); p.set_defaults(fn=fn)
    for name, fn in (('voice-pack', cmd_voice_pack), ('reactions-pack', cmd_reactions_pack)):
        p = sub.add_parser(name); p.add_argument('girl'); p.add_argument('dir'); p.add_argument('mod')
        p.set_defaults(fn=fn)
    p = sub.add_parser('portraits-unpack'); p.add_argument('girl'); p.add_argument('dir')
    p.add_argument('--all', action='store_true', help='also her cutscene / pose thumbnails and swimsuit icons')
    p.set_defaults(fn=cmd_portraits_unpack)
    p = sub.add_parser('portraits-pack'); p.add_argument('girl'); p.add_argument('dir'); p.add_argument('mod')
    p.add_argument('--keep-alpha', action='store_true', help="keep each texture's own transparency (its shape)")
    p.set_defaults(fn=cmd_portraits_pack)
    p = sub.add_parser('kslt-unpack'); p.add_argument('pattern'); p.add_argument('dir'); p.set_defaults(fn=cmd_kslt_unpack)
    p = sub.add_parser('kslt-pack'); p.add_argument('pattern'); p.add_argument('dir'); p.add_argument('mod')
    p.add_argument('--keep-alpha', action='store_true'); p.set_defaults(fn=cmd_kslt_pack)
    p = sub.add_parser('model-export'); p.add_argument('target'); p.add_argument('dir')
    p.add_argument('--no-textures', action='store_true'); p.set_defaults(fn=cmd_model_export)
    p = sub.add_parser('model-import'); p.add_argument('target'); p.add_argument('mod')
    p.add_argument('--gltf', help='the edited or new model (.gltf / .glb), meshes named "<object>#<submesh>"')
    p.add_argument('--textures', help='a folder of tex_NN.<image>: texture NN replaced (same size and format)')
    p.add_argument('--all-textures', action='store_true', help='re-encode even the textures model-export wrote unchanged')
    p.add_argument('--keep-renpack', action='store_true', help="keep the face's RENPACK (made for its original mesh)")
    p.add_argument('--only-given', action='store_true',
                   help='submeshes of the objects the .gltf replaces that it does not give are emptied (hidden)')
    p.add_argument('--hide', help='these submeshes emptied (hidden), comma separated: WGT_face#1,WGT_zdmodel#0')
    p.add_argument('--copy-k', type=int, default=4, metavar='N',
                   help='copied weights blend the N nearest original vertices (default 4; more is smoother: long hair '
                        'on a girl whose own hair is shaped differently)')
    p.add_argument('--alpha-coverage', type=float, metavar='CUT',
                   help="the textures' smaller mips keep as much alpha above CUT (0-1) as the full size: alpha-cut hair "
                        'stops thinning out with distance')
    p.add_argument('--hold-from-colour', action='store_true',
                   help="each vertex's colour (red, 0-1, the glTF's COLOR_0) is how much of it is held to the head "
                        "bone: paint hair roots red so they stay on the scalp while the ends swing")
    p.add_argument('--material-colour', action='append', metavar='M:ROW=R,G,B,A',
                   help="a material's colour row (MtrCol) set (* keeps a value): a hair material's row 8 is its highlight colour "
                        "(Kasumi 0.9,0.75,0.65 warm; Momiji 1.25,1.3,1.35)")
    p.add_argument('--rigid', action='store_true',
                   help="every vertex on the object's main bone: follows the head, no hair physics / expressions")
    p.add_argument('--copy-weights', action='store_true',
                   help='every mesh takes its bone weights from the nearest original vertex (else only those without '
                        'weights or with other bones)')
    p.set_defaults(fn=cmd_model_import)
    p = sub.add_parser('tmc-info'); p.add_argument('file'); p.set_defaults(fn=cmd_tmc_info)
    p = sub.add_parser('check'); p.add_argument('mod'); p.set_defaults(fn=cmd_check)
    a = ap.parse_args()
    return a.fn(a) or 0


if __name__ == '__main__':
    sys.exit(main())

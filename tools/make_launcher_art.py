"""The launcher's title logo from your own game files: the title screen's DEAD OR ALIVE Xtreme 3 Scarlet logo
(00/ui/01_01_title/LAYOUT_01_01_TITLE_KSLT.kslt, texture doax3_logo_ps4, in romfs/00's package), cropped to what's
drawn, as a PNG with its transparency. The launcher (launcher/launcher.c) reads it when it starts, if it's there.
It's the game's artwork, so it stays in build/: never committed, never in a release.
  python tools/make_launcher_art.py extracted build/launcher_art/logo.png
Art is optional: when the game files don't have it, this says so and stops without an error, and the launcher shows
the title as text."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mods'))
import imgio  # noqa: E402
import kslt  # noqa: E402
import ktpkg  # noqa: E402

PACKAGE = os.path.join('romfs', '00')
KSLT = '00/ui/01_01_title/LAYOUT_01_01_TITLE_KSLT.kslt'
TEXTURE = 'doax3_logo_ps4'


def logo(game_dir):
    """(width, height, rgba) of the logo, cropped to its visible pixels."""
    pkg = ktpkg.Package(os.path.join(game_dir, PACKAGE))
    rec = pkg.by_path.get(KSLT)
    if rec is None:
        raise LookupError(f'{KSLT} is not in {PACKAGE}')
    tex = kslt.Kslt(pkg.read(rec)).by_name.get(TEXTURE)
    if tex is None or not tex.supported:
        raise LookupError(f'{TEXTURE} is not in {KSLT}, or not in a format this reads')
    w, h, px = tex.width, tex.height, tex.rgba()
    rows = [y for y in range(h) if any(px[(y * w + x) * 4 + 3] > 8 for x in range(w))]
    if not rows:
        raise LookupError(f'{TEXTURE} is empty')
    cols = [x for x in range(w) if any(px[(y * w + x) * 4 + 3] > 8 for y in rows)]
    x0, x1, y0, y1 = min(cols), max(cols) + 1, min(rows), max(rows) + 1
    out = b''.join(px[(y * w + x0) * 4:(y * w + x1) * 4] for y in range(y0, y1))
    return x1 - x0, y1 - y0, out


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    game_dir, out = sys.argv[1:]
    try:
        w, h, rgba = logo(game_dir)
    except (OSError, LookupError, AssertionError, ValueError) as e:
        print(f'make_launcher_art: no title logo for the launcher ({e}); it shows the title as text')
        return
    os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
    imgio.save_png(out + '.tmp', w, h, rgba)
    os.replace(out + '.tmp', out)
    print(f'{out}: {w}x{h}')


if __name__ == '__main__':
    main()

"""Optional (make GAME_ICON=1): the exes' icon from your own copy's home menu icon (extracted/control/
icon_AmericanEnglish.dat, a 256x256 JPEG), as a Windows .ico with every size Explorer, the taskbar and the title bar ask
for. It's the game's artwork, so it's never committed: the default icon is assets/icon.ico. From 32 px up it's the whole
picture; below that it's a blur, so the small sizes crop to a detail.
  python tools/make_icon.py extracted/control/icon_AmericanEnglish.dat build/icon/icon.ico"""
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mods'))
import imgio  # noqa: E402

SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
FACE = 'crop=72:72:150:6,'  # (in the 256x256 picture)
FACE_UP_TO = 24


def png_bytes(src, n):
    r = subprocess.run([imgio.ffmpeg(), '-v', 'error', '-i', src, '-vf',
                        f'{FACE if n <= FACE_UP_TO else ""}scale={n}:{n}:flags=lanczos', '-frames:v', '1',
                        '-pix_fmt', 'rgba', '-f', 'rawvideo', '-'], capture_output=True)
    if r.returncode or len(r.stdout) != n * n * 4:
        raise SystemExit(f'{src}: ffmpeg could not read it as an image: {r.stderr.decode(errors="replace").strip()}')
    fd, tmp = tempfile.mkstemp(suffix='.png')
    os.close(fd)
    try:
        imgio.save_png(tmp, n, n, r.stdout)
        with open(tmp, 'rb') as f:
            return f.read()
    finally:
        os.remove(tmp)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src, out = sys.argv[1:]
    images = [png_bytes(src, n) for n in SIZES]
    # ICONDIR, then one ICONDIRENTRY per image (0 means 256), then the PNGs (Windows Vista and later read PNG entries)
    off = 6 + 16 * len(images)
    head = struct.pack('<HHH', 0, 1, len(images))
    for n, png in zip(SIZES, images):
        head += struct.pack('<BBBBHHII', n % 256, n % 256, 0, 0, 1, 32, len(png), off)
        off += len(png)
    os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
    with open(out + '.tmp', 'wb') as f:
        f.write(head + b''.join(images))
    os.replace(out + '.tmp', out)
    print(f'{out}: {len(images)} sizes, {off} bytes')


if __name__ == '__main__':
    main()

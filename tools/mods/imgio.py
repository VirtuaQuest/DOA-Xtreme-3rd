"""Images for the mod tools: PNG out (no libraries needed), any image in through ffmpeg (MSYS2's), scaled to a size."""
import os
import shutil
import struct
import subprocess
import zlib

MSYS_BIN = r'C:\msys64\ucrt64\bin'


def save_png(path, w, h, rgba):
    """rgba: w * h * 4 bytes, rows top to bottom."""
    raw = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def ffmpeg():
    ff = shutil.which('ffmpeg') or os.path.join(MSYS_BIN, 'ffmpeg.exe')
    if not os.path.exists(ff) and not shutil.which('ffmpeg'):
        raise SystemExit('ffmpeg not found: install MSYS2 UCRT64 ffmpeg (pacman -S mingw-w64-ucrt-x86_64-ffmpeg)')
    return ff


def load_rgba(path, w, h):
    """Any image ffmpeg reads (PNG with transparency, JPG, WebP, ...), scaled to exactly w x h: w * h * 4 bytes."""
    r = subprocess.run([ffmpeg(), '-v', 'error', '-i', path, '-vf', f'scale={w}:{h}:flags=lanczos', '-frames:v', '1',
                        '-pix_fmt', 'rgba', '-f', 'rawvideo', '-'], capture_output=True)
    if r.returncode or len(r.stdout) != w * h * 4:
        raise SystemExit(f'{path}: ffmpeg could not read it as an image: {r.stderr.decode(errors="replace").strip()}')
    return r.stdout

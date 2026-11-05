"""Draws this project's own icon (assets/icon.ico, assets/logo.png) from nothing but the shapes below: a sun over two
waves on a rounded square. It uses no artwork from any game. Only needed to change the icon, and needs Pillow:
  python assets/make_icon.py"""
import math
import os

from PIL import Image, ImageChops, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SIZE = 1024
TOP, BOTTOM = (20, 184, 166), (37, 56, 140)  # teal to indigo
SUN = (255, 214, 130)
WAVES = ((255, 255, 255, 235), (255, 255, 255, 150))


def gradient():
    im = Image.new('RGB', (SIZE, SIZE))
    px = im.load()
    for y in range(SIZE):
        t = y / (SIZE - 1)
        c = tuple(round(a + (b - a) * t) for a, b in zip(TOP, BOTTOM))
        for x in range(SIZE):
            px[x, y] = c
    return im.convert('RGBA')


def wave(draw, base, amplitude, length, phase, colour):
    """Everything below a sine line, in this colour."""
    top = [(x, base + amplitude * math.sin(2 * math.pi * x / length + phase)) for x in range(0, SIZE + 8, 8)]
    draw.polygon(top + [(SIZE, SIZE), (0, SIZE)], fill=colour)


im = gradient()
layer = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
d = ImageDraw.Draw(layer)
d.ellipse((322, 170, 702, 550), fill=SUN)
wave(d, 560, 46, 620, 0.6, WAVES[1])
wave(d, 690, 40, 500, 2.4, WAVES[0])
im = Image.alpha_composite(im, layer)
mask = Image.new('L', (SIZE, SIZE), 0)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, SIZE - 1, SIZE - 1), radius=210, fill=255)
im.putalpha(ImageChops.multiply(im.getchannel('A'), mask))
im.resize((256, 256), Image.LANCZOS).save(os.path.join(HERE, 'logo.png'))
im.resize((256, 256), Image.LANCZOS).save(os.path.join(HERE, 'icon.ico'),
                                          sizes=[(n, n) for n in (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)])
print('assets/icon.ico, assets/logo.png')

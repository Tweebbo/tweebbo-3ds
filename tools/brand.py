#!/usr/bin/env python3
"""HOME Menu icon (48×48), banner (256×128) and banner sound, with avatars drawn by the game.

Requires build/brand/avatar*.png (node tools/export/brand.mjs) and build/export (room backgrounds).
"""
import math
import struct
import wave
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
B = ROOT / 'build/brand'
avatars = [Image.open(B / f'avatar{i}.png').convert('RGBA') for i in range(4)]


def bbox_alpha(img):
    return img.getchannel('A').getbbox()


# --- icon: full bust (whole head and shoulders), at 1:1, on a clean gradient ---
a = avatars[0]
x0, y0, x1, y1 = bbox_alpha(a)
cx = (x0 + x1) // 2
icon = Image.new('RGBA', (48, 48))
d = ImageDraw.Draw(icon)
for y in range(48):  # light sky, like the web background
    t = y / 47
    d.line([(0, y), (47, y)], fill=(int(0xcf + (0xf4 - 0xcf) * t), int(0xe9 + (0xfa - 0xe9) * t), 255, 255))
bust = a.crop((cx - 24, y0 - 6, cx + 24, y0 + 42))
icon.alpha_composite(bust, (0, 0))
icon.convert('RGB').save(ROOT / 'assets/icon.png')

# --- banner: a classic room with four Tweebbos and the name ---
room = Image.open(ROOT / 'build/export/data/classic/model_a.png').convert('RGBA')
# Back wall with the windows and the floor of classic room A.
bg = room.crop((372, 52, 628, 180))
banner = Image.new('RGBA', (256, 128), (0xe8, 0xe8, 0xe0, 255))
banner.alpha_composite(bg)
for img, x in zip(avatars, (18, 70, 122, 176)):
    ax0, ay0, ax1, ay1 = bbox_alpha(img)
    banner.alpha_composite(img, (x - ax0 + 4, 128 - ay1 - 4))
d = ImageDraw.Draw(banner)
try:
    font = ImageFont.truetype('DejaVuSans-Bold.ttf', 24)
except OSError:
    font = ImageFont.load_default()
text = 'Tweebbo'
box = d.textbbox((0, 0), text, font=font)
tx, ty = 256 - (box[2] - box[0]) - 10, 6
for dx in (-2, -1, 0, 1, 2):
    for dy in (-2, -1, 0, 1, 2):
        d.text((tx + dx, ty + dy - box[1]), text, font=font, fill=(0x11, 0x19, 0x20))
d.text((tx, ty - box[1]), text, font=font, fill=(0xff, 0xd3, 0x4d))
banner.convert('RGB').save(ROOT / 'assets/banner.png')

# --- banner sound: a short synthesized chime ---
rate, secs = 22050, 1.4
frames = bytearray()
for i in range(int(rate * secs)):
    t = i / rate
    v = 0.0
    for start, freq in ((0.0, 1046.5), (0.12, 1318.5), (0.24, 1568.0)):
        if t >= start:
            u = t - start
            v += math.sin(2 * math.pi * freq * u) * math.exp(-u * 4.5) * 0.28
    frames += struct.pack('<h', int(max(-1, min(1, v)) * 32767))
with wave.open(str(ROOT / 'assets/banner.wav'), 'wb') as f:
    f.setnchannels(1)
    f.setsampwidth(2)
    f.setframerate(rate)
    f.writeframes(bytes(frames))
print('assets/icon.png, assets/banner.png, assets/banner.wav')

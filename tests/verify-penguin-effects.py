#!/usr/bin/env python3
"""Compare native decoded effect layers with the existing rendered scene atlas."""
import json
import re
import sys
import zlib
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[2]
report = json.loads((root / 'tests-penguin-effects.json').read_text())
assert len(report['layers']) == 626
for pack, generated in [(root / '3ds/build', root / '3ds/source/gen'), (root / 'switch/build/pack-hd', root / 'switch/source/gen')]:
    source = (generated / 'assets.c').read_text()
    body = re.search(r'static const SceneArt scene_art_igloo_waddle\[\] = \{(.*?)\n\};', source, re.S)[1]
    rows = re.findall(r'\{"penguin-(\d+)-(base|body)", (\d+), (\d+), (\d+), (\d+), (\d+), (-?\d+), (-?\d+)\}', body)
    assert len(rows) == 626
    actual = {(frame, layer): (width, height, crc) for frame, layer, width, height, crc in report['layers']}
    pages = {}
    for frame, layer, page, x, y, width, height, rx, ry in rows:
        page, x, y, width, height = map(int, (page, x, y, width, height))
        if page not in pages:
            pages[page] = Image.open(pack / 'gfx' / f'scene_igloo_waddle_{page}.png').convert('RGBA')
        raw = pages[page].crop((x, y, x + width, y + height)).tobytes()
        expected = width, height, zlib.crc32(raw)
        assert actual[int(frame), int(layer == 'body')] == expected, (pack, frame, layer)
    print(f'PASS: all 626 decoded layers match {pack.relative_to(root)} scene PNG pixels')
print('PASS: ' + ', '.join(report['checks']))

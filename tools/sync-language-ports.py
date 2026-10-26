#!/usr/bin/env python3
"""Refresh generated language assets without repacking room and furniture atlases."""
import argparse
import os
import shutil
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--check', action='store_true')
parser.add_argument('ports', nargs='*', type=Path)
args = parser.parse_args()
ports = args.ports or [root.parent / 'Tweebbo-Switch', root.parent / 'Tweebbo-Vita']
files = [(root / 'source/gen/font.c', Path('source/gen/font.c'))]
files += [(root / f'build/gfx/{name}', Path(f'romfs/gfx/{name}'))
          for name in ('font0.png', 'font-zh.png', 'font-ja.png')]
files += [(root / f'romfs/lang/{name}', Path(f'romfs/lang/{name}'))
          for name in ('manifest.json', 'es.bin', 'zh.bin', 'ja.bin', 'fr.bin', 'pt-BR.bin')]
for source, _ in files:
    if not source.is_file():
        parser.error(f'Missing generated input: {source}; import the pack and build the fonts first')
for port in ports:
    if not (port / 'source').is_dir() or not (port / 'romfs').is_dir():
        parser.error(f'Not a native port: {port}')

different = []
for port in ports:
    for source, relative in files:
        target = port / relative
        if target.is_file() and target.read_bytes() == source.read_bytes():
            continue
        different.append(target)
        print(('Stale: ' if args.check else 'Updated: ') + str(target))
        if not args.check:
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = target.with_name(target.name + '.tmp')
            # A fresh mtime makes the NRO/VPK dependency notice the changed content.
            shutil.copyfile(source, temporary)
            os.replace(temporary, target)
if args.check and different:
    raise SystemExit(1)
print(f'PASS: {len(files)} generated language files matched in {len(ports)} ports.')

#!/usr/bin/env python3
"""Install a generated pet pack without replacing platform-specific room definitions."""
import re
import shutil
import sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
pack = Path(sys.argv[1])
pattern = r'const PetPart pet_parts\[.*?const int pet_kind_count = \d+;'
source = (pack / 'gen/assets.c').read_text()
block = re.search(pattern, source, re.S).group(0)
for repo, graphics in [(root, 'build/gfx'), (root.parent/'Tweebbo-Switch', 'romfs/gfx'), (root.parent/'Tweebbo-Vita', 'romfs/gfx')]:
    target = repo / 'source/gen/assets.c'
    current = target.read_text()
    updated, count = re.subn(pattern, lambda _: block, current, flags=re.S)
    assert count == 1, target
    if updated != current: target.write_text(updated)
    for image in (pack/'gfx').glob('pets_*.png'):
        shutil.copy2(image, repo/graphics/image.name)
    print(repo.name, 'pet declarations and atlas pages installed')

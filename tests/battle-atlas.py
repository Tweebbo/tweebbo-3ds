"""Round-trip every generated native sprite back to its original bitmap."""
import json
import os
import re
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
web = Path(os.environ['TWEEBBO_WEB'])
art = json.loads((web/'client/public/activities/art.json').read_text())
original = [Image.open(web/'client/public/activities'/p).convert('RGBA') for p in art['pages']]
overlay = json.loads((web/'client/src/battle-presentation.json').read_text())['overlay']
lookup = json.loads((root/'build/battle/lookup.json').read_text())
rows = [tuple(map(int,m)) for m in re.findall(r'\{(\d+),(\d+),(\d+),(\d+),(\d+),0,0\}',(root/'source/gen/battle_assets.c').read_text())]
count = 0
for arena,bank in lookup['banks'].items():
    pages = {}
    for name,sprite in bank.items():
        page,x,y,w,h = rows[sprite['id']]
        if page not in pages:
            file = root/f'build/battle/arena{arena}_{page}.png'
            pages[page] = Image.open(file).convert('RGBA')
            for target in ['Tweebbo-Switch','Tweebbo-Vita']:
                assert file.read_bytes() == (root.parent/target/'romfs/battle'/file.name).read_bytes()
        if name in overlay:
            expected = Image.open(web/'client/public/battleball'/overlay[name]['file']).convert('RGBA')
            reg = overlay[name]['reg']
        else:
            s = art['sprites'][name]
            expected = original[s['page']].crop((s['x'],s['y'],s['x']+s['w'],s['y']+s['h']))
            reg = s['reg']
        assert sprite['reg'] == reg
        assert expected.size == (w,h)
        assert pages[page].crop((x,y,x+w,y+h)).tobytes() == expected.tobytes(), (arena,name)
        count += 1
print(f'PASS: {count} native Battle Ball sprites in five arenas match original pixels, registration points and generated C coordinates; Switch/Vita atlas files match.')

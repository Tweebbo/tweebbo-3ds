import json
import re
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
report = json.loads((root/'build/fastfood/source.json').read_text())
assert report['rgbaBytes'] == 4*1024*1024 and report['resolution'] == 3 and report['edgePadding'] == 1
atlas = Image.open(root/'build/fastfood/color0.png').convert('RGBA')
source = (root/'source/gen/fastfood_assets.c').read_text()
rects = [tuple(map(int, row.split(','))) for row in re.findall(r'\{(\d+,\d+,\d+,\d+,\d+,0,0)\}', source)]
assert len(rects) == 205
for i, (page,x,y,w,h,_,_) in enumerate(rects):
    assert page == 0 and x >= 1 and y >= 1 and x+w < atlas.width and y+h < atlas.height
    for _,bx,by,bw,bh,_,_ in rects[:i]:
        assert x+w+1 <= bx-1 or bx+bw+1 <= x-1 or y+h+1 <= by-1 or by+bh+1 <= y-1
    for dx in range(w):
        assert atlas.getpixel((x+dx,y-1)) == atlas.getpixel((x+dx,y))
        assert atlas.getpixel((x+dx,y+h)) == atlas.getpixel((x+dx,y+h-1))
    for dy in range(h):
        assert atlas.getpixel((x-1,y+dy)) == atlas.getpixel((x,y+dy))
        assert atlas.getpixel((x+w,y+dy)) == atlas.getpixel((x+w-1,y+dy))
print('PASS: 205 sprites at one-third resolution, non-overlapping extruded edges, one 4 MiB page')

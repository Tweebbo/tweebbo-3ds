#!/usr/bin/env python3
"""Packs the web client export for the 3DS.

Input: build/export (from tools/export/run.mjs).
Output:
  build/gfx/*.png           atlas pages (1024x1024) and room backgrounds
  source/gen/assets.c       sprite indices, catalogue, wardrobe and rooms

build.sh converts every PNG in build/gfx into romfs/gfx/*.t3x with tex3ds.
"""
import hashlib
import json
import re
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build/export'
GFX = ROOT / 'build/gfx'
GEN = ROOT / 'source/gen'
PAGE = 1024
GAP = 1

# Rooms included in the offline mode: public model key.
ROOMS = ['newbie_lobby']
# Private room backgrounds that are packed (classic models).
# Classic icons the interface draws scaled down (name, maximum side in pixels).
SMALL_ICONS = [('building_icon', 24), ('brochure_icon', 24), ('hand_icon', 26), ('help_icon', 24), ('mes_lite_icon', 24),
               ('credits_icon', 14), ('mes_smallbuddy_head', 13), ('building_icon', 18)]
# Public rooms with their own layers.
SCENES = ['park_a', 'pool_a']
CLASSIC = ['model_a', 'model_b', 'model_c', 'model_d', 'model_e', 'model_f']
# Catalogue flag bits used by the simulation and the renderer.
FLAG_BITS = ['can_sit_on_top', 'can_lay_on_top', 'can_stack_on_top', 'teleporter', 'gate', 'solid', 'redeemable']


class Packer:
    """Simple shelves on PAGE x PAGE pages, with deduplication."""

    def __init__(self):
        self.pages = [Image.new('RGBA', (PAGE, PAGE))]
        self.x = self.y = self.shelf = 0
        self.seen = {}

    def add(self, img):
        img = img.convert('RGBA')
        key = hashlib.sha1(img.tobytes() + bytes(str(img.size), 'ascii')).hexdigest()
        if key in self.seen:
            return self.seen[key]
        w, h = img.size
        if w + GAP > PAGE or h + GAP > PAGE:
            raise SystemExit(f'sprite too large: {w}x{h}')
        if self.x + w + GAP > PAGE:
            self.x, self.y, self.shelf = 0, self.y + self.shelf, 0
        if self.y + h + GAP > PAGE:
            self.pages.append(Image.new('RGBA', (PAGE, PAGE)))
            self.x = self.y = self.shelf = 0
        page = len(self.pages) - 1
        self.pages[page].paste(img, (self.x, self.y))
        rect = (page, self.x, self.y, w, h)
        self.x += w + GAP
        self.shelf = max(self.shelf, h + GAP)
        self.seen[key] = rect
        return rect


def pack_def(images):
    """Packs the layers of one furniture item into the smallest power-of-two texture.

    Returns (canvas, rects) with rects[i] = (x, y, w, h)."""
    order = sorted(range(len(images)), key=lambda i: -images[i].height)
    best = None
    for width in (64, 128, 256, 512, 1024):
        if max(im.width for im in images) + GAP > width:
            continue
        x = y = shelf = 0
        rects = [None] * len(images)
        for i in order:
            w, h = images[i].size
            if x + w + GAP > width:
                x, y, shelf = 0, y + shelf, 0
            rects[i] = (x, y, w, h)
            x += w + GAP
            shelf = max(shelf, h + GAP)
        height = max(8, 1 << (y + shelf - 1).bit_length())
        if height > 1024:
            continue
        if not best or width * height < best[0] * best[1]:
            best = (width, height, rects)
    if not best:
        raise SystemExit('furniture too large for a 1024x1024 texture')
    width, height, rects = best
    canvas = Image.new('RGBA', (width, height))
    for im, (x, y, _, _) in zip(images, rects):
        canvas.paste(im, (x, y))
    return canvas, rects


def c_str(s):
    s = s.replace('\\', '\\\\').replace('"', '\\"').replace('\r', '').replace('\n', '\\n').replace('\t', ' ')
    # "??" followed by certain characters would form a trigraph.
    return '"' + s.replace('??', '?\\?') + '"'


def main():
    GFX.mkdir(parents=True, exist_ok=True)
    GEN.mkdir(parents=True, exist_ok=True)
    for old in GFX.glob('*.png'):
        old.unlink()
    # Two groups: grey parts (tinted on the GPU) on LA8 pages at half the memory, the rest in RGBA8.
    color_packer, gray_packer = Packer(), Packer()

    class Dual:
        def add(self, img):
            img = img.convert('RGBA')
            px = [q for q in img.get_flattened_data() if q[3]] if hasattr(img, 'get_flattened_data') else [q for q in img.getdata() if q[3]]
            gray = all(abs(q[0] - q[1]) < 3 and abs(q[1] - q[2]) < 3 for q in px)
            r = (gray_packer if gray else color_packer).add(img)
            return ('g' if gray else 'c',) + r
    packer = Dual()

    # Pets: the same layout in every texture, one texture per breed palette.
    pets_data = json.loads((SRC / 'data/models/pets.json').read_text())
    pet_src = Image.open(SRC / 'data/models/pets-0.png').convert('RGBA')
    pet_names = sorted(pets_data['sprites'], key=lambda n: n.encode())
    order = sorted(pet_names, key=lambda n: -pets_data['sprites'][n]['h'])
    PW = 512
    x = y = shelf = 0
    pet_rects = {}
    for n in order:
        sp = pets_data['sprites'][n]
        if x + sp['w'] + 1 > PW:
            x, y, shelf = 0, y + shelf, 0
        pet_rects[n] = (x, y)
        x += sp['w'] + 1
        shelf = max(shelf, sp['h'] + 1)
    PH = max(8, 1 << (y + shelf - 1).bit_length())
    pet_palettes = sorted(pets_data['palettes'])
    for pi, pal_name in enumerate(pet_palettes):
        pal = pets_data['palettes'][pal_name]
        page = Image.new('RGBA', (PW, PH))
        for n in pet_names:
            sp = pets_data['sprites'][n]
            crop = pet_src.crop((sp['x'], sp['y'], sp['x'] + sp['w'], sp['y'] + sp['h']))
            px = crop.load()
            out = Image.new('RGBA', crop.size)
            po = out.load()
            for yy in range(sp['h']):
                for xx in range(sp['w']):
                    c = pal[px[xx, yy][0]]
                    po[xx, yy] = (c[0], c[1], c[2], 255)
            # Director matte: white connected to the border is transparent.
            seen = set()
            stack = [(xx, yy) for xx in range(sp['w']) for yy in (0, sp['h'] - 1)] + [(xx, yy) for yy in range(sp['h']) for xx in (0, sp['w'] - 1)]
            while stack:
                xx, yy = stack.pop()
                if xx < 0 or yy < 0 or xx >= sp['w'] or yy >= sp['h'] or (xx, yy) in seen:
                    continue
                if po[xx, yy][:3] != (255, 255, 255):
                    continue
                seen.add((xx, yy))
                stack += [(xx - 1, yy), (xx + 1, yy), (xx, yy - 1), (xx, yy + 1)]
            for xx, yy in seen:
                po[xx, yy] = (255, 255, 255, 0)
            page.paste(out, pet_rects[n])
        page.save(GFX / f'pets_{pi}.png')

    # --- avatars: parts sorted by name (binary search in C) ---
    people = json.loads((SRC / 'people.json').read_text())
    people.sort(key=lambda s: s['name'].encode())
    # Sorting by height packs the shelves better.
    order = sorted(range(len(people)), key=lambda i: -people[i]['h'])
    rects = {}
    for i in order:
        s = people[i]
        rects[i] = packer.add(Image.open(SRC / 'people' / f"{s['name']}.png"))

    # --- furniture: one texture per definition (romfs:/furni/d<id>.t3x), loaded on demand ---
    catalogue = json.loads((SRC / 'data/models/catalogue.json').read_text())
    furni = json.loads((SRC / 'furni.json').read_text())
    furni.sort(key=lambda v: (v['def'], v['rot'], v['state'], v.get('frame', 0)))
    FURNI = GFX / 'furni'
    FURNI.mkdir(parents=True, exist_ok=True)
    for old in FURNI.glob('*.png'):
        old.unlink()
    icons = {}
    by_def = {}
    for v in furni:
        by_def.setdefault(v['def'], []).append(v)
    for def_id, variants in by_def.items():
        images, index = [], {}
        def add(img):
            img = img.convert('RGBA')
            key = hashlib.sha1(img.tobytes() + bytes(str(img.size), 'ascii')).hexdigest()
            if key not in index:
                index[key] = len(images)
                images.append(img)
            return index[key]
        for v in variants:
            for p in v['parts']:
                p['img'] = add(Image.open(SRC / 'furni' / p['file']))
        asset = catalogue['definitions'][str(def_id)].get('asset') or {}
        icon_file = SRC / 'data/icons' / (asset.get('icon') or '-')
        icon = add(Image.open(icon_file)) if asset.get('icon') and icon_file.exists() else None
        canvas, def_rects = pack_def(images)
        canvas.save(FURNI / f'd{def_id}.png')
        for v in variants:
            for p in v['parts']:
                p['rect'] = (0,) + def_rects[p['img']]
        if icon is not None:
            icons[def_id] = def_rects[icon]

    # --- swimming bodies (pool), white so they can be tinted ---
    swim_file = SRC / 'swimming.json'
    swim = json.loads(swim_file.read_text()) if swim_file.exists() else []
    swim.sort(key=lambda s: s['name'].encode())
    swim_rects = [packer.add(Image.open(SRC / 'swimming' / f"{s['name']}.png")) for s in swim]

    # --- classic interface (windows, buttons, bar icons), like the web ---
    classic_ui = json.loads((SRC / 'data/classic/index.json').read_text())['sprites']
    ui_names = sorted((n for n in classic_ui if not n.startswith(('room_hand', 'campic', 'mes_1', 'mes_2', 'mes_3', 'mes_4'))
                       and classic_ui[n]['w'] <= 320 and classic_ui[n]['h'] <= 240), key=lambda n: n.encode())
    # Only graphics the interface uses (named in the C code): saves linear memory.
    used_src = ' '.join(f.read_text() for f in (ROOT / 'source').glob('*.c'))
    ui_names = [n for n in ui_names if f'"{n}"' in used_src]
    ui_rects = {}
    for n in ui_names:
        f = SRC / 'data/classic' / classic_ui[n]['file']
        if f.exists():
            ui_rects[n] = packer.add(Image.open(f))
    # Icons shrunk in high quality (premultiplied alpha) to draw them at 1:1 on the console:
    # "<name>@<side>" fits in a square of that side.
    for n, box in SMALL_ICONS:
        f = SRC / 'data/classic' / classic_ui[n]['file']
        img = Image.open(f).convert('RGBA')
        k = min(box / img.width, box / img.height, 1)
        size = (max(1, round(img.width * k)), max(1, round(img.height * k)))
        small = img.convert('RGBa').resize(size, Image.LANCZOS).convert('RGBA') if k < 1 else img
        key = f'{n}@{box}'
        classic_ui[key] = {'reg': [0, 0]}
        ui_names.append(key)
        ui_rects[key] = packer.add(small)
    ui_names.sort(key=lambda n: n.encode())

    for old in GFX.glob('atlas*.png'):
        old.unlink()
    page_files = []
    for group, pk in (('c', color_packer), ('g', gray_packer)):
        for i, page in enumerate(pk.pages):
            if i == len(pk.pages) - 1:
                # The last page only takes the used height (power of two): saves linear memory.
                used = pk.y + pk.shelf
                page = page.crop((0, 0, PAGE, max(8, 1 << (used - 1).bit_length())))
            name = f'atlas{i}' if group == 'c' else f'atlasg{i}'
            page.save(GFX / f'{name}.png')
            page_files.append(name)
    n_color = len(color_packer.pages)

    def fix(r):
        group, page, x, y, w_, h_ = r
        return (page if group == 'c' else n_color + page, x, y, w_, h_)
    rects = {k: fix(v) for k, v in rects.items()}
    swim_rects = [fix(r) for r in swim_rects]
    ui_rects = {k: fix(v) for k, v in ui_rects.items()}

    # --- classic room backgrounds: each in its own power-of-two texture ---
    classic = json.loads((SRC / 'data/classic/index.json').read_text())
    backgrounds = []
    for name in CLASSIC:
        room = classic['rooms'][name]
        img = Image.open(SRC / 'data/classic' / room['file']).convert('RGBA')
        tw = 1 << (img.width - 1).bit_length()
        th = 1 << (img.height - 1).bit_length()
        canvas = Image.new('RGBA', (max(tw, 8), max(th, 8)))
        canvas.paste(img, (0, 0))
        canvas.save(GFX / f'room_{name}.png')
        backgrounds.append((name, room, img.size))

    wardrobe = json.loads((SRC / 'data/wardrobe-data.json').read_text())
    anims = json.loads((SRC / 'data/models/people.json').read_text())['animations']
    public_rooms = json.loads((SRC / 'data/public-rooms/index.json').read_text())
    drinks_src = (SRC / 'data/drinks.ts').read_text()

    out = []
    w = out.append
    w('// Generated by tools/pack.py. Do not edit by hand.')
    w('#include "assets.h"')
    w('')
    w(f'const int atlas_page_count = {len(page_files)};')
    w('const char *const atlas_page_files[] = {' + ', '.join(c_str(n) for n in page_files) + '};')
    w('')
    w(f'const PeopleSprite people_sprites[{len(people)}] = {{')
    for i, s in enumerate(people):
        page, x, y, sw, sh = rects[i]
        w(f"  {{{c_str(s['name'])}, {page}, {x}, {y}, {sw}, {sh}, {s['reg'][0]}, {s['reg'][1]}}},")
    w('};')
    w(f'const int people_sprite_count = {len(people)};')
    w(f'const PeopleSprite swim_sprites[{max(1, len(swim))}] = {{')
    for s_, (page, x, y, sw, sh) in zip(swim, swim_rects):
        w(f"  {{{c_str(s_['name'])}, {page}, {x}, {y}, {sw}, {sh}, {s_['reg'][0]}, {s_['reg'][1]}}},")
    w('};')
    w(f'const int swim_sprite_count = {len(swim)};')
    w('')
    ui_list = [n for n in ui_names if n in ui_rects]
    w(f'const UiSprite ui_sprites[{len(ui_list)}] = {{')
    for n in ui_list:
        page, x, y, sw, sh = ui_rects[n]
        reg = classic_ui[n]['reg']
        w(f"  {{{c_str(n)}, {page}, {x}, {y}, {sw}, {sh}, {reg[0]}, {reg[1]}}},")
    w('};')
    w(f'const int ui_sprite_count = {len(ui_list)};')
    w('')

    parts = []
    w('static const FurniPart furni_parts[] = {')
    first = []
    for v in furni:
        first.append(len(parts))
        for p in v['parts']:
            page, x, y, pw, ph = p['rect']
            flags = (1 if p['shadow'] else 0) | (2 if p['add'] else 0)
            depth = -10000.0 if p['shadow'] else p['depth']
            parts.append(1)
            w(f"  {{{page}, {x}, {y}, {pw}, {ph}, {p['x']}, {p['y']}, {depth:.4f}f, {round(p['alpha'] * 255)}, {flags}}},")
    if not parts:
        w('  {0},')
    w('};')
    w(f'const FurniVariant furni_variants[{max(1, len(furni))}] = {{')
    for v, start in zip(furni, first):
        w(f"  {{{v['def']}, {v['rot']}, {v['state']}, {v.get('frame', 0)}, {len(v['parts'])}, &furni_parts[{start}]}},")
    w('};')
    w(f'const int furni_variant_count = {len(furni)};')
    w('')

    # Full catalogue: needed for collisions and seats in any room.
    defs = sorted(catalogue['definitions'].values(), key=lambda d: d['id'])
    w(f'const FurniDef furni_defs[{len(defs)}] = {{')
    for d in defs:
        bits = 0
        for b, flag in enumerate(FLAG_BITS):
            if flag in d['flags']:
                bits |= 1 << b
        if (d.get('asset') or {}).get('name') in ('hologram', 'redhologram'):
            bits |= 128  # DEF_HOLO: 32 frames while state > 0
        rots = 0
        for r in d.get('rotations') or [0, 1, 2, 3]:
            rots |= 1 << r
        drink = (d.get('drinks') or [0])[0]
        ix, iy, iw, ih = icons.get(d['id'], (0, 0, 0, 0))
        tex = 1 if d['id'] in by_def else 0
        drinks = ','.join(str(x) for x in (d.get('drinks') or []))
        w(f"  {{{d['id']}, {d['kind']}, {d['width']}, {d['height']}, {d['seatHeight']}, {d['states']}, {bits}, {rots}, {drink}, {c_str(d['name'])}, {tex}, {ix}, {iy}, {iw}, {ih}, {c_str(drinks)}, {c_str(d.get('description') or '')}}},")
    w('};')
    w(f'const int furni_def_count = {len(defs)};')
    w('')

    # Shop catalogue: the same pages and offers as the web (ui.ts renderShop).
    pages = sorted((p for p in catalogue['pages'] if p.get('visible')), key=lambda p: (p['order'], int(p['id']) if p['id'].isdigit() else 0))
    w(f'const CatPage cat_pages[{len(pages)}] = {{')
    for pg in pages:
        body = (pg.get('body') or '').replace('\r', '\n')
        w(f"  {{{c_str(pg['id'])}, {c_str(pg['name'])}, {c_str(pg['layout'])}, {c_str(body)}}},")
    w('};')
    w(f'const int cat_page_count = {len(pages)};')
    offers = sorted((o for o in catalogue['offers'] if not o.get('hidden') and o['contents']), key=lambda o: (o['order'], o['id']))
    w(f'const CatOffer cat_offers[{len(offers)}] = {{')
    for o in offers:
        c = o['contents'][0]
        total = sum(x['amount'] * (2 if 'teleporter' in catalogue['definitions'].get(str(x['def']), {}).get('flags', []) else 1) for x in o['contents'])
        pages_s = ',' + ','.join(o['pages']) + ','
        w(f"  {{{o['id']}, {c['def']}, {total}, {1 if o.get('available') else 0}, {o['price']}, {c_str(o.get('name') or '')}, {c_str(o.get('description') or '')}, {c_str(pages_s)}}},")
    w('};')
    w(f'const int cat_offer_count = {len(offers)};')
    w('')

    # Wardrobe: the same lists and order as wardrobe-data.json.
    def parts_str(entry):
        return ' '.join(f'{k}:{v}' for k, v in entry['parts'].items())
    for key in ['hair', 'tops', 'bottoms', 'shoes', 'faces']:
        w(f'const WardrobeEntry wardrobe_{key}[{len(wardrobe[key])}] = {{')
        for e in wardrobe[key]:
            w(f"  {{{c_str(e['label'])}, {c_str(parts_str(e))}, '{e.get('sex', 'U')}'}},")
        w('};')
        w(f'const int wardrobe_{key}_count = {len(wardrobe[key])};')
    w('')

    # Dance animations (dance.1..4) per part.
    w('const DanceTrack dance_tracks[] = {')
    count = 0
    for name in sorted(anims):
        index = int(name.split('.')[1])
        for part, a in anims[name].items():
            n = len(a['act'])
            assert n <= 16, f'baile demasiado largo: {name} {part} {n}'
            acts = ', '.join(c_str(x) for x in a['act'])
            nums = lambda arr: ', '.join(str(int(x or 0)) for x in (arr + [0] * n)[:n])
            w(f"  {{{index}, {c_str(part)}, {n}, {{{acts}}}, {{{nums(a['frm'])}}}, {{{nums(a['OffX'])}}}, {{{nums(a['OffY'])}}}, {{{nums(a['OffD'])}}}}},")
            count += 1
    w('};')
    w(f'const int dance_track_count = {count};')
    w('')

    models = dict(re.findall(r'(\d+):\'(\d+)\'', drinks_src.split('drinkModels')[1].split(';')[0]))
    names = dict(re.findall(r"(\d+):'([^']*)'", drinks_src.split('drinkNames')[1].split(';')[0]))
    pets = json.loads((SRC / 'data/pet-options.json').read_text())
    w(f"const int pet_price = {pets['price']};")
    w(f'const PetPart pet_parts[{len(pet_names)}] = {{')
    for n in pet_names:
        sp = pets_data['sprites'][n]
        x_, y_ = pet_rects[n]
        w(f"  {{{c_str(n)}, {x_}, {y_}, {sp['w']}, {sp['h']}, {sp['reg'][0]}, {sp['reg'][1]}}},")
    w('};')
    w(f'const int pet_part_count = {len(pet_names)};')
    aliases = sorted(pets_data['aliases'].items(), key=lambda kv: kv[0].encode())
    w(f'const PetAlias pet_aliases[{len(aliases)}] = {{')
    for a, b in aliases:
        w(f"  {{{c_str(a)}, {c_str(b.rstrip('*'))}, {1 if b.endswith('*') else 0}}},")
    w('};')
    w(f'const int pet_alias_count = {len(aliases)};')
    offs = []
    for group, table in pets_data['offsets'].items():
        for k, pairs in table.items():
            offs.append((f'{group}/{k}', pairs))
    offs.sort(key=lambda kv: kv[0].encode())
    w(f'const PetOffset pet_offsets[{len(offs)}] = {{')
    for k, pairs in offs:
        pairs = (pairs + [[0, 0]] * 5)[:5]
        w(f"  {{{c_str(k)}, {{{', '.join(str(p[0]) for p in pairs)}}}, {{{', '.join(str(p[1]) for p in pairs)}}}}},")
    w('};')
    w(f'const int pet_offset_count = {len(offs)};')
    w(f'const char *const pet_palettes[{len(pet_palettes)}] = {{' + ', '.join(c_str(p) for p in pet_palettes) + '};')
    w(f'const int pet_palette_count = {len(pet_palettes)};')
    w(f'const char *const pet_kind_keys[{len(pets["kinds"])}] = {{' + ', '.join(c_str(k['key']) for k in pets['kinds']) + '};')
    w(f"const PetKind pet_kinds[{len(pets['kinds'])}] = {{")
    for k in pets['kinds']:
        cols = ', '.join('0x' + c.lstrip('#').lower() for c in k['colors'])
        w(f"  {{{c_str(k['name'])}, {k['breeds']}, {len(k['colors'])}, {{{cols}}}}},")
    w('};')
    w(f"const int pet_kind_count = {len(pets['kinds'])};")
    w('const DrinkName drink_names[] = {')
    for k, v in sorted(names.items(), key=lambda kv: int(kv[0])):
        w(f'  {{{k}, {c_str(v)}}},')
    w('};')
    w(f'const int drink_name_count = {len(names)};')
    w('const DrinkModel drink_models[] = {')
    for k, v in sorted(models.items(), key=lambda kv: int(kv[0])):
        w(f'  {{{k}, "{v}"}},')
    w('};')
    w(f'const int drink_model_count = {len(models)};')
    w('')

    # Floors of rooms without an original background (Renderer.buildFloor on the web).
    floors_file = SRC / 'floors.json'
    for f in (json.loads(floors_file.read_text()) if floors_file.exists() else []):
        img = Image.open(SRC / 'floors' / f"{f['key']}.png").convert('RGBA')
        tw = 1 << (img.width - 1).bit_length()
        th = 1 << (img.height - 1).bit_length()
        if tw > 1024 or th > 1024:
            print(f"warning: floor {f['key']} too large ({img.size})")
            continue
        canvas = Image.new('RGBA', (max(tw, 8), max(th, 8)))
        canvas.paste(img, (0, 0))
        canvas.save(GFX / f"room_{f['key']}.png")
        backgrounds.append((f['key'], {'x': f['x'], 'y': f['y'], 'door': f['door'], 'rows': f['rows']}, img.size))

    # Layered public rooms (park and pool): background, depth masks and animations.
    scenes = []
    for key in SCENES:
        r = public_rooms[key]
        sp = Packer()
        sprites = []
        bg_name = r.get('background') or 'park_bg'
        for l in r['layers']:
            frames = l.get('frames') or [l]
            for fi, fr in enumerate(frames):
                img = Image.open(SRC / 'data/public-rooms' / fr['file']).convert('RGBA')
                if l.get('flip'):
                    img = img.transpose(Image.FLIP_LEFT_RIGHT)
                page, x, y, w_, h_ = sp.add(img)
                depth = -10000.0 if l['name'] == bg_name else float(l['depth'])
                sprites.append((page, x, y, w_, h_, fr['x'], fr['y'], fr['w'], fr['h'], depth, round(l['alpha'] * 255),
                                len(frames), fi, 1 if l['name'] == bg_name else 0))
        for i, page in enumerate(sp.pages):
            if i == len(sp.pages) - 1:
                used = sp.y + sp.shelf
                page = page.crop((0, 0, PAGE, max(8, 1 << (used - 1).bit_length())))
            page.save(GFX / f'scene_{key}_{i}.png')
        scenes.append((key, r, len(sp.pages), sprites))

    w(f'const RoomBackground room_backgrounds[{len(backgrounds)}] = {{')
    for name, room, (iw, ih) in backgrounds:
        rows = '\\n'.join(room['rows'])
        w(f"  {{{c_str(name)}, \"room_{name}\", {room['x']}, {room['y']}, {iw}, {ih}, {room['door'][0]}, {room['door'][1]}, \"{rows}\"}},")
    w('};')
    w(f'const int room_background_count = {len(backgrounds)};')
    w('')
    for key, r, pages_n, sprites in scenes:
        w(f'static const SceneSprite scene_sprites_{key}[] = {{')
        for (page, x, y, w_, h_, dx, dy, dw, dh, depth, alpha, frames, fi, bg) in sprites:
            w(f'  {{{page}, {x}, {y}, {w_}, {h_}, {dx}, {dy}, {dw}, {dh}, {depth:.3f}f, {alpha}, {frames}, {fi}, {bg}}},')
        w('};')
    w(f'const Scene scenes[{max(1, len(scenes))}] = {{')
    for key, r, pages_n, sprites in scenes:
        water = r.get('waterHeight', -1)
        w(f'  {{{c_str(key)}, {pages_n}, {len(sprites)}, scene_sprites_{key}, {water}}},')
    w('};')
    w(f'const int scene_count = {len(scenes)};')
    w('')

    rooms = []
    for key in ROOMS:
        r = public_rooms[key]
        items = r.get('furniture', [])
        w(f'static const RoomItem room_items_{key}[] = {{')
        for it in items:
            w(f"  {{{it['id']}u, {it['def']}, {it['x']}, {it['y']}, {it['rot']}, {it['z']}}},")
        if not items:
            w('  {0},')
        w('};')
        rooms.append((key, r, len(items)))
    w(f'const RoomModel room_models[{len(rooms)}] = {{')
    for key, r, n in rooms:
        rows = '\\n'.join(r['rows'])
        target = r.get('entranceTarget') or [255, 255]
        w(f"  {{{c_str(key)}, {c_str(r['name'])}, {c_str(r.get('classicModel', ''))}, \"{rows}\", {r['door'][0]}, {r['door'][1]}, {r.get('direction', 2)}, {target[0]}, {target[1]}, {n}, room_items_{key}}},")
    w('};')
    w(f'const int room_model_count = {len(rooms)};')

    (GEN / 'assets.c').write_text('\n'.join(out) + '\n')
    src = json.loads((SRC / 'source.json').read_text())
    print(f"atlas pages: {n_color} RGBA8 + {len(gray_packer.pages)} LA8, unique sprites: {len(color_packer.seen) + len(gray_packer.seen)}, "
          f"avatar parts: {len(people)}, furniture variants: {len(furni)}, definitions: {len(defs)}")
    print(f"origen: {src['web']} ({src['exportedAt']})")


if __name__ == '__main__':
    main()

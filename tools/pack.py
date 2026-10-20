#!/usr/bin/env python3
"""Packs the web client export for the 3DS.

Input: build/export (from tools/export/run.mjs).
Output:
  build/gfx/*.png           atlas pages (1024x1024) and room backgrounds
  source/gen/assets.c       sprite indices, catalogue, wardrobe and rooms

build.sh converts every PNG in build/gfx into romfs/gfx/*.t3x with tex3ds.
"""
import hashlib
import os
import math
import struct
import io
import json
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
SRC = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build/export'
# PACK_OUT packs into another tree (the Switch/Vita fetch their own); PACK_HD=1 keeps the whole
# classic interface at its original size (catalogue headlines and every icon) for the consoles
# with memory to spare, instead of only the graphics the 3DS source names.
OUT = Path(os.environ['PACK_OUT']) if os.environ.get('PACK_OUT') else None
HD = os.environ.get('PACK_HD') == '1'
# Switch/Vita keep the original item state in the offline public rooms; the 3DS output
# keeps its historical six-field RoomItem until its source adopts the field.
WITH_STATE = os.environ.get('PACK_STATE') == '1'
GFX = OUT / 'gfx' if OUT else ROOT / 'build/gfx'
GEN = OUT / 'gen' if OUT else ROOT / 'source/gen'
PAGE = 1024
GAP = 1
GENERATED_PNGS = set()
def save_png(image, path):
    GENERATED_PNGS.add(path.resolve())
    data = io.BytesIO(); image.save(data, format='PNG'); raw = data.getvalue()
    if not path.exists() or path.read_bytes() != raw: path.write_bytes(raw)


# Rooms included in the offline mode: public room model keys, in navigator order.
ROOMS = ['newbie_lobby', 'park_a', 'pool_a', 'pool_b', 'md_a', 'casino', 'afterhours']
# Private room backgrounds that are packed (classic models).
# Classic icons the interface draws scaled down (name, maximum side in pixels).
SMALL_ICONS = [('building_icon', 24), ('brochure_icon', 24), ('hand_icon', 26), ('help_icon', 24), ('mes_lite_icon', 24),
               ('credits_icon', 14), ('mes_smallbuddy_head', 13), ('building_icon', 18),
               ('door_password', 18), ('bar_cigar_icon', 18), ('mes_dark_icon', 24), ('credits_icon', 18)]
# Classic 9-slice chrome drawn at 1:1 by the Switch client (the 3DS draws it with primitives).
# The generic "only names used by the 3DS source" filter below would drop them.
EXTRA_UI = [
    'window.top.left', 'window.top.middle', 'window.top.right',
    'window.middle.left', 'window.middle.middle', 'window.middle.right',
    'window.bottom.left', 'window.bottom.middle', 'window.bottom.right',
    'content.top.left', 'content.top.middle', 'content.top.right',
    'content.middle.left', 'content.middle.middle', 'content.middle.right',
    'content.bottom.left', 'content.bottom.middle', 'content.bottom.right',
    'button.a.left.active', 'button.a.middle.active', 'button.a.right.active',
    'button.a.left.pressed', 'button.a.middle.pressed',
    'button.b.left.active', 'button.b.middle.active', 'button.b.right.active',
    'button.b.left.pressed', 'button.b.middle.pressed', 'button.b.right.pressed',
    'field.left', 'field.middle', 'field.right',
    # Catalogue product slot (Switch/Vita catalogue grid, like the PC edition).
    'catalog_bg_icon_slot',
]
# Public rooms with their own layers.
SCENES = ['park_a', 'pool_a', 'pool_b', 'md_a']
# Layer roles (SceneSprite.role) of the Lido rooms: drawn under the floor, and the two states
# of the booth curtains and the lift door (open = 10 + 2k, closed = 11 + 2k), source/pool.c.
ROLE_UNDER = 1
SWITCHES = ['curtains1', 'curtains2', 'lift_door']
# Classic activity artwork (client/public/activities, tools/import-classic-activities.py) packed
# into the pages of the scene that uses it: name prefixes per scene.
SCENE_ART = {
    'pool_b': ['sh_', 'pelle_bg3', 'pomppulauta_4', 'pilvi', 'big_splash_', 'splash_'],
    'md_a': ['shp_', 'paalu hit ', 'startcount ', 'splash_'],
}
# The Switch/Vita changing booth uses the original ph_swimsuit.window pieces, so they
# must be in the scene pages of every Lido room with a booth (pool_a, pool_b, md_a).
if HD:
    for _scene in ('pool_a', 'pool_b', 'md_a'):
        SCENE_ART.setdefault(_scene, []).extend([
            'pelle1', 'ph_biggerbutton_', 'ph_smallerbutton_',
            'char.button.', 'char.part.viewer',
        ])
CLASSIC = ['model_a', 'model_b', 'model_c', 'model_d', 'model_e', 'model_f', 'casino_hall', 'lobby_hall', 'bar_hall']
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
        return None, None
    width, height, rects = best
    canvas = Image.new('RGBA', (width, height))
    for im, (x, y, _, _) in zip(images, rects):
        canvas.paste(im, (x, y))
    return canvas, rects


def has_soft_alpha(img):
    """True when some pixel is partly transparent: RGBA5551 would lose it."""
    hist = img.getchannel('A').histogram()
    return any(hist[1:255])


def c_str(s):
    s = s.replace('\\', '\\\\').replace('"', '\\"').replace('\r', '').replace('\n', '\\n').replace('\t', ' ')
    # "??" followed by certain characters would form a trigraph.
    return '"' + s.replace('??', '?\\?') + '"'


def main():
    GFX.mkdir(parents=True, exist_ok=True)
    GEN.mkdir(parents=True, exist_ok=True)
    # Linear memory is the 3DS's scarcest resource, so each sprite goes to the cheapest page
    # format that shows it exactly:
    #   'c' colour with on/off alpha  -> atlas<i>,  RGBA5551 (16 bpp)
    #   's' colour with soft alpha    -> atlass<i>, RGBA8 (32 bpp)
    #   'g' grey, tinted on the GPU   -> atlasg<i>, LA8 (16 bpp)
    #   'k' character skins           -> atlask<i>, RGBA8, loaded only while a skin is on screen
    color_packer, soft_packer, gray_packer, skin_packer = Packer(), Packer(), Packer(), Packer()
    # Textures that must stay RGBA8 (tools/convert-textures.sh reads build/gfx/soft-alpha.txt).
    soft_alpha = []

    class Dual:
        def add(self, img, skin=False):
            img = img.convert('RGBA')
            if skin:
                return ('k',) + skin_packer.add(img)
            px = [q for q in img.get_flattened_data() if q[3]] if hasattr(img, 'get_flattened_data') else [q for q in img.getdata() if q[3]]
            gray = all(q[0] == q[1] == q[2] for q in px)
            if gray:
                return ('g',) + gray_packer.add(img)
            soft = any(q[3] < 255 for q in px)
            return ('s',) + soft_packer.add(img) if soft else ('c',) + color_packer.add(img)
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
        save_png(page, GFX / f'pets_{pi}.png')

    # --- avatars: parts sorted by name (binary search in C) ---
    people = json.loads((SRC / 'people.json').read_text())
    extra_images = {}
    for folder, prefix in [('wardrobe-extras', 'x'), ('frank', 'f')]:
        atlas = json.loads((SRC / 'data' / folder / 'parts.json').read_text())
        source_pages = [Image.open(SRC / 'data' / folder / f).convert('RGBA') for f in atlas['pages']]
        for name, sp in atlas['sprites'].items():
            if not name.startswith('h_'):
                continue
            img = source_pages[sp['page']].crop((sp['x'], sp['y'], sp['x'] + sp['w'], sp['y'] + sp['h']))
            variants = []
            if prefix == 'x':
                # Split tintable pixels from fixed colours; GPU multiplication must not recolour details.
                tint, fixed = Image.new('RGBA', img.size), Image.new('RGBA', img.size)
                a, b, src = tint.load(), fixed.load(), img.load()
                for yy in range(img.height):
                    for xx in range(img.width):
                        q = src[xx, yy]
                        if max(q[:3]) - min(q[:3]) <= 12: a[xx, yy] = q
                        else: b[xx, yy] = q
                variants = [('x' + name[1:], tint), ('z' + name[1:], fixed)]
            else:
                part = name.split('_')[2]
                for tag, color in [('f', 0x9a9a9a if part == 'fa' else 0xb3202a), ('o', 0x14434c)]:
                    if tag == 'o' and part != 'cc': continue
                    out = img.copy(); px = out.load()
                    for yy in range(img.height):
                        for xx in range(img.width):
                            q = px[xx, yy]
                            if q[3] and max(q[:3]) - min(q[:3]) <= 24:
                                px[xx, yy] = tuple(round(q[k] * ((color >> (16 - k * 8)) & 255) / 255) for k in range(3)) + (q[3],)
                    if tag == 'o' and '_lay_' not in name and int(name.split('_')[-2]) in (1, 2, 3):
                        from PIL import ImageDraw
                        draw = ImageDraw.Draw(out); cx, cy = round(img.width*.55), round(img.height*.32)
                        for dx, dy, ww, hh in [(-3,0,1,1),(-4,1,1,1),(-3,2,1,1),(0,0,1,3),(2,0,1,1),(3,1,1,1),(2,2,1,1)]:
                            draw.rectangle((cx+dx,cy+dy,cx+dx+ww-1,cy+dy+hh-1), fill='#ebc96b')
                        ky=round(img.height*.76)
                        for dx,dy,ww,hh in [(-2,0,2,2),(-1,2,1,4),(0,4,2,1)]:
                            draw.rectangle((cx+dx,ky+dy,cx+dx+ww-1,ky+dy+hh-1),fill='#ebc96b')
                    variants.append((tag + name[1:], out))
            for key, img in variants:
                bounds = img.getbbox()
                if not bounds: continue
                img = img.crop(bounds)
                people.append({'name': key, 'w': img.width, 'h': img.height, 'reg': [sp['reg'][0] - bounds[0], sp['reg'][1] - bounds[1]]})
                extra_images[key] = img
    skin_dir = SRC / 'data/character-skins'
    skin_data = json.loads((skin_dir / 'index.json').read_text())
    skin_pages = [Image.open(skin_dir / f).convert('RGBA') for f in skin_data['pages']]
    skin_counts, skin_rates = [], []
    for skin_id, skin in enumerate(skin_data['skins'], 1):
        counts, rates = [], []
        for pose_id, pose in enumerate(['std', 'walk', 'sit', 'floor', 'wave', 'dance']):
            counts.append([len(c['frames']) for c in skin['clips'][pose]])
            rates.append([c['fps'] for c in skin['clips'][pose]])
            for direction, clip in enumerate(skin['clips'][pose]):
                for frame, index in enumerate(clip['frames']):
                    s = skin_data['sprites'][index]
                    img = skin_pages[s['page']].crop((s['x'], s['y'], s['x']+s['w'], s['y']+s['h']))
                    bounds = img.getbbox()
                    if not bounds: raise ValueError('Empty character frame')
                    img = img.crop(bounds)
                    name = f'k_{skin_id}_{pose_id}_{direction}_{frame}'
                    people.append({'name': name, 'w': img.width, 'h': img.height, 'reg': [bounds[0], bounds[1]]})
                    extra_images[name] = img
        skin_counts.append(counts)
        skin_rates.append(rates)
    def initializer(value):
        return '{' + ','.join(initializer(v) if isinstance(v, list) else str(v) for v in value) + '}'
    (GEN / 'character_skins.h').write_text('// Generated by tools/pack.py.\n#pragma once\n'
        + f'#define CHARACTER_SKIN_COUNT {len(skin_counts)}\n'
        + 'static const unsigned short character_skin_frames[][6][8] = ' + initializer(skin_counts) + ';\n'
        + 'static const float character_skin_fps[][6][8] = ' + initializer(skin_rates) + ';\n')
    people.sort(key=lambda s: s['name'].encode())
    # Sorting by height packs the shelves better.
    order = sorted(range(len(people)), key=lambda i: -people[i]['h'])
    rects = {}
    for i in order:
        s = people[i]
        rects[i] = packer.add(extra_images[s['name']] if s['name'] in extra_images else Image.open(SRC / 'people' / f"{s['name']}.png"),
                              skin=s['name'].startswith('k_'))

    # --- furniture: one texture per definition (romfs:/furni/d<id>.t3x), loaded on demand ---
    catalogue = json.loads((SRC / 'data/models/catalogue.json').read_text())
    furni = json.loads((SRC / 'furni.json').read_text())
    furni.sort(key=lambda v: (v['def'], v['rot'], v['state'], v.get('frame', 0)))
    FURNI = GFX / 'furni'
    FURNI.mkdir(parents=True, exist_ok=True)
    icons = {}
    textures = []
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
        # The web client's own icon for this definition (with its colours); older exports only
        # have the bank's shared, uncoloured -icon.png.
        icon_file = SRC / 'icons' / f'{def_id}.png'
        if not icon_file.exists():
            icon_file = SRC / 'data/icons' / (asset.get('icon') or '-')
        icon = add(Image.open(icon_file)) if icon_file.exists() and icon_file.is_file() else None
        canvas, def_rects = pack_def(images)
        if canvas is not None:
            canvases = [canvas]
            def_rects = [(0,) + r for r in def_rects]
        else:
            pk = Packer()
            def_rects = [None] * len(images)
            for i in sorted(range(len(images)), key=lambda i: -images[i].height):
                def_rects[i] = pk.add(images[i])
            canvases = pk.pages
            used = pk.y + pk.shelf
            canvases[-1] = canvases[-1].crop((0, 0, PAGE, max(8, 1 << (used - 1).bit_length())))
            print(f'Furniture {def_id}: {len(canvases)} texture pages (original resolution)')
        for page, canvas in enumerate(canvases):
            save_png(canvas, FURNI / f'd{def_id}_{page}.png')
            # RGBA5551 (half the memory) unless the art has soft alpha (glass, shadows, glows).
            soft = has_soft_alpha(canvas)
            if soft:
                soft_alpha.append(f'd{def_id}_{page}')
            textures.append((def_id, page, canvas.width * canvas.height * (4 if soft else 2)))
        for v in variants:
            for p in v['parts']:
                p['rect'] = def_rects[p['img']]
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
    if not HD:
        ui_names = [n for n in ui_names if f'"{n}"' in used_src]
    # Chrome the Switch client draws with the original 9-slice art.
    # Original catalogue page icons (ctlg_icon_*) for the Switch/Vita catalogue index.
    page_icons = {p['icon'] for p in json.loads((SRC / 'data/models/catalogue.json').read_text())['pages']
                  if p.get('visible') and p.get('icon') in classic_ui}
    ui_names = sorted(set(ui_names) | set(EXTRA_UI) | page_icons, key=lambda n: n.encode())
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
        # Area averaging: nearest-neighbour at ~1/2 scale drops whole rows and columns (the
        # navigator icon lost its blue fill and text lines). BOX keeps every source pixel's share.
        small = img.resize(size, Image.Resampling.BOX) if k < 1 else img
        key = f'{n}@{box}'
        classic_ui[key] = {'reg': [0, 0]}
        ui_names.append(key)
        ui_rects[key] = packer.add(small)
    qr = SRC / 'data/qr-site.png'
    if qr.exists():
        classic_ui['qr-site'] = {'reg': [0, 0]}
        ui_names.append('qr-site')
        ui_rects['qr-site'] = packer.add(Image.open(qr).convert('RGBA'))
    for badge in ['VIP', 'ADM', 'CLUB1', 'CLUB2']:
        name = 'badge-' + badge
        classic_ui[name] = {'reg': [0, 0]}
        ui_names.append(name)
        ui_rects[name] = packer.add(Image.open(SRC / 'data/badges' / f'{badge}.gif').convert('RGBA'))
    maintenance = Image.open(SRC / 'data/status/frank-maintenance.gif').convert('RGBA')
    classic_ui['frank-maintenance'] = {'reg': [0, 0]}
    ui_names.append('frank-maintenance')
    ui_rects['frank-maintenance'] = packer.add(maintenance)
    for path in sorted((SRC / 'data/casino').rglob('*.png')):
        if not any(p in path.parts for p in ('cards', 'video', 'slots')):
            continue
        name = 'casino-' + path.relative_to(SRC / 'data/casino').with_suffix('').as_posix().replace('/', '-')
        classic_ui[name] = {'reg': [0, 0]}
        ui_names.append(name)
        ui_rects[name] = packer.add(Image.open(path).convert('RGBA'))
    casino_icon = SRC / 'data/casino/icon.png'
    if casino_icon.exists():
        classic_ui['casino-icon'] = {'reg': [0, 0]}
        ui_names.append('casino-icon')
        ui_rects['casino-icon'] = packer.add(Image.open(casino_icon).convert('RGBA'))
    # The web's Games toolbar icon (duels.ts gamesIcon): a snowball behind a chess pawn, drawn the same way.
    games = Image.new('RGBA', (30, 30))
    g = ImageDraw.Draw(games)
    disc = lambda x, y, r, c: g.ellipse((x - r, y - r, x + r, y + r), fill=c)
    disc(20, 11, 9.5, '#1d2a33'); disc(20, 11, 8.5, '#d9e8f4'); disc(18.5, 9, 5.5, '#ffffff'); disc(23, 15, 3, '#a9c6dc')
    for grow, color in ((1, '#111111'), (0, '#3c3f4a')):
        disc(9, 12, 4 + grow, color)
        g.rectangle((5 - grow, 16 - grow, 13 + grow - 1, 19 + grow - 1), fill=color)
        g.polygon([(6 - grow, 19), (12 + grow, 19), (15 + grow, 25), (3 - grow, 25)], fill=color)
        g.rectangle((2 - grow, 25 - grow, 16 + grow - 1, 28 + grow - 1), fill=color)
    for box in ((7, 10, 8, 12), (6, 20, 7, 23), (4, 26, 7, 26)):
        g.rectangle(box, fill='#7a7f90')
    classic_ui['games-icon'] = {'reg': [0, 0]}
    ui_names.append('games-icon')
    ui_rects['games-icon'] = packer.add(games)
    ui_names.sort(key=lambda n: n.encode())

    page_files = []
    first_page = {}
    # Resident groups first; the skin pages come last and load on demand (atlas_page_resident).
    for group, pk, prefix in (('c', color_packer, 'atlas'), ('s', soft_packer, 'atlass'), ('g', gray_packer, 'atlasg'), ('k', skin_packer, 'atlask')):
        first_page[group] = len(page_files)
        if not pk.seen:
            continue
        for i, page in enumerate(pk.pages):
            if i == len(pk.pages) - 1:
                # The last page only takes the used height (power of two): saves linear memory.
                used = pk.y + pk.shelf
                page = page.crop((0, 0, PAGE, max(8, 1 << (used - 1).bit_length())))
            name = f'{prefix}{i}'
            save_png(page, GFX / f'{name}.png')
            page_files.append(name)
    for stale in GFX.glob('atlas*.png'):
        if stale.stem not in page_files:
            stale.unlink()
    n_color = len(color_packer.pages)
    resident_pages = first_page['k']

    def fix(r):
        group, page, x, y, w_, h_ = r
        return (first_page[group] + page, x, y, w_, h_)
    rects = {k: fix(v) for k, v in rects.items()}
    swim_rects = [fix(r) for r in swim_rects]
    ui_rects = {k: fix(v) for k, v in ui_rects.items()}

    # --- classic room backgrounds: each in its own power-of-two texture ---
    classic = json.loads((SRC / 'data/classic/index.json').read_text())
    backgrounds = []
    for name in CLASSIC:
        room = classic['rooms'][name]
        img = Image.open(SRC / 'data/classic' / room['file']).convert('RGBA')
        # Trim transparent margins on the right/bottom (offsets stay relative to the origin) and use
        # 1024x512 pages: a 1024x1024 RGBA8 page needs 4 MB of linear memory in one block.
        box = img.getbbox() or (0, 0, 1, 1)
        img = img.crop((0, 0, box[2], box[3]))
        tiles = []
        for ty in range(0, img.height, 512):
            for tx in range(0, img.width, 1024):
                cut = img.crop((tx, ty, min(tx + 1024, img.width), min(ty + 512, img.height)))
                if not cut.getbbox(): continue
                canvas = Image.new('RGBA', (max(8, 1 << (cut.width - 1).bit_length()), max(8, 1 << (cut.height - 1).bit_length())))
                canvas.paste(cut, (0, 0))
                file = f'room_{name}_{len(tiles)}'
                save_png(canvas, GFX / f'{file}.png')
                if has_soft_alpha(canvas):
                    soft_alpha.append(file)
                tiles.append((file, tx, ty, cut.width, cut.height))
        backgrounds.append((name, room, img.size, tiles))

    spaces = json.loads((SRC / 'data/spaces/index.json').read_text())
    spaces_dir = OUT / 'spaces' if OUT else ROOT / 'romfs/spaces'
    spaces_dir.mkdir(parents=True, exist_ok=True)
    for model in CLASSIC:
        mask = Image.open(SRC / 'data/spaces' / f'{model}.mask.png').convert('RGBA')
        values = [(q[0], q[1]) for q in mask.getdata()]
        encoded = bytearray(struct.pack('<HH', *mask.size))
        i = 0
        while i < len(values):
            j = i + 1
            while j < len(values) and j - i < 65535 and values[j] == values[i]: j += 1
            encoded.extend(struct.pack('<HBB', j - i, *values[i])); i = j
        (spaces_dir / f'{model}.mask').write_bytes(encoded)
    for texture in spaces['textures']:
        image = Image.open(SRC / 'data/spaces' / f'tex_{texture}.png').convert('RGB')
        (spaces_dir / f'{texture}.rgb').write_bytes(image.tobytes())

    wardrobe = json.loads((SRC / 'data/wardrobe-data.json').read_text())
    anims = json.loads((SRC / 'data/models/people.json').read_text())['animations']
    public_rooms = json.loads((SRC / 'data/public-rooms/index.json').read_text())
    drinks_src = (SRC / 'data/drinks.ts').read_text()

    out = []
    w = out.append
    w('// Generated by tools/pack.py. Do not edit by hand.')
    w('#include "assets.h"')
    w('')
    (GFX / 'soft-alpha.txt').write_text(''.join(f'{n}\n' for n in sorted(soft_alpha)))
    w(f'const int atlas_page_count = {len(page_files)};')
    w(f'/** Pages before this index stay loaded; the rest (character skins) load on demand. */')
    w(f'const int atlas_page_resident = {resident_pages};')
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
    w('const FurniTexture furni_textures[] = {')
    for def_id, page, size in textures:
        w(f'  {{{def_id}, {page}, {size}}},')
    w('};')
    w(f'const int furni_texture_count = {len(textures)};')
    animations = json.loads((SRC / 'data/models/furni-anim.json').read_text())
    timelines = json.loads((SRC / 'furni-timelines.json').read_text())
    anim_rows = []
    for definition in sorted(catalogue['definitions'].values(), key=lambda d: d['id']):
        bank = (definition.get('asset') or {}).get('bank')
        anim = animations.get(bank)
        if not anim or definition['id'] <= 8: continue
        for state, seq in sorted(anim['states'].items(), key=lambda pair: int(pair[0])):
            key = f"{definition['id']}/{state}"
            if key not in timelines: continue
            frames = timelines[key]
            name = f"furni_time_{definition['id']}_{state}"
            w(f'static const u16 {name}[] = {{' + ','.join(map(str, frames)) + '};')
            anim_rows.append((definition['id'], int(state), anim['tick'], len(frames), name))
    w('const FurniAnimation furni_animations[] = {')
    for row in anim_rows: w('  {' + ', '.join(map(str, row)) + '},')
    w('};')
    w(f'const int furni_animation_count = {len(anim_rows)};')
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
    # Jukeboxes and Trax machines (server catalogue::music_machine): the room playlist, source/music.c.
    music = [d['id'] for d in defs if d.get('sprite', '').split('*')[0] in ('sound_machine', 'sound_machine_pro', 'ads_idol_trax', 'jukebox', 'jukebox_ptv')]
    w(f'const u16 music_machines[{max(1, len(music))}] = {{{", ".join(map(str, music)) or "0"}}};')
    w(f'const int music_machine_count = {len(music)};')
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
        ip, ix, iy, iw, ih = icons.get(d['id'], (0, 0, 0, 0, 0))
        tex = 1 if d['id'] in by_def else 0
        drinks = ','.join(str(x) for x in (d.get('drinks') or []))
        games = ['slot', 'roulette', 'blackjack', 'crash', 'poker', 'videopoker', 'dash']
        casino = next((i + 1 for i, name in enumerate(games) if 'casino_' + name in d['flags']), 0)
        # Chip tokens of the Tweebbo Exchange (CHIP_<value>_token): redeemed for chips, not credits.
        chips = int(d['sprite'].split('_')[1]) if 'redeemable_chips' in d['flags'] else 0
        w(f"  {{{d['id']}, {d['kind']}, {d['width']}, {d['height']}, {d['seatHeight']}, {d['states']}, {bits}, {rots}, {drink}, {c_str(d['name'])}, {tex}, {ix}, {iy}, {iw}, {ih}, {c_str(drinks)}, {c_str(d.get('description') or '')}, {ip}, {casino}, {chips}}},")
    w('};')
    w(f'const int furni_def_count = {len(defs)};')
    w('')

    # Shop catalogue: the same pages and offers as the web (ui.ts renderShop).
    pages = sorted((p for p in catalogue['pages'] if p.get('visible')), key=lambda p: (p['order'], int(p['id']) if p['id'].isdigit() else 0))
    w(f'const CatPage cat_pages[{len(pages)}] = {{')
    for pg in pages:
        body = (pg.get('body') or '').replace('\r', '\n')
        head = pg.get('headline') or ''
        head = head if head in classic_ui else ''
        w(f"  {{{c_str(pg['id'])}, {c_str(pg['name'])}, {c_str(pg['layout'])}, {c_str(body)}, {c_str(pg.get('icon') or '')}, {c_str(pg.get('group') or '')}, {c_str(head)}}},")
    w('};')
    w(f'const int cat_page_count = {len(pages)};')
    # Chip tokens cost chips, which consoles do not spend: they reach a console only by trading.
    offers = sorted((o for o in catalogue['offers'] if not o.get('hidden') and o['contents'] and o.get('currency') != 'chips'),
                    key=lambda o: (o['order'], o['id']))
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
            layers = ' '.join(f'{part}:{model}:{tint}' for part, model, tint in e.get('layers', []))
            w(f"  {{{c_str(e['label'])}, {c_str(parts_str(e))}, '{e.get('sex', 'U')}', {c_str(layers) if layers else 'NULL'}}},")
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

    models = dict(re.findall(r'(\d+):\'(\d+)\'', drinks_src.split('drinkModels')[1].split('};')[0]))
    # After Hours whisky, cigarette and cigar: pre-rendered models of their own (tools/export/src/export.ts).
    for item in ('200', '201', '202'):
        if item in models and any(p['name'].startswith(f'h_crr_ri_{item}_') for p in people):
            models[item] = item
    names = dict(re.findall(r"(\d+):'([^']*)'", drinks_src.split('drinkNames')[1].split('};')[0]))
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
        save_png(canvas, GFX / f"room_{f['key']}.png")
        backgrounds.append((f['key'], {'x': f['x'], 'y': f['y'], 'door': f['door'], 'rows': f['rows']}, img.size, [(f"room_{f['key']}", 0, 0, img.width, img.height)]))

    # Layered public rooms (park and pool): background, depth masks and animations.
    scenes = []
    for key in SCENES:
        r = public_rooms[key]
        sp = Packer()
        sprites = []
        bg_name = r.get('background') or 'park_bg'
        for l in r['layers']:
            frames = l.get('frames') or [l]
            switch = SWITCHES.index(l['name']) if l['name'] in SWITCHES and l.get('closed') else -1
            role = ROLE_UNDER if l.get('under') else 10 + 2 * switch if switch >= 0 else 0
            # A switch layer also carries its closed picture (drawn instead when pool.c says so).
            states = [(frames, role)] + ([([l['closed']], 11 + 2 * switch)] if switch >= 0 else [])
            for frames_, role_ in states:
                for fi, fr in enumerate(frames_):
                    # Original hidden members (barmask on the rooftop) sit millions of pixels away.
                    if abs(fr['x']) > 30000 or abs(fr['y']) > 30000:
                        continue
                    img = Image.open(SRC / 'data/public-rooms' / fr['file']).convert('RGBA')
                    if l.get('flip'):
                        img = img.transpose(Image.FLIP_LEFT_RIGHT)
                    page, x, y, w_, h_ = sp.add(img)
                    depth = -10000.0 if l['name'] == bg_name else -20000.0 if role_ == ROLE_UNDER else float(l['depth'])
                    sprites.append((page, x, y, w_, h_, fr['x'], fr['y'], fr['w'], fr['h'], depth, round(l['alpha'] * 255),
                                    len(frames_), fi, 1 if l['name'] == bg_name or role_ == ROLE_UNDER else 0, role_))
        art = []
        prefixes = SCENE_ART.get(key, [])
        if prefixes:
            atlas = json.loads((SRC / 'data/activities/art.json').read_text())
            art_pages = [Image.open(SRC / 'data/activities' / f).convert('RGBA') for f in atlas['pages']]
            for name in sorted(n for n in atlas['sprites'] if n.startswith(tuple(prefixes))):
                a = atlas['sprites'][name]
                img = art_pages[a['page']].crop((a['x'], a['y'], a['x'] + a['w'], a['y'] + a['h']))
                page, x, y, w_, h_ = sp.add(img)
                art.append((name, page, x, y, w_, h_, a['reg'][0], a['reg'][1]))
        for i, page in enumerate(sp.pages):
            if i == len(sp.pages) - 1:
                used = sp.y + sp.shelf
                page = page.crop((0, 0, PAGE, max(8, 1 << (used - 1).bit_length())))
            save_png(page, GFX / f'scene_{key}_{i}.png')
        scenes.append((key, r, len(sp.pages), sprites, art))

    choices = []
    for kind, key in [(1, 'walls'), (2, 'floors')]:
        for id, (color, texture) in spaces[key].items():
            tw, th = spaces['textures'][texture]
            choices.append((kind, id, color, texture, tw, th))
    w('const FinishChoice finish_choices[] = {')
    for kind, id, color, texture, tw, th in choices:
        w(f'  {{{kind}, {c_str(id)}, 0x{color[1:]}, {c_str(texture)}, {tw}, {th}}},')
    w('};')
    w(f'const int finish_choice_count = {len(choices)};')
    w(f'const int finish_price = {spaces["price"]};')
    for name, room, size, tiles in backgrounds:
        w(f'static const RoomTile room_tiles_{name}[] = {{')
        for file, x, y, tw, th in tiles:
            w(f'  {{{c_str(file)}, {x}, {y}, {tw}, {th}}},')
        w('};')
    w(f'const RoomBackground room_backgrounds[{len(backgrounds)}] = {{')
    for name, room, (iw, ih), tiles in backgrounds:
        rows = '\\n'.join(room['rows'])
        w(f"  {{{c_str(name)}, \"room_{name}\", {room['x']}, {room['y']}, {iw}, {ih}, {room['door'][0]}, {room['door'][1]}, \"{rows}\", {len(tiles)}, room_tiles_{name}}},")
    w('};')
    w(f'const int room_background_count = {len(backgrounds)};')
    w('')
    for key, r, pages_n, sprites, art in scenes:
        w(f'static const SceneSprite scene_sprites_{key}[] = {{')
        for (page, x, y, w_, h_, dx, dy, dw, dh, depth, alpha, frames, fi, bg, role) in sprites:
            w(f'  {{{page}, {x}, {y}, {w_}, {h_}, {dx}, {dy}, {dw}, {dh}, {depth:.3f}f, {alpha}, {frames}, {fi}, {bg}, {role}}},')
        w('};')
        # Sorted by name (strcmp order) for the binary search in source/pool.c.
        art.sort(key=lambda a: a[0].encode())
        w(f'static const SceneArt scene_art_{key}[] = {{')
        for (name, page, x, y, w_, h_, rx, ry) in art:
            w(f'  {{{c_str(name)}, {page}, {x}, {y}, {w_}, {h_}, {rx}, {ry}}},')
        if not art:
            w('  {0},')
        w('};')
    w(f'const Scene scenes[{max(1, len(scenes))}] = {{')
    for key, r, pages_n, sprites, art in scenes:
        water = r.get('waterHeight', -1)
        w(f'  {{{c_str(key)}, {pages_n}, {len(sprites)}, scene_sprites_{key}, {water}, {len(art)}, scene_art_{key}}},')
    w('};')
    w(f'const int scene_count = {len(scenes)};')
    w('')

    rooms = []
    for key in ROOMS:
        r = public_rooms[key]
        items = r.get('furniture', [])
        w(f'static const RoomItem room_items_{key}[] = {{')
        for it in items:
            if WITH_STATE:
                w(f"  {{{it['id']}u, {it['def']}, {it['x']}, {it['y']}, {it['rot']}, {it['z']}, {it.get('state', 0)}}},")
            else:
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

    for old in GFX.rglob('*.png'):
        if old.resolve() not in GENERATED_PNGS and not old.name.startswith('font'): old.unlink()
    (GEN / 'assets.c').write_text('\n'.join(out) + '\n')
    src = json.loads((SRC / 'source.json').read_text())
    print(f"atlas pages: {n_color} RGBA5551 + {len(soft_packer.pages) if soft_packer.seen else 0} RGBA8 + {len(gray_packer.pages)} LA8 + "
          f"{len(skin_packer.pages) if skin_packer.seen else 0} skin RGBA8 on demand, unique sprites: "
          f"{len(color_packer.seen) + len(soft_packer.seen) + len(gray_packer.seen) + len(skin_packer.seen)}, "
          f"avatar parts: {len(people)}, furniture variants: {len(furni)}, definitions: {len(defs)}")
    print(f"source: {src['web']} ({src['exportedAt']})")


if __name__ == '__main__':
    main()

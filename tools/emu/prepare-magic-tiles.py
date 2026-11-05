#!/usr/bin/env python3
"""Prepare original helpers in the isolated Old 3DS touch fixture."""
import json
import argparse
import socket
import sqlite3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--gameplay',action='store_true');args=parser.parse_args()
with socket.socket() as probe:
    if probe.connect_ex(('127.0.0.1', 18103)) == 0:
        raise SystemExit('Stop the isolated 18103 server before preparing a cold room reload.')
database = ROOT.parent / 'hotel/tests/expansion-server-data/tweebbo.db'
profile = ROOT / 'build/azahar-profile/user/sdmc/3ds/Tweebbo-debug' / ('magic-gameplay-items.json' if args.gameplay else 'magic-items.json')
catalogue = json.loads((ROOT.parent / 'hotel/server/src/catalogue-data.json').read_text())
definitions = sorted((d for d in catalogue['definitions'].values() if d.get('heightHelper')), key=lambda d: d['id'])
assert len(definitions) == 13
items = json.loads(profile.read_text()) if profile.exists() else {}
with sqlite3.connect(database) as connection:
    owner = connection.execute('SELECT owner FROM rooms WHERE slug=?', ('expansion-emulator',)).fetchone()[0]
    slug = 'magic-gameplay' if args.gameplay else 'expansion-emulator'
    if args.gameplay:
        connection.execute("INSERT OR IGNORE INTO rooms(slug,title,model,owner,created_at) VALUES(?1,'Magic helper gameplay','cuarto',?2,strftime('%s','now'))",(slug,owner))
    for definition in definitions:
        key = definition['sprite']
        placed = definition['width'] == definition['height'] == 1
        x, y = ((2, 2) if definition['heightHelper'] == 1 else (6, 4)) if placed else (0, 0)
        z, fine = (0, 32) if placed else (0, 0)
        if key not in items:
            connection.execute('INSERT INTO items(owner,def,room,x,y,rot,state,z,z_fine) VALUES(?,?,?,?,?,0,0,?,?)',
                               (owner, definition['id'], slug if placed else None, x, y, z, fine))
            items[key] = connection.execute('SELECT last_insert_rowid()').fetchone()[0]
        assert connection.execute('SELECT owner,def FROM items WHERE id=?', (items[key],)).fetchone() == (owner, definition['id'])
        connection.execute('UPDATE items SET room=?,x=?,y=?,rot=0,z=?,z_fine=? WHERE id=?',
                           (slug if placed else None, x, y, z, fine, items[key]))
        data = {'height': 1 if placed else 0}
        if definition['heightHelper'] == 2:
            data['multiWalk'] = False
        connection.execute('INSERT INTO item_metadata(item,kind,data) VALUES(?,?,?) '
                           'ON CONFLICT(item) DO UPDATE SET kind=excluded.kind,data=excluded.data',
                           (items[key], 'magic_tile', json.dumps(data)))
profile.write_text(json.dumps(items, indent=2) + '\n')
print('Prepared thirteen owned helpers for the isolated Old 3DS touch fixture; offers remain closed.')

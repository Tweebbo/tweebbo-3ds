#!/usr/bin/env python3
"""Prepare owned movement boxes in the retained, isolated Old 3DS touch fixture."""
import json
import socket
import sqlite3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
with socket.socket() as probe:
    if probe.connect_ex(('127.0.0.1', 18103)) == 0:
        raise SystemExit('Stop the isolated 18103 server before preparing a cold room reload.')
database = ROOT.parent / 'hotel/tests/expansion-server-data/tweebbo.db'
profile = ROOT / 'build/azahar-profile/user/sdmc/3ds/Tweebbo-debug/motion-items.json'
items = json.loads(profile.read_text())
catalogue = json.loads((ROOT.parent / 'hotel/server/src/catalogue-data.json').read_text())
definitions = {d['sprite']: d['id'] for d in catalogue['definitions'].values()}
with sqlite3.connect(database) as connection:
    slug = 'expansion-emulator'
    owner = connection.execute('SELECT owner FROM rooms WHERE slug=?', (slug,)).fetchone()[0]
    for key, sprite, params, y in [
        ('avatar', 'wf_act_move_rotate_user', [2, 8, 0], 6),
        ('carry', 'wf_xtra_mov_carry_users', [0, 0], 7),
        ('physics', 'wf_xtra_mov_physics', [1, 1, 1, 1, 100, 101, 100], 7),
        ('furniToFurni', 'wf_act_furni_to_furni', [100, 101], 6),
        ('furniToUser', 'wf_act_furni_to_user', [100, 100], 6),
        ('groupMovement', 'wf_act_move_furni_as_group', [1, 0, 0, 0, 100, 101, 100], 6),
        ('moveToOffset', 'wf_act_move_furni_to', [2, 2, 100, 101], 6),
        ('headingMovement', 'wf_act_move_to_dir', [0, 2, 100, 1], 6),
        ('chaseMovement', 'wf_act_chase', [100], 7),
        ('fleeMovement', 'wf_act_flee', [100], 7),
        ('collisionTrigger', 'wf_trg_collision', [], 7),
    ]:
        definition = definitions[sprite]
        if key not in items:
            connection.execute('INSERT INTO items(id,owner,def,room,x,y,rot,state,z) '
                               'VALUES((SELECT last_id+1 FROM item_id_sequence WHERE singleton=1),?,?,?,?,?,0,0,0)',
                               (owner, definition, slug, {'physics':1,'furniToFurni':2,'furniToUser':3,'groupMovement':4,'moveToOffset':5,'headingMovement':6,'chaseMovement':2,'fleeMovement':3,'collisionTrigger':4}.get(key,0), y))
            items[key] = connection.execute('SELECT last_insert_rowid()').fetchone()[0]
        assert connection.execute('SELECT owner,def,room FROM items WHERE id=?', (items[key],)).fetchone() == (owner, definition, slug)
        config = dict(operation=sprite, params=params, users=[], targets=[], text='', delay=0)
        if key in ['physics','furniToFurni','furniToUser','groupMovement','moveToOffset']:
            config['targets'] = [items['avatar']]
            config['secondary_targets'] = [items['carry']]
        if key in ['headingMovement','chaseMovement','fleeMovement']:config['targets'] = [items['avatar']]
        connection.execute('INSERT INTO item_metadata(item,kind,data) VALUES(?,?,?) '
                           'ON CONFLICT(item) DO UPDATE SET kind=excluded.kind,data=excluded.data',
                           (items[key], 'wired', json.dumps(config)))
    for key in ['animation', 'reference']:
        assert connection.execute('SELECT owner,room FROM items WHERE id=?', (items[key],)).fetchone() == (owner, slug)
        config = json.loads(connection.execute("SELECT data FROM item_metadata WHERE item=? AND kind='wired'", (items[key],)).fetchone()[0])
        if key == 'animation':
            config['params'] = [1500]
        else:
            config['params'][1] = 0
        connection.execute("UPDATE item_metadata SET data=? WHERE item=? AND kind='wired'", (json.dumps(config), items[key]))
profile.write_text(json.dumps(items, indent=2) + '\n')
print('Prepared eleven synthetic movement/collision boxes and reset the isolated touch-test settings.')

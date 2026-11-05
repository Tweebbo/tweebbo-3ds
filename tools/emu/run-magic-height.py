#!/usr/bin/env python3
"""Verify real Old 3DS touch edits against the isolated helper server."""
import argparse
import hashlib
import json
import sqlite3
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--emulator', type=Path, required=True)
parser.add_argument('--gameplay', action='store_true')
args = parser.parse_args()
emulator = args.emulator.resolve()
assert (emulator / 'squashfs-root/AppRun').is_file()
profile = ROOT / 'build/azahar-profile'
sd = profile / 'user/sdmc/3ds/Tweebbo-debug'
rom = ROOT / 'out/debug/tweebbo-debug.3dsx'
database = ROOT.parent / 'hotel/tests/expansion-server-data/tweebbo.db'
previous_room=(sd/'room.txt').read_text();assert previous_room.strip()=='expansion-emulator'
room='magic-gameplay' if args.gameplay else 'expansion-emulator'
assert b'ws://127.0.0.1:18103' in rom.read_bytes()
if not (sd / 'server.txt').exists():
    (sd / 'server.txt').write_text('ws://127.0.0.1:18103\n')
assert (sd / 'server.txt').read_text().strip() == 'ws://127.0.0.1:18103'
items = json.loads((sd / ('magic-gameplay-items.json' if args.gameplay else 'magic-items.json')).read_text())
stack = items['tile_stackmagic']
container = 'tweebbo-expansion-azahar-magic-height'
(sd / 'furniture-probe.txt').write_text('0:0\n')
(sd / 'height-probe.txt').write_text('0:0:0:0\n')
(sd/'room.txt').write_text(room+'\n')
log = (profile / 'magic-height-container.log').open('w')
command = ['podman', 'run', '--rm', '--name', container, '--network', 'host',
           '-v', f'{emulator}:/emu:ro,z', '-v', f'{ROOT}:/work:ro,z', '-v', f'{profile}:/profile:rw,z',
           '-w', '/profile', '-e', 'XDG_CONFIG_HOME=/profile/config', '-e', 'XDG_DATA_HOME=/profile/data',
           '-e', 'XDG_CACHE_HOME=/profile/cache', '-e', 'LIBGL_ALWAYS_SOFTWARE=1',
           '-e', 'QT_X11_NO_MITSHM=1', '-e', 'SDL_AUDIODRIVER=dummy', '-e', 'LANG=C.UTF-8',
           'localhost/tweebbo-expansion-azahar', 'bash', '-c',
           'set -e; Xvfb :97 -screen 0 1000x1000x24 > xvfb-magic-height.log 2>&1 & '
           'export DISPLAY=:97; sleep 1; exec /emu/squashfs-root/AppRun /work/out/debug/tweebbo-debug.3dsx']
process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
captures = []

def wait_for(test, seconds=25):
    end = time.monotonic() + seconds
    while not test():
        assert process.poll() is None, 'The isolated emulator exited before completing the touch test.'
        assert time.monotonic() < end, 'Timed out waiting for the isolated helper touch test.'
        time.sleep(.2)

def inside(*command):
    return subprocess.check_output(['podman', 'exec', '-e', 'DISPLAY=:97', container, *command], stderr=subprocess.STDOUT).decode().strip()

def capture(name):
    path = 'old3ds-magic-' + name + '.png'
    inside('import', '-window', window, '/profile/' + path)
    captures.append(path)

def stored(item=stack):
    with sqlite3.connect(f'file:{database}?mode=ro', uri=True) as connection:
        z, fine = connection.execute('SELECT z,z_fine FROM items WHERE id=?', (item,)).fetchone()
        data = json.loads(connection.execute('SELECT data FROM item_metadata WHERE item=?', (item,)).fetchone()[0])
    return z, fine, data['height']

try:
    def visible_window():
        try:
            return bool(inside('xdotool','search','--onlyvisible','--name','Azahar'))
        except subprocess.CalledProcessError:
            return False
    wait_for(visible_window,35)
    window = inside('xdotool', 'search', '--onlyvisible', '--name', 'Azahar').splitlines()[0]
    wait_for(lambda: (sd / 'log.txt').is_file() and 'after gfx_init' in (sd / 'log.txt').read_text())
    assert 'on Old 3DS/2DS' in (sd / 'log.txt').read_text()
    time.sleep(8)
    geometry = dict(line.split('=', 1) for line in inside('xdotool', 'getwindowgeometry', '--shell', window).splitlines())
    left = int(geometry['X']) + (int(geometry['WIDTH']) - 320) // 2
    top = int(geometry['Y']) + 20 + 240
    inside('xdotool', 'windowfocus', window)

    def tap(x, y):
        inside('xdotool', 'mousemove', str(left + x), str(top + y))
        time.sleep(.15)
        inside('xdotool', 'mousedown', '1')
        time.sleep(.7)
        inside('xdotool', 'mouseup', '1')
        time.sleep(.8)

    def open_item(serial, item):
        opened = f'Furniture fixture opened {item} (magic_tile)'
        previous = (sd / 'log.txt').read_text().count(opened)
        (sd / 'furniture-probe.txt').write_text(f'{serial}:{item}\n')
        wait_for(lambda: f'Furniture fixture requested {serial} ({item})' in (sd / 'log.txt').read_text())
        wait_for(lambda: (sd / 'log.txt').read_text().count(opened) > previous)
        time.sleep(1.2)

    capture('room')
    open_item(1001, stack)
    capture('editor')
    tap(120, 61)
    for _ in range(4):
        tap(244, 207)
    for x, y in [(304, 71), (272, 173), (48, 71), (272, 71)]:
        tap(x, y)
    capture('decimal-keyboard')
    tap(295, 207)
    capture('decimal-entered')
    tap(82, 184)
    wait_for(lambda: stored() == (9, 28, 29))
    capture('decimal-saved')
    tap(288, 89)
    capture('match-below-selected')
    tap(82, 184)
    wait_for(lambda: stored() == (0, 0, 0))
    capture('match-below-saved')
    for serial, (sprite, item) in enumerate(items.items(), 1010):
        open_item(serial, item)
        capture(sprite)
    if args.gameplay:
        walk=items['tile_walkmagic'];open_item(1100,walk);tap(120,61)
        for _ in range(8):tap(244,207)
        for x,y in [(304,71),(272,173),(304,71),(304,71)]:tap(x,y)
        tap(295,207);tap(82,184);wait_for(lambda:stored(walk)==(0,0,0))
        (sd/'height-probe.txt').write_text('1:6:4:0\n');tap(305,29);tap(199,129)
        wait_for(lambda:'Height fixture reached 1 (6,4;0)' in (sd/'log.txt').read_text(),35)
        open_item(1101,walk);tap(120,61)
        for _ in range(8):tap(244,207)
        for x,y in [(112,71),(304,71),(272,173),(304,71),(304,71)]:tap(x,y)
        tap(295,207);tap(82,184);wait_for(lambda:stored(walk)==(1280,0,4000));capture('height-40-saved')
        (sd/'height-probe.txt').write_text('2:6:4:128000\n');tap(305,29)
        wait_for(lambda:'Height fixture reached 2 (6,4;128000)' in (sd/'log.txt').read_text(),35)
        time.sleep(1.5);capture('height-40-gameplay')
    report = {'status': 'passed', 'emulator': 'Azahar 2126.2', 'model': 'Old 3DS/2DS',
              'physicalDeviceAcceptance': 'pending', 'server': 'Private loopback 18103',
              'room': room, 'romSha256': hashlib.sha256(rom.read_bytes()).hexdigest(),
              'exactTouchSave': {'sourceHeight': '0.29', 'storedPixelHeight': '9.28', 'metadataHeight': 29},
              'matchBelowTouchSave': {'storedPixelHeight': 0, 'metadataHeight': 0},
              'helpersOpened': len(items), 'captures': captures}
    if args.gameplay:report['gameplay']={'touchTarget':[6,4],'sourceHeight':40,'nativeAvatarPixelHeight':1280,'sittingOrLying':False,'nativeStateAcknowledged':True}
    (profile / 'magic-height-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': 'passed', 'helpersOpened': len(items), 'captures': len(captures)}))
except BaseException:
    if 'window' in locals():
        try:capture('failure')
        except Exception:pass
    raise
finally:
    (sd / 'furniture-probe.txt').write_text('0:0\n')
    (sd / 'height-probe.txt').write_text('0:0:0:0\n')
    (sd/'room.txt').write_text(previous_room)
    subprocess.run(['podman', 'stop', '-t', '2', container], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    try:
        process.wait(timeout=8)
    except subprocess.TimeoutExpired:
        process.terminate()
    log.close()

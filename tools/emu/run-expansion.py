#!/usr/bin/env python3
"""Exercise the private 3DS build in an isolated Azahar container/profile."""
import argparse
import hashlib
import json
import sqlite3
import subprocess
import time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--emulator',type=Path,required=True,help='Verified extracted Azahar AppImage directory')
parser.add_argument('--ui-only',action='store_true',help='Keep the verified effect captures and repeat only the touch flow')
args=parser.parse_args()
emulator=args.emulator.resolve()
assert (emulator/'squashfs-root/AppRun').is_file()
profile=ROOT/'build/azahar-profile'
sd=profile/'user/sdmc/3ds/Tweebbo-debug'
rom=ROOT/'out/debug/tweebbo-debug.3dsx'
assert (sd/'room.txt').read_text().strip()=='expansion-emulator'
assert rom.is_file()
container='tweebbo-expansion-azahar-probe'
log=(profile/'probe-container.log').open('w')
command=['podman','run','--rm','--name',container,'--network','host',
         '-v',f'{emulator}:/emu:ro,z','-v',f'{ROOT}:/work:ro,z','-v',f'{profile}:/profile:rw,z',
         '-w','/profile','-e','XDG_CONFIG_HOME=/profile/config','-e','XDG_DATA_HOME=/profile/data',
         '-e','XDG_CACHE_HOME=/profile/cache','-e','LIBGL_ALWAYS_SOFTWARE=1',
         '-e','QT_X11_NO_MITSHM=1','-e','SDL_AUDIODRIVER=dummy','-e','LANG=C.UTF-8',
         'localhost/tweebbo-expansion-azahar','bash','-c',
         'set -e; Xvfb :97 -screen 0 1000x1000x24 > xvfb-probe.log 2>&1 & '
         'export DISPLAY=:97; sleep 1; exec /emu/squashfs-root/AppRun /work/out/debug/tweebbo-debug.3dsx']
(sd/'furniture-probe.txt').write_text('0:0\n')
process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
def wait_for(test,seconds=20):
    end=time.monotonic()+seconds
    while not test():
        assert process.poll() is None,'Azahar exited before the probe completed; inspect the private profile log.'
        assert time.monotonic()<end,'Timed out waiting for the private 3DS effect probe.'
        time.sleep(.2)
def inside(*command):
    return subprocess.check_output(['podman','exec','-e','DISPLAY=:97',container,*command],stderr=subprocess.STDOUT).decode().strip()
def capture(name):
    inside('import','-window',window,f'/profile/{name}.png')
    return name+'.png'
try:
    time.sleep(5)
    window=inside('xdotool','search','--onlyvisible','--name','Azahar').splitlines()[0]
    wait_for(lambda:(sd/'log.txt').is_file() and 'after gfx_init' in (sd/'log.txt').read_text())
    assert 'on Old 3DS/2DS' in (sd/'log.txt').read_text(),'Set System/is_new_3ds=false in the isolated Azahar configuration.'
    time.sleep(8)
    captures=[capture('old3ds-room')]
    effects=[] if args.ui_only else [11,12,13,38,45,46,55,2,3,0]
    for effect in effects:
        (sd/'effect-probe.txt').write_text(str(effect)+'\n')
        wait_for(lambda:f'Effect fixture requested {effect}\n' in (sd/'log.txt').read_text())
        time.sleep(2)
        captures.append(capture(f'old3ds-effect-{effect}'))
    geometry=dict(line.split('=',1) for line in inside('xdotool','getwindowgeometry','--shell',window).splitlines())
    x=int(geometry['X'])+(int(geometry['WIDTH'])-320)//2+161
    y=int(geometry['Y'])+20+240+219
    inside('xdotool','windowfocus',window)
    inside('xdotool','mousemove',str(x),str(y),'mousedown','1');time.sleep(.2)
    inside('xdotool','mouseup','1');time.sleep(2)
    captures.append(capture('old3ds-hand'))
    def tap(x,y):
        left=int(geometry['X'])+(int(geometry['WIDTH'])-320)//2
        top=int(geometry['Y'])+20+240
        inside('xdotool','mousemove',str(left+x),str(top+y),'mousedown','1');time.sleep(.2)
        inside('xdotool','mouseup','1');time.sleep(1)
    tap(75,60);captures.append(capture('old3ds-hand-note-selected'))
    tap(179,175);time.sleep(1);captures.append(capture('old3ds-hand-note-open'))
    motion_items=json.loads((sd/'motion-items.json').read_text())
    def open_motion(serial,name):
        item=motion_items[name];(sd/'furniture-probe.txt').write_text(f'{serial}:{item}\n')
        wait_for(lambda:f'Furniture fixture requested {serial} ({item})' in (sd/'log.txt').read_text());time.sleep(2)
    open_motion(1,'animation');captures.append(capture('old3ds-animation-time'))
    tap(120,61)
    for _ in range(4):tap(244,207)
    tap(48,71)
    for _ in range(3):tap(304,71)
    tap(295,207);captures.append(capture('old3ds-animation-time-edited'))
    tap(82,184);wait_for(lambda:f"Furniture fixture saved {motion_items['animation']}" in (sd/'log.txt').read_text());captures.append(capture('old3ds-animation-time-saved'))
    database=ROOT.parent/'hotel/tests/expansion-server-data/tweebbo.db'
    with sqlite3.connect(f'file:{database}?mode=ro',uri=True) as connection:
        saved=json.loads(connection.execute('SELECT data FROM item_metadata WHERE item=?',(motion_items['animation'],)).fetchone()[0]);assert saved['params']==[2000],'The real touch edit/save did not persist 2000 ms'
    open_motion(2,'instant');captures.append(capture('old3ds-instant-motion'))
    tap(82,184);wait_for(lambda:f"Furniture fixture saved {motion_items['instant']}" in (sd/'log.txt').read_text());captures.append(capture('old3ds-instant-motion-saved'))
    with sqlite3.connect(f'file:{database}?mode=ro',uri=True) as connection:
        saved=json.loads(connection.execute('SELECT data FROM item_metadata WHERE item=?',(motion_items['instant'],)).fetchone()[0]);assert saved['params']==[]
    open_motion(3,'echo');captures.append(capture('old3ds-variable-echo'))
    tap(82,184);wait_for(lambda:f"Furniture fixture saved {motion_items['echo']}" in (sd/'log.txt').read_text());captures.append(capture('old3ds-variable-echo-saved'))
    with sqlite3.connect(f'file:{database}?mode=ro',uri=True) as connection:
        saved=json.loads(connection.execute('SELECT data FROM item_metadata WHERE item=?',(motion_items['echo'],)).fetchone()[0]);assert saved['params']==[(3<<32)|motion_items['reference']]
    open_motion(4,'reference');captures.append(capture('old3ds-variable-reference'))
    tap(288,113);captures.append(capture('old3ds-variable-access'))
    tap(288,57);tap(82,184)
    wait_for(lambda:f"Furniture fixture saved {motion_items['reference']}" in (sd/'log.txt').read_text());captures.append(capture('old3ds-variable-reference-saved'))
    with sqlite3.connect(f'file:{database}?mode=ro',uri=True) as connection:
        saved=json.loads(connection.execute('SELECT data FROM item_metadata WHERE item=?',(motion_items['reference'],)).fetchone()[0]);assert saved['params'][1]==1
        assert saved['variable_room']=='expansion-emulator-shared' and saved['params'][0]>(1<<34),'Shared selection or room binding changed during native save'
    open_motion(5,'avatar');captures.append(capture('old3ds-avatar-move'))
    tap(288,135);captures.append(capture('old3ds-avatar-direction'))
    tap(288,57);captures.append(capture('old3ds-avatar-rotation'))
    tap(82,184);wait_for(lambda:f"Furniture fixture saved {motion_items['avatar']}" in (sd/'log.txt').read_text());captures.append(capture('old3ds-avatar-saved'))
    with sqlite3.connect(f'file:{database}?mode=ro',uri=True) as connection:
        saved=json.loads(connection.execute('SELECT data FROM item_metadata WHERE item=?',(motion_items['avatar'],)).fetchone()[0]);assert saved['params']==[2,9,0],'Real touch rotation selection did not persist'
    open_motion(6,'carry');captures.append(capture('old3ds-carry-users'))
    tap(288,57);captures.append(capture('old3ds-carry-mode'))
    tap(82,184);wait_for(lambda:f"Furniture fixture saved {motion_items['carry']}" in (sd/'log.txt').read_text());captures.append(capture('old3ds-carry-saved'))
    with sqlite3.connect(f'file:{database}?mode=ro',uri=True) as connection:
        saved=json.loads(connection.execute('SELECT data FROM item_metadata WHERE item=?',(motion_items['carry'],)).fetchone()[0]);assert saved['params']==[1,0],'Real touch carry selection did not persist'
    open_motion(7,'physics');captures.append(capture('old3ds-physics'))
    tap(288,57);captures.append(capture('old3ds-physics-altitude'))
    tap(288,135);captures.append(capture('old3ds-physics-page2'))
    tap(288,135);captures.append(capture('old3ds-physics-sources'))
    tap(43,115);captures.append(capture('old3ds-physics-secondary'))
    tap(166,115);captures.append(capture('old3ds-physics-secondary-pick'))
    tap(288,135);tap(43,115);tap(166,115);captures.append(capture('old3ds-physics-players'))
    tap(82,184);wait_for(lambda:f"Furniture fixture saved {motion_items['physics']}" in (sd/'log.txt').read_text());captures.append(capture('old3ds-physics-saved'))
    with sqlite3.connect(ROOT.parent/'hotel/tests/expansion-server-data/tweebbo.db') as connection:
        saved=json.loads(connection.execute('SELECT data FROM item_metadata WHERE item=?',(motion_items['physics'],)).fetchone()[0]);assert saved['params']==[0,1,1,1,100,101,100]
        assert saved['secondary_targets']!=[motion_items['carry']],'Real touch secondary picks did not persist'
        assert saved['targets']==[motion_items['avatar']],'The secondary picker changed primary picks'
        assert saved['users'],'Real touch player picks did not persist'
    for serial,name,stem in [(8,'furniToFurni','furni-destination'),(9,'furniToUser','player-destination'),(10,'groupMovement','group-destination'),(11,'moveToOffset','destination-offset')]:
        open_motion(serial,name);captures.append(capture('old3ds-'+stem))
        if name=='furniToFurni':
            tap(288,89);captures.append(capture('old3ds-'+stem+'-source'))
            tap(43,115);tap(166,115);captures.append(capture('old3ds-'+stem+'-secondary'))
        elif name=='furniToUser':
            tap(43,115);tap(166,115);captures.append(capture('old3ds-'+stem+'-players'))
        elif name=='groupMovement':
            tap(288,89);tap(288,135);captures.append(capture('old3ds-'+stem+'-offset'))
            tap(43,115);tap(166,115);captures.append(capture('old3ds-'+stem+'-secondary'))
        else:
            tap(288,89);tap(288,135);captures.append(capture('old3ds-'+stem+'-spacing'))
        tap(82,184);wait_for(lambda:f"Furniture fixture saved {motion_items[name]}" in (sd/'log.txt').read_text());captures.append(capture('old3ds-'+stem+'-saved'))
        with sqlite3.connect(f'file:{database}?mode=ro',uri=True) as connection:
            saved=json.loads(connection.execute('SELECT data FROM item_metadata WHERE item=?',(motion_items[name],)).fetchone()[0])
            expected={'furniToFurni':[101,101],'furniToUser':[100,100],'groupMovement':[2,0,0,0,100,101,100],'moveToOffset':[4,2,100,101]}[name]
            assert saved['params']==expected,(name,saved['params'])
            assert saved['targets']==[motion_items['avatar']],name+' changed the independent primary picks'
            if name in ['furniToFurni','groupMovement']:assert saved['secondary_targets']!=[motion_items['carry']],name+' secondary picks did not persist'
            if name=='furniToUser':assert saved['users'],'Player destination picks did not persist'
    for serial,name,stem in [(12,'headingMovement','heading'),(13,'chaseMovement','chase'),(14,'fleeMovement','flee'),(15,'collisionTrigger','collision-trigger')]:
        open_motion(serial,name);captures.append(capture('old3ds-'+stem))
        if name=='headingMovement':
            tap(288,135);captures.append(capture('old3ds-heading-rules'))
            tap(288,57);captures.append(capture('old3ds-heading-turn'))
            tap(43,115);tap(166,115);captures.append(capture('old3ds-heading-picks'))
        elif name=='chaseMovement':
            tap(288,89);captures.append(capture('old3ds-chase-source'))
        elif name=='fleeMovement':
            tap(28,89);captures.append(capture('old3ds-flee-source'))
        tap(82,184);wait_for(lambda:f"Furniture fixture saved {motion_items[name]}" in (sd/'log.txt').read_text());captures.append(capture('old3ds-'+stem+'-saved'))
        with sqlite3.connect(f'file:{database}?mode=ro',uri=True) as connection:
            saved=json.loads(connection.execute('SELECT data FROM item_metadata WHERE item=?',(motion_items[name],)).fetchone()[0])
            expected={'headingMovement':[0,3,100,1],'chaseMovement':[200],'fleeMovement':[0],'collisionTrigger':[]}[name]
            assert saved['params']==expected,(name,saved['params'])
            if name=='headingMovement':assert saved['targets']!=[motion_items['avatar']],'Real touch heading picks did not persist'
            if name in ['chaseMovement','fleeMovement']:assert saved['targets']==[motion_items['avatar']]
    report=dict(status='captured',emulator='Azahar 2126.2',model='Old 3DS/2DS',
                server='Private loopback 18103',room='expansion-emulator',
                romSha256=hashlib.sha256(rom.read_bytes()).hexdigest(),
                requestedEffects=effects,captures=captures,windowGeometry=geometry,
                acceptance='Screenshots and native diagnostics still require inspection; this is not physical hardware acceptance.')
    (profile/('ui-probe-report.json' if args.ui_only else 'probe-report.json')).write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':report['status'],'model':report['model'],'captures':len(captures)}))
finally:
    subprocess.run(['podman','stop','--time','2',container],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    process.wait(timeout=10);log.close()

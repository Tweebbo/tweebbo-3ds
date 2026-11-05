#!/usr/bin/env python3
"""Capture actual source highlight matrices in the isolated Old 3DS renderer."""
import argparse
import asyncio
import copy
import hashlib
import json
import re
import sqlite3
import subprocess
import time
from pathlib import Path
import websockets

ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--emulator',type=Path,required=True)
args=parser.parse_args()
profile=ROOT/'build/azahar-profile'
sd=profile/'user/sdmc/3ds/Tweebbo-debug'
rom=ROOT/'out/debug/tweebbo-debug.3dsx'
manifest=json.loads((ROOT.parent/'hotel/tests/expansion-server-data/wired-live-scene.json').read_text())
database=ROOT.parent/manifest['database']
assert database==ROOT.parent/'hotel/tests/expansion-server-data/tweebbo.db'
assert manifest['owner']==88
container='tweebbo-expansion-azahar-wired-highlight'
output=ROOT.parent/'hotel/docs/expansion/old3ds-wired-highlight'
output.mkdir(exist_ok=True)


def running(name):
    return bool(subprocess.check_output(['podman','ps','--filter','name='+name,'--format','{{.Names}}'],text=True).strip())


def varint(value):
    out=[]
    while True:
        byte=value&127
        value>>=7
        out.append(byte|(128 if value else 0))
        if not value:return bytes(out)


def text(value):
    encoded=value.encode()
    return varint(len(encoded))+encoded


def furniture_value(data):
    if not data or data[0]!=149:return None
    at=1;size=0;shift=0
    while True:
        byte=data[at];at+=1;size|=(byte&127)<<shift;shift+=7
        if not byte&128:break
    return json.loads(data[at:at+size])


async def main():
    assert not running('tweebbo-expansion-azahar-magic-height'),'Wait for the helper probe to restore its profile.'
    assert (sd/'room.txt').read_text().strip()=='expansion-emulator'
    previous_room=(sd/'room.txt').read_text()
    assert (sd/'server.txt').read_text().strip()=='ws://127.0.0.1:18103'
    assert b'ws://127.0.0.1:18103' in rom.read_bytes()
    build=int(re.search(r'WEB_CLIENT_BUILD: u8 = (\d+)',(ROOT.parent/'hotel/server/src/proto.rs').read_text())[1])
    with sqlite3.connect(f'file:{database}?mode=ro',uri=True) as connection:
        token=connection.execute('SELECT token FROM guests WHERE id=?',(manifest['owner'],)).fetchone()[0]
        original=json.loads(connection.execute('SELECT data FROM item_metadata WHERE item=?',(manifest['items']['glowAction']['id'],)).fetchone()[0])
        target_location=connection.execute('SELECT x,y,z,z_fine,rot,state FROM items WHERE id=?',(manifest['items']['fxTarget']['id'],)).fetchone()
    log=(profile/'wired-highlight-container.log').open('w')
    process=None
    report={'status':'incomplete','emulator':'Azahar 2126.2','model':'Old 3DS/2DS','physicalDeviceAcceptance':'pending','scope':'Private synthetic room on loopback port 18103; actual private 3DSX and actual Wired stack','room':manifest['rooms']['effects'],'target':{**manifest['items']['fxTarget'],**dict(zip(['x','y','z','zFine','rotation','state'],target_location))},'romSha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'captures':[]}
    async with websockets.connect('ws://127.0.0.1:18103/ws/'+manifest['rooms']['effects']) as socket:
        await socket.send(bytes([1,6,0])+text(token)+bytes([0,0,0,build,32]))
        while True:
            packet=await asyncio.wait_for(socket.recv(),10)
            if packet[0]==101:break

        async def save(config):
            await socket.send(bytes([28])+text(json.dumps({'op':'wired_save','item':manifest['items']['glowAction']['id'],'config':config},separators=(',',':'))))
            deadline=time.monotonic()+10
            while time.monotonic()<deadline:
                packet=await asyncio.wait_for(socket.recv(),10)
                value=furniture_value(packet)
                if value and value.get('op')=='wired_saved' and value.get('item')==manifest['items']['glowAction']['id']:return
                if packet[0]==113:raise AssertionError('Owner configuration rejected; inspect private server without exposing credentials.')
            raise AssertionError('Wired save acknowledgment missing')

        async def apply(kind,strength,rgb):
            config=copy.deepcopy(original)
            config['params']=[1,kind,strength,rgb,0,0,0]
            config['targets']=[manifest['items']['fxTarget']['id']]
            config['furni_sources']={'items':100}
            config['user_sources']={'audience':0,'targets':0}
            await save(config)
            await socket.send(bytes([3])+text('glow'))
            await asyncio.sleep(6.5)

        def inside(*command):
            return subprocess.check_output(['podman','exec','-e','DISPLAY=:97',container,*command],stderr=subprocess.STDOUT,text=True).strip()

        async def wait_for(test,seconds=30):
            deadline=time.monotonic()+seconds
            while not test():
                assert process.poll() is None,'Isolated emulator exited early.'
                assert time.monotonic()<deadline,'Timed out waiting for isolated emulator.'
                await asyncio.sleep(.2)

        def capture(name):
            filename='old3ds-wired-'+name+'.png'
            inside('import','-window',window,'/profile/'+filename)
            import shutil
            shutil.copy2(profile/filename,output/filename)
            report['captures'].append(filename)

        try:
            (sd/'room.txt').write_text(manifest['rooms']['effects']+'\n')
            for name,value in [('furniture-probe.txt','0:0\n'),('height-probe.txt','0:0:0:0\n')]:
                (sd/name).write_text(value)
            process=subprocess.Popen(['podman','run','--rm','--name',container,'--network','host','-v',f'{args.emulator.resolve()}:/emu:ro,z','-v',f'{ROOT}:/work:ro,z','-v',f'{profile}:/profile:rw,z','-w','/profile','-e','XDG_CONFIG_HOME=/profile/config','-e','XDG_DATA_HOME=/profile/data','-e','XDG_CACHE_HOME=/profile/cache','-e','LIBGL_ALWAYS_SOFTWARE=1','-e','QT_X11_NO_MITSHM=1','-e','SDL_AUDIODRIVER=dummy','-e','LANG=C.UTF-8','localhost/tweebbo-expansion-azahar','bash','-c','set -e; Xvfb :97 -screen 0 1000x1000x24 > xvfb-wired-highlight.log 2>&1 & export DISPLAY=:97; sleep 1; exec /emu/squashfs-root/AppRun /work/out/debug/tweebbo-debug.3dsx'],stdout=log,stderr=subprocess.STDOUT)
            def visible():
                try:return bool(inside('xdotool','search','--onlyvisible','--name','Azahar'))
                except subprocess.CalledProcessError:return False
            await wait_for(visible,40)
            window=inside('xdotool','search','--onlyvisible','--name','Azahar').splitlines()[0]
            await wait_for(lambda:'on Old 3DS/2DS' in (sd/'log.txt').read_text())
            await asyncio.sleep(9)
            await apply(3,0,0xff0000);capture('baseline')
            await apply(3,100,0xff0000);capture('red-tint')
            await apply(4,38,0xffffff);capture('brighten')
            await apply(2,50,0);capture('darken')
            await apply(3,0,0xff0000);capture('restored')
            report['status']='captured'
            report['sourceSettings']={'redTint':{'kind':3,'strength':100,'rgb':16711680},'brighten':{'kind':4,'wireStrength':38,'factoryPeakStrength':75,'animation':1,'explanation':'Source dispatcher doubles38 to76 and forces animation 1; the factory quantizes the live cosine pulse in five-percent steps.'},'darken':{'kind':2,'strength':50}}
        finally:
            try:
                await apply(3,0,0xff0000)
                await save(original)
                report['originalWiredConfigRestored']=True
            finally:
                (sd/'room.txt').write_text(previous_room)
                report['profileRoomRestored']=(sd/'room.txt').read_text().strip()
                if process:
                    subprocess.run(['podman','stop','-t','2',container],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=False)
                    try:process.wait(timeout=8)
                    except subprocess.TimeoutExpired:process.terminate()
                log.close()
                (output/'capture-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':report['status'],'captures':len(report['captures']),'credentialsPrinted':False,'originalWiredConfigRestored':report.get('originalWiredConfigRestored',False)}))

asyncio.run(main())

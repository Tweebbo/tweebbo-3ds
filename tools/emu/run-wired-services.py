#!/usr/bin/env python3
"""Capture visitor service windows in the actual isolated Old 3DS client."""
import argparse
import hashlib
import json
import shutil
import subprocess
import time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--emulator',type=Path,required=True)
args=parser.parse_args()
profile=ROOT/'build/azahar-profile'
sd=profile/'user/sdmc/3ds/Tweebbo-debug'
rom=ROOT/'out/debug/tweebbo-debug.3dsx'
previous=(sd/'room.txt').read_text()
assert previous.strip()=='expansion-emulator'
assert not subprocess.check_output(['podman','ps','--filter','name=tweebbo-expansion-azahar','--format','{{.Names}}'],text=True).strip(),'Wait for the other isolated profile probe.'
assert (sd/'server.txt').read_text().strip()=='ws://127.0.0.1:18103'
container='tweebbo-expansion-azahar-wired-services'
output=ROOT.parent/'hotel/docs/expansion/old3ds-wired-services'
output.mkdir(exist_ok=True)
log=(profile/'wired-services-container.log').open('w')
report={'status':'incomplete','emulator':'Azahar 2126.2','model':'Old 3DS/2DS','profile':'Synthetic visitor 19','room':'wired-services-lab','scope':'Actual private 3DSX and loopback server on port 18103; visitor reading and controls only; no transfer or purchase confirmations','romSha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'captures':[],'physicalDeviceAcceptance':'pending'}
(sd/'room.txt').write_text('wired-services-lab\n')
(sd/'furniture-probe.txt').write_text('0:0\n')
(sd/'height-probe.txt').write_text('0:0:0:0\n')
process=subprocess.Popen(['podman','run','--rm','--name',container,'--network','host','-v',f'{args.emulator.resolve()}:/emu:ro,z','-v',f'{ROOT}:/work:ro,z','-v',f'{profile}:/profile:rw,z','-w','/profile','-e','XDG_CONFIG_HOME=/profile/config','-e','XDG_DATA_HOME=/profile/data','-e','XDG_CACHE_HOME=/profile/cache','-e','LIBGL_ALWAYS_SOFTWARE=1','-e','QT_X11_NO_MITSHM=1','-e','SDL_AUDIODRIVER=dummy','-e','LANG=C.UTF-8','localhost/tweebbo-expansion-azahar','bash','-c','set -e; Xvfb :97 -screen 0 1000x1000x24 > xvfb-wired-services.log 2>&1 & export DISPLAY=:97; sleep 1; exec /emu/squashfs-root/AppRun /work/out/debug/tweebbo-debug.3dsx'],stdout=log,stderr=subprocess.STDOUT)

def inside(*command):
    return subprocess.check_output(['podman','exec','-e','DISPLAY=:97',container,*command],stderr=subprocess.STDOUT,text=True).strip()


def wait_for(test,seconds=30):
    deadline=time.monotonic()+seconds
    while not test():
        assert process.poll() is None,'Isolated emulator exited early.'
        assert time.monotonic()<deadline,'Timed out waiting for service probe.'
        time.sleep(.2)


def capture(name):
    filename='old3ds-wired-service-'+name+'.png'
    inside('import','-window',window,'/profile/'+filename)
    shutil.copy2(profile/filename,output/filename)
    report['captures'].append(filename)

try:
    def visible():
        try:return bool(inside('xdotool','search','--onlyvisible','--name','Azahar'))
        except subprocess.CalledProcessError:return False
    wait_for(visible,40)
    window=inside('xdotool','search','--onlyvisible','--name','Azahar').splitlines()[0]
    wait_for(lambda:'on Old3DS/2DS' in (sd/'log.txt').read_text().replace('Old 3DS','Old3DS'))
    time.sleep(9)
    capture('room')
    for serial,(name,item)in enumerate([('chest',211),('contract',218),('label',219),('link',220)],3001):
        (sd/'furniture-probe.txt').write_text(f'{serial}:{item}\n')
        wait_for(lambda:f'Furniture fixture requested {serial} ({item})' in (sd/'log.txt').read_text())
        time.sleep(2.5)
        capture(name)
    report['status']='captured'
finally:
    (sd/'furniture-probe.txt').write_text('0:0\n')
    (sd/'room.txt').write_text(previous)
    report['profileRoomRestored']=(sd/'room.txt').read_text().strip()
    subprocess.run(['podman','stop','-t','2',container],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=False)
    try:process.wait(timeout=8)
    except subprocess.TimeoutExpired:process.terminate()
    log.close()
    (output/'capture-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'status':report['status'],'captures':len(report['captures']),'profileRoomRestored':report['profileRoomRestored']}))

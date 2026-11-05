#!/usr/bin/env python3
"""Exercise the production worker without modifying an installed application."""
import hashlib
import json
import struct
import subprocess
import tempfile
from pathlib import Path

root=Path(__file__).resolve().parents[1]
out=root/'build/update-background-tests'
out.mkdir(parents=True,exist_ok=True)
binary=out/'worker'
subprocess.run(['cc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-format-truncation','-Wno-misleading-indentation','-Wno-unused-function','-DAPP_VERSION="1.3.14"','-I'+str(root/'tests/stubs/updater'),'-I'+str(root/'source'),str(root/'tests/update-background-worker.c'),str(root/'source/update.c'),str(root/'source/incremental.c'),'-lcrypto','-ljansson','-lz','-pthread','-o',str(binary)],check=True)
cases=['homebrew-success','homebrew-legacy-size','homebrew-full-choice','homebrew-corrupt','pause','cancel','cia-success','cia-corrupt','short-write','write-failure','finish-failure']
results=[]
for case in cases:
 with tempfile.TemporaryDirectory(prefix='tweebbo-background-') as temp:
  folder=Path(temp);(folder/'sdmc:/3ds/Tweebbo').mkdir(parents=True)
  data=bytearray(b'\x5a'*(512*1024))
  if case.startswith('homebrew') or case in ['pause','cancel']:data[:4]=b'3DSX'
  else:
   data[:32]=bytes(32);struct.pack_into('<I',data,0,32)
   data[64:68]=b'\x00\x01\x00\x04';data[64+0x140+0x4c:64+0x140+0x54]=bytes.fromhex('0004000007ebb000')
  (folder/'payload.bin').write_bytes(data);(folder/'app.3dsx').write_bytes(b'3DSX previous app')
  sha=hashlib.sha256(data).hexdigest()
  manifest={'version':'1.3.15','cia':'https://tweebbo.lol/test.cia','3dsx':'https://tweebbo.lol/test.3dsx','sha256':sha,'3dsx_sha256':sha,'size':len(data),'3dsx_size':len(data)}
  if case=='homebrew-legacy-size':del manifest['3dsx_size']
  if case=='homebrew-full-choice':manifest['incremental']=[{'format':'tupd1','from':'1.3.14','3dsx':{'url':'https://tweebbo.lol/test.tupd','bytes':16,'sha256':'0'*64}}]
  (folder/'manifest.json').write_text(json.dumps(manifest))
  result=subprocess.run([str(binary),case],cwd=folder,text=True,capture_output=True,timeout=15)
  assert result.returncode==0,(case,result.stdout,result.stderr)
  results.append({'case':case,'status':'PASS','output':result.stdout.strip()})
  if case=='cancel':
   partial=folder/'app.3dsx.new';assert 0<partial.stat().st_size<len(data)
   resumed=subprocess.run([str(binary),'homebrew-resume'],cwd=folder,text=True,capture_output=True,timeout=15)
   assert resumed.returncode==0,(resumed.stdout,resumed.stderr)
   results.append({'case':'homebrew-resume','status':'PASS','output':resumed.stdout.strip()})
(out/'report.json').write_text(json.dumps({'status':'PASS','physicalConsole':False,'scope':'Production worker with real file I/O, threads and SHA; mocked transport and AM installation service.','cases':results},indent=2)+'\n')
print('PASS:',len(results),'production 3DS background worker, pause, cancel, staged corruption and installer fault cases')

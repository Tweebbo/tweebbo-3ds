#!/usr/bin/env python3
"""Prepare a curated LAN download directory without SD data or build internals."""
from pathlib import Path
import hashlib
import html
import json
import os
import shutil
import sys
from urllib.parse import urlparse

root = Path(__file__).resolve().parent.parent
share = root / 'out/debug/share'
share.mkdir(parents=True, exist_ok=True)
if len(sys.argv) < 2:
    raise SystemExit('Usage: tools/debug-page.py http://<PC LAN address>:8097')
base = sys.argv[1]
hotel_host = urlparse(base).hostname
if not hotel_host:
    raise SystemExit('Pass a complete LAN download URL.')
version = root.joinpath('VERSION').read_text().strip() + '-debug'
files = []
for extension in ('cia', '3dsx'):
    name = f'tweebbo-debug.{extension}'
    source = root / 'out/debug' / name
    # Atomic replace: a console downloading the previous build keeps reading complete files.
    shutil.copy2(source, share / (name + '.tmp'))
    os.replace(share / (name + '.tmp'), share / name)
    files.append({'file': name, 'bytes': source.stat().st_size, 'sha256': hashlib.sha256(source.read_bytes()).hexdigest()})
shutil.copy2(root / 'docs/DEBUG.md', share / 'debug-guide.txt')
shutil.copy2(root / 'assets/icon.png', share / 'icon.png')
share.joinpath('manifest.json').write_text(json.dumps({'version': version, 'localOnly': True, 'files': files}, indent=2) + '\n')
# Consoles running a debug build poll latest.json and install a higher build number.
build_number = int((root / 'build/debug-build').read_text().strip()) if (root / 'build/debug-build').exists() else 0
by_ext = {f['file'].split('.')[-1]: f for f in files}
share.joinpath('latest.json.tmp').write_text(json.dumps({
    'version': f'{version}.{build_number}', 'build': build_number,
    'cia': f"{base}/{by_ext['cia']['file']}", 'size': by_ext['cia']['bytes'], 'sha256': by_ext['cia']['sha256'],
    '3dsx': f"{base}/{by_ext['3dsx']['file']}", '3dsx_sha256': by_ext['3dsx']['sha256'],
    'notes': os.environ.get('NOTES', 'New local debug build.'),
}, indent=1) + '\n')
# The manifest goes last, so it never points at files that are not there yet.
os.replace(share / 'latest.json.tmp', share / 'latest.json')
# Keep each build's ELF: crash.txt addresses only make sense against the exact binary.
(root / 'build/debug-elf').mkdir(parents=True, exist_ok=True)
shutil.copy2(root / 'out/debug/tweebbo-debug.elf', root / f'build/debug-elf/build-{build_number}.elf')
share.joinpath('SHA256SUMS').write_text(''.join(f"{f['sha256']}  {f['file']}\n" for f in files))
captures = [
    ('10-founder-profile', 'Founder profile and original badges'),
    ('01-lobby', 'Classic Lobby'),
    ('02-navigator', 'Room navigator'),
    ('03-catalogue', 'Furniture catalogue'),
    ('04-spaces', 'Walls and floors'),
    ('05-finish-preview', 'Finish preview'),
    ('07-wardrobe', 'Wardrobe'),
    ('08-help', 'Local debug connection'),
    ('11-anonymous', 'Anonymous appearance'),
    ('12-presence-off', 'Founder Presence off'),
    ('16-chess', 'Chess with a browser player'),
    ('18-crossplay-move', 'Server-confirmed chess moves'),
    ('19-resign-confirmation', 'Game confirmation dialog'),
    ('22-over-capacity', 'Hotel capacity and Frank status screen'),
    ('32-snow-play', 'Snow Storm practice match'),
    ('41-casino-room', 'Casino'),
    ('44-casino-slot', 'Slot machine'),
    ('45-casino-spin', 'Server-confirmed spin'),
    ('autumn-piano', 'Lobby piano, lamps and Overview'),
    ('autumn-dash', 'VIP Dash with server-confirmed amounts'),
    ('autumn-trade', 'Safe Trading final confirmation'),
]
captures = [(key, label) for key, label in captures if (share / f'screenshots/{key}.png').exists()]
options = ''.join(f'<option value="{key}">{html.escape(label)}</option>' for key, label in captures)
checksums = ''.join(f'<p><b>{f["file"]}</b><br><code>{f["sha256"]}</code></p>' for f in files)
sizes = {f['file'].split('.')[-1]: f'{f["bytes"]/1048576:.1f} MiB' for f in files}
page = '''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tweebbo Debug · Nintendo 3DS</title><link rel="icon" href="icon.png">
<style>
*{box-sizing:border-box}body{margin:0;background:#c3d4d9;color:#202020;font:15px/1.5 Verdana,Arial,sans-serif}
main{max-width:980px;margin:28px auto;padding:0 16px}header{display:flex;align-items:center;gap:14px;margin-bottom:20px}
header img{width:48px;height:48px;image-rendering:pixelated}h1{font-size:25px;margin:0}header p{margin:0;font-size:13px}
.window{border:2px solid #202020;border-radius:7px;background:#eee;margin:0 0 20px;overflow:hidden}
h2{font-size:15px;font-weight:normal;background:#426777;color:white;text-align:center;margin:0;padding:7px 12px}
.body{padding:20px}.install{display:grid;grid-template-columns:240px 1fr;gap:26px}.qr{display:block;width:228px;max-width:100%;image-rendering:pixelated;background:white;border:1px solid #555}
p{margin:0 0 14px}a{color:#15495d;text-underline-offset:3px}a:focus-visible,select:focus-visible,summary:focus-visible{outline:3px solid #9c3c00;outline-offset:4px}
.downloads{display:flex;flex-wrap:wrap;gap:10px;margin:18px 0}.button{display:inline-block;background:#fff;border:1px solid #222;border-bottom:3px solid #999;border-radius:4px;padding:8px 13px;color:#202020;font-weight:bold;text-decoration:none}.button:hover{background:#f5e9be}
.primary{background:#286018;color:white;border-color:#15370c}.primary:hover{background:#1e4a11}
small,.muted{font-size:12px;color:#505050}code{overflow-wrap:anywhere;font-size:13px}.columns{display:grid;grid-template-columns:1fr 1fr;gap:20px}
ul,ol{padding-left:22px;margin:8px 0 16px}li{margin:7px 0}h3{font-size:16px;margin:0 0 10px}
select{font:inherit;width:100%;padding:8px;border:1px solid #555;background:white;color:#202020;margin:8px 0 18px}
.view{position:relative;width:100%;max-width:400px;aspect-ratio:5/6;overflow:hidden;background:#111;margin:0 auto 12px}
.view img{position:absolute;width:320%;max-width:none;height:auto;left:-110%;top:-52.083333%;image-rendering:pixelated}
figure{margin:0}figcaption{text-align:center;font-size:12px}details{border-top:1px solid #aaa;margin-top:18px;padding-top:12px}summary{cursor:pointer;font-weight:bold}details p{margin:12px 0}
footer{font-size:12px;margin:20px 0 32px} @media(max-width:680px){main{margin-top:16px}.install,.columns{grid-template-columns:1fr}.qr{margin:auto}.body{padding:16px}h1{font-size:21px}}
</style></head><body><main>
<header><img src="icon.png" alt="Tweebbo"><div><h1>Tweebbo Debug</h1><p>Nintendo 3DS · @VERSION@ · Local testing</p></div></header>
<section class="window"><h2>Install on your console</h2><div class="body install">
<div><img class="qr" src="qr.png" alt="FBI remote installation QR for the local Tweebbo Debug CIA"><p class="muted">FBI → Remote Install → Scan QR Code</p></div>
<div><p><b>Ready for your New 3DS / New 3DS XL / New 2DS XL test.</b> Keep the console and this PC on the same local network, with the PC running.</p>
<p>The HOME Menu installs a separate <b>Tweebbo Debug</b> title. Open it to join the local hotel.</p>
<div class="downloads"><a class="button primary" href="tweebbo-debug.cia" download>Download CIA · @CIA_SIZE@</a><a class="button" href="tweebbo-debug.3dsx" download>Download 3DSX · @3DSX_SIZE@</a></div>
<p class="muted">CIA: use your existing FBI setup. 3DSX: place in SD:/3ds/ and open with the Homebrew Launcher.</p>
<p><a href="http://192.168.1.20:8092/" target="_blank" rel="noopener">Open the local browser hotel</a> · <a href="debug-guide.txt">Installation and test guide</a></p>
<small>Local hotel: ws://192.168.1.20:8092<br>Settings and character key: SD:/3ds/Tweebbo-debug/<br>Public hotel sessions are disabled; updates come from this page.</small>
</div></div></section>
<div class="columns"><section class="window"><h2>What to try</h2><div class="body">
<ul><li>Native-sized text, updated wardrobe and catalogue.</li><li>Original furniture textures and animations, with lossless compression and memory-aware loading.</li><li>Wall/floor previews and purchases in your own room.</li><li>VIP / Staff badges, Founder appearance and anonymous mode.</li><li>Tic-tac-toe, Connect Four and Chess with a browser player.</li><li>Chat, friends, pets, swimming, locked rooms and reconnection.</li></ul>
<h3>Before the public release</h3><p>Snow Storm and Casino game windows still need native implementations. Physical-console performance, sleep/wake and crowded-room tests are pending.</p>
<p>This is a debug build for review; full browser feature parity is still pending.</p>
<details><summary>Build verification</summary><p>Native host and transport tests, isolated server tests, native/browser board-game round trips and emulator interface tours passed. Emulator timing does not establish console performance.</p><a href="SHA256SUMS">SHA-256 checksums</a> · <a href="manifest.json">Build manifest</a>@CHECKSUMS@</details>
</div></section><section class="window"><h2>Actual emulator captures</h2><div class="body">
<label for="capture">Screen to inspect</label><select id="capture">@OPTIONS@</select>
<figure><div class="view"><img id="shot" src="screenshots/10-founder-profile.png" alt="Founder profile and original badges in the emulator"></div>
<figcaption>Top: 400 × 240. Bottom: 320 × 240.<br><a id="full" href="screenshots/10-founder-profile.png" target="_blank" rel="noopener">Open full capture</a></figcaption></figure>
</div></section></div>
<footer>For an issue report, note the console model, room and action. Diagnostics are log.txt and last-error.txt in the debug SD folder. Keep character keys and access/transfer codes private.</footer>
</main><script>
const select=document.querySelector('#capture'),shot=document.querySelector('#shot'),full=document.querySelector('#full');
select.addEventListener('change',()=>{shot.src='screenshots/'+select.value+'.png';shot.alt=select.selectedOptions[0].text+' in the emulator';full.href=shot.src;});
</script></body></html>'''
for key, value in {'VERSION': version, 'CIA_SIZE': sizes['cia'], '3DSX_SIZE': sizes['3dsx'], 'OPTIONS': options, 'CHECKSUMS': checksums}.items():
    page = page.replace('@' + key + '@', value)
page = page.replace('192.168.1.20:8092', html.escape(hotel_host) + ':8092')
share.joinpath('index.html').write_text(page)
print(f'Prepared {share}; CIA URL: {base}/tweebbo-debug.cia')

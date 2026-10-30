#!/usr/bin/env python3
"""Run the real Tweebbo updater with mocked AM/network and real files/checksums."""
import hashlib
import json
import os
import shlex
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

project = Path(__file__).resolve().parents[1]
build = project / 'build/incremental-worker-tests'
build.mkdir(parents=True, exist_ok=True)
binary = build / 'worker'
subprocess.run(['cc', '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-format-truncation',
                *shlex.split(os.environ.get('CPPFLAGS', '')), '-DAPP_VERSION="1.3.5"',
                '-I' + str(project / 'tests/stubs/updater'), '-I' + str(project / 'source'),
                str(project / 'tests/incremental-worker.c'), str(project / 'source/update.c'),
                str(project / 'source/incremental.c'), '-lz', '-lcrypto', '-l:libjansson.so.4', '-o', str(binary)], check=True)
sha = lambda value: hashlib.sha256(value).hexdigest()
scenarios = ['legacy', 'checksum', 'wrong-title', 'short-write', 'finish-fail', 'small-success',
             'small-base-missing', 'small-base-damaged', 'small-invalid', 'small-interrupted',
             'homebrew', 'homebrew-small', 'homebrew-fat', 'homebrew-replace-fail']
results = []
with tempfile.TemporaryDirectory(prefix='updater-', dir=build) as temp:
    for scenario in scenarios:
        root = Path(temp) / scenario
        (root / 'sdmc:/3ds/Tweebbo').mkdir(parents=True)
        (root / 'romfs:/games').mkdir(parents=True)
        homebrew = scenario.startswith('homebrew')
        payload = bytearray(b'Z' * (512 * 1024))
        if homebrew:
            payload[:4] = b'3DSX'
        else:
            payload[:32] = b'\0' * 32
            struct.pack_into('<I', payload, 0, 32)
            struct.pack_into('>I', payload, 64, 0x10004)
            struct.pack_into('>Q', payload, 64 + 0x140 + 0x4c, 0x0004000007E90200 if scenario == 'wrong-title' else 0x0004000007EBB000)
        payload = bytes(payload)
        (root / 'payload.bin').write_bytes(payload)
        (root / 'previous.bin').write_bytes(b'current application')
        (root / 'app.3dsx').write_bytes(b'current application')
        manifest = {'version': '1.3.6', 'size': len(payload), '3dsx_size': len(payload),
                    'sha256': sha(payload), '3dsx_sha256': sha(payload),
                    'cia': 'https://tweebbo.lol/test.cia', '3dsx': 'https://tweebbo.lol/test.3dsx'}
        small = scenario.startswith('small-') or scenario == 'homebrew-small'
        if small:
            source = payload[4096:]
            if scenario != 'small-base-missing':
                (root / 'romfs:/games/bank.gpk').write_bytes(b'X' * len(source) if scenario == 'small-base-damaged' else source)
            header = (b'TUPD001\0' + struct.pack('<III', 2 if homebrew else 1, len(payload), 2) + b'0004000007EBB000' +
                      b'1.3.5'.ljust(24, b'\0') + b'1.3.6'.ljust(24, b'\0') + bytes.fromhex(sha(payload)))
            name = b'games/bank.gpk'
            patch = zlib.compress(header + struct.pack('<BI', 1, 4096) + payload[:4096] +
                                  struct.pack('<BIH', 2, len(source), len(name)) + name + bytes.fromhex(sha(source)), 9)
            if scenario == 'small-invalid':
                patch = zlib.compress(b'not a compatible patch')
            (root / 'payload.tupd').write_bytes(patch)
            manifest['incremental'] = [{'format': 'tupd1', 'from': '1.3.5', '3dsx' if homebrew else 'cia':
                                       {'url': 'https://tweebbo.lol/test.tupd', 'bytes': len(patch), 'sha256': sha(patch)}}]
        (root / 'manifest.json').write_text(json.dumps(manifest))
        result = subprocess.run([str(binary), scenario], cwd=root, capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, (scenario, result.stdout, result.stderr)
        results.append({'scenario': scenario, 'status': 'PASS', 'output': result.stdout.strip()})
report = {'status': 'PASS', 'count': len(results), 'cases': results,
          'scope': 'Production update.c with mocked libctru/network; real SD file I/O, resumable downloads, SHA-256 and reconstruction',
          'physicalConsoleTested': False}
(build / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS Tweebbo updater:', len(results), 'legacy, incremental, resume, explicit fallback, AM and atomic replacement cases')

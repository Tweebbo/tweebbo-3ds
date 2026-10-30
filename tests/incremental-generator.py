#!/usr/bin/env python3
"""Round-trip real 3DSX/RomFS fixtures, including empty files sharing an offset."""
import hashlib
import json
import struct
import subprocess
import tempfile
from pathlib import Path

project = Path(__file__).resolve().parents[1]
reader = project / 'build/incremental-tests/reader'
if not reader.exists():
    subprocess.run(['python3', str(project / 'tests/incremental.py')], check=True)


def artifact(code, data):
    header = bytearray(48)
    header[:4] = b'3DSX'
    struct.pack_into('<H', header, 4, 44)
    struct.pack_into('<I', header, 0x28, 48)
    header[44] = code
    directory = struct.pack('<6I', 0, 0xffffffff, 0xffffffff, 0, 0xffffffff, 0)
    entries = bytearray()
    for name, next_file, count in [('same', 40, len(data)), ('empty', 0xffffffff, 0)]:
        text = name.encode('utf-16le')
        entries.extend(struct.pack('<IIQQII', 0, next_file, 0, count, 0xffffffff, len(text)))
        entries.extend(text)
        entries.extend(b'\0' * (-len(entries) % 4))
    payload = 160
    level = struct.pack('<10I', 40, 40, 0, 40, len(directory), 64, 0, 64, len(entries), payload)
    romfs = level + directory + entries
    return bytes(header) + romfs.ljust(payload, b'\0') + data


with tempfile.TemporaryDirectory(prefix='incremental-generator-') as temporary:
    root = Path(temporary)
    source = root / 'romfs'
    source.mkdir()
    original = bytes(range(256)) * 512
    (source / 'same').write_bytes(original)
    (source / 'empty').write_bytes(b'')
    base = root / 'old.3dsx'
    base.write_bytes(artifact(1, original))
    for label, data in [('unchanged', original), ('changed', b'changed asset' * 9000)]:
        target, patch, rebuilt = root / 'new.3dsx', root / 'update.tupd', root / 'rebuilt.3dsx'
        target.write_bytes(artifact(2, data))
        subprocess.run(['python3', str(project / 'tools/make-incremental.py'), '--base', str(base),
                        '--target', str(target), '--output', str(patch), '--title-id', '0004000007E90200',
                        '--from-version', '1.2.1', '--to-version', '1.2.2'], check=True, capture_output=True)
        report = json.loads(patch.with_name(patch.name + '.json').read_text())
        assert report['reusedBytes'] == (len(original) if label == 'unchanged' else 0)
        subprocess.run([str(reader), 'apply', str(patch), str(rebuilt), str(source), report['titleId'],
                        '1.2.1', '1.2.2', report['targetSha256'], str(report['targetBytes']), '1',
                        report['sha256'], str(report['bytes'])], check=True, capture_output=True)
        assert rebuilt.read_bytes() == target.read_bytes()
        assert hashlib.sha256(rebuilt.read_bytes()).hexdigest() == report['targetSha256']
print('PASS generator: unchanged/changed assets and empty shared-offset file round-trip through production reader')

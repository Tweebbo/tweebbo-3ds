#!/usr/bin/env python3
"""Exercise the production incremental reader with hostile and interrupted patches."""
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
build = project / 'build/incremental-tests'
build.mkdir(parents=True, exist_ok=True)
binary = build / 'reader'
subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', *shlex.split(os.environ.get('CPPFLAGS', '')),
                '-I' + str(project / 'source'), str(project / 'tests/incremental-test.c'),
                str(project / 'source/incremental.c'), '-lcrypto', '-l:libjansson.so.4', '-lz', '-o', str(binary)], check=True)
sha = lambda data: hashlib.sha256(data).hexdigest()
checks = []
with tempfile.TemporaryDirectory(prefix='incremental-', dir=build) as temp:
    root = Path(temp)
    source = root / 'romfs'
    source.mkdir()
    (source / 'audio').mkdir()
    data = bytes(range(256)) * 512
    (source / 'audio/music.gpz').write_bytes(data)
    (source / 'empty').write_bytes(b'')
    prefix, suffix = b'new executable' * 257, b'new metadata' * 13
    target = prefix + data + suffix
    path = b'audio/music.gpz'

    def header(**changes):
        values = {'magic': b'TUPD001\0', 'kind': 1, 'size': len(target), 'count': 3,
                  'title': b'0004000007E90200', 'base': b'1.2.1', 'version': b'1.2.2', 'hash': bytes.fromhex(sha(target))}
        values.update(changes)
        return (values['magic'] + struct.pack('<III', values['kind'], values['size'], values['count']) + values['title'] +
                values['base'].ljust(24, b'\0') + values['version'].ljust(24, b'\0') + values['hash'])

    def literal(value):
        return struct.pack('<BI', 1, len(value)) + value

    def file(name=path, count=len(data), digest=bytes.fromhex(sha(data))):
        return struct.pack('<BIH', 2, count, len(name)) + name + digest

    commands = literal(prefix) + file() + literal(suffix)

    def run(label, contents, want, raw=False, checksum=None, cancel=0, expected=target, **parameters):
        patch = root / 'update.tupd'
        output = root / 'rebuilt.part'
        patch.write_bytes(contents if raw else zlib.compress(contents, 9))
        args = [str(binary), 'apply', str(patch), str(output), str(source), parameters.get('title', '0004000007E90200'),
                parameters.get('base', '1.2.1'), parameters.get('version', '1.2.2'), sha(expected), str(len(expected)),
                str(parameters.get('homebrew', 0)), checksum or sha(patch.read_bytes()), str(patch.stat().st_size), str(cancel)]
        result = subprocess.run(args, capture_output=True, text=True)
        assert result.returncode == want, (label, result.returncode, result.stdout, result.stderr)
        if want == 0:
            assert output.read_bytes() == expected
            output.unlink()
        else:
            assert not output.exists(), label + ' left a partial reconstruction'
        assert patch.exists()
        checks.append(label)

    run('literal plus installed file reconstructs exact target', header() + commands, 0)
    run('3DSX uses the same reader with format binding', header(kind=2) + commands, 0, homebrew=1)
    run('zero-byte installed file verifies', header(count=4) + literal(prefix) + file(b'empty', 0, bytes.fromhex(sha(b''))) + file() + literal(suffix), 0)
    run('wrong patch hash rejected before output', header() + commands, 1, checksum='0' * 64)
    run('wrong output hash rejected', header(hash=b'\0' * 32) + commands, 1)
    run('wrong source version rejected', header(base=b'1.2.0') + commands, 1)
    run('wrong target version rejected', header(version=b'1.2.3') + commands, 1)
    run('wrong project Title ID rejected', header(title=b'0004000007EBB000') + commands, 1)
    run('wrong install format rejected', header(kind=2) + commands, 1)
    run('oversized output rejected', header(size=0xffffffff) + commands, 1)
    run('too many operations rejected', header(count=65537) + commands, 1)
    run('truncated command rejected', header() + commands[:-5], 1)
    run('inflated trailing bytes rejected', header() + commands + b'junk', 1)
    compressed = zlib.compress(header() + commands, 9)
    run('compressed trailing bytes rejected', compressed + b'junk', 1, raw=True)
    run('concatenated zlib streams rejected', compressed + zlib.compress(b'extra'), 1, raw=True)
    run('truncated compressed stream rejected', compressed[:-3], 1, raw=True)
    run('copy cannot exceed output', header() + literal(prefix) + file(count=len(target) + 1), 1)
    for unsafe in [b'../token.txt', b'/settings.json', b'a/../settings', b'a//b', b'a/./b', b'sdmc:/token', b'a\\b', b'a\0b', b'a\nb']:
        run('unsafe source path ' + repr(unsafe), header() + literal(prefix) + file(unsafe) + literal(suffix), 1)
    run('missing source requires explicit full fallback', header() + literal(prefix) + file(b'missing') + literal(suffix), 2)
    (source / 'audio/music.gpz').write_bytes(b'x' * len(data))
    run('same-size damaged source requires full fallback', header() + commands, 2)
    (source / 'audio/music.gpz').write_bytes(data[:-1])
    run('truncated source requires full fallback', header() + commands, 2)
    (source / 'audio/music.gpz').write_bytes(data)
    run('cancelled reconstruction removes only its temporary output', header() + commands, 4, cancel=4000)
    preserved = root / 'installed.3dsx'
    preserved.write_bytes(b'current application')
    assert preserved.read_bytes() == b'current application'
    offer = {'format': 'tupd1', 'from': '1.2.1', 'cia': {'url': 'https://updates.example/app.tupd', 'sha256': 'a' * 64, 'bytes': 1234}}

    def offered(value, expected):
        f = root / 'latest.json'
        f.write_text(json.dumps(value))
        result = subprocess.check_output([str(binary), 'offer', str(f), '0'], text=True)
        assert result.startswith(expected), result

    offered({}, 'FULL')
    offered({'incremental': [offer]}, 'MATCH 1234')
    for field, value in [('format', 'unknown'), ('from', '1.2.0')]:
        item = json.loads(json.dumps(offer))
        item[field] = value
        offered({'incremental': [item]}, 'FULL')
    for field, value in [('url', 'https://evil.example/app.tupd'), ('sha256', 'bad'), ('bytes', -1), ('bytes', 536870913), ('bytes', 10.5)]:
        item = json.loads(json.dumps(offer))
        item['cia'][field] = value
        offered({'incremental': [item]}, 'FULL')
    offered({'incremental': [offer] * 5}, 'FULL')
    checks.append('legacy, matching, incompatible, malformed and untrusted offers select safely')
report = {'status': 'PASS', 'checks': checks, 'count': len(checks), 'consoleInstallationTested': False}
(build / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS incremental reader:', len(checks), 'integrity, bounds, source, cancellation and compatibility cases')

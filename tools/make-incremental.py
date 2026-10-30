#!/usr/bin/env python3
"""Build a bounded TUPD1 update using ordinary files from the installed RomFS."""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

MAX_BYTES = 512 * 1024 * 1024


def align(value, boundary=64):
    return (value + boundary - 1) // boundary * boundary


def checksum(file, offset=0, size=None):
    digest = hashlib.sha256()
    with file.open('rb') as source:
        source.seek(offset)
        left = file.stat().st_size - offset if size is None else size
        while left:
            chunk = source.read(min(65536, left))
            if not chunk:
                raise ValueError('Truncated input')
            digest.update(chunk)
            left -= len(chunk)
    return digest.hexdigest()


def safe_path(value):
    return (0 < len(value.encode()) <= 240 and
            all(32 <= ord(c) <= 126 and c not in '\\:' for c in value) and
            all(part not in ['', '.', '..'] for part in value.split('/')))


def romfs_entries(path, homebrew, title):
    size = path.stat().st_size
    if not 32 <= size <= MAX_BYTES:
        raise ValueError('Input exceeds the application size limit')
    with path.open('rb') as source:
        def region(offset, length):
            if offset < 0 or length < 0 or offset + length > size:
                raise ValueError('Input region outside the file')
            source.seek(offset)
            result = source.read(length)
            if len(result) != length:
                raise ValueError('Truncated input region')
            return result

        header = region(0, 48)
        if homebrew:
            if header[:4] != b'3DSX' or struct.unpack_from('<H', header, 4)[0] < 44:
                raise ValueError('Expected an extended 3DSX header')
            start = struct.unpack_from('<I', header, 0x28)[0]
        else:
            content = sum(align(struct.unpack_from('<I', header, offset)[0]) for offset in [0, 8, 12, 16])
            ncch = region(content, 512)
            if ncch[0x100:0x104] != b'NCCH' or f'{struct.unpack_from("<Q", ncch, 0x118)[0]:016X}' != title:
                raise ValueError('CIA program identity differs')
            start = content + struct.unpack_from('<I', ncch, 0x1b0)[0] * 512
        header = region(start, 0x60)
        if header[:4] == b'IVFC':
            exponent = struct.unpack_from('<I', header, 0x4c)[0]
            if not 9 <= exponent <= 20:
                raise ValueError('Invalid RomFS block size')
            start += align(0x60 + struct.unpack_from('<I', header, 8)[0], 1 << exponent)
            header = region(start, 0x28)
        if struct.unpack_from('<I', header)[0] != 0x28:
            raise ValueError('Missing RomFS level-three header')
        directory_offset, directory_size = struct.unpack_from('<II', header, 0xc)
        file_offset, file_size = struct.unpack_from('<II', header, 0x1c)
        payload = struct.unpack_from('<I', header, 0x24)[0]
        if directory_size > 8 * 1024 * 1024 or file_size > 8 * 1024 * 1024:
            raise ValueError('RomFS metadata exceeds its limit')
        directories = region(start + directory_offset, directory_size)
        files = region(start + file_offset, file_size)
        result = {}
        seen_directories, seen_files = set(), set()

        def walk(offset, parent, depth=0):
            if depth > 32 or offset in seen_directories or offset + 24 > len(directories):
                raise ValueError('Invalid directory graph')
            seen_directories.add(offset)
            length = struct.unpack_from('<I', directories, offset + 20)[0]
            if length % 2 or offset + 24 + length > len(directories):
                raise ValueError('Invalid directory name')
            name = directories[offset + 24:offset + 24 + length].decode('utf-16le')
            if name and ('/' in name or not safe_path(name)):
                raise ValueError('Unsafe directory name')
            prefix = parent + (name + '/' if name else '')
            child = struct.unpack_from('<I', directories, offset + 12)[0]
            while child != 0xffffffff:
                if child in seen_files or child + 32 > len(files):
                    raise ValueError('Invalid file graph')
                seen_files.add(child)
                length = struct.unpack_from('<I', files, child + 28)[0]
                if length % 2 or child + 32 + length > len(files):
                    raise ValueError('Invalid file name')
                name = files[child + 32:child + 32 + length].decode('utf-16le')
                path_name = prefix + name
                if '/' in name or not safe_path(path_name) or path_name in result:
                    raise ValueError('Unsafe or duplicate RomFS path')
                position, count = struct.unpack_from('<QQ', files, child + 8)
                absolute = start + payload + position
                if absolute + count > size:
                    raise ValueError('RomFS payload outside the artifact')
                result[path_name] = {'offset': absolute, 'bytes': count, 'sha256': checksum(path, absolute, count)}
                child = struct.unpack_from('<I', files, child + 4)[0]
            child = struct.unpack_from('<I', directories, offset + 8)[0]
            while child != 0xffffffff:
                walk(child, prefix, depth + 1)
                child = struct.unpack_from('<I', directories, child + 4)[0]

        walk(0, '')
        return result


def build(base, target, output, title, source_version, target_version):
    homebrew = target.suffix == '.3dsx'
    if base.suffix != target.suffix or target.suffix not in ['.cia', '.3dsx']:
        raise ValueError('Base and target must use the same CIA or 3DSX format')
    if len(title) != 16 or any(c not in '0123456789ABCDEF' for c in title):
        raise ValueError('Use a sixteen-digit uppercase Title ID')
    for version in [source_version, target_version]:
        parts = version.split('.')
        if len(version) > 23 or len(parts) != 3 or any(not p.isascii() or not p.isdigit() for p in parts):
            raise ValueError('Use semantic source and target versions')
    if output.resolve() in [base.resolve(), target.resolve()]:
        raise ValueError('Output cannot replace an input')
    old, new = romfs_entries(base, homebrew, title), romfs_entries(target, homebrew, title)
    operations, position, reused = [], 0, 0
    for name, entry in sorted(new.items(), key=lambda item: item[1]['offset']):
        start, count = entry['offset'], entry['bytes']
        if not count:
            continue
        if start < position:
            raise ValueError('Overlapping RomFS file data')
        previous = old.get(name)
        if previous and previous['bytes'] == count and previous['sha256'] == entry['sha256']:
            if start > position:
                operations.append(('literal', position, start - position))
            operations.append(('file', name, count))
            reused += count
            position = start + count
    if position < target.stat().st_size:
        operations.append(('literal', position, target.stat().st_size - position))
    if not operations or len(operations) > 65536:
        raise ValueError('Update recipe exceeds its operation limit')
    target_sha = checksum(target)
    header = (b'TUPD001\0' + struct.pack('<III', 2 if homebrew else 1, target.stat().st_size, len(operations)) +
              title.encode() + source_version.encode().ljust(24, b'\0') + target_version.encode().ljust(24, b'\0') +
              bytes.fromhex(target_sha))
    temporary = output.with_name(output.name + '.building')
    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        with temporary.open('wb') as destination, target.open('rb') as source:
            compressor = zlib.compressobj(9)

            def emit(data):
                destination.write(compressor.compress(data))

            emit(header)
            for kind, value, count in operations:
                emit(struct.pack('<BI', 1 if kind == 'literal' else 2, count))
                if kind == 'file':
                    path = value.encode()
                    emit(struct.pack('<H', len(path)) + path + bytes.fromhex(new[value]['sha256']))
                else:
                    source.seek(value)
                    left = count
                    while left:
                        data = source.read(min(65536, left))
                        if not data:
                            raise ValueError('Target changed while generating the patch')
                        emit(data)
                        left -= len(data)
            destination.write(compressor.flush())
        if checksum(target) != target_sha:
            raise ValueError('Target changed while generating the patch')
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    report = {'format': 'tupd1', 'from': source_version, 'version': target_version, 'titleId': title,
              'artifact': '3dsx' if homebrew else 'cia', 'file': output.name, 'bytes': output.stat().st_size,
              'sha256': checksum(output), 'targetBytes': target.stat().st_size, 'targetSha256': target_sha,
              'reusedBytes': reused, 'reusedFiles': sum(op[0] == 'file' for op in operations), 'operations': len(operations)}
    output.with_name(output.name + '.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['base', 'target', 'output']:
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ['title-id', 'from-version', 'to-version']:
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.base, args.target, args.output, args.title_id.upper(), args.from_version, args.to_version), indent=2))

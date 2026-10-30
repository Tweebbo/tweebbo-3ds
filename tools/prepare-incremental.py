#!/usr/bin/env python3
"""Add optional TUPD1 offers while keeping the legacy full-artifact manifest."""
import argparse
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('incremental_builder', Path(__file__).with_name('make-incremental.py'))
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def version(value):
    parts = value.split('.')
    if len(parts) != 3 or any(not p.isascii() or not p.isdigit() for p in parts):
        raise ValueError('Use semantic release versions')
    return tuple(map(int, parts))


def artifact(manifest, folder, app, kind, repository):
    file = folder / (app + '.' + kind)
    record = manifest[kind]
    if isinstance(record, dict):
        url, sha, size = record['url'], record['sha256'], record['bytes']
    else:
        url = record
        sha = manifest['sha256' if kind == 'cia' else '3dsx_sha256']
        size = manifest.get('size' if kind == 'cia' else '3dsx_size', file.stat().st_size)
    expected = f'https://github.com/{repository}/releases/download/v{manifest["version"]}/{file.name}'
    if url != expected or file.stat().st_size != size or builder.checksum(file) != sha:
        raise ValueError('Release artifact differs from its verified manifest: ' + kind)
    return file, size


def prepare(path, bases, app, repository, title):
    if not 1 <= len(bases) <= 4:
        raise ValueError('Use one to four previous releases')
    latest = json.loads(path.read_text())
    current = version(latest['version'])
    targets = {kind: artifact(latest, path.parent, app, kind, repository) for kind in ['cia', '3dsx']}
    entries, seen = [], set()
    for folder in bases:
        base = json.loads((folder / 'latest.json').read_text())
        previous = base['version']
        if previous in seen or version(previous) >= current:
            raise ValueError('Base versions must be unique and older than the target')
        seen.add(previous)
        entry = {'format': 'tupd1', 'from': previous}
        for kind in ['cia', '3dsx']:
            old, _ = artifact(base, folder, app, kind, repository)
            target, size = targets[kind]
            output = path.parent / f'{app}-from-{previous}.{kind}.tupd'
            report = builder.build(old, target, output, title.upper(), previous, latest['version'])
            if report['bytes'] < size:
                entry[kind] = {'url': f'https://github.com/{repository}/releases/download/v{latest["version"]}/{output.name}',
                               'sha256': report['sha256'], 'bytes': report['bytes']}
        if 'cia' in entry or '3dsx' in entry:
            entries.append(entry)
    if entries:
        latest['incremental'] = entries
    else:
        latest.pop('incremental', None)
    encoded = json.dumps(latest, indent=2) + '\n'
    if len(encoded.encode()) > 16384:
        raise ValueError('Manifest exceeds the oldest supported client size limit')
    temporary = path.with_name(path.name + '.building')
    temporary.write_text(encoded)
    temporary.replace(path)
    return {'status': 'PREPARED', 'version': latest['version'], 'incrementalBases': len(entries),
            'legacyFullArtifactsRetained': True, 'published': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--base', type=Path, action='append', required=True)
    parser.add_argument('--app', required=True)
    parser.add_argument('--repository', required=True)
    parser.add_argument('--title-id', required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.manifest, args.base, args.app, args.repository, args.title_id), indent=2))

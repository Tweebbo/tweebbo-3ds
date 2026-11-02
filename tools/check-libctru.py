#!/usr/bin/env python3
"""Reject SDKs without the extended-memory heap allocation fix."""
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def main():
    root = Path(os.environ.get('DEVKITPRO', '/opt/devkitpro'))
    library_root = Path(os.environ.get('CTRULIB', str(root / 'libctru')))
    library = library_root / 'lib/libctru.a'
    manager = root / 'pacman/bin/pacman'
    if not manager.is_file():
        manager = shutil.which('dkp-pacman') or shutil.which('pacman')
    if not manager or not library.is_file():
        raise RuntimeError('A devkitPro SDK with packaged libctru 2.7.1 or newer is required.')
    result = subprocess.run([str(manager), '-Q', 'libctru'], check=True, capture_output=True, text=True)
    version = result.stdout.split()[1]
    match = re.fullmatch(r'(\d+)\.(\d+)\.(\d+)(?:-\d+)?', version)
    if not match or tuple(map(int, match.groups())) < (2, 7, 1):
        raise RuntimeError(f'libctru {version} lacks the extended-memory heap fix; install 2.7.1 or newer.')
    owner = subprocess.run([str(manager), '-Qqo', str(library)], check=True, capture_output=True, text=True)
    if owner.stdout.strip() != 'libctru':
        raise RuntimeError('The linked libctru archive must belong to the checked SDK package.')
    digest = hashlib.sha256(library.read_bytes()).hexdigest()
    print(f'{version}:{digest}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError, IndexError) as error:
        print(f'Toolchain error: {error}', file=sys.stderr)
        sys.exit(1)

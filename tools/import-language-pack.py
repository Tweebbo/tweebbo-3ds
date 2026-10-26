#!/usr/bin/env python3
"""Create bounded native lookup files; only the selected dictionary is resident."""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path
from language_contract import native_keys, validate

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('package', type=Path)
parser.add_argument('--web', type=Path, required=True)
args = parser.parse_args()
overrides = json.loads(subprocess.check_output(['node', str(args.web / 'tools/export-language-overrides.mjs')]))
out = ROOT / 'romfs/lang'
out.mkdir(parents=True, exist_ok=True)
glyphs = {}
report = {'package': json.loads((args.package / 'manifest.json').read_text()), 'files': {}}
keys = native_keys([ROOT, ROOT.parent/'Tweebbo-Switch'])
for lang in ['es', 'zh', 'ja', 'fr', 'pt-BR']:
    texts = json.loads((args.package / f'locales/{lang}.json').read_text())
    texts.update({source: values[lang] for source, values in overrides.items() if lang in values})
    missing = keys - texts.keys() - {'ABC', 'abc', 'tweebbo.lol'}
    if missing:
        raise ValueError(f'{lang}: native labels missing from dictionary: {sorted(missing)}')
    validate(texts, keys, lang)
    rows = sorted(texts.items(), key=lambda row: row[0].encode())
    offsets, strings = bytearray(), bytearray()
    base = 8 + len(rows) * 8
    for source, value in rows:
        for text in (source, value):
            assert '\0' not in text
            offsets.extend(struct.pack('<I', base + len(strings)))
            strings.extend(text.encode() + b'\0')
    data = b'TWL1' + struct.pack('<I', len(rows)) + offsets + strings
    assert len(data) <= 2 * 1024 * 1024
    (out / f'{lang}.bin').write_bytes(data)
    glyphs[lang] = ''.join(sorted(set(''.join(texts.values()) + 'English Español 中文 日本語 Français Português (Brasil)')))
    report['files'][lang] = {'bytes': len(data), 'entries': len(rows), 'sha256': hashlib.sha256(data).hexdigest()}
assets = ROOT / 'assets/fonts'
(assets / 'language-glyphs.json').write_text(json.dumps(glyphs, ensure_ascii=False, indent=2) + '\n')
(out / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report['files'], indent=2))

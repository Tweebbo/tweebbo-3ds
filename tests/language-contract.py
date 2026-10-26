#!/usr/bin/env python3
"""Reject unsafe formats and translated texture identifiers in native builds."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from language_contract import arguments, validate

assert arguments('%d:%02d %s %%') == ['d', 'd', 's']
assert arguments('%*.*s %zu %lld') == ['int', 'int', 's', 'zu', 'lld']
validate({'%s: %d': '%s : %04d'}, {'%s: %d'}, 'test')
for changed in ['%d: %s', '%s: %s', '%s: %lld', '%s: %d %n', '%2$s: %1$d', '%s: %*d']:
    try: validate({'%s: %d': changed}, {'%s: %d'}, 'test')
    except ValueError: pass
    else: raise AssertionError(changed)
for repo, name in [('Tweebbo-3DS','pool.c'),('Tweebbo-Switch','client_pool.c'),('Tweebbo-Vita','client_pool.c')]:
    source=(ROOT.parent/repo/'source'/name).read_text()
    for key in ['paalu hit %d %d', 'startcount %d %d']:
        assert f'TR("{key}")' not in source, (repo, key)
print('PASS: printf types/order/width, forbidden conversions, untranslated texture identifiers')

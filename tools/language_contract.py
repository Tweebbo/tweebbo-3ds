"""Validate native printf arguments before writing a translation dictionary."""
import json
import re

PRINTF = re.compile(r'%(?P<position>\d+\$)?[-+ #0]*(?P<width>\*|\d+)?(?:\.(?P<precision>\*|\d+))?(?P<length>hh|ll|[hljztL])?(?P<type>[diouxXfFeEgGaAcspn%])')
LITERAL = re.compile(r'TR\(\s*("(?:\\.|[^"\\])*")\s*\)')

def arguments(text):
    result = []
    for match in PRINTF.finditer(text):
        kind = match['type']
        if kind == '%':
            continue
        if match['position'] or kind == 'n':
            raise ValueError('Positional arguments and %n are not supported')
        for field in ('width', 'precision'):
            if match[field] == '*': result.append('int')
        kind = {'i':'d', 'o':'u', 'x':'u', 'X':'u', 'F':'f', 'e':'f', 'E':'f', 'g':'f', 'G':'f', 'a':'f', 'A':'f'}.get(kind, kind)
        result.append((match['length'] or '') + kind)
    return result

def native_keys(roots):
    return {json.loads(m[1]) for root in roots for path in (root/'source').glob('*.c') for m in LITERAL.finditer(path.read_text())}

def validate(texts, keys, language):
    for source in sorted(keys):
        expected = arguments(source)
        if source in texts and expected != arguments(texts[source]):
            raise ValueError(f'{language}: printf argument mismatch for {source!r}')

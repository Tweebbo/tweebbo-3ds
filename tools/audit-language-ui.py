#!/usr/bin/env python3
"""Find literal UI labels that bypass the native translation lookup."""
import argparse
import json
import re
from pathlib import Path

# Only presentation arguments are inspected. Protocol keys and player text are not labels.
LABELS = {
    'text_draw': [0], 'corner_tool': [5],
    'sk_text': [0], 'sk_paragraph': [0], 'sk_paragraph_page': [0],
    'sk_button': [3], 'sk_button_big': [3], 'sk_go': [4], 'sk_tab': [4],
    'sk_window': [4], 'sk_status_window': [4], 'sk_field': [4],
    'ui_confirm': [0, 1], 'ui_toast': [0], 'osk_open': [0], 'osk_open_secret': [0],
    'client_ui_button': [4], 'client_ui_label': [0], 'client_ui_paragraph': [0],
    'client_ui_window': [4], 'client_ui_field': [5], 'client_ui_capsule': [4],
    'client_ui_key_hint': [1], 'client_ui_prompt': [1], 'client_ui_edit': [0],
    'client_ui_edit_secret': [0], 'client_ui_edit_draft': [0],
}
TOKEN = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|[^\s]')
FORMAT = re.compile(r'%(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diouxXfFeEgGaAcspn%]')
NEUTRAL = {'A', 'B', 'X', 'Y', 'L', 'R', 'L/R', 'R/L', 'OK', 'VIP', 'TC', 'USD', 'EUR', 'I', 'II', 'III', 'IV', 'V', 'VI', 'VII', 'VIII', 'Tweebbo', 'YouTube', 'youtube'}

def scan(path):
    source = path.read_text()
    tokens = [m for m in TOKEN.finditer(source) if not m[0].startswith(('//', '/*'))]
    for i, token in enumerate(tokens[:-1]):
        if token[0] not in {*LABELS, 'snprintf'} or tokens[i+1][0] != '(':
            continue
        depth, start, arguments = 0, i+2, []
        for j in range(i+2, len(tokens)):
            value = tokens[j][0]
            if value in ('(', '[', '{'): depth += 1
            if value in (')', ']', '}'):
                if not depth:
                    arguments.append((start, j)); break
                depth -= 1
            if value == ',' and not depth:
                arguments.append((start, j)); start = j+1
        if token[0]=='snprintf':
            if len(arguments)==3:
                a,b=arguments[2]
                if any(t[0]=='TR' for t in tokens[a:b]):
                    yield {'file':str(path),'line':source.count('\n',0,token.start())+1,
                           'call':'snprintf','text':'Copy translated text with "%s" instead of treating it as a format',
                           'start':tokens[a].start(),'end':tokens[b-1].end()}
            continue
        for argument in LABELS[token[0]]:
            if argument >= len(arguments): continue
            a, b = arguments[argument]
            stack = []
            for k in range(a, b):
                value = tokens[k][0]
                if value == '(':
                    previous = tokens[k-1][0] if k > a else ''
                    stack.append(previous if re.fullmatch(r'[A-Za-z_]\w*', previous) else '')
                elif value == ')' and stack:
                    stack.pop()
                elif value.startswith('"') and not any(stack):
                    text = json.loads(value)
                    if text in NEUTRAL or not re.search('[A-Za-z]{2}', FORMAT.sub('', text)):
                        continue
                    yield {'file': str(path), 'line': source.count('\n', 0, tokens[k].start())+1,
                           'call': token[0], 'text': text, 'start': tokens[k].start(), 'end': tokens[k].end()}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('roots', nargs='+', type=Path)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    findings = [entry for root in args.roots for path in sorted((root/'source').glob('*.c')) for entry in scan(path)]
    if args.json: print(json.dumps(findings, ensure_ascii=False, indent=2))
    else:
        for item in findings: print(f"{item['file']}:{item['line']}: {item['call']}: {item['text']}")
        print(f'{len(findings)} untranslated presentation literals')
    raise SystemExit(bool(findings))

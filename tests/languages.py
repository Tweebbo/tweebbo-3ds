#!/usr/bin/env python3
"""Exercise the shipped C lookup and rollback paths with disposable dictionaries."""
import ctypes
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from language_contract import native_keys
required = native_keys([ROOT, ROOT.parent/'Tweebbo-Switch']) - {'ABC', 'abc', 'tweebbo.lol'}
required.update(['All Rooms','Public Rooms','My Rooms','Front page','Furniture','Arena',
                 'Teams (1 = solo)','Players per team','Minutes','Bot level','Fill after (min)',
                 'Quick throw','Short arc','Long arc','Quick (Y)','Short arc (Y)','Long arc (Y)',
                 'Red','Blue','Yellow','Green','Wave (X)'])
with tempfile.TemporaryDirectory(prefix='tweebbo-language-test-') as folder:
    temp = Path(folder)
    shutil.copytree(ROOT / 'romfs/lang', temp / 'romfs:/lang')
    (temp / 'stubs.c').write_text('''
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
static char saved[16];
int reject_font;
bool store_load(const char *key,char *out,int cap){(void)key;snprintf(out,cap,"%s",saved);return saved[0]!=0;}
void store_save(const char *key,const char *value){(void)key;snprintf(saved,sizeof saved,"%s",value);}
const char *saved_language(void){return saved;}
bool gfx_language_font(int lang){(void)lang;return !reject_font;}
''')
    subprocess.run(['cc', '-shared', '-fPIC', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(ROOT/'source'), str(ROOT/'source/language.c'), str(ROOT/'source/gen/font.c'),
                    str(temp/'stubs.c'), '-o', str(temp/'language.so')], check=True)
    lib = ctypes.CDLL(str(temp/'language.so'))
    lib.tw_translate.argtypes = [ctypes.c_char_p]
    lib.tw_translate.restype = ctypes.c_char_p
    lib.tw_language_error.restype = ctypes.c_char_p
    lib.saved_language.restype = ctypes.c_char_p
    lib.tw_reception_slug.restype = ctypes.c_char_p
    lib.tw_cjk_glyph.argtypes = [ctypes.c_uint]
    lib.tw_cjk_glyph.restype = ctypes.c_void_p
    os.chdir(temp)
    checked = 0
    for code, lang in enumerate(['en','es','zh','ja','fr','pt-BR']):
        lib.tw_language_request(code);lib.tw_language_update()
        assert lib.tw_language()==code and lib.tw_language_error()==b''
        if code:assert lib.saved_language()==lang.encode()
        assert lib.tw_translate(None)==b''
        assert lib.tw_translate(b'A player wrote this')==b'A player wrote this'
        if code:
            data=(temp/f'romfs:/lang/{lang}.bin').read_bytes()
            count=struct.unpack_from('<I',data,4)[0]
            present=set()
            for i in range(count):
                key,value=struct.unpack_from('<II',data,8+8*i)
                source=data[key:data.index(0,key)];expected=data[value:data.index(0,value)]
                present.add(source.decode())
                assert lib.tw_translate(source)==expected,(lang,source)
                checked+=1
            assert required <= present, (lang, sorted(required-present))
        if lang in ['fr','pt-BR']:assert lib.tw_reception_slug()==b'lobby'
    lib.tw_language_request(99);lib.tw_language_update();assert lib.tw_language()==5
    ctypes.c_int.in_dll(lib,'reject_font').value=1
    lib.tw_language_request(2);lib.tw_language_update();assert lib.tw_language()==5 and lib.tw_language_error()
    ctypes.c_int.in_dll(lib,'reject_font').value=0
    chinese=temp/'romfs:/lang/zh.bin';original=chinese.read_bytes()
    for damaged in [b'TWL1', b'TWL1'+struct.pack('<I',0xffffffff), original[:20],original[:8]+b'\xff'*8+original[16:]]:
        chinese.write_bytes(damaged)
        lib.tw_language_request(2);lib.tw_language_update();assert lib.tw_language()==5 and lib.tw_language_error()
    chinese.write_bytes(original)
    lib.tw_language_request(0);lib.tw_language_update();assert lib.tw_translate(b'Language')==b'Language'
    lib.store_save(b'language',b'fr');lib.tw_language_init();assert lib.tw_language()==4
    print(f'PASS: {checked} C lookups; unknown/null keys, language persistence, invalid packs and font-load rollback.')

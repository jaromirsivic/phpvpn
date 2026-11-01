"""Checks a finished English translation against its Czech source.

Usage: python -I check_translation.py <cz-new> <en-new>
Checks: same line count, no '@@CZ@@' marker left, headings on the same lines, inline code spans
and numbers per line preserved, no Czech diacritics outside links and code, blank lines aligned.
Exit code 1 when a hard check fails (line count, markers, headings, blank lines).
"""
import io
import re
import sys

cz_path, en_path = sys.argv[1:3]


def lines(path):
    return io.open(path, encoding='utf-8', newline='').read().replace('\r\n', '\n').split('\n')


cz, en = lines(cz_path), lines(en_path)
hard = 0
if len(cz) != len(en):
    print(f'LINE COUNT differs: cz {len(cz)}, en {len(en)}')
    hard += 1
for i, line in enumerate(en, 1):
    if '@@CZ@@' in line:
        print(f'MARKER left on line {i}')
        hard += 1
fence_cz = fence_en = False
dia = re.compile('[áčďéěíňóřšťúůýžÁČĎÉĚÍŇÓŘŠŤÚŮÝŽ]')
code = re.compile(r'`[^`]*`')
num = re.compile(r'\d+(?:[ ,]\d{3})*(?:[.,]\d+)?')
for i, (c, e) in enumerate(zip(cz, en), 1):
    if c.lstrip().startswith('```'):
        fence_cz = not fence_cz
    if e.lstrip().startswith('```'):
        fence_en = not fence_en
    if (c.strip() == '') != (e.strip() == ''):
        print(f'BLANK mismatch line {i}')
        hard += 1
    hc = re.match(r'^(#{1,6})\s', c)
    he = re.match(r'^(#{1,6})\s', e)
    if bool(hc) != bool(he) or (hc and he and hc.group(1) != he.group(1)):
        print(f'HEADING mismatch line {i}')
        hard += 1
    if fence_cz or fence_en:
        continue
    stripped = re.sub(r'\]\([^)]*\)', '', code.sub('', e))
    if dia.search(stripped):
        print(f'DIACRITIC line {i}: {stripped.strip()[:100]}')
    sc, se = sorted(set(code.findall(c))), sorted(set(code.findall(e)))
    if sc != se:
        print(f'CODE line {i}: {sorted(set(sc) ^ set(se))}')
    nc = sorted(n.replace(' ', '').replace(',', '') for n in num.findall(re.sub(r'\]\([^)]*\)', '', c)))
    ne = sorted(n.replace(' ', '').replace(',', '') for n in num.findall(re.sub(r'\]\([^)]*\)', '', e)))
    if nc != ne:
        print(f'NUM line {i}: cz-only {[x for x in nc if x not in ne]} en-only {[x for x in ne if x not in nc]}')
print('HARD FAILURES:', hard)
sys.exit(1 if hard else 0)

"""Builds a line-aligned translation draft of a changed Czech document.

Inputs: the old Czech document, its line-aligned English translation and the new Czech document.
Output: a draft with the same number of lines as the new Czech document. Lines that did not change
keep their English translation; changed or new Czech lines are copied verbatim with the prefix
'@@CZ@@ ' so a translator can replace them. A translator removes every prefix and keeps the line count.

Usage: python -I merge_translation.py <cz-old> <en-old> <cz-new> <draft-out>
"""
import difflib
import io
import sys

cz_old, en_old, cz_new, out = sys.argv[1:5]


def lines(path):
    return io.open(path, encoding='utf-8', newline='').read().replace('\r\n', '\n').split('\n')


a, e, b = lines(cz_old), lines(en_old), lines(cz_new)
assert len(a) == len(e), f'old translation is not line aligned: {len(a)} vs {len(e)}'
draft, marked = [], 0
for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
    if tag == 'equal':
        draft.extend(e[i1:i2])
    else:
        for line in b[j1:j2]:
            draft.append(('@@CZ@@ ' + line) if line.strip() else line)
            marked += bool(line.strip())
assert len(draft) == len(b)
io.open(out, 'w', encoding='utf-8', newline='\n').write('\n'.join(draft))
print(f'{out}: {len(b)} lines, {marked} lines to translate')

"""Checks relative Markdown links of the distributed documents against the distributed file set.

Usage: python -I links.py <root> <include-list>
"""
import os
import posixpath
import re
import sys
from urllib.parse import unquote

root, include_path = sys.argv[1:3]
included = set(open(include_path, encoding='utf-8').read().split('\n')) - {''}
folders = {posixpath.dirname(p) for p in included}
for p in list(folders):
    while p:
        p = posixpath.dirname(p)
        folders.add(p)
link = re.compile(r'\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
problems = 0
for rel in sorted(p for p in included if p.lower().endswith('.md')):
    text = open(os.path.join(root, rel), encoding='utf-8', errors='replace').read()
    for target in link.findall(text):
        if re.match(r'^[a-z]+:', target, re.I) or target.startswith('#'):
            continue
        path = unquote(target.split('#')[0])
        if not path:
            continue
        resolved = posixpath.normpath(posixpath.join(posixpath.dirname(rel), path)) if not path.startswith('/') else path.lstrip('/')
        if resolved not in included and resolved.rstrip('/') not in folders:
            problems += 1
            print(f'{rel}: {target} -> {resolved}')
print(f'{problems} broken link(s)')

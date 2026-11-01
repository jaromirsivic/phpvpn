"""Writes or checks source-distribution-manifest.json of a staging tree.

Usage: python -I manifest.py write <root> <version> <commit>
       python -I manifest.py check <root>
"""
import hashlib
import json
import os
import sys

NAME = 'source-distribution-manifest.json'


def files(root):
    result = []
    for folder, _, names in os.walk(root):
        for name in names:
            rel = os.path.relpath(os.path.join(folder, name), root).replace(os.sep, '/')
            if rel == NAME:
                continue
            data = open(os.path.join(root, rel), 'rb').read()
            result.append({'path': rel, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    return sorted(result, key=lambda item: item['path'])


if sys.argv[1] == 'write':
    root, version, commit = sys.argv[2:5]
    manifest = {
        'schemaVersion': 1,
        'project': 'phpvpn',
        'projectVersion': version,
        'sourceCommit': commit,
        'sourceUncommittedChanges': False,
        'note': 'Documents that were Czech in the source are English translations; see the distribution report.',
        'files': files(root),
    }
    path = os.path.join(root, NAME)
    with open(path, 'w', encoding='utf-8', newline='\n') as handle:
        json.dump(manifest, handle, indent=2, ensure_ascii=False)
        handle.write('\n')
    print(f'{len(manifest["files"])} files, manifest sha256 {hashlib.sha256(open(path, "rb").read()).hexdigest()}')
else:
    root = sys.argv[2]
    manifest = json.load(open(os.path.join(root, NAME), encoding='utf-8'))
    actual = files(root)
    print('MATCH' if actual == manifest['files'] else 'MISMATCH', len(actual), 'files')
    sys.exit(0 if actual == manifest['files'] else 1)

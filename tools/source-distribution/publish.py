"""Copies a verified staging into the target clone file by file.

Never touches .git, never mirrors or empties the target. Files only in the target are removed
only when listed in the obsolete list. Usage:
  python -I publish.py <staging> <target> <obsolete-list> [--apply]
"""
import filecmp
import os
import shutil
import sys

staging, target, obsolete_path = sys.argv[1:4]
apply = '--apply' in sys.argv
obsolete = [p for p in open(obsolete_path, encoding='utf-8').read().split('\n') if p]


def walk(root):
    for folder, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if not (folder == root and d == '.git')]
        for name in files:
            yield os.path.relpath(os.path.join(folder, name), root).replace(os.sep, '/')


staged = sorted(walk(staging))
existing = set(walk(target))
added = changed = same = 0
for rel in staged:
    source, destination = os.path.join(staging, rel), os.path.join(target, rel)
    if rel not in existing:
        added += 1
        action = 'add'
    elif filecmp.cmp(source, destination, shallow=False):
        same += 1
        continue
    else:
        changed += 1
        action = 'update'
    if apply:
        os.makedirs(os.path.dirname(destination), exist_ok=True)
        shutil.copyfile(source, destination)
        shutil.copymode(source, destination)
    print(action, rel)
unexpected = sorted(existing - set(staged) - set(obsolete))
for rel in obsolete:
    if rel in existing:
        print('remove', rel)
        if apply:
            os.remove(os.path.join(target, rel))
if apply:
    for folder, dirs, files in sorted(os.walk(target), key=lambda item: -len(item[0])):
        if '.git' not in folder.split(os.sep) and folder != target and not os.listdir(folder):
            os.rmdir(folder)
print(f'added {added}, updated {changed}, unchanged {same}, removed {len([p for p in obsolete if p in existing])}')
if unexpected:
    print('NOT IN INVENTORY (kept, needs a decision):', *unexpected, sep='\n  ')

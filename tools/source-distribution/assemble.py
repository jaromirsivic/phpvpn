"""Assembles the source distribution staging from the snapshot, the inventory and the translations.

- Copies the included files byte for byte from the snapshot.
- Replaces the Czech documents with their line-aligned English translations (translations/).
- Remaps anchors into translated documents (same heading line in source and translation).
- Turns links to files that are not distributed into plain text.
- Replaces phpvpn.config.json with a portable template: every machine specific value is emptied,
  the keys and the relative test server paths stay (see README.md of the distribution).
- Adds the extras: the source distribution README.md, LICENSE and client/tools/get-wintun.ps1.

Usage: python -I assemble.py <snapshot> <include-list> <staging> <changes-log>
            [--translations DIR] [--extras DIR]
The translations and extras default to the folders next to this script.
"""
import json
import os
import posixpath
import re
import shutil
import sys
import unicodedata
from urllib.parse import unquote

HERE = os.path.dirname(os.path.abspath(__file__))
args = sys.argv[1:]
snapshot, include_path, staging, changes_path = args[:4]
translations = args[args.index('--translations') + 1] if '--translations' in args else os.path.join(HERE, 'translations')
extras = args[args.index('--extras') + 1] if '--extras' in args else os.path.join(HERE, 'extras')

included = [p for p in open(include_path, encoding='utf-8').read().split('\n') if p]
TRANSLATED = ['client.md', 'tray_UX.md', 'distribution.md', 'create_source_code_distribution.md', 'protocol.md',
              'client/tests/e2e/manual-gates.md', 'client/tests/manual/README.md']
EXTRAS = ['README.md', 'LICENSE', 'client/tools/get-wintun.ps1']
CONFIG = 'phpvpn.config.json'
# Keys of phpvpn.config.json whose values belong to one computer. Every other key keeps its value.
MACHINE_KEYS = ['signing.certificateThumbprint', 'tools.*', 'linuxHost.sshHost', 'linuxHost.sshIdentityFile',
                'linuxHost.sshKnownHostsFile', 'linuxHost.serverPython', 'android.deviceSerial', 'android.relayUrl',
                'android.relayHost', 'android.relayServerFile', 'hyperV.vmName', 'hyperV.destinationDirectory']
changes = []


def slug(heading: str) -> str:
    text = heading.strip().lower()
    text = re.sub(r'[^\w\- ]', '', text, flags=re.UNICODE)
    return text.replace(' ', '-')


def heading_slugs(text: str):
    """line number -> GitHub anchor, with -1, -2 suffixes for repeated headings."""
    seen, result, fence = {}, {}, False
    for number, line in enumerate(text.split('\n'), start=1):
        if line.lstrip().startswith('```'):
            fence = not fence
            continue
        match = None if fence else re.match(r'^#{1,6}\s+(.*?)\s*#*\s*$', line)
        if match:
            base = slug(re.sub(r'`|\*\*|\[|\]\([^)]*\)', '', match.group(1)))
            count = seen.get(base, 0)
            seen[base] = count + 1
            result[number] = base if count == 0 else f'{base}-{count}'
    return result


def read(path):
    return open(path, encoding='utf-8').read().replace('\r\n', '\n')


# Anchor maps: Czech anchor of a document -> English anchor of its translation.
czech_source = {rel: read(os.path.join(snapshot, rel)) for rel in TRANSLATED}
english = {rel: read(os.path.join(translations, rel)) for rel in TRANSLATED}
anchor_map = {}
for rel in TRANSLATED:
    source_slugs = heading_slugs(czech_source[rel])
    target_slugs = heading_slugs(english[rel])
    if source_slugs.keys() != target_slugs.keys():
        raise SystemExit(f'{rel}: the translation is not line aligned with the snapshot (heading lines differ)')
    anchor_map[rel] = {unicodedata.normalize('NFC', source_slugs[n]): target_slugs[n] for n in source_slugs}
    # English anchors already in place stay valid.
    anchor_map[rel].update({value: value for value in target_slugs.values()})

included_set = set(included)
folders = set()
for p in included_set | set(EXTRAS):
    while p:
        p = posixpath.dirname(p)
        folders.add(p)
LINK = re.compile(r'(!?)\[([^\]]*)\]\(([^)\s]+)((?:\s+"[^"]*")?)\)')


def fix_links(rel: str, text: str) -> str:
    def replace(match):
        bang, label, target, title = match.groups()
        if re.match(r'^[a-z]+:', target, re.I):
            return match.group(0)
        path, _, anchor = target.partition('#')
        resolved = posixpath.normpath(posixpath.join(posixpath.dirname(rel), unquote(path))) if path else rel
        if path and resolved not in included_set and resolved not in EXTRAS and resolved not in folders:
            if rel.startswith('client/third_party/'):
                return match.group(0)
            changes.append(f'{rel}: link to undistributed {resolved} turned into plain text')
            return label
        if anchor and resolved in anchor_map:
            key = unicodedata.normalize('NFC', unquote(anchor))
            mapped = anchor_map[resolved].get(key)
            if mapped is None:
                changes.append(f'{rel}: UNRESOLVED anchor #{anchor} in {resolved}')
            elif mapped != anchor:
                changes.append(f'{rel}: anchor #{anchor} -> #{mapped}')
                return f'{bang}[{label}]({path}#{mapped}{title})'
        return match.group(0)
    return LINK.sub(replace, text)


def portable_config(text: str) -> str:
    """The development configuration with every machine specific value emptied."""
    document = json.loads(text)

    def clear(node, parts):
        head, rest = parts[0], parts[1:]
        if head == '*':
            for key in node:
                clear(node, [key] + rest)
            return
        if not isinstance(node, dict) or head not in node:
            return
        if rest:
            clear(node[head], rest)
        elif isinstance(node[head], dict):
            node[head] = {platform: '' for platform in node[head]}
        else:
            node[head] = ''

    for key in MACHINE_KEYS:
        clear(document, key.split('.'))
    return json.dumps(document, indent=2) + '\n'


if os.path.exists(staging):
    raise SystemExit(f'staging already exists: {staging}')
for rel in included + EXTRAS:
    target = os.path.join(staging, rel)
    os.makedirs(os.path.dirname(target) or staging, exist_ok=True)
    if rel in TRANSLATED:
        text = fix_links(rel, english[rel])
        changes.append(f'{rel}: replaced by the English translation')
        with open(target, 'w', encoding='utf-8', newline='\n') as handle:
            handle.write(text)
    elif rel in EXTRAS:
        shutil.copyfile(os.path.join(extras, rel), target)
        changes.append(f'{rel}: added for the source distribution')
    elif rel == CONFIG:
        with open(target, 'w', encoding='utf-8', newline='\n') as handle:
            handle.write(portable_config(read(os.path.join(snapshot, rel))))
        changes.append(f'{rel}: machine specific values emptied ({", ".join(MACHINE_KEYS)})')
    else:
        source = os.path.join(snapshot, rel)
        if rel.lower().endswith('.md'):
            raw = open(source, 'rb').read()
            text = raw.decode('utf-8')
            fixed = fix_links(rel, text)
            if fixed != text:
                with open(target, 'w', encoding='utf-8', newline='') as handle:
                    handle.write(fixed)
                continue
        shutil.copyfile(source, target)
        mode = os.stat(source).st_mode
        os.chmod(target, mode)
with open(changes_path, 'w', encoding='utf-8', newline='\n') as handle:
    handle.write('\n'.join(changes) + '\n')
print(f'staged {len(included) + len(EXTRAS)} files, {len(changes)} recorded changes')

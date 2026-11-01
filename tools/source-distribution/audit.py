"""Read-only audit of a source tree for the source distribution rules.

Reports forbidden names (built from code points, never printed), certificates and private keys,
native binaries and backup files. Usage: python -I audit.py <root> [--only <list file>]
"""
import io
import os
import re
import sys
import unicodedata
import zipfile
import zlib

NAMES = [''.join(map(chr, points)) for points in (
    (0x004A, 0x0061, 0x0072, 0x006F, 0x006D, 0x00ED, 0x0072),
    (0x0160, 0x0069, 0x0076, 0x0069, 0x0063),
)]


def fold(text: str) -> str:
    return unicodedata.normalize('NFC', text).casefold()


def strip_marks(text: str) -> str:
    return ''.join(c for c in unicodedata.normalize('NFD', text) if not unicodedata.combining(c))


NEEDLES = {}
for index, name in enumerate(NAMES):
    NEEDLES[f'name{index + 1}'] = fold(name)
    NEEDLES[f'name{index + 1}-ascii'] = fold(strip_marks(name))
# escaped forms of the accented letters, e.g. \u00ed or &#237;; the name fragments are built from the
# code points so that this script does not carry the names itself.
ESCAPES = [r'\\u00ed\w', r'\\u0160', r'&#237;', r'&#352;', '&iacute;' + NAMES[0][-1], '&Scaron;' + NAMES[1][1:],
           r'%C3%AD', r'%C5%A0']


def texts(data: bytes):
    for encoding in ('utf-8', 'utf-16-le', 'utf-16-be', 'cp1250'):
        try:
            yield data.decode(encoding)
        except UnicodeDecodeError:
            continue


def name_hits(data: bytes):
    hits = set()
    for text in texts(data):
        folded = fold(text)
        plain = fold(strip_marks(text))
        for key, needle in NEEDLES.items():
            if needle in folded or needle in plain:
                hits.add(key.split('-')[0])
        for pattern in ESCAPES:
            if re.search(pattern, text, re.I):
                window = re.search(pattern + r'.{0,12}', text, re.I)
                if window and any(n[:3] in fold(strip_marks(window.group(0))) for n in ('mir', 'ivi')):
                    hits.add('escaped')
    return hits


def png_text_chunks(data: bytes) -> bytes:
    out = []
    pos = 8
    while pos + 8 <= len(data):
        length = int.from_bytes(data[pos:pos + 4], 'big')
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind in (b'tEXt', b'iTXt'):
            out.append(body)
        elif kind == b'zTXt':
            key, _, rest = body.partition(b'\0')
            try:
                out.append(key + b' ' + zlib.decompress(rest[1:]))
            except zlib.error:
                out.append(body)
        pos += 12 + length
    return b'\n'.join(out)


def classify(path: str, data: bytes):
    findings = []
    head = data[:4]
    if data[:2] == b'MZ':
        findings.append('native-binary:PE')
    if head == b'\x7fELF':
        findings.append('native-binary:ELF')
    if head in (b'\xfe\xed\xfa\xce', b'\xfe\xed\xfa\xcf', b'\xce\xfa\xed\xfe', b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe'):
        findings.append('native-binary:Mach-O')
    if re.search(rb'-----BEGIN [A-Z ]*(CERTIFI' + rb'CATE|PRIVATE' + rb' KEY|PKCS7)', data):
        findings.append('pem-certificate-or-key')
    if data[:2] == b'\x30\x82' and path.lower().endswith(('.cer', '.crt', '.der', '.p7b', '.pfx', '.p12')):
        findings.append('der-certificate')
    if re.search(rb'-----BEGIN OPENSSH PRIV' + rb'ATE KEY|-----BEGIN RSA PRIV' + rb'ATE KEY|PuTTY-User-' + rb'Key-File', data):
        findings.append('private-key')
    if path.endswith('~') or re.search(r'\.(bak|orig|tmp)$', path, re.I):
        findings.append('backup-file')
    return findings


def scan(root: str, only=None):
    results = []
    for folder, dirs, files in os.walk(root):
        for file in files:
            full = os.path.join(folder, file)
            rel = os.path.relpath(full, root).replace(os.sep, '/')
            if only is not None and rel not in only:
                continue
            data = open(full, 'rb').read()
            found = classify(rel, data)
            hits = name_hits(rel.encode('utf-8')) | name_hits(data)
            if data[:8] == b'\x89PNG\r\n\x1a\n':
                hits |= name_hits(png_text_chunks(data))
            if data[:4] == b'PK\x03\x04':
                try:
                    with zipfile.ZipFile(io.BytesIO(data)) as archive:
                        for info in archive.infolist():
                            hits |= name_hits(info.filename.encode('utf-8'))
                            if info.file_size < 64 * 1024 * 1024 and not info.filename.endswith('/'):
                                inner = archive.read(info)
                                hits |= name_hits(inner)
                                found += [f'{f} (in {info.filename})' for f in classify(info.filename, inner)]
                except zipfile.BadZipFile:
                    found.append('unreadable-zip')
            if hits:
                found.append('forbidden-name:' + ','.join(sorted(hits)))
            if found:
                results.append((rel, found))
    return results


if __name__ == '__main__':
    root = sys.argv[1]
    only = None
    if '--only' in sys.argv:
        only = set(open(sys.argv[sys.argv.index('--only') + 1], encoding='utf-8').read().split('\n')) - {''}
    for rel, found in sorted(scan(root, only)):
        print(rel, '|', '; '.join(found))

"""Build a deterministic WordPress.org submission ZIP from the maintained plugin.

Run from any directory: uv run --no-project python WordPress/build-release.py
The source remains in server/wordpress/version7/vpn-server. No credentials,
test fixtures, registration notes, or marketing assets enter this ZIP.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = Path(__file__).resolve().parent
PLUGIN = ROOT / 'server/wordpress/version7/vpn-server'
FILES = (
    'LICENSE.txt', 'readme.txt', 'uninstall.php', 'vpn-server.php',
    'assets/admin.css', 'assets/admin.js', 'assets/status-page.js', 'assets/tutorial.png',
    'includes/class-admin.php', 'includes/class-fast-route.php',
    'includes/class-server.php', 'includes/class-settings.php',
    'includes/class-status-page.php', 'includes/server-core.php',
    'languages/vpn-server.pot',
)


def main():
    subprocess.run([sys.executable, str(ROOT / 'tools/build-wordpress-plugin.py'), '--check'], check=True)
    actual = {p.relative_to(PLUGIN).as_posix() for p in PLUGIN.rglob('*') if p.is_file()}
    if actual != set(FILES):
        raise SystemExit(f'Review the release allowlist. Extra: {actual - set(FILES)}. Missing: {set(FILES) - actual}.')
    header = (PLUGIN / 'vpn-server.php').read_text(encoding='utf-8')
    version = re.search(r'^ \* Version:\s+(\d+\.\d+\.\d+)$', header, re.M).group(1)
    readme = (PLUGIN / 'readme.txt').read_text(encoding='utf-8')
    assert f'Stable tag: {version}' in readme
    assert f'= {version} =' in readme, 'Current release needs a changelog entry.'
    assert 'Contributors: pilandrome' in readme
    archive_path = OUTPUT / f'vpn-server-{version}.zip'
    manifest = []
    with zipfile.ZipFile(archive_path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted(FILES):
            path = PLUGIN / name
            if path.is_symlink():
                raise SystemExit(f'Symlink is not a release file: {name}')
            data = path.read_bytes()
            if path.suffix in ('.php', '.txt', '.js', '.css', '.pot'):
                data.decode('utf-8')
                assert not data.startswith(b'\xef\xbb\xbf'), f'Unexpected BOM: {name}'
            entry = zipfile.ZipInfo('vpn-server/' + name, date_time=(2026, 1, 1, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = 0o100644 << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
            manifest.append({'path': entry.filename, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    with zipfile.ZipFile(archive_path) as archive:
        assert archive.testzip() is None
        assert len(archive.namelist()) == len(FILES)
    digest = hashlib.sha256(archive_path.read_bytes()).hexdigest()
    (OUTPUT / 'SHA256SUMS.txt').write_text(f'{digest}  {archive_path.name}\n', encoding='utf-8')
    (OUTPUT / 'manifest.json').write_text(json.dumps({
        'plugin': 'vpn-server', 'version': version, 'archive': archive_path.name,
        'sha256': digest, 'files': manifest,
    }, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f'{archive_path}\n{len(FILES)} files, {archive_path.stat().st_size} bytes\nSHA256 {digest}')


if __name__ == '__main__':
    main()

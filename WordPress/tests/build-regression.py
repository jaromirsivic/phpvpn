"""Regenerate the core and exercise both existing packaging paths.

Verifies that WordPress's source template survives regeneration without an author
footer, while the standalone PHP template keeps it. Runs on Windows because the
main distribution packager is PowerShell. Test ZIPs stay below ignored work/.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'work'
TEMPLATE = ROOT / 'server/wordpress/version7/vpn-server/includes/class-status-page.php'
STANDALONE = ROOT / 'server/php/phpvpn.php'


def main():
    template = TEMPLATE.read_bytes()
    standalone = STANDALONE.read_bytes()
    assert b'Developed by' not in template and b'id="vpn-server-footer"' not in template
    assert b'<h1>VPN server is running</h1>' in template
    assert b'Developed by <a href=https://www.pilandrome.com>' in standalone
    WORK.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='wp-build-regression-', dir=WORK) as directory:
        directory = Path(directory).resolve()
        assert directory.is_relative_to(WORK.resolve())
        subprocess.run([sys.executable, str(ROOT / 'tools/build-wordpress-plugin.py'), '--zip', str(directory)], check=True)
        assert TEMPLATE.read_bytes() == template, 'Core regeneration overwrote the maintained WP template.'
        assert STANDALONE.read_bytes() == standalone, 'Standalone source changed.'
        print('PASS: Actual core regeneration preserves both independent status templates.')
        # This is exactly the function called by tools/make-distribution.ps1.
        command = ". ./tools/distribution-common.ps1\nWrite-WordPressPluginZip $PWD.Path $args[0]\nAssert-WordPressPluginZip $PWD.Path $args[0]\n"
        script = directory / 'pack.ps1'
        script.write_text(command, encoding='utf-8')
        subprocess.run(['powershell', '-NoProfile', '-File', str(script), str(directory / 'distribution.zip')], cwd=ROOT, check=True)
        for archive_path in directory.glob('*.zip'):
            with zipfile.ZipFile(archive_path) as archive:
                assert archive.read('vpn-server/includes/class-status-page.php') == template
                assert b'Developed by' not in archive.read('vpn-server/includes/server-core.php')
            print('PASS: ' + archive_path.name + ' contains the maintained footer-free WordPress template.')
    subprocess.run([sys.executable, str(ROOT / 'WordPress/build-release.py')], check=True)
    manifest = json.loads((ROOT / 'WordPress/manifest.json').read_text(encoding='utf-8'))
    submission = ROOT / 'WordPress' / manifest['archive']
    first = hashlib.sha256(submission.read_bytes()).hexdigest()
    subprocess.run([sys.executable, str(ROOT / 'WordPress/build-release.py')], check=True)
    second = hashlib.sha256(submission.read_bytes()).hexdigest()
    assert first == second, 'Submission ZIP is not reproducible.'
    with zipfile.ZipFile(submission) as archive:
        assert archive.read('vpn-server/includes/class-status-page.php') == template
    print('PASS: Submission ZIP is byte-reproducible and contains the same footer-free template.')


if __name__ == '__main__':
    main()

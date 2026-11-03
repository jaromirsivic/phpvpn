"""Real HTTP checks against a disposable local WordPress fixture.

uv run --no-project --with requests python WordPress/tests/http-smoke.py --help
No credentials are printed. This script exports only the fixture's own configuration.
"""
import argparse
from html.parser import HTMLParser
from pathlib import Path
import json
import requests


class Forms(HTMLParser):
    def __init__(self):
        super().__init__()
        self.forms = []
        self.current = None

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == 'form':
            self.current = {}
        elif tag == 'input' and self.current is not None and attrs.get('name'):
            self.current[attrs['name']] = attrs.get('value', '')

    def handle_endtag(self, tag):
        if tag == 'form' and self.current is not None:
            self.forms.append(self.current)
            self.current = None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--password-file', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    args = parser.parse_args()
    assert args.url.startswith('http://127.0.0.1:'), 'Only a loopback disposable site is accepted.'
    base = args.url.rstrip('/')
    fixture = json.loads(args.fixture.read_text(encoding='utf-8'))
    password = args.password_file.read_text(encoding='utf-8').strip()
    passed = 0

    def check(condition, description):
        nonlocal passed
        if not condition:
            raise AssertionError(description)
        passed += 1
        print('PASS: ' + description)

    public = requests.get(fixture['baseUrl'], timeout=30)
    check(public.status_code == 200 and '<h1>VPN server is running</h1>' in public.text, 'Exact requested public heading and HTTP 200.')
    check('X-Powered-By' not in public.headers, 'Protocol endpoint does not expose the PHP runtime header.')
    check('Developed by' not in public.text and 'id="vpn-server-footer"' not in public.text, 'No author-credit footer.')
    check('id="vpn-server-downloads"' in public.text and '>Download</a>' in public.text, 'Functional client download selector is visible by default.')
    check(fixture['serverSecret'] not in public.text and all(k not in public.text for k in fixture['accessKeys'].values()), 'Public page does not reveal fixture credentials.')
    for relative in ('assets/admin.js', 'assets/status-page.js', 'assets/tutorial.png'):
        result = requests.get(base + '/wp-content/plugins/vpn-server/' + relative, timeout=20)
        check(result.status_code == 200, 'Local asset served: ' + relative)
    for relative in ('vpn-server.php', 'includes/server-core.php', 'includes/class-admin.php', 'uninstall.php'):
        result = requests.get(base + '/wp-content/plugins/vpn-server/' + relative, timeout=20)
        check(result.content == b'', 'Direct PHP access is inert: ' + relative)

    def login(username):
        session = requests.Session()
        session.get(base + '/wp-login.php', timeout=30)
        response = session.post(base + '/wp-login.php', data={
            'log': username, 'pwd': password, 'wp-submit': 'Log In', 'testcookie': '1',
            'redirect_to': base + '/wp-admin/',
        }, timeout=45)
        check(any('wordpress_logged_in' in c.name for c in session.cookies), 'Local login succeeds for ' + username)
        return session

    with login('review-admin') as admin:
        page = admin.get(base + '/wp-admin/options-general.php?page=vpn-server', timeout=30)
        check(page.status_code == 200 and 'Client App Download Links' in page.text, 'Administrator can view configuration.')
        parsed = Forms()
        parsed.feed(page.text)
        export = next(form for form in parsed.forms if form.get('action') == 'vpn_server_client_config')
        action_url = base + '/wp-admin/admin-post.php'
        invalid = admin.post(action_url, data={**export, '_wpnonce': 'invalid'}, timeout=30)
        check(invalid.status_code == 403 and fixture['serverSecret'] not in invalid.text, 'Invalid nonce blocks credential export.')
        missing = admin.post(action_url, data={k: v for k, v in export.items() if k != '_wpnonce'}, timeout=30)
        check(missing.status_code == 403, 'Missing nonce blocks credential export.')
        valid = admin.post(action_url, data=export, timeout=30)
        config = valid.json()['masterSetup']
        check(valid.status_code == 200 and config['serverSecret'] == fixture['serverSecret'], 'Authorized export includes the correct server configuration.')
        check(config['accessKey'] == fixture['accessKeys'][config['virtualIp']], 'Authorized export selects the correct client key.')
        check('attachment;' in valid.headers.get('Content-Disposition', '') and 'no-store' in valid.headers.get('Cache-Control', ''), 'Credential export is an attachment and is not cacheable.')

    with login('review-subscriber') as subscriber:
        page = subscriber.get(base + '/wp-admin/options-general.php?page=vpn-server', timeout=30)
        check(page.status_code == 403, 'Subscriber cannot view VPN settings.')
        result = subscriber.post(base + '/wp-admin/admin-post.php', data=export, timeout=30)
        check(result.status_code == 403 and fixture['serverSecret'] not in result.text, 'Subscriber cannot export credentials even with an administrator form.')
    unauth = requests.post(base + '/wp-admin/admin-post.php', data=export, timeout=30, allow_redirects=False)
    check(unauth.status_code in (302, 400, 403) and fixture['serverSecret'] not in unauth.text, 'Anonymous request cannot export credentials.')
    print(f'Passed {passed} HTTP assertions.')


if __name__ == '__main__':
    main()

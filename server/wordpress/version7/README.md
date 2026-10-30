# VPN Server – WordPress plugin

`vpn-server/` is the plugin exactly as it is distributed: the content of the ZIP and of the
WordPress.org SVN `trunk`. Everything else in this folder is for development only.

Plan and decisions: [Plans/2026-10-28-02-30-52/plan-wordpress-v001.md](../../../Plans/2026-10-28-02-30-52/plan-wordpress-v001.md).

## Server core

`vpn-server/includes/server-core.php` is generated from `server/php/phpvpn.php`, never edited by hand:

```
python tools/build-wordpress-plugin.py            regenerate
python tools/build-wordpress-plugin.py --check    stale core or version mismatch fails
python tools/build-wordpress-plugin.py --zip DIR  installable vpn-server-X.Y.Z.zip
```

The build copies everything after `namespace PhpVpn;` except the regions between
`// @wp-skip-begin` and `// @wp-skip-end` (configuration constants, `Runtime`,
`StatusPageHandler`, `Request::fromGlobals` and the autorun). The plugin passes the secret,
access keys and storage folder to `Config::fromValues()` and calls
`Application::handleRoot()` and `Application::handleAction()`.

The distribution (`tools/make-distribution.ps1`) checks the core and ships the plugin as
`server/wordpress/vpn-server-X.Y.Z.zip` next to the user guide `server/wordpress/README.md`.

`tools/bump-version.ps1` raises the plugin version together with the servers. Add a
`== Changelog ==` entry to `readme.txt` by hand.

## Request flow

1. `plugins_loaded` with priority `PHP_INT_MIN` (`Server::maybe_serve`) compares the request path
   with `home_url( <endpoint> )`. Other requests pay one autoloaded `get_option()` and a string
   comparison.
2. On a match the core answers and the request ends with `exit`: no theme, `init` or main query.
3. The optional fast route (`wp-content/mu-plugins/vpn-server-fast-route.php`, installed from
   the settings) calls the same method before the regular plugins load.

The client appends `?action=...` to the Server URL and rejects a URL with a query, so the
endpoint is a path and the site needs pretty permalinks.

## Checks

On a local test WordPress (here `D:\nginx_wordpress`, nginx on port 5080) with the plugin
folder linked into `wp-content/plugins` and the Plugin Check plugin installed:

```
D:\nginx_wordpress\tools\wp.bat plugin check vpn-server --include-experimental --require=D:/nginx_wordpress/html/wp-content/plugins/plugin-check/cli.php
D:\nginx_wordpress\tools\wp.bat i18n make-pot <plugin> <plugin>/languages/vpn-server.pot --slug=vpn-server --domain=vpn-server --exclude=includes/server-core.php
uv run --no-project python -m pytest tests-integration -q --server-config work/wp-plugin-test/server.json --large-body-url http://127.0.0.1:5080/vpn-server/
```

`work/wp-plugin-test/server.json` is a deployment description with `baseUrl`
`http://127.0.0.1:5080/vpn-server/` and the secret, keys and storage folder saved in the plugin
settings.

## WordPress.org submission

Not part of the ZIP: banner, icon and screenshots go to the SVN `assets/` folder.
`Contributors:` in `readme.txt` must be the WordPress.org user name of the submitter.

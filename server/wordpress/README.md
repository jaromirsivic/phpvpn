# PHP VPN server for WordPress (VPN Server plugin)

The VPN Server plugin turns a WordPress site into a PHP VPN server. It connects your own
computers and servers into one private IPv4 network (`10.99.99.0/24`) over ordinary HTTP or
HTTPS, so it works wherever a web page can be opened, even behind NAT and firewalls that allow
nothing else.

The plugin uses the same server core as `phpvpn.php` and speaks the same protocol, so the PHP VPN
clients for Windows and Linux connect to it without any change. It does not route your devices
to the internet and does not hide your public IP address. It only forwards traffic between your
own PHP VPN clients.

## What is in this folder

| File | Purpose |
|---|---|
| `vpn-server-X.Y.Z.zip` | The installable plugin. Every entry is below the plugin folder `vpn-server/`. |
| `README.md` | This guide. |

## Requirements

| Item | Value |
|---|---|
| WordPress | 6.6 or newer |
| PHP | 8.2 or newer, 64-bit build, standard WordPress requirements, no additional cryptography extension |
| Permalinks | pretty permalinks (Settings, Permalinks, anything except Plain) |
| Storage | a folder writable by PHP, by default a folder with a random name inside `wp-content/uploads` |

Many hosting providers forbid proxy or VPN services in their terms. Check the terms of your
provider before you run the plugin on shared hosting.

## Installation

1. In the WordPress administration open Plugins, Add New Plugin, Upload Plugin, choose
   `vpn-server-X.Y.Z.zip` and install it. Alternatively extract the ZIP into
   `wp-content/plugins`, so that the plugin ends up in `wp-content/plugins/vpn-server`.
2. Activate the plugin.
3. Make sure pretty permalinks are on. The Server URL does not work with plain permalinks.

## Configuration

1. Open Settings, VPN Server. The plugin starts with a random server secret and, like
   `phpvpn.php`, with the clients `10.99.99.2` to `10.99.99.15` and their default access keys.
   The default access keys are the same on every installation and work with the default values
   of the client Configuration Wizard.
2. Replace the default access key of every client you use with New Key, remove the other
   clients and add more if you need them.
3. Download the `config.json` of each client and import it in the PHP VPN client, or copy the
   Server URL, the server secret, the VPN address and the access key into its Configuration
   Wizard.
4. Set up the communication contracts and payload keys between the clients in the clients
   themselves. Payload keys never reach the server.
5. On nginx add the rule shown under Main Configuration in the settings, then press Run self
   test.

The Server URL is a plain path such as `https://example.com/vpn-server/`, without a `.php` file
name. The client appends `?action=...` itself. Opened in a browser, the Server URL shows a status
page with a select box of client downloads, setup instructions and the request headers that the Configuration
Wizard can reuse. A key generator is shown only when the server secret is predictable. An
administrator must save the new secret in Settings, VPN Server. The page can be turned off in the settings.

The separate **Client App Download Links** section below the complete Clients section lists the entries of
the select box on the status page, the `CLIENT_DOWNLOADS` of the standalone servers. Every row has a
name, an absolute HTTP or HTTPS URL, the type File (Download button) or Link (Open button, new tab)
and the operating system for which the status page preselects the entry. **Add Download** adds a
row, the arrows move a row and **Remove** deletes it, at most 20 rows. Without JavaScript one spare
row is shown and a row with an empty name and URL is not saved. Click **Save Changes** to update the
public status page. The list is prefilled with `phpvpn-win.zip`, `phpvpn-macos.zip` and
`phpvpn-linux.zip` from `https://www.pilandrome.com/downloads/phpvpn/client/1.x/` and the source code
link `https://github.com/pilandrome/phpvpn`. Custom Windows, macOS and Linux URLs saved by an older
version are kept, a saved empty list hides the select box.

## Storage protection

Client messages are encrypted end to end between the clients and wait in the storage folder for
delivery for at most 60 seconds. Expired files are removed by request-driven cleanup and may remain
on disk while the server is idle. The folder has generated `.htaccess` and `web.config` deny files,
which require a web server configuration that honors them.
nginx ignores them and needs the rule shown in the settings. The settings can also move the
folder outside the web root.

Storage type and Storage folder are set under **Main Configuration**. The only storage type so
far is the disk folder (**Disk, RAM disk**): a folder on a RAM disk works the same way. Redis,
Memcached and LiteSpeed Cache are listed but cannot be selected yet.

## Poll intervals

The clients ask the Server URL for new messages in an interval between the minimum and the maximum
that the server publishes in its `info` response. **Min Data Poll Interval** (25 to 500 ms) is
used while packets flow, **Max Data Poll Interval** (500 to 5000 ms) is the longest wait of an idle
client. Both are set under **Main Configuration** and saved with their own **Save Changes** button.
The defaults are `MIN_DATA_POLL_INTERVAL_MS` and `MAX_DATA_POLL_INTERVAL_MS` of `phpvpn.php`;
`tools/build-wordpress-plugin.py` copies them into `includes/class-settings.php`, and its `--check`
fails when they differ or fall outside these ranges.

## Fast route

Client requests are answered right after the plugins load, before the theme. The optional fast
route answers them even before other plugins load: it writes
`wp-content/mu-plugins/vpn-server-fast-route.php`. It is installed and removed in the settings,
and deactivating the plugin removes it. Use it together with a PHP opcode cache on sites with
many plugins.

## Uninstall and privacy

Deactivating the plugin removes the fast route, except while another site in a multisite network
still uses it. Deleting the plugin removes settings and attempts to delete its managed storage
folder. Administrator-selected folders are preserved, and filesystem permissions can prevent
removal. Verify cleanup and handle backups separately.

The plugin stores its settings (server secret, client VPN addresses and access keys) in the
WordPress options table. Messages expire for delivery after 60 seconds, with physical cleanup
performed by later requests. Per-client `info.json` files retain last-seen metadata and masked
communication contracts. The queue also contains virtual IPv4 routing addresses. The plugin
does not deliberately log public IP addresses, set its own cookies or send telemetry. Host logs,
WordPress login cookies and backups operate independently. The self-test sends requests only
from the site to its own configured Server URL and storage folder. Following client download
links contacts the selected destination from the visitor's browser. See the plugin readme for
the complete privacy and retention description.

The WordPress status page keeps the functional client download selector but has no promotional
author footer. The standalone server's status page is separate and is not changed by this.

## WordPress.org submission package

The repository's top-level `WordPress/` directory holds the deterministic submission ZIP, its
SHA-256 manifest, registration instructions, reviewer notes and current verification evidence.
The maintained plugin source remains `server/wordpress/version7/vpn-server`.

```sh
uv run --no-project python WordPress/build-release.py
```

The build checks the generated core and uses an explicit file allowlist. It does not publish
anything. All registration and test files remain outside the installable ZIP.

## Development

In the source repository this folder is `server/wordpress`. The plugin itself is
`version7/vpn-server`, and `version7/README.md` holds the developer notes. Its server core
`includes/server-core.php` is generated from `server/php/phpvpn.php`:

```sh
python tools/build-wordpress-plugin.py            regenerate the server core
python tools/build-wordpress-plugin.py --check    fail when the core or a version is stale
```

`tools/make-distribution.ps1` checks the core and packs the plugin folder into
`server/wordpress/vpn-server-X.Y.Z.zip` of the distribution.

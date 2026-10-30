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
| PHP | 8.2 or newer, 64-bit build, no extensions |
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
5. On nginx add the rule shown under Storage protection in the settings, then press Run self
   test.

The Server URL is a plain path such as `https://example.com/vpn-server/`, without a `.php` file
name. The client appends `?action=...` itself. Opened in a browser, the Server URL shows a status
page with a key generator and the request headers that the Configuration Wizard can reuse. The
page can be turned off in the settings.

## Storage protection

Client messages are encrypted end to end between the clients and wait in the storage folder for
at most 60 seconds. The folder is protected by generated `.htaccess` and `web.config` files.
nginx ignores them and needs the rule shown in the settings. The settings can also move the
folder outside the web root.

## Fast route

Client requests are answered right after the plugins load, before the theme. The optional fast
route answers them even before other plugins load: it writes
`wp-content/mu-plugins/vpn-server-fast-route.php`. It is installed and removed in the settings,
and deactivating the plugin removes it. Use it together with a PHP opcode cache on sites with
many plugins.

## Uninstall and privacy

Deactivating the plugin removes the fast route. Deleting the plugin deletes the storage folder
it created.

The plugin stores its settings (server secret, client VPN addresses and access keys) in the
WordPress options table. The storage folder holds encrypted client messages for at most 60
seconds, and per client a small `info.json` with the time it was last seen and the masked list
of its communication contracts. The plugin stores no IP addresses, uses no cookies and contacts
no external service. The self test sends requests only from the site to its own Server URL and
storage folder.

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

=== VPN Server ===
Contributors: pilandrome
Tags: vpn, self-hosted, tunnel, private network, encryption
Requires at least: 6.6
Tested up to: 7.1
Requires PHP: 8.2
Stable tag: 1.0.4
License: MIT
License URI: https://opensource.org/license/mit

Connect your own devices in a private IPv4 network using a self-hosted PHPVPN relay and the PHPVPN desktop clients.

== Description ==

This is an implementation of PHPVPN (Packet Handoff Protocol VPN) for WordPress. To connect, please install the PHPVPN client for Windows, macOS, or Linux.

VPN Server turns a WordPress site into a PHPVPN server. PHPVPN connects your own computers and servers into one private IPv4 network (10.99.99.0/24) over ordinary HTTPS. Clients initiate outgoing requests, so no incoming port forwarding is needed on the client devices. A host, firewall or security plugin may still block or limit this traffic.

The plugin does not route your devices to the internet and does not hide your public IP address. It only forwards traffic between your own PHPVPN clients.

**How it works**

* Every client polls the Server URL over HTTP or HTTPS and exchanges small encrypted messages with it.
* Payloads are encrypted end to end with AES-256-GCM between each pair of clients. The VPN server stores encrypted payloads alongside routing metadata. It cannot decrypt the payloads because the payload keys never reach the server.
* Requests are authenticated with HMAC-SHA-256 using the server secret and a per client access key.
* Queued messages expire after 60 seconds and are deleted after delivery. Expired files are removed by request-driven maintenance, so they can remain on disk while the server is idle.

**Features**

* Settings, VPN Server: Server URL, clients with VPN addresses and access keys, server secret, storage folder and the poll intervals of the clients.
* Download a ready config.json for each client.
* Client App Download Links: a separate section below Clients with an editable list of the entries offered in the select box of the status page, prefilled with the PHPVPN downloads for Windows, macOS and Linux and the source code link.
* Checks and a self test of the permalinks, HTTPS, the endpoint and the storage protection.
* Fast: client requests are answered right after the plugins load, before the theme. The optional fast route answers them even before other plugins load.
* No vendor service, subscription, telemetry or advertising. The protocol cryptography runs in plain PHP without an additional cryptography extension.

The PHPVPN clients are available from [www.pilandrome.com](https://www.pilandrome.com). The [source repository](https://github.com/pilandrome/phpvpn) includes the client source, protocol specification and build instructions. No client application is installed or executed on the WordPress server.

Developed by Jaromír Šivic, an independent developer. The plugin code and included tutorial image are distributed under the MIT license in LICENSE.txt.

**Requirements and safe deployment**

* WordPress 6.6 or newer, PHP 8.2 or newer with 64-bit integers, pretty permalinks and writable local storage.
* Use HTTPS and exclude the endpoint from page caching, CDN caching and proxy response caching.
* The initial server secret is random. Initial client access keys are deliberately predictable for compatibility with the client wizard. Anyone who knows the shared server secret can impersonate a client whose default key is unchanged. Use New Key for every client requiring separate authentication, distribute the configuration privately and remove unused clients.
* Use private storage outside every web root where possible. Apache/IIS guard files require a server configuration that honors them. nginx requires an explicit deny rule. Test the actual deployment.
* The plugin exposes a polling endpoint. Account for PHP workers, bandwidth, storage and abuse protection, and check your hosting provider's terms. It does not provide DDoS protection.
* Multisite is supported through per-site activation only. Network activation is refused. Only a network administrator can manage the shared fast route.

== Installation ==

1. Install and activate the plugin.
2. Use pretty permalinks (Settings, Permalinks, anything except Plain). The Server URL does not work with plain permalinks.
3. Open Settings, VPN Server. The plugin starts with a random server secret and, like phpvpn.php, with the clients 10.99.99.2 to 10.99.99.15 and their default access keys. The default access keys are the same on every installation and work with the default values of the client Configuration Wizard.
4. Replace the default access key of every client you use with New Key, remove the others, add more if you need them, and download the config.json of each client.
5. Import config.json in the PHPVPN client, or copy Server URL, server secret, VPN address and access key into its Configuration Wizard.
6. Set up the communication contracts and payload keys between the clients in the clients themselves.
7. On nginx add the rule shown under Main Configuration, then press Run self test.

== Frequently Asked Questions ==

= Can I run it on shared hosting? =

It needs a working WordPress installation, 64-bit PHP 8.2 or newer, pretty permalinks and a writable folder. Many hosting providers forbid proxy or VPN services in their terms. Check the terms and resource limits of your provider first.

= Why do the clients poll so often? =

PHP cannot keep connections open on most hosts, so the clients ask for new messages every 50 to 3600 milliseconds by default. Each poll is a short request. The Min Data Poll Interval (25 to 500 ms) and the Max Data Poll Interval (500 to 5000 ms) under Main Configuration change this range. Use the fast route on sites with many plugins and a PHP opcode cache for best response times.

= Where are the messages stored? =

In a folder with a random name inside wp-content/uploads, protected by .htaccess and web.config files. nginx needs the rule shown in the settings. You can move the folder outside the web root under Main Configuration. The disk folder is the only storage type so far, a folder on a RAM disk works the same way. Redis, Memcached and LiteSpeed Cache are listed but cannot be selected yet.

= Why does the Server URL have no .php file name? =

The PHPVPN client appends ?action=... to the Server URL itself, so the Server URL is a plain path such as https://example.com/vpn-server/.

= Can a browser open the Server URL? =

Yes. It shows a select box with the client downloads and setup instructions, plus the request headers that the Configuration Wizard of the client can reuse. A key generator is shown only when the server secret is predictable. Generating or copying a key does not change the saved secret. An administrator must save the new value in Settings, VPN Server. You can turn the page off.

= How can the client download links be changed? =

In Settings, VPN Server, use the separate Client App Download Links section below Clients. Every row is one entry of the select box on the status page, in this order: a name, an absolute HTTP or HTTPS URL, the type File (Download button) or Link (Open button, new tab) and the operating system for which the status page preselects the entry. Add Download adds a row, the arrows move a row and Remove deletes it. Click Save Changes to update the public status page. The list is prefilled with https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-win.zip, https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-macos.zip, https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-linux.zip and the source code link https://github.com/pilandrome/phpvpn. Custom Windows, macOS and Linux URLs saved by an older version are kept.

= What does the fast route do? =

It writes wp-content/mu-plugins/vpn-server-fast-route.php, which answers client requests before the regular plugins load. You install and remove it in the settings, and deactivating VPN Server removes it.

The fast route is off by default. Because it runs before regular plugins, security and logging plugins may not see VPN requests. The VPN protocol still authenticates its requests. Do not enable it when those other plugins must inspect the endpoint. A pre-existing file owned by another tool is not overwritten.

= Does the public status page connect to the developer's website automatically? =

No. The status page is on by default, with local JavaScript and a local tutorial image. Its download selector contains functional links to the required client software and source code. Only following a link contacts its destination. Administrators can change or remove every entry in Client App Download Links, or disable the status page. There is no author-credit footer or promotional backlink on the public page.

= What happens when I deactivate or delete the plugin? =

Deactivation stops the endpoint and removes the managed fast route, except while another site in a multisite network still uses the plugin. Settings and queued data remain for reactivation. Deleting the plugin removes its settings and self-test transient and attempts to remove the storage folder it created using the WordPress filesystem API. An administrator-selected storage folder is preserved and must be cleaned up manually. Filesystem permissions can prevent removal. Backup copies are outside the plugin's control.

== Privacy ==

The plugin stores the server secret, client VPN addresses and access keys, endpoint and other configuration in the WordPress options table. These authentication secrets are readable to a site administrator and anyone with database or backup access. Exported config.json files contain credentials and must be shared privately.

The storage folder holds encrypted client payloads and routing metadata, including virtual IPv4 addresses, message identifiers and expiry times. Messages stop being eligible for delivery after 60 seconds. Physical removal of expired files is request-driven and may occur later, especially while the server is idle. Per-client info.json files retain last-seen metadata and masked communication contracts until replaced or the storage is removed. Masked contracts do not contain payload keys.

The plugin does not deliberately log clients' public IP addresses, set its own cookies or send telemetry. WordPress login cookies and the host's, proxy's or security plugins' access logs operate independently. Those logs may include public IP addresses. The site administrator controls retention and access to storage, logs and backups.

The status page shows a visitor the request headers of their own request, omitting Cookie, Authorization and common proxy headers, so they can copy them into the client. Custom headers may still contain sensitive information, so review them before copying or sharing. The self test in the settings sends requests from the site to its own Server URL and storage folder only.

== External connections ==

No account, license server or hosted vendor service is required. The plugin answers incoming PHPVPN client requests. Its administrator-initiated self-test uses the WordPress HTTP API for loopback requests to this site's configured URL. It does not download executable code or implement an external updater.

The default client download links point to https://www.pilandrome.com/downloads/phpvpn/client/1.x/ and the source link points to https://github.com/pilandrome/phpvpn. Clicking a link makes the visitor's browser contact that destination, which receives ordinary web request information. These are optional, editable download links rather than a required service API. All runtime scripts and images are included locally.

== Development ==

All plugin PHP and JavaScript is readable in this distribution. The maintained source is server/wordpress/version7/vpn-server in the source repository. includes/server-core.php is generated from server/php/phpvpn.php by tools/build-wordpress-plugin.py. The WordPress status page has its own template and is not copied from the standalone server.

From a source checkout, run `uv run --no-project python tools/build-wordpress-plugin.py` to regenerate the core, then `uv run --no-project python tools/build-wordpress-plugin.py --check`. `uv run --no-project python WordPress/build-release.py` creates the submission ZIP and SHA-256 manifest. Development tools and test fixtures are not required on a production site.

== Changelog ==

= 1.0.4 =
* Initial WordPress.org submission package aligned with PHPVPN server 1.0.4.
* Default maximum polling interval is 3600 ms.
* Include the full MIT license and clarify privacy, retention and client requirements.
* Remove the public author-credit footer from the WordPress status page.
* Report inconclusive storage protection tests accurately.
* Preserve an existing fast-route file owned by another tool.
* Remove the PHP runtime identification header from VPN endpoint responses.

= 1.0.1 =
* Initial development release, PHPVPN protocol version 1.

== Upgrade Notice ==

= 1.0.4 =
Initial directory submission. Existing settings are retained. Review storage protection and replace predictable client access keys where separate client authentication is required.

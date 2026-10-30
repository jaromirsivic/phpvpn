=== VPN Server ===
Contributors: pilandrome
Tags: vpn, self-hosted, tunnel, private network, encryption
Requires at least: 6.6
Tested up to: 7.1
Requires PHP: 8.2
Stable tag: 1.0.2
License: MIT
License URI: https://opensource.org/license/mit

Runs a VPN server on this site, enabling individuals and small businesses to quickly set up a lightweight virtual private network.

== Description ==

This is an implementation of PHPVPN (Packet Handoff Protocol VPN) for WordPress. To connect, please install the PHPVPN client for Windows, macOS, or Linux.

VPN Server turns a WordPress site into a PHPVPN server. PHPVPN connects your own computers and servers into one private IPv4 network (10.99.99.0/24) over ordinary HTTPS, so it works wherever a web page can be opened, even behind NAT and firewalls that allow nothing else.

The plugin does not route your devices to the internet and does not hide your public IP address. It only forwards traffic between your own PHPVPN clients.

**How it works**

* Every client polls the Server URL over HTTP or HTTPS and exchanges small encrypted messages with it.
* Payloads are encrypted end to end with AES-256-GCM between each pair of clients. The VPN server stores only ciphertext and cannot read it, the payload keys never reach the server.
* Requests are authenticated with HMAC-SHA-256 using the server secret and a per client access key.
* Messages wait in a short lived queue on disk for at most 60 seconds and are deleted after delivery.

**Features**

* Settings, VPN Server: Server URL, clients with VPN addresses and access keys, server secret, storage folder and the poll intervals of the clients.
* Download a ready config.json for each client.
* Client App Download Links: a separate section below Clients with an editable list of the entries offered in the select box of the status page, prefilled with the PHPVPN downloads for Windows, macOS and Linux and the source code link.
* Checks and a self test of the permalinks, HTTPS, the endpoint and the storage protection.
* Fast: client requests are answered right after the plugins load, before the theme. The optional fast route answers them even before other plugins load.
* No external services, no tracking, no ads. All cryptography runs in plain PHP without extensions.

The PHPVPN clients and the protocol specification are available from [www.pilandrome.com](https://www.pilandrome.com).

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

Technically yes, the plugin needs only 64-bit PHP 8.2 and a writable folder. Many hosting providers forbid proxy or VPN services in their terms, though. Check the terms of your provider first.

= Why do the clients poll so often? =

PHP cannot keep connections open on most hosts, so the clients ask for new messages every 50 to 1800 milliseconds by default. Each poll is a short request. The Min Data Poll Interval (25 to 500 ms) and the Max Data Poll Interval (500 to 5000 ms) under Main Configuration change this range. Use the fast route on sites with many plugins and a PHP opcode cache for best response times.

= Where are the messages stored? =

In a folder with a random name inside wp-content/uploads, protected by .htaccess and web.config files. nginx needs the rule shown in the settings. You can move the folder outside the web root in the settings.

= Why does the Server URL have no .php file name? =

The PHPVPN client appends ?action=... to the Server URL itself, so the Server URL is a plain path such as https://example.com/vpn-server/.

= Can a browser open the Server URL? =

Yes. It shows a select box with the client downloads and setup instructions, plus the request headers that the Configuration Wizard of the client can reuse. A key generator is shown only when the server secret is predictable. Generating or copying a key does not change the saved secret. An administrator must save the new value in Settings, VPN Server. You can turn the page off.

= How can the client download links be changed? =

In Settings, VPN Server, use the separate Client App Download Links section below Clients. Every row is one entry of the select box on the status page, in this order: a name, an absolute HTTP or HTTPS URL, the type File (Download button) or Link (Open button, new tab) and the operating system for which the status page preselects the entry. Add Download adds a row, the arrows move a row and Remove deletes it. Click Save Changes to update the public status page. The list is prefilled with https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-win.zip, https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-macos.zip, https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-linux.zip and the source code link https://github.com/pilandrome/phpvpn. Custom Windows, macOS and Linux URLs saved by an older version are kept.

= What does the fast route do? =

It writes wp-content/mu-plugins/vpn-server-fast-route.php, which answers client requests before the regular plugins load. You install and remove it in the settings, and deactivating VPN Server removes it.

== Privacy ==

The plugin stores its settings (server secret, client VPN addresses and access keys) in the WordPress options table. Its storage folder holds encrypted client messages for at most 60 seconds, and per client a small info.json file with the time it was last seen and the masked list of its communication contracts, which the client replaces whenever it publishes a new one. Uninstalling the plugin deletes the storage folder it created. It does not store IP addresses, does not use cookies and does not contact any external service.

The status page shows a visitor the request headers of their own request (without cookies, credentials and proxy headers), so they can copy them into the client. The self test in the settings sends requests from the site to its own Server URL and storage folder only.

== Changelog ==

= 1.0.1 =
* First release, PHPVPN protocol version 1.

== Upgrade Notice ==

= 1.0.1 =
First release.

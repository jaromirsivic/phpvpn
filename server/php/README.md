# PHP VPN server for PHP

`phpvpn.php` is the PHP implementation of the PHP VPN relay protocol. It is a single
self-contained file: no Composer packages, no PHP extensions and no separate assets. All
AES-256-GCM and HMAC operations use the pure PHP implementation inside the file, never OpenSSL,
Sodium or any other crypto extension. The clients talk to it over HTTP or HTTPS with the actions
`health`, `info` and `data`.

Choose one server implementation per deployment. The PHP, Python and Node.js servers speak the
same protocol, but their queue storage formats differ and must never share a folder.

## Requirements

| Item | Value |
|---|---|
| PHP | 8.2 or newer, 64-bit build. A 32-bit PHP only shows an error page. |
| Extensions | none |
| `memory_limit` | at least `64M` |
| `post_max_size` | strictly higher than `MAX_REQUEST_BODY_BYTES` (983040 bytes), for example `2M` |
| Web server | any server that runs PHP (Apache, nginx with PHP-FPM, IIS, the PHP built-in server for tests) |
| Storage | a local folder writable by the PHP process, with `flock()`, exclusive file creation and atomic `rename()` |

## Deployment

1. Copy `phpvpn.php` to the document root of the web server or to any web directory.
2. Open the file in an editor and change the configuration header at its top (section
   "1. Configuration header"):
   - replace `SERVER_SECRET` with a long random value,
   - replace `ACCESS_KEYS` with your own clients and keys,
   - set `BUFFER_DIRECTORY` to a writable folder, ideally outside the document root.
3. If the storage folder stays inside the document root, deny all direct web access to it
   (see [Storage protection](#storage-protection)).
4. Open the deployed URL in a browser, for example `https://vpn.example.org/phpvpn.php`. The
   status page confirms that PHP runs the file. Use the same URL as the Server URL in the clients.

The file may be renamed. Clients use whatever URL the web server maps to it and append
`?action=...` themselves.

## Configuration

All administrator settings are constants in the configuration header. Everything below the
header is the implementation and is not meant to be edited.

| Constant | Default | Meaning |
|---|---|---|
| `SERVER_SECRET` | `CHANGE_THIS_SERVER_SECRET` | Secret shared with all authorized clients. Exact UTF-8 bytes. Only an empty string is normalized to the default. A predictable value shows a red warning on the status page, the API keeps working. |
| `ACCESS_KEYS` | `10.99.99.2` to `10.99.99.15` | Client VPN address mapped to exactly 64 lowercase hexadecimal characters (32 bytes). The defaults are public examples and must be replaced. |
| `BUFFER_DIRECTORY` | `./phpvpn_data` | Storage root. A relative path is resolved against the folder of `phpvpn.php`, not against the working directory. Data in the older default `./phpvpn` is not migrated. |
| `STATUS_PAGE_ENABLED` | `true` | `false` answers `GET`/`HEAD` with `404 not_found`. The API stays available. |
| `STORAGE_DRIVER` | `disk` | The only storage driver of the PHP server. |
| `VPN_ADDRESS_PREFIX`, `VPN_HOST_MIN`, `VPN_HOST_MAX` | `10.99.99.`, 2, 254 | VPN address space. Must be the same on every client. |
| Protocol limits | see `protocol.md` §11.1 | `MAX_DATA_RECORDS_PER_REQUEST`, `MAX_AUTH_RECORD_BYTES`, `MAX_REQUEST_BODY_BYTES`, `MAX_INFO_PLAINTEXT_BYTES`, `MAX_INFO_REQUEST_BODY_BYTES`, `MAX_MASKED_COMMUNICATION_CONTRACTS`, `INFO_TEMP_TTL_SECONDS`, `MAX_QUEUE_BYTES_PER_CLIENT`, `AUTH_TIMESTAMP_SKEW_SECONDS`, `QUEUE_TTL_SECONDS`, `LOCK_TIMEOUT_MS`, `MIN_DATA_POLL_INTERVAL_MS`, `MAX_DATA_POLL_INTERVAL_MS`. The active values are published by `info`. |
| `CLIENT_STATS_MIN_UPDATE_SECONDS` | 30 | Minimum age before the lastSeen marker of a client is refreshed. |
| Maintenance | 5 s, 8, 256, 20 ms | `SWEEP_MIN_INTERVAL_SECONDS`, `SWEEP_MAX_DIRECTORIES`, `SWEEP_MAX_FILES`, `SWEEP_MAX_MILLISECONDS`: budget of the opportunistic cleanup. |
| `PROTECT_BUFFER_DIRECTORY` | `true` | Writes best effort `.htaccess`, `web.config` and `index.html` guards into the storage root. |
| `CLIENT_DOWNLOADS` | Windows, macOS and Linux archives from `https://www.pilandrome.com/downloads/phpvpn/client/1.x/` and the Source Code link `https://github.com/pilandrome/phpvpn` | Entries of the client select box on the status page, in this order. Each entry has a `name`, a `url`, a `type` of `file` (Download button) or `link` (Open button, new tab) and an optional `os` of `windows`, `macos` or `linux`, which preselects the entry for the visitor's system. An empty array hides the select box. |

A broken limit invariant (for example a minimum poll interval above the maximum) does not stop
the server. The status page keeps working and the API answers `503 server_misconfigured`.

## Requests

| Request | Answer |
|---|---|
| `GET` or `HEAD` without `action` | Status page, or `404` when `STATUS_PAGE_ENABLED` is `false`. Other methods get `405`. |
| `POST ?action=health`, `info`, `data` | Protocol API. Other methods get `405`, unknown actions `404`. |

The status page includes a key generator and a Show HTTP Headers button that lists the request
headers received from the browser, for reuse in the Configuration Wizard of the client.

## Storage protection

The storage folder holds only encrypted client messages for at most `QUEUE_TTL_SECONDS`
(60 seconds), plus a small `info.json` per client. It must still never be reachable over HTTP.

- Best: put `BUFFER_DIRECTORY` outside the document root.
- Apache and IIS honour the generated `.htaccess` and `web.config` guards.
- nginx ignores them. Add a rule for the storage folder, for example:

```nginx
location ^~ /phpvpn_data/ { deny all; return 404; }
```

Each target queue is guarded by an exclusive `flock()`. A request waits at most
`LOCK_TIMEOUT_MS` (200 ms) for it and then answers `503 queue_busy`. Other storage errors answer
`503 queue_unavailable`. A full queue is not an error: the answer is `200` and the deferred
messages are listed in `deferredMessageIds`.

## HTTPS and proxies

TLS is terminated by the web server or a reverse proxy in front of it. Any proxy must pass the
request and the response bytes unchanged, must not compress, cache or redirect them, and its
request body limit must be strictly higher than `MAX_REQUEST_BODY_BYTES`. A response can carry a
whole client queue of up to 16 MiB. Authorization never depends on the TCP address, the scheme,
`Host` or `Forwarded`/`X-Forwarded-*` headers.

## Logs and privacy

Error answers carry only a generic code. The server never logs or returns secrets, tags,
ciphertext or internal paths.

## Development

In the source repository this folder is `server/php`. The tests run from the repository root:

```sh
php server/php/tests/tests.php
php server/php/tests/tests-info-pretty-boundaries.php
```

The shared HTTP suite in `tests-integration` runs against an isolated copy of the file with
`tools/test-python-server.py --profiles php-disk` (see `server/python/README.md` for the Python
environment). The WordPress plugin core in `server/wordpress` is generated from this file by
`tools/build-wordpress-plugin.py`.

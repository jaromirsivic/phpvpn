# PHP VPN server for Node.js

`phpvpn.js` is a standalone Node.js implementation of the relay protocol from `protocol.md`.
The existing client talks to it without any change: the same URL `/phpvpn.php`, the same
actions `health`, `info` and `data`, the same cryptography, limits and errors. The only
persistent storage is the local disk. A memory driver, Memcached or Redis are not part of this
version.

## What is deployed

| Item | Value |
|---|---|
| Application files | exactly one: `phpvpn.js` |
| Dependencies | none. Only built-in Node.js modules (`node:http`, `node:crypto`, `node:fs`, `node:path`, `node:perf_hooks`). No `npm install`, build or assets. |
| Runtime | Node.js 24 LTS (the latest patch release of the line). Other major versions are not declared as supported. |
| Optional | a configuration JSON file outside the script, a reverse proxy for HTTPS |
| Operational data | the `BUFFER_DIRECTORY` folder, by default `./phpvpn_data` next to the script |

The HTML status page is embedded in the script, no `assets` folder is deployed. Its Show HTTP
Headers button lists the received headers for the Configuration Wizard (`protocol.md` §5.1).

Copy this folder to a writable deployment directory outside the web document root.

## Running

```sh
node phpvpn.js
```

The server listens on `http://127.0.0.1:8080/phpvpn.php` and writes the single line
`listening http://127.0.0.1:8080/phpvpn.php` to stderr. It stops on SIGINT (Ctrl+C) or SIGTERM:
it stops accepting connections, finishes the requests in progress and exits after 10 seconds at
the latest.

Other modes:

```sh
node phpvpn.js --config /etc/phpvpn/server.json
node phpvpn.js --check-config --config /etc/phpvpn/server.json
node phpvpn.js --offline-recovery --config /etc/phpvpn/server.json
```

`--check-config` only validates the configuration and exits with code 0, an error returns code 2.
`--offline-recovery` is described in [Recovery after a hard crash](#recovery-after-a-hard-crash).

The script does not create an OS service. For systemd, NSSM or another process manager run
`node /path/phpvpn.js --config /path/server.json` directly under a dedicated unprivileged
account. Several instances with different storage roots can run on one machine. Several
processes over the same root are supported too, see [Concurrency](#concurrency).

## Configuration

The configuration has two layers. The defaults are in the `DEFAULT_CONFIG` header at the
beginning of `phpvpn.js`. The file passed with `--config` is a JSON object with the same key
names and its values take precedence. The recommended setup keeps the script unchanged and holds
the secrets in a configuration file readable only by the service account. An unknown key, a
wrong type or unreadable JSON stops the start with code 2. The server reads no environment
variables and never accepts secrets as command line arguments.

A minimal production file:

```json
{
    "SERVER_SECRET": "put a long random value here",
    "ACCESS_KEYS": {
        "10.99.99.2": "64 lowercase hex characters",
        "10.99.99.3": "64 lowercase hex characters"
    },
    "BUFFER_DIRECTORY": "/var/lib/phpvpn",
    "LISTEN_HOST": "127.0.0.1",
    "LISTEN_PORT": 8080
}
```

`ACCESS_KEYS` in the file replaces the whole default map. The default map holds the publicly
derivable keys `.2` to `.15` from the protocol. They are not secret and must be replaced for
production use.

| Key | Default | Meaning |
|---|---|---|
| `SERVER_SECRET` | `CHANGE_THIS_SERVER_SECRET` | Exact UTF-8 bytes, no trimming and no hex decoding. Only an empty string is normalized to the default. A predictable value turns on a red warning on the status page, the API keeps working. |
| `ACCESS_KEYS` | `.2` to `.15` | VPN address mapped to exactly 64 lowercase hex characters. |
| `BUFFER_DIRECTORY` | `./phpvpn_data` | A relative path is resolved against the folder of `phpvpn.js`, not against the working directory. |
| `STATUS_PAGE_ENABLED` | `true` | `false` answers GET/HEAD with `404`, the API keeps working. |
| `CLIENT_WINDOWS_DOWNLOAD_URL`, `CLIENT_MACOS_DOWNLOAD_URL`, `CLIENT_LINUX_DOWNLOAD_URL` | `https://www.pilandrome.com/downloads/phpvpn/client/1.x/` + `phpvpn-win-x64.zip`, `phpvpn-macos.zip`, `phpvpn-linux.zip` | Client download links on the status page. Set them in the header or the JSON configuration file. The setup screenshot is embedded in the script. |
| `VPN_ADDRESS_PREFIX`, `VPN_HOST_MIN`, `VPN_HOST_MAX` | `10.99.99.`, 2, 254 | Address space. Must be the same on all clients. |
| `LISTEN_HOST`, `LISTEN_PORT` | `127.0.0.1`, 8080 | Bind address. Direct remote HTTP traffic needs an explicit `0.0.0.0` or a specific address. |
| `SERVER_PATH` | `/phpvpn.php` | Public path used by the client. Other paths answer `404`. |
| `REQUEST_TIMEOUT_SECONDS` | 30 | Longest time to receive a whole request. Protects against slow uploads. |
| `SYNC_WRITES` | `false` | `true` adds an fsync before a batch or the contracts are published. |
| Protocol limits | see `protocol.md` §11.1 | `MAX_DATA_RECORDS_PER_REQUEST`, `MAX_AUTH_RECORD_BYTES`, `MAX_REQUEST_BODY_BYTES`, `MAX_INFO_REQUEST_BODY_BYTES`, `MAX_MASKED_COMMUNICATION_CONTRACTS`, `MAX_INFO_PLAINTEXT_BYTES`, `MAX_QUEUE_BYTES_PER_CLIENT`, `AUTH_TIMESTAMP_SKEW_SECONDS`, `QUEUE_TTL_SECONDS`, `LOCK_TIMEOUT_MS`, `MIN_DATA_POLL_INTERVAL_MS`, `MAX_DATA_POLL_INTERVAL_MS`, `INFO_TEMP_TTL_SECONDS`, `CLIENT_STATS_MIN_UPDATE_SECONDS`. The active values are published by `info`. |
| Maintenance | 5 s, 8, 256, 20 ms | `SWEEP_MIN_INTERVAL_SECONDS`, `SWEEP_MAX_DIRECTORIES`, `SWEEP_MAX_FILES`, `SWEEP_MAX_MILLISECONDS`: budget of the opportunistic cleanup after a `data` answer. |

A broken limit invariant (for example a minimum poll interval above the maximum) does not stop
the server. The status page keeps working and the API answers `503 server_misconfigured`, just
like the PHP implementation.

## HTTPS and reverse proxy

The server itself speaks HTTP only. HTTPS is terminated by a reverse proxy. The application
behaves the same over HTTP and HTTPS and does not read `X-Forwarded-*` or `Forwarded`.

The proxy must:

- pass the request and response bodies byte for byte, without gzip or any other transformation;
- have a request body limit strictly higher than 983040 bytes, for example 1 MiB;
- be able to return a response of up to 16 MiB plus two final lines without paging;
- not redirect and not cache;
- publish only the path `/phpvpn.php`, never the script folder, the configuration or
  `BUFFER_DIRECTORY`.

nginx example:

```nginx
upstream phpvpn_node {
    server 127.0.0.1:8080;
    keepalive 16;
    # Shorter than the 5 s keep-alive of the Node server, so nginx never
    # reuses a connection the server is just closing.
    keepalive_timeout 4s;
}

server {
    listen 443 ssl;
    server_name vpn.example.org;
    ssl_certificate     /etc/ssl/vpn.crt;
    ssl_certificate_key /etc/ssl/vpn.key;
    client_max_body_size 1m;
    server_tokens off;

    location = /phpvpn.php {
        proxy_pass http://phpvpn_node;
        proxy_http_version 1.1;
        proxy_set_header Connection "";
        proxy_request_buffering on;
        proxy_buffering off;
        proxy_read_timeout 60s;
        gzip off;
    }
    location / { return 404; }
}
```

Without an `upstream` with `keepalive`, nginx opens a new connection for every request. With
frequent polling this exhausts the ephemeral ports on Windows (`10048`) and nginx starts to
answer `502`.

The Node server has no generic static file handler, so the storage is not reachable through it.
Still keep `BUFFER_DIRECTORY` outside any web server document root.

## Storage

The format is private to the Node server and is **not compatible** with `phpvpn.php` or
`phpvpn.py`. When moving from PHP, use a new empty root and switch the client URL. The old PHP
queue is neither converted nor migrated. Queued data lives at most `QUEUE_TTL_SECONDS` (60 s),
so the switch loses at most the packets waiting at that moment.

Layout:

```text
BUFFER_DIRECTORY/
  10_99_99_3/                     queue of the target address 10.99.99.3
    <expiry>_<bytes>_<src>_<uuid>.data       published batch
    <expiry>_<bytes>_<src>_<uuid>.receiving  being written, never published
    <expiry>_<bytes>_<src>_<uuid>.sending    taken over, never handed out again
    info.json                     lastSeen marker and published contracts
    .info-<uuid>.tmp              short lived file of a contract publication
    .lock/                        queue lock, exists only during a write or a takeover
```

A batch starts with one line of a private header
`{"format":"phpvpn-node-1","bytes":N,"records":[[messageId,tag],...]}` followed by the original
data lines. The header is checked against the lines when the batch is read.

Folders are created lazily, only from a validated address. The root belongs to the service
account of the server, without write access for others (POSIX for example `0700`). Supported is
a local filesystem with atomic rename within a folder (NTFS, ext4, XFS, APFS). Shared NFS/SMB and
concurrent access of the PHP or Python server to the same root are not a verified mode. A
symlink or reparse point in place of a client folder is rejected with `503 queue_unavailable`.

The publication guarantee covers a process crash: after a crash a batch is either a complete
`.data` file or does not exist. Protection against a power failure depends on the filesystem and
on `SYNC_WRITES`.

## Concurrency

Several Node processes may run over one root. Every target queue has a lock made by an atomic
`mkdir` of the `.lock` folder. The lock is held only while a batch is written and while batches
are taken over for sending. A request waits at most `LOCK_TIMEOUT_MS` (200 ms), then the server
answers `503 queue_busy`. Any other I/O error means `503 queue_unavailable`. A full queue is not
an error: the answer is `200` and the items are listed in `deferredMessageIds`.

Taking a batch over is a rename of `.data` to `.sending` under the queue lock. On Windows several
concurrent renames of the same source to the same target can succeed at the same time, so the
rename alone does not guarantee exclusivity.

## Recovery after a hard crash

A regular restart after SIGINT/SIGTERM needs no action. Locks are released in `finally` and
published batches that were not handed out survive the restart until their TTL.

A hard crash (kill -9, machine failure) in the middle of a write or a takeover can leave an
orphaned `.lock` folder. For safety the server never removes it by itself based on age or PID,
because it cannot verify that no other process runs over the same root. The affected queue then
answers `503 queue_busy`. The other queues keep working.

Recovery:

1. Stop **all** processes that use the given `BUFFER_DIRECTORY`.
2. Verify that they really do not run (`ps`, `systemctl status`, Task Manager).
3. Run `node phpvpn.js --offline-recovery --config <the same file>`. It removes only the `.lock`
   folders and leftover `.receiving` and `.sending` files in the client folders. Published
   `.data` and `info.json` stay. Only counts are written to stderr.
4. Start the server again.

A batch that was taken over (`.sending`) is never handed out after a crash. This is the
deliberate at-most-once behaviour of the protocol.

## Memory and logs

The queue always lives on disk. Before sending, a `data` answer loads the batches taken over for
the caller into memory, so it can remove them from the storage before the transfer. One
concurrent `data` request can therefore temporarily hold up to `MAX_QUEUE_BYTES_PER_CLIENT`
(16 MiB) plus the request body (983040 bytes). Plan RAM as roughly 17 MiB times the number of
full queues handed out at the same time, plus the usual Node.js overhead. Writing the answer
respects backpressure and the data is released when the client disconnects.

Measured on Windows 11 with Node.js v23.3.0 and queues of 16 267 040 bytes:

| Full queues handed out concurrently | RSS before | RSS peak | Hand-out time |
|---:|---:|---:|---:|
| 1 | 74.8 MiB | 90.9 MiB | 0.36 s |
| 4 | 100.0 MiB | 154.2 MiB | 1.34 s |

The log goes to stderr and holds only the time, an event code and, where relevant, the system
error code, for example `queue_unavailable EACCES`. It never contains secrets, tags, ciphertext,
requests or internal paths. Error answers carry only a generic code.

## Known limitations

- An orphaned lock after a hard crash needs a manual offline recovery.
- The disk format is not compatible with the PHP or Python server and is not migrated.
- NFS/SMB and sharing a root with another implementation are not verified.
- HTTPS is provided only by a reverse proxy.
- The target runtime is Node.js 24 LTS, but the first validation ran only on Node.js v23.3.0
  (Windows 11). Linux and Node.js 24 are not validated yet.

## Development

In the source repository this folder is `server/node.js`. The deterministic server tests need no
npm packages and run from the repository root:

```sh
node --check server/node.js/phpvpn.js
node --test "server/node.js/tests/*.test.js"
```

The shared integration suite against an isolated copy of the server (Python environment through
uv):

```powershell
uv run --no-project --python 3.13 --with pytest --with requests --with cryptography --with psutil python tools/test-python-server.py --profiles node-disk --node "C:/Program Files/nodejs/node.exe" --report-dir reports/node-server -- -q
```

The real C++ client against the Node server (a binary built with
`PHPVPN_BUILD_CONTRACT_TESTS=ON`):

```powershell
uv run --no-project --python 3.13 --with pytest --with requests --with cryptography python tools/test-python-server.py --profiles node-disk --contract-executable work/build/release/tests/phpvpn-contract-tests.exe --contract-only
```

The runner copies `phpvpn.js` to a temporary folder, passes it its own JSON configuration with
the port and the root and after the tests stops only its own process.

# PHP VPN server for Python

`phpvpn.py` is a standalone implementation of the relay protocol from `protocol.md`. The
existing client uses the same URL, authentication and messages as with `phpvpn.php`. The PHP
file stays usable on its own. Choose one implementation and one instance per deployment. The
disk data of PHP and Python are not interchangeable.

## Installation and start

The validated runtime is Python 3.13. Dependencies are installed with `uv` from the locked
`requirements-server.txt`. The server installs nothing while it runs.

In the extracted distribution open `server/python`. Windows requires Windows PowerShell 5.1,
Linux and macOS a POSIX shell and curl or wget. Init needs access to the official Astral servers
and the package registry. Administrator rights are not needed, but the deployed folder must be
writable by the service user.

On Windows:

```powershell
.\init.bat
.\start.bat
.\stop.bat
```

On Linux and macOS:

```sh
sh init.sh
sh start.sh
sh stop.sh
```

Calling the scripts through `sh` works even after an archiver that did not keep the executable
bit. The scripts can be called with an absolute path from another working directory, the venv is
never activated. `init` uses a working uv from PATH, otherwise it installs uv with the official
[Astral installer](https://docs.astral.sh/uv/getting-started/installation/) into `.tools/uv`. It
changes neither the system execution policy nor shell profiles. The downloaded uv is usable at
once, a new terminal is not needed. Init prepares a managed CPython 3.13 and its own `.venv` and
synchronizes the exact `requirements-server.txt`. Another active virtual environment does not
affect it. Start and stop download nothing.

Init can be repeated. While the managed server runs, it refuses to change the environment. It
does not delete a damaged or incompatible `.venv` automatically: stop the server first, move that
folder aside and run init again. After an interrupted installation make sure that no other init
runs, and only then remove the empty leftover `.bootstrap.lock` (and a downloaded uv installer
inside it, if any). Then repeat init. Network and other errors return a nonzero exit code.

Start launches one detached application process that survives the end of the launcher and the
closing of its terminal. Within 30 seconds it requires its own bind/startup, a matching process
identity and a cryptographically verified health answer on the printed URL. Only then it shows
`Server started successfully.` and a separate line `Open in your browser: ...`. Stop sends a
private addressed request and waits for the graceful shutdown, the end of the application
lifespan, the real end of the original process and the release of its lock. The HTTP shutdown
is limited to 30 seconds, the whole stop check to 45 seconds. No automatic hard kill is used. A
successful stop shows `Server stopped successfully.` without a URL. Both ASCII banners stay
visible for at least three seconds before the script ends normally. A key press does not shorten
that time.

Exit code 0 means success or a server that is not running (`Server is not running.`, without a
banner). A second start returns code 1 and `Server is already running.`. Errors have a nonzero
code and no success banner. A stop timeout keeps the process and the state available for
diagnosis and for another stop. When the process crashes, no successful graceful stop is
claimed.

The log is `.runtime/server.log`. `.runtime` holds private state, operation and lifetime OS locks
and the addressed control channel, never the server secrets. On Windows the ACL grants access to
the service user, SYSTEM and administrators, on POSIX the modes are 0700/0600. Symlink and
reparse paths are rejected. The same OS user is the trust boundary. The state must not be moved
between deployments. Damaged or mismatching metadata is kept for manual inspection, a foreign PID
is never terminated automatically. Do not delete `.runtime` or the lock files while the server
runs. Resolve an unclear state with a held lock from the log and the process identity, not by
deleting the metadata and starting again.

To move a deployment, stop it first, then rebuild `.venv` in the new copy with init and do not
carry `.runtime` over. The scripts create no OS service and no automatic start after a reboot.
Every concurrent deployment needs its own port and its own buffer.

A direct foreground run is still available: `.venv/Scripts/python.exe -B phpvpn.py` on Windows
or `.venv/bin/python -B phpvpn.py` on POSIX. It stops with Ctrl+C and is not managed by the stop
script. Do not combine a direct run with a managed instance of the same deployment.

The default address is `http://127.0.0.1:8000/phpvpn.php`. The `.php` suffix of the HTTP path is
deliberate, the real file is called **phpvpn.py**. `SERVER_PATH` sets the path, `LISTEN_HOST`
and `LISTEN_PORT` the bind address. Other paths answer a generic 404. The server serves no
static files from its folder.

The banner uses the real port and the URL encoded `SERVER_PATH`. A bind to `0.0.0.0` is shown as
`127.0.0.1`, `::` as `[::1]`. IPv6 addresses are in square brackets. It is a verified local HTTP
URL, the public HTTPS address is set separately by the proxy. With
`STATUS_PAGE_ENABLED = False` the endpoint is ready through the health API, but opening the same
URL in a browser deliberately answers 404.

The launcher passes the application instance to Uvicorn, sets `workers=1` and turns off reload,
WebSockets, trust in proxy headers and the access log. It uses `asyncio` and `h11`. The
`WEB_CONCURRENCY` variable does not override this explicit setting. Do not use an external
multiprocess launcher or several replicas. Importing the file creates no global application and
the command `uvicorn phpvpn:app` is not supported. The helper threads of the storage belong to
the same process. On Windows the interpreter in the venv can have a helper launcher and a system
`conhost`, neither is another application worker.

## Configuration

All administrator settings are in one block at the top of `phpvpn.py`. Their names and default
values match the PHP server, including the fourteen default `ACCESS_KEYS` for the addresses `.2`
to `.15`.

- Change `SERVER_SECRET` and `ACCESS_KEYS`. An access key is exactly 64 lowercase hexadecimal
  characters. The secret is a Python string encoded as UTF-8. Only an exactly empty string is
  normalized to `CHANGE_THIS_SERVER_SECRET`.
- `STORAGE_DRIVER = 'disk'` is the default, the alternative is `'memory'`.
- `BUFFER_DIRECTORY = './phpvpn_data'` is relative to the location of the script, not to the
  working directory of the process. Use your own folder outside the web root.
- `STATUS_PAGE_ENABLED` turns the public GET/HEAD page on. Turning it off does not restrict the
  API. A default, empty, zero or ASCII zero secret shows a warning panel. The Show HTTP Headers
  button lists the received headers for the Configuration Wizard (`protocol.md` §5.1).
- `CLIENT_WINDOWS_DOWNLOAD_URL`, `CLIENT_MACOS_DOWNLOAD_URL` and `CLIENT_LINUX_DOWNLOAD_URL`
  set the three client download links on the status page. By default they point to
  `https://www.pilandrome.com/downloads/phpvpn/client/1.x/` with the files
  `phpvpn-win-x64.zip`, `phpvpn-macos.zip` and `phpvpn-linux.zip`, respectively.
  The setup screenshot is embedded in the script, so no separate assets are needed.
- `VPN_ADDRESS_PREFIX`, `VPN_HOST_MIN`, `VPN_HOST_MAX`, all `MAX_*`, the TTLs, the
  authentication window, the polling and sweep constants mean what the protocol describes.
- `PROTECT_BUFFER_DIRECTORY` creates best effort disk guards. They do not replace a correct
  reverse proxy setup. `INFO_TEMP_TTL_SECONDS` concerns only temporary disk publications of
  contracts.

With memory storage, `BUFFER_DIRECTORY`, `PROTECT_BUFFER_DIRECTORY` and `INFO_TEMP_TTL_SECONDS`
are not used for operational storage. Importing the file and validating the configuration create
nothing. With an invalid configuration the API answers a generic `503 server_misconfigured`, a
wrong driver is never silently replaced.

## Disk and memory

The disk driver stores whole batches in per-client folders through atomic transitions
`receiving → data → sending`. The `phpvpn-python-1` index is an internal Python format. A batch
is published by a rename after it was completely written, flushed and closed. No fsync is done,
so no extra resilience against a power failure is promised. Live `.data` files survive a
restart, taken over `.sending` files are not restored. Forgotten files are removed by the
bounded maintenance after they expire. One buffer must not be shared by several processes.

The memory driver keeps the queues, lastSeen, the published contracts and the maintenance state
in memory only. It creates no buffer, journal, checkpoint, temporary files or spool. All of this
state is gone after a restart. The `-B` option turns off the Python bytecode cache. Package
installation and the uv cache are not operational storage. Memory mode does not prevent OS swap.

Both drivers have the same capacity, counted from the original data lines including LF. A target
group that does not fit is deferred as a whole in a successful answer, another target can be
accepted. A poll takes over the whole live queue found, without paging. No lock is held while
sending over the network. A takeover cannot be undone, even when the client disconnects, so the
protocol provides at-most-once delivery, not confirmed delivery.

The queue limit applies per client, not per process. Slow answers own batches already taken
over while new data can arrive in the queues. RAM can therefore exceed the sum of the data
currently queued. Size it by the number of clients, the traffic and concurrent slow connections.
The transfer goes batch by batch and never builds one copy of the whole answer. The final MAC is
not sent when reading a batch fails.

## HTTPS and reverse proxy

Terminate HTTPS in front of the process listening locally. The proxy must keep the method, the
query and the exact body bytes. It must not rewrite JSON or LF, decompress content into other
bytes or cache answers. Set the request limit **above** `MAX_REQUEST_BODY_BYTES`, so that the
application boundary returns its own generic 413. Do not limit an answer to the size of one
request, it can carry a whole 16 MiB queue.

An nginx location with the default values:

```nginx
location = /phpvpn.php {
    client_max_body_size 2m;
    proxy_pass http://127.0.0.1:8000;
    proxy_http_version 1.1;
    proxy_cache off;
    proxy_buffering off;
    proxy_request_buffering off;
    proxy_read_timeout 120s;
}
```

The TLS certificate and the other virtual host settings belong to the proxy. Neither the source
nor the data folder may be its static web root. Authorization never depends on the TCP address,
the scheme, `Host` or `Forwarded` and `X-Forwarded-*`.

## Development

In the source repository this folder is `server/python`, with the tests in
`server/python/tests`. A development environment can be prepared with `uv venv --python 3.13`
and `uv pip sync --python <venv interpreter> server/python/requirements-server.txt`. The lock is
generated from `requirements-server.in`.

Add the test packages to the development environment with uv and run from the repository root:

```powershell
uv pip install pytest requests psutil
uv run --no-project python -B -m pytest server/python/tests -q
uv run --no-project python tools/test-python-server.py --profiles python-disk python-memory -- -q
uv run --no-project python tools/test-python-server.py --profiles php-disk python-disk python-memory --report-dir reports/python-server -- -q
```

The last command takes PHP from `--php`, otherwise from `tools.php` of the global configuration
`phpvpn.config.json` or from PATH. Every profile has its own port, server copy, configuration
and storage outside the web root. Neither the original server nor the deployment description of
the test server is overwritten. Without `-m "not slow"` the real expiry runs too. Memory skips
only four tests of the physical disk layout. The shared queue assertions are the same.

`server/python/tests` additionally covers exact times and limits, storage failures,
cancellation, a restart of the real process, an HTTPS proxy with a private test CA and a single
worker even with `WEB_CONCURRENCY=4`. It changes neither system certificates nor an operational
service. The optional `--contract-executable <phpvpn-contract-tests.exe>` of the runner runs the
real C++/WinHTTP/CNG contract over the same isolated fixtures. `--contract-only` skips the HTTP
pytest suite.

`--measure-memory` samples the RSS of the server process every 10 ms and with `--report-dir`
stores the initial, peak and final value in JSON. To compare all profiles use the same test
filters, for example `-- -q -m "not slow"`. Latencies are printed by the shared performance
tests. RSS is a measurement of one run, not a guaranteed consumption limit nor an exact capture of
every short peak.

Concurrent unread answers and new filling of the same queues can be measured separately:

```powershell
uv run --no-project python -B tools/measure-python-queue-lifetime.py --output reports/python-server/queue-lifetime.json
```

For both disk and memory this scenario opens three answers without reading the body, each with
about 9.4 MiB of data, and fills the same queues again. Then it verifies all original and newly
accepted messages and their MACs. It measures only the RSS of the server.

`tools/test-server-scripts.py` runs the start, stop and init scripts for real against an
isolated copy of this folder, or of `server/python` of a distribution with `--distribution`.

The protocol and its expectations are shared. The internal disk formats and the loss of the
memory state after a restart are tested separately. Detailed results and scenarios that were not
run belong to a dated validation report, not to a claim about universal performance.

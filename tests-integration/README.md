# phpvpn integration test suite

Python integration tests for the deployed phpvpn v1 relay. The suite talks to a
running server over real HTTP and verifies the normative requirements of
`protocol.md`, which is the single source of truth. Every assertion states what
the specification requires, not what the PHP implementation happens to do.

The client side cryptography is implemented independently with `hashlib`,
`hmac` and the `cryptography` package. Every AES-256-GCM verification and
decryption uses `cryptography.hazmat.primitives.ciphers.aead.AESGCM`, so the
hand written pure PHP AES-256-GCM implementation of the server is cross
validated against a separate, widely tested implementation.

## Running the suite

There is no system Python. Everything runs through `uv`. From the repository
root:

    uv run --python 3.13 --with pytest --with cryptography --with requests -- pytest tests-integration -v

The full run takes about three minutes, most of which is the queue expiry test
that has to outwait `QUEUE_TTL_SECONDS`.

To skip the tests that wait longer than the queue time to live:

    uv run --python 3.13 --with pytest --with cryptography --with requests -- pytest tests-integration -v -m "not slow"

To run only the latency measurements:

    uv run --python 3.13 --with pytest --with cryptography --with requests -- pytest tests-integration -v -m perf

The measured latencies are printed in a summary section at the end of every
run, whether or not the tests pass.

## Prerequisites

`tools/deploy-test.ps1 -Profile wizard-default` creates the separate default
fixture with `CHANGE_THIS_SERVER_SECRET` and identities `.2` through `.15`.
The default `integration` profile retains its original four custom access keys.
Use `-WebRoot`, `-BufferDirectory`, `-BaseUrl`, `-LargeBodyBaseUrl` and
`-ConfigurationPath` to isolate the fixture, then pass the generated file to
pytest using `--server-config path/to/fixture.json`. Header replacement rejects
anything other than exactly one expected secret, key-map and buffer block.
`test_default_profile.py` verifies authentication for all fourteen defaults.

- nginx and php-cgi are running. Start them with
  `powershell -ExecutionPolicy Bypass -File tools\start-test-server.ps1`.
- After changing `phpvpn.php`, redeploy with
  `powershell -ExecutionPolicy Bypass -File tools\deploy-test.ps1`.
- `curl.exe -s -i "http://127.0.0.1/phpvpn.php"` returns the HTML status page.

The deployment description is read from `tests-integration/server.json`, which
`tools/deploy-test.ps1` writes. It holds `baseUrl`, `serverSecret`,
`accessKeys`, `bufferDirectory` and `deployedFile`. Nothing is hardcoded in the
tests.

### The second endpoint for large bodies

The nginx deployment keeps the default `client_max_body_size` of one mebibyte,
which is above `MAX_REQUEST_BODY_BYTES` of 983040 bytes. A body above one
mebibyte is therefore rejected by nginx before PHP runs. To exercise the real
application limit, `tools/start-test-server.ps1` also starts the PHP built in
server on `http://127.0.0.1:8081/phpvpn.php`, serving the same deployed file
with `post_max_size` of eight mebibytes. The body limit tests use it and skip
when it is not reachable. Override it with:

    --large-body-url http://host:port/phpvpn.php

or with the environment variable `PHPVPN_LARGE_BODY_URL`.

## Layout

| File | Content |
|---|---|
| `phpvpn_client.py` | Independent protocol client: base64url, timestamps, HKDF, HMAC, AES-GCM, request builders, response parsing and independent verification of the `health` tag, the `info` ciphertext and the `data-mac`. |
| `conftest.py` | Configuration, client, server clock, published limits, queue cleanup and the latency summary. |
| `test_status_and_routing.py` | Status page, `HEAD`, unknown actions, disallowed methods, media types, forged proxy headers. |
| `test_headers.py` | Mandatory headers on every status, the generic error object, absence of leaks. |
| `test_health.py` | `auth-none`, the seven response fields, independent GMAC verification, malformed bodies. |
| `test_info.py` | `auth-semi`, decryption of the response, the plaintext contract, `lastSeen`, malformed input. |
| `test_time_skew.py` | The 60 s authentication window of both authenticated actions. |
| `test_data_transport.py` | Round trip, at most once delivery, `data-result` counters, `data-mac` coverage. |
| `test_data_errors.py` | Every documented `401` and `400` of the `data` action. |
| `test_limits.py` | Record count, auth line size and whole body size at `limit-1`, `limit` and `limit+1`. |
| `test_queue_capacity.py` | A full 16 MiB queue defers instead of failing, other destinations and the incoming drain are unaffected, the whole buffer is returned unpaginated. |
| `test_expiry.py` | Queue time to live, marked `slow`. |
| `test_concurrency.py` | Parallel polls, parallel senders, interleaved traffic. |
| `test_security.py` | Hostile input, path traversal, storage hygiene, the accepted replay risks. |
| `test_crypto_vectors.py` | Known answer tests of the verifier itself, no network. |
| `test_performance.py` | Latency measurement with a generous ceiling. |

## Conventions

- The mandatory headers of chapter 4.1 are asserted by
  `phpvpn_client.assert_protocol_headers`, which the client applies to every
  response it returns. A new test cannot forget the check. Responses produced
  by nginx before the request reaches PHP are recognised and exempt from the
  application header rules, but are still scanned for leaks.
- Tests are independent and do not rely on execution order. Tests that need an
  empty queue use the `clean_queues` fixture, which drains every configured
  client with ordinary authenticated polls. The suite never deletes anything
  from the buffer directory, it only reads the directory listing.
- Timestamps are derived from the measured server clock rather than the local
  clock, so a drifting test host cannot produce spurious `401` answers.

## Deliberate coverage gaps

- **The exact millisecond of the timestamp boundary.** Over real HTTP the
  deviation the server sees is the intended one minus the transport delay, so a
  single request cannot separate 60.000 s from 60.001 s. `test_time_skew.py`
  bisects the accepted window instead and requires it to be 60.000 s within
  100 ms, then probes 250 ms inside and outside the boundary.
- **A `503` response.** `queue_busy`, `queue_unavailable` and
  `server_misconfigured` need storage contention or a broken configuration
  that the suite must not create. The header helper asserts `Retry-After` on
  any `503` that does appear, and one test provokes contention and skips when
  the deployment stays healthy.
- **An empty or all zero `SERVER_SECRET`.** Chapter 16.3.15 needs a second
  deployment with a different configuration header, which the suite must not
  write. The normalization rule is covered against the client implementation
  in `test_crypto_vectors.py`, and the status page warning is asserted to
  match the configured secret of the running deployment.
- **Crash recovery of a batch.** Chapters 16.4.1 and 16.4.5 need a PHP worker
  to be killed between two renames, which is outside the reach of an HTTP
  client.
- **Writing to the buffer directory.** Chapters 10.6.3 and 10.6.4 describe the
  behaviour for a missing marker or an mtime in the future. The suite only
  reads the directory, so it verifies the 30 second rule through two
  observations over time rather than by manipulating the marker.
- **HTTPS and a reverse proxy.** Chapter 16.1.1 asks for the same result over
  HTTPS and behind a proxy. The deployment offers plain HTTP only. Forged
  `X-Forwarded-*` headers are covered.

Status fixtures cover enabled/disabled GET and HEAD and predictable/secure secrets. Default storage is `./phpvpn_data`, without automatic migration from `./phpvpn`. Deploy only `phpvpn.php`, without assets, and keep fixture storage outside the webroot.

Run `uv run --python 3.13 python tools/test-status-variants.py` for four isolated PHP HTTP variants. The test checks exact embedded HTML, removal of the entire warning panel, HEAD without a body, successful health/info/data with the page disabled, custom/default storage paths and no migration. It uses `D:/nginx/php8.5.9/php.exe` and disposes its temporary servers and directories.

## Masked communication contracts

`test_info_extensions.py` independently signs full-auth info extensions. It
checks A/B/C/D privacy, include-only reads, empty publication, signed optional
presence, wrong credentials/domain, replay-window semantics and body limits.
`tests-info-extensions.php` runs with `tests.php` and checks duplicate JSON members,
stored corruption, 252/253 records and temp-file cleanup. Run real storage races
and write/partial-write/flush fault injection, interrupted-writer cleanup and a
forced Windows rename failure without changing a deployment:

```powershell
uv run --python 3.13 python tools/test-info-storage.py --php D:/nginx/php8.5.9/php.exe
```

The storage test creates and removes only its temporary directory below `work`.
Concurrent publication can safely fail while a Windows reader holds a file. The
test requires complete old/new snapshots, rather than assuming every competing
rename succeeds. The HTTP handler reports failed publication as 503.

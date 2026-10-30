# macOS service end-to-end test

`run-service-e2e.sh` drives one isolated `phpvpn-service` instance on the
local Mac with the real CLI and the Wizard IPC probe against a private local
relay (plan-macos-v001 step 7). It is the macOS counterpart of
`tests/e2e/linux/run-service-e2e.sh`, reduced to what one host without network
namespaces can check.

## Prerequisites

- macOS 14 or later on Apple Silicon, root (`sudo`), because the service
  creates a `utun` interface, an address and a route.
- A build of the client with the e2e helpers (`PHPVPN_BUILD_MACOS_E2E`, on by
  default), for example `sh client/tools/macos/build.sh macos-release`. The
  script expects `apps/phpvpn-service`, `apps/phpvpn` and
  `tests/phpvpn-wizard-ipc-probe` inside `PHPVPN_BUILD_DIR`.
- `php` (8.x, the CLI built-in server) and `curl` on `PATH`. sudo keeps the
  caller's `PATH` only when it is passed on explicitly, see below.
- TCP port 18094 free on 127.0.0.1 (`PHPVPN_E2E_PORT` changes it) and no
  interface of the host using `10.99.99.2`.

## Running

```sh
sudo env "PATH=$PATH" PHPVPN_BUILD_DIR="$PWD/work/build/macos-release" \
    sh client/tests/e2e/macos/run-service-e2e.sh
```

| Variable | Default | Meaning |
|---|---|---|
| `PHPVPN_BUILD_DIR` | required | CMake build directory. |
| `PHPVPN_E2E_PORT` | `18094` | Port of the local php relay. |
| `PHPVPN_E2E_WORK` | `work/e2e-macos/<timestamp>-<pid>` | Logs and artifacts. |
| `PHPVPN_PHP` | `php` | PHP CLI used for the relay and for reading JSON. |

Every step prints `PASS:` or `FAIL:`, the run ends with
`RESULT: <n> passed, <m> failed` and exits 0 only without failures. A missing
prerequisite or a service that does not open its socket within 30 s is
`FATAL:` with exit 1.

The work directory keeps the service stdout (`service.log`), the copied
service file log (`service-logs/phpvpn-service.log`), the relay log, the CLI
transcript (`cli.log`), the Wizard probe output, the stored and returned
configuration documents, the status and diagnostics envelopes, and the
interface and route listings taken while connected.

## What is covered

1. `phpvpn-service --verify-tunnel` resolves the utun kernel control without
   creating an interface.
2. A fresh copy of `server/php/phpvpn.php` (wizard-default profile:
   `CHANGE_THIS_SERVER_SECRET`, default access keys) served by `php -S` with
   the buffer outside the web root.
3. `phpvpn-service --foreground` with `--config-dir`, `--state-dir`,
   `--runtime-dir` and `--log-dir` inside a fresh root owned directory
   `/private/tmp/phpvpn-e2e.XXXXXX`. The service's trusted directory walk
   refuses ancestors owned by another user, so the service directories cannot
   live inside a checkout under `/Users`; only the logs are kept in the work
   directory.
4. `phpvpn --socket <socket> status` answers, the state is `disconnected idle`.
5. The Wizard IPC probe: POSIX config path, explicit url, secret and access
   checks against the relay, finish with a `commitId`; the committed document
   is the file at the config path, `0600` root in a `0700` directory.
6. `set-config` with the document of the Linux test for `10.99.99.2` (one
   communication contract for `10.99.99.3`) and `get-config` byte for byte.
7. `connect`, then `connected running yes` within 30 s; `ifconfig` shows a
   `utunN` interface with `inet 10.99.99.2`, `netstat -rn -f inet` lists
   `10.99.99/24` through it and `route -n get 10.99.99.3` resolves to it.
8. `info` and `diagnostics` succeed while connected.
9. `disconnect`, `disconnected idle` within 30 s, and the address, the route
   and the utun interface are gone.
10. A second instance on the same runtime directory exits 1 with
    `service_already_running`; the first one still answers.
11. SIGTERM stops the service with exit code 0 within 45 s, the control socket
    is removed and the rotating file log exists.
12. The default route of the host is the same before and after the run.

## What is not covered

- No peer-to-peer traffic: there is only one client instance and no second
  host or namespace, so no ping or TCP transfer goes through the tunnel and the
  traffic counters are not compared.
- The production launchd daemon, the `.pkg` and the production paths
  (`/Library/Application Support/phpvpn`, `/private/var/run/phpvpn`,
  `/private/var/log/phpvpn`) are not used; `tools/macos/package-pkg.sh` and the
  package scripts are tested separately.
- No `kill -9` recovery, no `autoConnectOnStartup` restart, no DNS comparison
  and no tray.
- The relay runs on `127.0.0.1`, so the reachability of a remote relay outside
  the tunnel is not exercised.

The script changes nothing outside the private service directory, which it
removes at the end, and the work directory; the utun interface disappears with
the service process.

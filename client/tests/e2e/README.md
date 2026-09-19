# Explicit isolated SCM/Wintun end-to-end controller

`service_e2e.cpp` is an opt-in elevated integration executable, not a unit test.
The separate runner builds and prepares its environment, checks host conflicts,
owns the UAC interaction and captures cleanup/baseline evidence. Do not add this
target to unattended CTest.

The controller accepts exactly one argument:

- `--run-isolated`: actual SCM/service/Wizard/WinHTTP/Wintun lifecycle.
- `--run-isolated-with-udp`: the same lifecycle plus a real UDP roundtrip.

No argument or an unknown argument returns 2 before registration. The target
links `phpvpn-platform`, `phpvpn-core`, production `apps/tray/status_model.cpp`,
`advapi32` and `ws2_32`. Compile for AMD64 with the ordinary project options.

The sibling executable must be `phpvpn-e2e-service.exe`. The only service name
registered is `phpvpn-plan3-e2e-d6c8`, its binary command is the quoted sibling
path without arguments. CreateService must succeed as a new registration. An
existing service is never opened or adopted by the controller. Its only pipe is
`\\.\pipe\phpvpn-plan3-e2e-d6c8`, its adapter alias is
`PHPVPN Plan3 E2E d6c8`, and its configuration is the sibling
`isolated-data/config.json`. The injected host owns the fixed test adapter GUID.
The controller never writes a configuration directly and never adds/removes
addresses or routes itself. It uses the production NetConfig snapshot and alias
lookup to verify the exact adapter LUID. Its scope guard stops and deletes only
the service handle returned by its own successful CreateService call.

Before running, the runner must reject an existing test service/adapter/data
directory and conflicting `10.99.99.0/24` addresses or routes, preserve and compare
the real service/config and global routes/DNS baseline, and make a genuine
isolated default-profile PHP relay available at
`http://127.0.0.1:18093/phpvpn.php`. The controller verifies the live relay before
registering anything. Errors or unavailability produce a failure, not a skip.

Lifecycle assertions cover SCM RUNNING without a config, disconnected status,
missing classification, failed Connect without creating config, actual strict
Wizard IPC and three explicit Next operations, confirmed Finish, identical
commit receipt replay, close without implicit Connect, actual connected Wintun
IP/on-link route, Disconnect acceptance below 1000 ms and completion below
5000 ms, reconnect, SCM stop below 5000 ms, then restart without automatic
connection or changes to the published bytes. Network snapshots must preserve
the original default route/DNS, and the test adapter's addresses/routes must
disappear on teardown. The outer runner additionally verifies adapter deletion
and all host baselines.

After that clean SCM restart, both modes reconnect and exercise one deliberate
process crash. The controller gets a nonzero PID from its own newly created
service handle, opens it with only PROCESS_TERMINATE and
PROCESS_QUERY_LIMITED_INFORMATION, verifies QueryFullProcessImageName against
the exact normalized sibling `phpvpn-e2e-service.exe` path using ordinal Windows
case-insensitive comparison, and rechecks the same RUNNING service PID before
TerminateProcess. The process handle remains pinned throughout this sequence.
No production process name, arbitrary PID argument or process enumeration is
accepted. Both actual process exit and SCM STOPPED must occur within five seconds.

Before the crash, the production OwnershipJournal parser must confirm the exact
test alias, GUID, LUID, `.2/24` address and on-link route, with applied flags.
The abrupt exit must preserve this journal and the config bytes. After restart,
the service must be disconnected/idle with a new serviceInstanceId, unchanged
config and no ownership journal, address or route on the original exact LUID.
The test adapter alias must also be absent. A further real Connect/Disconnect
proves that the recovered backend is usable. Failures remain failures and fixture
evidence is left to the runner, never replaced with a synthetic recovery result.

UDP mode binds a real Windows UDP socket to the service's `.2` address and sends
one nonce to `.3`. A separate production NetworkWorker authenticates as `.3`
through CNG/WinHTTP/PHP and decrypts the actual Wintun packet. It validates the
source, destination, UDP ports and exact nonce, swaps addresses/ports, repairs
the IPv4 checksum and encrypts the return packet with the generated `.2/.3`
contract key. The original Windows socket must receive the exact nonce, and
the service's packet counters must increase in both directions. A valid zero
UDP checksum is used for the IPv4 reply. Firewall or routing failure is reported
as failure without changing firewall policy. This peer creates no second adapter.

UDP mode then observes ten seconds without further VPN payload. It records only
monotonic elapsed time, observed poll gaps and counters, rejects unexpected packet
activity, and requires the idle service to leave fast polling. A second real UDP
roundtrip must restore fast polling. IPC sampling and OS scheduling add tolerance
to these observed gaps. The exact 5000 ms boundary is covered separately by the
deterministic runtime polling tests.

All operation/poll waits have finite deadlines, and a failed Next is never
automatically retried. The outer runner provides a final process watchdog.
Cleanup errors are reported explicitly. The controller leaves fixture files to
the runner so it can retain failure evidence or delete its verified temporary
data after success. No result is asserted merely because this source compiled.
Multiuser, sleep/resume and delayed DNS are not tested by this SCM target.
Delayed DNS is now independently verified by the opt-in process-local resource
target. See [resource report](../implementation-plan3-resource-report.md) for
real maximum IPC RSS, held DNS queries, HTTP lock contention and reproduction.

## Extended info contract coverage

The opt-in `phpvpn-contract-tests` target uses an isolated `wizard-default` PHP
fixture selected by `PHPVPN_CONTRACT_URL`. Its full-auth test publishes matching
A/B masks through CNG and WinHTTP, verifies the resulting model, withdraws a
contract with an empty list and confirms standard info compatibility. The
service-operation test calls `fetchServerInfoOnce`, counts exactly one config
read per action, changes the stored local key and verifies fresh comparison
hashes with no Connect, data polling, configuration write or tunnel operation.
Python `tests-integration/test_info_extensions.py` covers A/B/C/D filtering.
These contract tests do not claim a real four-checkpoint Wintun connection.

## Local flow scheduling regression

The native peer uses the same 16-bucket DirectionQueues with an injected clock,
HMAC flow hasher and direction-specific drop counters. Terminal packet reads
use the shared DRR/lease path. Default queue budgets are 128 MiB per direction,
peer minima are 256 KiB, and local packet age is 5000 ms. No queue metadata is
sent over the wire. The isolated UDP test remains a real Wintun/PHP regression.
Deterministic bulk/interactive fairness, expiry, rollback, partial ACK and
concurrent admission are tested separately in the production-component
`flow_integration`, `tunnel_queues`, `queue_drr`, `queue_expiry` and
`queue_concurrency` suites of phpvpn-client-tests.

## Automatic startup Connect (2026-09-19)

The isolated SCM lifecycle now restarts its private service with an explicit
`autoConnectOnStartup: true` configuration and requires connected status without
an IPC Connect request. A true but incomplete configuration must leave SCM RUNNING
and IPC available with error status. Restoring false must restart disconnected.
The legacy missing-field and malformed-document cases remain in the lifecycle.
The malformed document now exposes error status without initiating Connect.
Production service, default routes and DNS are covered by the runner cleanup
oracle. This does not test the production installer, machine Run/logon, reboot,
or multiple interactive users.
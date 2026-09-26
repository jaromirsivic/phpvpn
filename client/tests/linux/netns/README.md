# Privileged network namespace tests

`phpvpn-netns-tests` exercises the Linux TUN driver (`tunnel_tun.cpp`) and the
rtnetlink network configuration (`netconfig_netlink.cpp`) against the real
kernel, plan-v001 WP04. The tests create TUN and dummy interfaces, addresses,
routes and bind mount a file over `/etc/resolv.conf`, so they need root and must
never run in the host namespaces.

## Running

```sh
cmake --preset linux-debug && cmake --build --preset linux-debug
sudo sh tests/linux/netns/run-netns-tests.sh ../work/build/linux-debug
sudo sh tests/linux/netns/run-netns-tests.sh ../work/build/linux-debug NetnsTunnel.Recovery
```

The runner executes the binary in `unshare --net --mount --propagation private`,
remounts sysfs for the new namespace and brings `lo` up. Everything the tests
create disappears with the namespace. The binary itself refuses to run unless it
is root, `PHPVPN_NETNS_ISOLATED=1` is set and both its network and mount
namespaces differ from those of PID 1.

Requirements: root (`sudo`), util-linux `unshare`, iproute2 `ip`, the `tun` and
`dummy` kernel modules. On Ubuntu 24.04 unprivileged user namespaces are
restricted by AppArmor, therefore the runner uses real root. Inside a container
the tests need `--privileged` (for `/dev/net/tun` and mounts).

The binary is built by default (`PHPVPN_BUILD_PRIVILEGED_TESTS=ON`) but is not
registered with CTest, because CTest cannot provide root and the namespace.

## Coverage

- interface creation: exact name `phpvpn`, ownership mark
  `phpvpn-owner:<GUID>` in ifalias, MTU 1500, `addrgenmode none` and no IPv6
  address, IFF_UP, non-persistent, identity `(tag << 32) | ifindex`;
- address with `noprefixroute`, one route `proto static scope link metric 5`,
  exact "already exists" and "not found" handling, no change of foreign
  identities;
- raw socket ping to a peer of `vpnCidr` appears in `receive`, a written echo
  reply reaches the socket; oversized packets are counted and not delivered;
- `signalStop` and `endSession` wake a blocked `receive`, `send` is cancelled;
- preflight conflicts (foreign address, foreign route in any table) and a
  foreign interface named `phpvpn`, which stays untouched;
- `sameDefaultRouteAndDns` on a change of the default route and of
  `/etc/resolv.conf` (bind mount in the private mount namespace);
- recovery after `kill -9` of a process holding the TUN descriptor;
- ifindex recycling by a foreign interface does not recreate the identity;
- a persistent foreign `phpvpn` fails closed;
- `removeOwnedPhantomInterface` removes only a persistent, detached, root owned
  TUN interface with matching name, mark and identity;
- the whole `TunnelSession` start, traffic and stop with a temporary journal.

Not covered: the bounded `EAGAIN` wait of `send`, because a TUN write practically
never reports a full queue and the descriptor is private to the driver.

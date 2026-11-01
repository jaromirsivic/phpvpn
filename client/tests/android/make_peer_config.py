"""Builds the configuration document of the emulator for the WP11 peer tests.

usage: make_peer_config.py <peer config.json> <own ip> <server url> <output file> [runtime name=value ...]

The peer is a desktop client (Windows C:\\ProgramData\\phpvpn\\config.json or
Linux /etc/phpvpn/config.json) connected to the same relay. The emulator gets
the same server secret, vpnCidr and HTTP headers, its own virtual IP with the
default access key of the wizard-default relay profile ("<octet hex>" * 32,
the relay must use those keys) and one communication contract with the peer
whose payload key is the peer's contract key for <own ip>. Nothing is printed
except the two addresses, so no key reaches a log. Optional runtime members
(for example maxQueuedPacketAgeMs=600000 for T-A21) go into "runtime".
"""
import json
import sys


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    peer_path, own_ip, server_url, output = argv[:4]
    runtime = {}
    for item in argv[4:]:
        name, value = item.split("=", 1)
        runtime[name] = int(value)
    with open(peer_path, encoding="utf-8-sig") as handle:
        peer = json.load(handle)
    master = peer["masterSetup"]
    peer_ip = master["virtualIp"]
    contract = peer.get("communicationContracts", {}).get(own_ip)
    if not contract or "payloadKey" not in contract:
        print(f"the peer {peer_ip} has no communication contract for {own_ip}")
        return 1
    octet = int(own_ip.rsplit(".", 1)[1])
    document = {
        "masterSetup": {
            "serverSecret": master["serverSecret"],
            "serverUrl": server_url,
            "httpHeaders": master.get("httpHeaders", ""),
            "trustAnyTlsCertificate": master.get("trustAnyTlsCertificate", True),
            "useDefaultServerSecretInWizard": master.get("useDefaultServerSecretInWizard", True),
            "vpnCidr": master["vpnCidr"],
            "virtualIp": own_ip,
            "autoDetectIpAddressInWizard": False,
            "accessKey": f"{octet:02x}" * 32,
            "useDefaultAccessKeyInWizard": True,
            "autoConnectOnStartup": False,
        },
        "communicationContracts": {peer_ip: {"payloadKey": contract["payloadKey"]}},
    }
    if runtime:
        document["runtime"] = runtime
    with open(output, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(document, handle, indent=2)
        handle.write("\n")
    print(f"own {own_ip}, peer {peer_ip}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

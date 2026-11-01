#!/system/bin/sh
# T-A04 on hardware (shell uid 2000 = "another app"): routes, DNS, internet TCP/HTTP, LAN, IPv6.
# Usage: sh other_traffic.sh [relay host] [peer VPN address]   (both come from the host script, see env.sh)
RELAY=${1:-}; PEER=${2:-10.99.99.2}
echo "== routes"
for d in 8.8.8.8 1.1.1.1 $RELAY $PEER; do echo "$d -> $(ip route get $d 2>&1 | head -1)"; done
echo "== dns (getaddrinfo via ping -c1)"
for h in www.google.com example.com cloudflare.com; do r=$(ping -c 1 -W 3 $h 2>&1 | head -2 | tr '\n' ' '); echo "$h: $r"; done
echo "== tcp 443 to the internet"
for d in 1.1.1.1 8.8.8.8; do toybox nc -z -w 5 $d 443; echo "$d:443 rc=$?"; done
echo "== http example.com"
sh /data/local/tmp/phpvpn-http_get.sh example.com 80 /
echo "== lan relay (outside the tunnel)"
if [ -n "$RELAY" ]; then sh /data/local/tmp/phpvpn-http_get.sh $RELAY 80 /phpvpn.php; else echo "no relay host given"; fi
echo "== ipv6"
ip -6 addr show scope global 2>&1 | grep inet6 || echo "no global IPv6 address"
ip -6 route get 2001:4860:4860::8888 2>&1 | head -1
echo "== dns servers of the networks"
dumpsys connectivity 2>/dev/null | grep -o "DnsAddresses: \[[^]]*\]" | sort -u

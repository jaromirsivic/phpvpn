#!/system/bin/sh
# Usage: sh http_get.sh HOST [PORT] [PATH]  - one HTTP/1.0 GET through toybox nc, prints the status line and the
# time from start to the server closing the connection (whole response). toybox nc quits at stdin EOF, so stdin
# stays open with a sleep. mksh arithmetic is 32-bit: only the last 9 digits of the millisecond clock are used.
H=$1; P=${2:-80}; U=${3:-/phpvpn.php}
O=/data/local/tmp/phpvpn-http.out
ms() { t=$(date +%s%3N); echo ${t:4}; }
t0=$(ms)
(printf 'GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n' "$U" "$H"; sleep 8) \
  | (toybox nc -w 5 "$H" "$P" > $O 2>&1; echo $? > $O.rc; ms > $O.t)
t1=$(cat $O.t)
echo "rc=$(cat $O.rc) ms=$(( t1 - t0 )) bytes=$(wc -c < $O) $(head -1 $O | tr -d '\r')"
rm -f $O $O.rc $O.t

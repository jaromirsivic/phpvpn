"""TCP echo helper of the WP11 peer tests (peer-traffic-tests.sh).

server <bind ip> <port> [seconds]
    Echo server on the peer side. A connection starts with an ASCII line
    "<length>\\n"; the server echoes exactly <length> bytes and closes, so a
    plain `nc` on the emulator ends without an idle timeout. Without the
    header line (first byte not a digit) everything is echoed until EOF.
    Stops after [seconds] (default 600).
udp-sink <bind ip> <first port> <ports> [seconds]
    Receives and discards UDP on <ports> consecutive ports (T-A21 load), so
    the peer answers no ICMP port unreachable that would stop the senders.
    Prints the received bytes per port at the end.
client <ip> <port> <bytes> [timeout]
    Sends <bytes> random bytes to an echo server (`nc -L -p <port> cat` on the
    emulator), reads them back, verifies them and prints
    "echo ok bytes=<n> seconds=<s> kib_per_s=<r>" or "echo FAIL ...".
"""
import os
import socket
import sys
import threading
import time


def serve(bind, port, seconds):
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((bind, port))
    listener.listen(8)
    listener.settimeout(1.0)
    deadline = time.monotonic() + seconds
    print(f"echo server on {bind}:{port}", flush=True)
    while time.monotonic() < deadline:
        try:
            connection, peer = listener.accept()
        except socket.timeout:
            continue
        threading.Thread(target=handle, args=(connection, peer), daemon=True).start()


def handle(connection, peer):
    total = 0
    with connection:
        connection.settimeout(120)
        first = connection.recv(65536)
        if not first:
            return
        remaining = None
        if first[:1].isdigit() and b"\n" in first:
            header, first = first.split(b"\n", 1)
            remaining = int(header)
        while True:
            if first:
                chunk = first if remaining is None else first[:remaining]
                connection.sendall(chunk)
                total += len(chunk)
                if remaining is not None:
                    remaining -= len(chunk)
                    if remaining <= 0:
                        break
            first = connection.recv(65536)
            if not first:
                break
        try:
            connection.shutdown(socket.SHUT_WR)
        except OSError:
            pass
    print(f"echoed {total} bytes to {peer[0]}", flush=True)


def client(ip, port, size, timeout):
    payload = os.urandom(size)
    received = bytearray()
    started = time.monotonic()
    with socket.create_connection((ip, port), timeout=timeout) as connection:
        connection.settimeout(timeout)

        def send():
            connection.sendall(payload)

        sender = threading.Thread(target=send, daemon=True)
        sender.start()
        while len(received) < size:
            chunk = connection.recv(65536)
            if not chunk:
                break
            received.extend(chunk)
        sender.join(timeout)
    seconds = time.monotonic() - started
    rate = size / 1024 / seconds if seconds > 0 else 0
    if bytes(received) == payload:
        print(f"echo ok bytes={size} seconds={seconds:.2f} kib_per_s={rate:.1f}")
        return 0
    print(f"echo FAIL sent={size} received={len(received)} seconds={seconds:.2f}")
    return 1


def udp_sink(bind, first, count, seconds):
    sockets = []
    for port in range(first, first + count):
        sink = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sink.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
        sink.bind((bind, port))
        sink.setblocking(False)
        sockets.append(sink)
    received = [0] * count
    deadline = time.monotonic() + seconds
    print(f"udp sink on {bind}:{first}-{first + count - 1}", flush=True)
    import selectors
    selector = selectors.DefaultSelector()
    for index, sink in enumerate(sockets):
        selector.register(sink, selectors.EVENT_READ, index)
    while time.monotonic() < deadline:
        for key, _ in selector.select(timeout=1.0):
            try:
                while True:
                    received[key.data] += len(key.fileobj.recv(65536))
            except BlockingIOError:
                pass
    print(f"udp sink received {sum(received)} bytes {received}", flush=True)


def main(argv):
    if len(argv) >= 3 and argv[0] == "server":
        serve(argv[1], int(argv[2]), int(argv[3]) if len(argv) > 3 else 600)
        return 0
    if len(argv) >= 4 and argv[0] == "udp-sink":
        udp_sink(argv[1], int(argv[2]), int(argv[3]), int(argv[4]) if len(argv) > 4 else 600)
        return 0
    if len(argv) >= 4 and argv[0] == "client":
        return client(argv[1], int(argv[2]), int(argv[3]), float(argv[4]) if len(argv) > 4 else 60)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

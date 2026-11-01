"""TCP proxy in front of the host relay that can make it unreachable (WP11).

usage: relay_block_proxy.py <listen port> <target port> <block file> [target host]

Listens on 127.0.0.1:<listen port> (the emulator reaches it as
10.0.2.2:<listen port>) and forwards every connection to
<target host>:<target port> (default 127.0.0.1, the nginx relay). While
<block file> exists, new connections are closed at once and open ones are cut,
so the relay looks unreachable to one emulator only, without stopping nginx
for every other client. service-lifecycle-tests.sh takes the block file as
its fourth argument (T-A16 b to d). When the upstream side ends, the proxy
waits 200 ms before half-closing towards the client (WP04 finding 2 for
`php -S`, harmless for nginx).
"""
import os
import socket
import sys
import threading
import time


def pump(src, dst, delay, block_file):
    try:
        src.settimeout(1.0)
        while True:
            if os.path.exists(block_file):
                break
            try:
                data = src.recv(65536)
            except socket.timeout:
                continue
            if not data:
                break
            dst.sendall(data)
    except OSError:
        pass
    if delay:
        time.sleep(0.2)
    try:
        dst.shutdown(socket.SHUT_WR)
    except OSError:
        pass


def handle(client, target, block_file):
    if os.path.exists(block_file):
        client.close()
        return
    try:
        server = socket.create_connection(target)
    except OSError:
        client.close()
        return
    a = threading.Thread(target=pump, args=(client, server, False, block_file), daemon=True)
    b = threading.Thread(target=pump, args=(server, client, True, block_file), daemon=True)
    a.start()
    b.start()
    a.join()
    b.join()
    client.close()
    server.close()


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    listen_port, target_port, block_file = int(argv[0]), int(argv[1]), argv[2]
    target = (argv[3] if len(argv) > 3 else "127.0.0.1", target_port)
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", listen_port))
    listener.listen(64)
    print(f"proxy 127.0.0.1:{listen_port} -> {target[0]}:{target[1]}, block file {block_file}", flush=True)
    while True:
        connection, _ = listener.accept()
        threading.Thread(target=handle, args=(connection, target, block_file), daemon=True).start()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

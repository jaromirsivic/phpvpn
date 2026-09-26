#!/usr/bin/env python3
"""Userspace TCP <-> Unix socket bridge for the installed service check.

The relay listens on 127.0.0.1 in the main network namespace. A peer client in
a private network namespace reaches it through a Unix socket in the file
system, which crosses namespaces, so no interface, address or route has to be
added to the main namespace.

  tcp_unix_bridge.py unix-to-tcp UNIX_PATH HOST:PORT    (main namespace)
  tcp_unix_bridge.py tcp-to-unix HOST:PORT UNIX_PATH    (peer namespace)
"""
import os
import socket
import sys
import threading


def pump(source, target):
    try:
        while True:
            data = source.recv(65536)
            if not data:
                break
            target.sendall(data)
    except OSError:
        pass
    finally:
        try:
            target.shutdown(socket.SHUT_WR)
        except OSError:
            pass


def serve(listener, connect):
    while True:
        client, _ = listener.accept()
        try:
            upstream = connect()
        except OSError:
            client.close()
            continue
        for a, b in ((client, upstream), (upstream, client)):
            threading.Thread(target=pump, args=(a, b), daemon=True).start()


def tcp_address(text):
    host, port = text.rsplit(":", 1)
    return host, int(port)


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ("unix-to-tcp", "tcp-to-unix"):
        print(__doc__, file=sys.stderr)
        return 2
    mode, first, second = sys.argv[1:]
    if mode == "unix-to-tcp":
        if os.path.exists(first):
            os.unlink(first)
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(first)
        os.chmod(first, 0o600)
        address = tcp_address(second)

        def connect():
            upstream = socket.create_connection(address, timeout=10)
            upstream.settimeout(None)
            return upstream
    else:
        listener = socket.create_server(tcp_address(first))
        path = second

        def connect():
            upstream = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            upstream.connect(path)
            return upstream
    listener.listen(64)
    serve(listener, connect)
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Host -> phone TCP: connect to the echo listener, send a line, wait for the echo; repeat N times."""
import socket, sys, time

host, port, n = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]) if len(sys.argv) > 3 else 5
for i in range(n):
    t0 = time.perf_counter()
    try:
        s = socket.create_connection((host, port), timeout=10)
        t1 = time.perf_counter()
        msg = f"phpvpn-probe-{i}\n".encode()
        s.sendall(msg)
        got = b""
        while not got.endswith(b"\n"):
            chunk = s.recv(100)
            if not chunk:
                break
            got += chunk
        t2 = time.perf_counter()
        s.close()
        print(f"{i}: connect {1000*(t1-t0):.0f} ms, echo {1000*(t2-t1):.0f} ms, ok={got == msg}")
    except OSError as e:
        print(f"{i}: error {e} after {1000*(time.perf_counter()-t0):.0f} ms")
    time.sleep(1)

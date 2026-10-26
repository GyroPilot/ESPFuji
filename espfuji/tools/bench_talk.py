"""bench_talk.py  24-Oct-2026: drive the ESPFuji BENCH firmware over its USB serial port from the banana.
usage: py -3.12 bench_talk.py COM7 [request ...]      default requests: HELLO, WIFI, ECHO ping, OPEN, READ, CLOSE
Resets the board (DTR/RTS), prints its boot lines for 12 s, then sends each request and prints what comes back.
Never prints espfuji.ini (the firmware does not echo it either)."""
import sys, time, serial

port = sys.argv[1]
args = sys.argv[2:]
wait = 12                                   # seconds of boot lines to collect before the first request
if args and args[0] == "--wait":            # --wait N: give the Wi-Fi join N seconds first
    wait = int(args[1]); args = args[2:]
reqs = args or ["HELLO", "WIFI", "ECHO ping from the banana", "STATUS",
                        "OPEN http://fujinet.online/8bitnews/news.php?t=cr&ps=76x6&c=top&l=5", "READ 0 300", "CLOSE"]
s = serial.Serial(port, 115200, timeout=0.2)
s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False       # reset pulse via the CP2102 (EN)
t0 = time.time(); buf = b""
while time.time() - t0 < wait:
    buf += s.read(512)
for line in buf.decode("latin1").splitlines():
    if line.strip():
        print("boot | " + line.rstrip())
for r in reqs:
    s.reset_input_buffer()
    s.write((r + "\n").encode()); s.flush()
    t0 = time.time(); got = b""
    while time.time() - t0 < 45:
        got += s.read(512)
        if b"reply seq" in got and got.endswith(b"\n") or b"(no reply" in got:
            break
    print(">> " + r)
    for line in got.decode("latin1").splitlines():
        if line.strip():
            print("   " + line.rstrip())
s.close()

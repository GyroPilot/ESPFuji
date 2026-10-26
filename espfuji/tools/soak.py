"""soak.py  25-Oct-2026: overnight soak of ESPFuji on the external ESProFile over its USB serial port (COM7).
Opens the port WITHOUT resetting the ESP32 (DTR and RTS asserted before open), then runs N cycles of
OPEN <url> / READ 0 64 / CLOSE through the drive's mailbox path, with HELLO (heap) every 10 cycles.
Writes one line per cycle to soak_<date>.log and a summary at the end.  Read-only for the drive's card.
usage: py -3.12 soak.py COM7 [cycles] [pause_s]
"""
import sys, time, re, serial

port = sys.argv[1]
cycles = int(sys.argv[2]) if len(sys.argv) > 2 else 600
pause = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0
import os
URLS = os.environ.get("SOAK_URLS", "http://192.168.1.21:8080/").split()   # our own server: never soak a community server
log = open(time.strftime("soak_%Y%m%d_%H%M.log"), "w", encoding="utf-8")

s = serial.Serial()
s.port, s.baudrate, s.timeout = port, 115200, 0.2
s.dtr = True
s.rts = True
s.open()


def ask(req, wait=45):
    s.reset_input_buffer()
    s.write((req + "\n").encode())
    s.flush()
    t0 = time.time()
    got = b""
    while time.time() - t0 < wait:
        got += s.read(512)
        m = re.search(rb"reply seq \d+, \d+ bytes: ([^\r\n]*)\r?\n", got)
        if m:
            return m.group(1).decode("latin1"), time.time() - t0
    return None, time.time() - t0


def say(line):
    print(line, flush=True)
    log.write(line + "\n")
    log.flush()


ok = fail = 0
heap_first = heap_min = None
started = time.time()
for i in range(1, cycles + 1):
    url = URLS[i % 2]
    r_open, t_open = ask("OPEN " + url)
    r_read, _ = ask("READ 0 64") if r_open and r_open.startswith("OK") else (None, 0)
    ask("CLOSE")
    good = bool(r_open and r_open.startswith("OK") and r_read and not r_read.startswith("ERROR"))
    ok += good
    fail += not good
    line = "%s %4d %-5s %-34s %5.2fs %s" % (time.strftime("%H:%M:%S"), i, "ok" if good else "FAIL",
                                            url.split("//")[0] + "//", t_open, (r_open or "no reply")[:40])
    if i % 10 == 0 or not good:
        h, _ = ask("HELLO")
        m = re.search(r"heap (\d+)k/(\d+)k", h or "")
        if m:
            free, low = int(m.group(1)), int(m.group(2))
            heap_first = heap_first or free
            heap_min = low if heap_min is None else min(heap_min, low)
            line += "  heap %dk min %dk" % (free, low)
        else:
            line += "  HELLO: %s" % (h or "no reply")
    say(line)
    time.sleep(pause)

say("=== %d cycles in %.0f min: %d ok, %d failed; heap at start %sk, lowest seen %sk" %
    (cycles, (time.time() - started) / 60, ok, fail, heap_first, heap_min))
s.close()

# ESPFuji changes

Firmware versions are what HELLO prints ("ESPFuji 0.5.3"). The Lisa-side test tool has its own numbers (ESPFuji 0.2 REV C).

## 1.1b1 (beta, 30 October 2026): the Macintosh XL
- Mailbox protocol version 2, for a Lisa running MacWorks (a Macintosh XL). MacWorks writes the Mac File Manager's tag
  on the ProFile (file number, flags, logical block, date, and a checksum in byte 19), so the Lisa file ID that 1.0
  uses to find the mailbox is not there. A version 2 request carries its page index and the mailbox's page count; the
  drive answers on that block range only and keeps the checksum byte right. Version 1 (the Lisa Office System tools)
  is unchanged. New Mac application: ESPFuji XL 0.1d1 (`espfuji/macxl-tool`). Host-tested and run in an emulator;
  not yet on a real Macintosh XL.

## 1.0 (30 October 2026)
- The 0.5.3 code with the version string changed to 1.0; nothing else. Released after Alex's review of the fork and a
  day's idle soak of 0.5.3 on a second board (fetch and CLOCK every 15 minutes, 0 cut, 0 lost).

## 0.5.3 (29 October 2026, built; on the spare board from 29 Oct)
- `timezone=` in espfuji.ini (a POSIX TZ string, e.g. `EST5EDT,M3.2.0,M11.1.0`): CLOCK answers local time. Without it,
  CLOCK answers UTC as before. Checked on a board flashed from macOS: CLOCK gave 12:23 EDT at 12:23 EDT.

## 0.5.2 (28 October 2026; 8-hour idle soak passed; first complete LisaCheckers game through the ESProFile 29 Oct)
- A reply goes to the USB console only for a request typed there. 0.5.1 kept one "next reply to the console" flag,
  so a reply the Lisa was waiting for could be swallowed while somebody watched the console.
- A fetch with no IP address fails at once with "ERROR no wifi (no address)" and asks for a rejoin, instead of hanging
  on a connect. After 20 s without an address the board rejoins on its own.
- An idle board sends a small UDP keepalive to its gateway every 120 s, after an external board lost its address
  110 minutes into an idle evening (28 Oct).
- The body is read up to Content-Length (5929-byte pages used to arrive cut at 3840); a short body is flagged
  "truncated" in STATUS.

## 0.5.1 and 0.5 (27-28 October 2026)
- Crash fix in the page reader, CLOCK request (SNTP), modem sleep off so the board stays reachable, SCAN of
  networks, TRACE for the last fetch.

## 0.4 and earlier (October 2026)
- The mailbox: a Lisa file on the ProFile image (MAILBOX.DATA) that the Lisa writes requests into and the ESP32
  answers in, so the Lisa reaches the network through the drive it boots from. HELLO, WIFI, STATUS, OPEN, READ,
  CLOSE; the Lisa-side ESPFuji tool and LisaFuji's "through the ESProFile" route.

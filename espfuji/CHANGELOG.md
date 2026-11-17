# ESPFuji changes

Firmware versions are what HELLO prints ("ESPFuji 0.5.3"). The Lisa-side test tool has its own numbers (ESPFuji 0.2 REV C).

## 1.1.1 (17 November 2026): the memory fix
- **"connection refused" after a while on HTTPS pages is fixed.** The cause: the TLS library needs one large free
  block of memory for every secure connection, and the plain ESP32 had too little to spare, because a 64 KB file-copy
  buffer (used only by the Selector) sat in static memory all the time. It is now 8 KB, which gives TLS 56 KB more.
  Measured on one ESProFile over HTTPS, 620 fetches with each firmware, same script: 1.1 failed 5 times with "connection refused"
  (the lowest free memory 37 KB, as at the November show); 1.1.1 failed once, with a different TLS error while memory
  was healthy (the lowest free memory 92 KB).
  The release image itself (ESP32, sha256 2079efd0...) ran 60 more HTTPS fetches on 17 Nov: 59 fine, one handshake timeout, memory steady (lowest 92 KB).
- **Error messages say what happened**: a TLS handshake that runs out of time reads "ERROR tls handshake timed out
  (N ms)" and other TLS failures add the TLS layer's own words ("ERROR connection refused (tls -1 ...)") instead of a
  bare "connection refused". The handshake may take 15 s instead of 8 (slow hotspots).
- **Alex's ESProFile emulator 1.6 range fix** (his commit of 11 Nov 2026): a block number past the end of the image is
  refused; before, it could read or WRITE the block after the last one used. Emulator version 1.6.
- **Several Wi-Fi networks** in espfuji.ini: `ssid=`/`password=`, then `ssid2=`/`password2=` up to `ssid4=`. At
  power-up, and again on every fourth retry, the drive joins the first listed network it can see, so it can move between
  home and a hotspot without a card change. WIFI shows which one ("net 2/3"). **Only tried with one network listed so
  far**; one `ssid=` works exactly as before.
- Nothing changed for the Lisa or Mac tools: same mailbox, same requests, same answers.

## 1.1 (8 November 2026): the Macintosh XL
- The 1.1b3 firmware with the version string changed to 1.1; nothing else.
  Proven 12 Nov 2026 on a Lisa 2/10's internal ESProFile (a whole LisaCheckers game against the Macintosh XL over a travel hotspot) and on the Macintosh XL's internal ESProFile (HELLO 1.1, Wi-Fi at -46 dBm).
- With it, a Lisa running MacWorks (a Macintosh XL) uses the ESProFile too: ESPFuji XL 1.0 and the Mac XL applications
  FujiNet XL, Browser XL, Checkers XL and Salvo XL (github.com/GyroPilot, one repository each). The Lisa Office System
  tools (mailbox protocol version 1) work as with 1.0.
- Known limit: some large HTTPS files (tens of KB, e.g. from raw.githubusercontent.com) still come up short, and the
  next connection may be refused; timeapi.io refuses connections through the drive (LisaSolstice then uses the drive's
  own clock). News, Wikipedia, the space station, the weather, the LisaWeb gateway's pages and pictures arrive whole.

## 1.1b3 (beta, 3 November 2026)
- The page body is read every time round the loop (a TLS client decrypts the next record only inside read()), up to
  32 KB; TRACE reports the last failed read and the TLS error of the last short fetch.

## 1.1b2 (beta, 2 November 2026; found on a real Macintosh XL)
- MacWorks interleaves its logical blocks on the ProFile, so MAILBOX.DATA, in one piece to the Mac, is not a run of
  consecutive drive blocks: 1.1b1 could answer reads of other files' blocks with a mailbox page. A read now gets the
  reply only when the stored page is itself a mailbox page. A READ at offset 0 echoes 0 (ESPFuji XL 0.1d1/0.1d2
  stopped with "out of step").

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

# ESPFuji changes

Firmware versions are what HELLO prints ("ESPFuji 0.5.3"). The Lisa-side test tool has its own numbers (ESPFuji 0.2 REV C).

## 1.1.4 (24 November 2026): a guard for poor cables and faulty boards
- **The known issue of 1.1.3 is closed.** On a poor cable or a faulty board a control line can show one low sample
  when many data lines switch together. 1.1.3 left its protected loop on that sample, and a block could then be read
  one byte short. 1.1.4 looks again, up to four times, with the network core still parked: a line that is high again
  was a glitch, and the transfer goes on under protection.
- **A write whose command did not arrive as six bytes is refused.** The block is not stored (a damaged command may
  name another block); the drive answers with status bit 0 set and the Lisa Office System repeats the write. In
  STATUS, `cmdodd` counts such commands and `refused` the writes that were not stored.
- **Tested:** on a bench (one ESProFile reading and writing another, worst-case data, heavy Wi-Fi traffic and page
  fetches by the drive itself) more than 700,000 block transfers, none short and none refused; the same bench shows
  about 2 short reads in 1,000 with 1.1.3. A Lisa 2/10 with an internal ESProFile: a cold start and a session with a
  network game, about 3,300 transfers. A Macintosh XL with an internal ESProFile: Drive Test XL's Quick Test, 9,486
  reads and 2,767 writes. An external ESProFile (an ESP32 of chip revision 1.0) on its cable on a Lisa 2/10's 2-port
  parallel card, as a second disk: a tool copied onto it and compared byte for byte, 730 transfers. Nothing odd and
  nothing refused in any of them.
- **Not supported: starting (booting) a Lisa from an external ESProFile attached to a parallel card.** The Lisa
  shows error 85. Seen on a Lisa 2/10 with the 2-port card, with 1.1.2 as well as 1.1.4. Once the Lisa has started
  from its internal disk, the same ESProFile on the same card and cable works normally as a second disk. This is
  being looked into.
- The released code is the public "1.1.4 beta 1" with the version text changed.
- **For the plain ESP32 only**, like 1.1.3. The ESP32-S3 build has the write guards and the counters but NOT the
  parked core (and so not its glitch guard), and it has never run on hardware.
- Nothing changed for the Lisa or Mac programs: same mailbox, same requests, same answers. espfuji.ini is unchanged.

## 1.1.3 (23 November 2026): no more lost bytes
- **A block could lose a byte while the drive's own Wi-Fi was sending** (every version up to 1.1.2). When the ESP32's
  network core sends a frame, the disk interface's reads of the pin registers are delayed by about a microsecond.
  The emulator finds each strobe by polling the pin, so a delayed read could hide one. A block read by the Lisa then
  went out a byte short, and a block written by the Lisa could be STORED a byte short without the Lisa being told.
  Found on a Lisa 2/10 on 20 Nov 2026 (a catalog block with one byte missing: "You have no environments on the startup
  disk"). On a bench, one ESProFile reading another while it answered 50 pings a second, 1.1.2 gave 62 to 116 short
  reads in about 10,600.
- **The fix:** for the length of every strobe phase (the six command bytes, the 532 bytes of a write, the status and
  data of a read) the network core is parked with the IDF's cooperative stall and released at the phase's last byte,
  when the host ends the phase, or after about 30 ms without a strobe. The reaction to a strobe is the same as in
  Alex's loops, instruction for instruction. Same bench with 1.1.3: 52,982 reads and 42,356 writes at 50 pings a
  second and 26,502 reads at 200 a second, none short, none refused.
- **Write guard:** a block that does not arrive as exactly 532 bytes is not stored. The drive answers with status
  bit 0 set, which makes the Lisa Office System repeat the write.
- **The drive counts what it sees.** STATUS now ends with
  `disk wr N refused N rd N odd N last N init N cmdodd N quiet N early N timeouts N`: writes, writes refused by the
  guard, reads, reads the host did not take in full (and the index the last one stopped at), abandoned reads of the
  spare table (the Lisa Office System makes one or two at every start: normal), commands that were not six bytes,
  phases run with the network core parked (2 per read, 3 per write), phases the host ended early, phases released
  by the time limit. On a healthy drive refused, odd and cmdodd stay at 0. In the ESPFuji tool on the Lisa, S shows
  the line; on a Macintosh XL, Drive Test XL does.
- **Tested:** a Lisa 2/10 with an internal ESProFile: a 50-minute session with a network game (3,523 reads, 2,588
  writes, 784 requests served) and a restart (2,247 reads, 224 writes), nothing odd or refused. A Macintosh XL
  (MacWorks Plus II, System 7.5.5, 16 MHz XLerator, internal ESProFile): Drive Test XL's Long Test twice, 50 passes
  each of 256 KB written, read back and compared while the drive fetched a page per pass: 0 bytes differ, about
  60,000 block transfers per run, nothing odd or refused.
- **For the plain ESP32 only** (the chip on the ESProFile boards). The ESP32-S3 build keeps Alex's loops as in 1.1.2:
  it has the write guard and the counters but NOT the parked core, and it has never run on hardware.
- **Known issue, fix coming in 1.1.4** (found by the release check on 23 Nov): on a poor cable or a faulty board a control
  line can glitch for one sample when many data lines switch together; the parked loop is then left early and a block can
  be read one byte short (about 2 reads in 1,000 on a deliberately poor bench link with worst-case data; none on the
  internal drives of the Lisa 2/10 and the Macintosh XL). Earlier versions and the original firmware end the transfer
  at such a glitch. In STATUS, `timeouts` higher than `init` shows it.
- **With this release for the Macintosh XL:** Drive Test XL 1.0 (new) and ESPFuji XL 1.1 (adds Network > Drive Status).
- Nothing changed for the Lisa or Mac programs: same mailbox, same requests, same answers. espfuji.ini is unchanged.
  If you used 1.1.2 or earlier on a disk you care about: update, keep any older backup of the disk image, and repair
  the disk (Office System 1 diskette) or go back to the backup if the Lisa reports damage.

## 1.1.2 (20 November 2026): the disk is served at once
- **A Lisa that powers up together with its ESProFile could fail to start from it** (error 10726 on a Lisa 2/10 on
  20 Nov 2026; a restart without switching off then worked). The cause: 1.1.1 scanned for its Wi-Fi network (about 3 s)
  and waited up to 8 s for the join BEFORE it began serving the disk, so for up to 11 seconds after "ESProFile is
  ready!" the drive answered nothing. 1.1.2 serves the disk at once; the scan and the join run on the network side
  while the disk works. On a bench (one ESProFile reading another through its start-up): first block answered 0.4 s
  after "ready" instead of about 11 s; 95 reads, none failed.
- **Wi-Fi starts 60 seconds after power-up, or as soon as a program asks for the network**, whichever comes first, so
  the radio is quiet while the Lisa starts. A program that asks in the first minute waits up to about 10 s for the join.
- **The network side no longer freezes when none of the listed networks is in range.** A scan from the network task
  never finished, because the Arduino core handled Wi-Fi events on the core the disk interface holds with interrupts
  off; they are now handled on the network core (`-DARDUINO_EVENT_RUNNING_CORE=0` in build_opt.h). Seen in test
  builds; 1.1.1 has the same code path at its fourth retry.
- **Why it last started:** the serial console shows "ESPFuji 1.1.2: started by power-on" (or brownout, panic, ...),
  and the new request BOOTS lists the last eight starts. The network side's messages (joining, "on ... as ...") now
  appear on the serial console too.
- Tested on a Lisa 2/10 (2 MB) with an internal ESProFile, powered by the Lisa: four cold starts, Wi-Fi joined, LisaFuji
  fetched pages. A Macintosh XL (internal ESProFile, updated with the web installer, 20 Nov 2026): starts from it,
  HELLO answers 1.1.2, Wi-Fi joined; only a few starts so far. **Not yet tried on a 1 MB Lisa or on the ESP32-S3.**
- Nothing changed for the Lisa or Mac tools: same mailbox, same requests, same answers. espfuji.ini is unchanged.

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

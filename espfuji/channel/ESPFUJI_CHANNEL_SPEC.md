<!-- Published 11 November 2026 from the working specification of 8 November with its sections 16 to 20, local folder names removed. Status: proposed; the drive side is PC-tested code in a private copy, the Lisa side runs in LisaEm, nothing has run on real hardware. -->

# ESPFuji channel $FFFEF8: the firmware-side specification, draft 0.2 (8 November 2026; section 16 added 9 November, section 17 on 9 November, section 18 on 10 November, section 19 on 10 November, section 20 on 11 November)

DRAFT for Tim, then for stepleton and Alex (LisaList2 topic 897). Nothing here is flashed; since 9 November the
drive side exists as code in a private copy and passes its PC tests: READ SECTION 16 WITH SECTIONS 3 TO 12, it
corrects them in a few places (one new rule: a client starts with a CANCEL); SECTION 17 (9 November night) is the Lisa
side as built and tested in LisaEm, ESPFDEMO REV D, with the write path proven. This is
the drive's half of the design in ESPFUJI_DRIVER_DESIGN_DRAFT_2026-11-01.md (same folder): what the ESP32
answers for each read and write of the channel block, byte by byte, and why each rule is there. The Lisa half
(ESPFDEMO REV D, the FnBus route fnViaDrv) is sketched in section 13 and, as built, described in section 17; the Office System tool that drives it (REV F/G, 10 November) in section 18.

Marks: VERIFIED = read in the source cited or seen in LisaEm; INFERRED = reasoned, not tested; PROPOSED = a
choice to agree on. Sources: Alex's emulator `sw/ESProFile/ESProFile_Emulator.ino` (the 1.1
tree), the mailbox `mailbox_core.h` and `ESProFile_FujiNet.ino` (same folder), Apple's `SOURCE-PROFILEASM`,
`source-PROFILE`, `SOURCE-HDISK`, SdFat `FatFile.cpp`
(`Documents/Arduino/libraries/SdFat/src/FatLib`), OPTION_A_FEASIBILITY_2026-11-01.md, the LisaEm runs of
5-8 November (DRIVER_PROGRESS_2026-11-05 to -08).

## 0. In one paragraph

ESPFuji 1.2 adds one reserved block, $FFFEF8, the CHANNEL. A Lisa program (through its own small driver and
Apple's ProFile driver), a Macintosh XL driver, or a bare-metal program writes a 512-byte REQUEST block to
$FFFEF8 and later reads $FFFEF8 back; the drive answers with a REPLY block that says "pending" or carries the
answer. The request texts are the ones the mailbox already serves (HELLO, WIFI, CLOCK, OPEN, READ, CLOSE and so
on), so the network code does not change; only the transport does. Nothing is written to the disk image, the
20-byte tag and the retry and sparing bytes carry nothing, every command is answered at bus speed, and the
file mailbox (protocol versions 1 and 2) stays as it is for programs that do not have a driver.

## 1. Why $FFFEF8 (VERIFIED, with one INFERRED line)

The drive matches reserved blocks on the exact three bytes of the command (Emulator lines 224-235, 308, 435,
457, 665, 706, 736). Who puts what on the cable to reach the drive's $FFFEF8:

| Sender | Remap of the low 4 bits before the cable | Must ask for | Mark |
|---|---|---|---|
| Lisa Office System through Apple's ProFile driver | INT1TAB[n] = 5n mod 16 (SOURCE-PROFILEASM 346, 363-373); on for every ProFile-type drive, off only for a Widget (source-PROFILE 271, 283-301) | $FFFEF8 (fixed point) | VERIFIED in LisaEm: $FFFEF9 went out as $FFFEFD, 5 Nov |
| Our driver ESPFDEMO REV C | undoes the remap itself (13n mod 16) so callers give the cable number | $FFFEF8 | VERIFIED 6 Nov |
| Bare-metal programs (lisa_io, the Selector) | none | $FFFEF8 | VERIFIED (OPTION_A, Q1) |
| MacWorks (Macintosh XL) | 13n mod 16: measured order 0, 13, 10, 7, 4, 1, 14 ... (mailbox_core.h 1.1b2 note) | $FFFEF8 (fixed point) | INFERRED: the order is measured, whether MacWorks's driver passes a block number this high is not known |

5n mod 16 and 13n mod 16 are inverse permutations (5 x 13 = 65 = 1 mod 16) and they share the fixed points
0, 4, 8 and C. So $FFFEF8 is the same number from every sender, which is the reason to prefer it to
stepleton's $FFFEFB (which the Lisa OS would have to ask for as $FFFEFF, the key/value block's number).
Neighbours in use: $FFFEFC rescue, $FFFEFD status, $FFFEFE files, $FFFEFF key/value, $FFFFFD commands,
$FFFFFF spare table. Free fixed points for later: $FFFEF4, $FFFEF0.

## 2. What travels on the cable (VERIFIED: Emulator 100-180, 198-240, 576-620, 622-720, 935-975)

The drive sees the 6-byte ProFile command: byte 0 = 0 read, 1 write, 2 write-verify (the emulator treats 1, 2
and 3 alike), bytes 1-3 = block number, byte 4 = retry count, byte 5 = sparing threshold.

- Read: the drive sends 4 status bytes, then 532 bytes: 20 tag bytes, then 512 data bytes
  (`blockData[0..3]` status, `readStatusOffset` = 4).
- Write: the host sends 532 bytes (tag, then data); the drive then sends 4 status bytes.
- Through Apple's driver the retry and sparing bytes are ALWAYS $0A and $03 (or $0B after DCONTROL 21;
  source-PROFILE 269-270, 400-406). The channel therefore IGNORES bytes 4 and 5 on reads and writes alike.
  Everything is in the 512 data bytes.
- Single-block commands only: a multi-block request through Apple's driver remaps each block number again
  (SOURCE-PROFILEASM 614, 933, 986), so the second block would not be $FFFEF8.

## 3. The channel block: the 512 data bytes (PROPOSED)

The same layout as a mailbox page (mailbox_core.h), with protocol version 3, so the Lisa tools' page code and
the firmware's `mb_parse` family are reused. All numbers big-endian.

| Bytes | Field | Request (host writes) | Reply (host reads) |
|---|---|---|---|
| 0-7 | signature | `LISAFUJI` | `LISAFUJI` |
| 8-9 | protocol version | 3 | 3 |
| 10-11 | sequence number | the client's own, never 0, increasing (wraps 30000 to 1 in FnBus) | the sequence number this reply answers; 0 when idle |
| 12-13 | kind | 1 REQUEST, 3 POLL, 4 CANCEL | 2 REPLY |
| 14-15 | payload length | 0..492 | 0..492 |
| 16-17 | client id | 1..65535 (section 8); 0 is never a client | the client this reply is for; 0 = nobody (idle identification) |
| 18-19 | flags / status | 0 (reserved, must be 0) | 0 IDLE, 1 PENDING, 2 READY, 3 REFUSED |
| 20-511 | payload | the request text, no terminator | the reply text or bytes (READ), or the reason (REFUSED), or the identification (IDLE) |

A write whose bytes 0-9 are not `LISAFUJI`, 0, 3, or whose length is over 492, or whose client id is 0, is
NOT a channel request: the drive ignores it (status bytes 0, nothing stored anywhere, a counter `chbad`
goes up). Firmware 1.1 and the stock firmware never see version 3 pages as mailbox pages: `mb_parse`
rejects versions other than 1 and 2 (mailbox_core.h 76-78), and a channel block never reaches the image
anyway (section 10).

Why the mailbox's bytes 16-19 change meaning: in versions 1 and 2 a reply echoes the block or the READ offset
there. The channel does not need the echo (the sequence number identifies the reply, and the Lisa driver
matches client and sequence), and it does need the client id and the status. The FnBus unit's reader must
not look for the echo on the driver route.

## 4. The exchange (PROPOSED)

1. The host writes a REQUEST block (kind 1, client C, sequence S, payload = the request text).
   The drive stores it in C's slot, marks it PENDING, and hands it to the network task. It answers the write
   with 4 zero status bytes at once; nothing waits for the network.
2. The host reads $FFFEF8. The drive answers FOR THE CLIENT OF THE MOST RECENT WRITE to the channel (request,
   poll or cancel), with:
   - PENDING, sequence S, no payload: the request is taken, the answer is not there yet;
   - READY, sequence S, payload = the answer;
   - REFUSED, sequence S, payload = the reason in words (section 7);
   - IDLE, client C, sequence 0: nothing outstanding for C (never asked, or cancelled, or the slot was
     recycled); payload = the identification text of section 10.
   A read is IDEMPOTENT: it does not consume the reply. The reply stays until the same client writes a new
   request or a cancel, or the slot is recycled (section 8). (The mailbox has the same rule since 24 Oct: a
   read the OS makes on its own cannot swallow the reply, ESProFile_FujiNet.ino 177-179.)
3. The host polls by reading again after a short sleep. The Lisa driver's own wait is the application's
   affair (FnBus waits up to 40 s today, `fnMbWait`; an OPEN ends within 30 s on the drive, `MB_FETCH_MS`).
4. A POLL block (kind 3, client C, sequence S, no payload) is a write that changes nothing but "the most
   recent client": the following read answers for C. A client alone on the drive never needs one. The Lisa
   driver writes one before a read only when the previous channel write was for another client (two tools
   with requests outstanding).
5. A CANCEL block (kind 4, client C) forgets C's request and reply and frees the slot; a request already
   being worked on finishes on the drive and its reply is dropped. The Lisa driver sends one for each of
   its clients when it is taken out (CDKILL / Put Away), so no stale reply lingers for the next session.
6. A read before any write since power-up answers IDLE for client 0 with the identification text. That is
   the PROBE (section 10).

Worked example, HELLO from client 1, sequence 7 (data bytes 0-23, hex; the rest 0):

    request  4C 49 53 41 46 55 4A 49  00 03  00 07  00 01  00 05  00 01  00 00  48 45 4C 4C 4F ("HELLO")
    pending  4C 49 53 41 46 55 4A 49  00 03  00 07  00 02  00 00  00 01  00 01
    ready    4C 49 53 41 46 55 4A 49  00 03  00 07  00 02  00 3E  00 01  00 02  45 53 50 46 75 6A 69 20 31 2E 32 ... ("ESPFuji 1.2 up 83s connected 192.168.1.60 req 1 heap 150k/140k", 62 bytes)
    idle     4C 49 53 41 46 55 4A 49  00 03  00 00  00 02  00 24  00 00  00 00  "ESPFuji 1.2 channel 3 slots 4 body 1" (36 bytes)

## 5. The 20-byte tag and the 4 status bytes (VERIFIED, from the LisaEm runs and Apple's source)

- REPLY TAG: ALL 20 BYTES ZERO. Reasons: (a) tag byte 6 bit 7 set makes Apple's driver demand that the XOR of
  all 532 bytes is 0, else checksum error and up to 4 retries (SOURCE-PROFILEASM 591-598); (b) tag byte 2
  bits 7 and 6 set come back as warnings -626 / -625 and the request completes "unsuccessfully" with data
  (SOURCE-HDISK 263-269): seen in LisaEm on 5 and 6 Nov, when the spare table's "PROFILE 10M" text in the
  tag gave -626. A zero tag has neither bit set, so no checksum check and no warning. The mailbox's byte-11
  checksum (mailbox_core.h `mb_swap_data`) is not needed: that fixes a tag the OS wrote; here the tag is ours.
- REQUEST TAG: IGNORED. Apple's driver writes what it likes there (WRHDR forces bit 7 of byte 6 and puts a
  checksum in byte 11, SOURCE-PROFILEASM 806-836); a bare-metal program may send zeros. The drive reads only
  the 512 data bytes.
- STATUS BYTES: all four 0 (no error) for every channel read and write, as for every other block the
  emulator serves (Emulator 600-603, 960-963). The drive never signals "busy" or "error" for the channel: an
  error status would make Apple's driver retry the command up to 4 more times (SOURCE-HDISK 352-368) and
  then fail the request, which is slower and no more informative than a REFUSED block.
- Our driver reads with `io_mode = raw_io`: the 20 wire bytes arrive expanded to 24 header bytes, then 512
  data (SOURCE-PROFILEASM RDHDR 448-467; ESPFDEMO REV C `efbufsz = 536`). With a zero tag the header is 24
  zeros. Bare-metal readers get the 20 zeros raw.

## 6. Timing (VERIFIED facts, PROPOSED rules)

- The interface core runs with interrupts off and must not block (ESProFile_FujiNet.ino 10-17, "the
  interface core ... must be quick"). A channel write costs one `mb_parse` and a memcpy of up to 492 bytes
  into the slot; a channel read costs building 532 bytes (20 zeros + 512) from the slot. Both at bus speed.
- Apple's driver times a command out after about 10-12 s (`COUNTLIMIT`, source-PROFILE 31-32,
  SOURCE-PROFILEASM 1507-1513) and the whole disk queue, the boot volume included, waits meanwhile
  (SOURCE-HDISK 642-682: one queue per drive). So the drive NEVER holds BSY while the network works; the
  network task (core 0, `mbTask`) does the work and the reply is served by a later read.
- The network task serves one request at a time (as today); the channel's slots and the mailbox's request
  are taken in turn (round robin), oldest first. A fetch is bounded at 30 s (`MB_FETCH_MS`), so no client
  waits more than about 30 s for its turn plus 30 s for its own fetch. The STATUS text gets the number of
  channel requests waiting.

## 7. Repeats, retries and write-verify (VERIFIED facts, PROPOSED rules)

- After an error Apple's driver re-sends the SAME command up to 4 more times (`restrt_limit`, SOURCE-HDISK
  352-368, 557). The drive therefore keys requests by (client, sequence): a REQUEST that repeats the
  (client, sequence) of the request in the slot, PENDING or READY, is IGNORED (the slot is untouched, the
  reply stays). A REQUEST with a NEW sequence from the same client replaces a PENDING one not yet taken by
  the network task (the mailbox rule, ESProFile_FujiNet.ino 144), or supersedes one being worked on (its
  reply is dropped when it finishes), or replaces a READY reply.
- Write-verify: if `v_flag` is on (DCONTROL 21 with ar10[2] <> 0; default off, SOURCE-HDISK 554), Apple's
  driver follows every write with a read of the same block "to compute checksum only" (SOURCE-PROFILEASM S13,
  S20). That read reaches the channel and gets the PENDING (or READY) block: harmless, because reads are
  idempotent, and the zero tag passes the checksum logic because bit 7 of byte 6 is clear (INFERRED from
  591-598; LisaEm cannot try it, there is no channel). The Lisa driver may still refuse to run with verify on
  (DCONTROL 20 reports it in ar10[6]) until a real Lisa has shown it harmless.
- REFUSED replies, payload text: `no slot` (all slots hold other clients' live requests), `too long`
  (never happens through a correct driver; length over 492 is not even parsed), `busy` is NOT used (a request
  is always accepted into a free or own slot and queued).
- The drive never replies to a request it did not see; a client whose write was lost (parity, cable) sees
  IDLE or the previous reply and simply sends again with the next sequence number.

## 8. Clients and slots (PROPOSED)

- Client id: 16 bits, chosen by the client, 1..65535; 0 means "nobody". The id identifies a SESSION on the
  drive, not a product. Suggested: the Lisa Office System driver uses one id per attached tool (the control
  block it hands the tool), starting at 1; a MacWorks driver starts at 256; bare-metal programs at 4096. Ids
  are not registered anywhere (stepleton: no central registry); collisions only matter between programs that
  run at the same time on the same drive, which is one operating system at a time.
- Slots: CH_SLOTS = 4 on the plain ESP32 (each slot = request header + 492 bytes + reply 492 bytes, about
  1 KB, allocated once in `mbSetup` on the heap; the static DRAM is full, ESProFile_FujiNet.ino 97, 0.5 note).
  8 on an ESP32-S3 if the heap allows. A slot belongs to a client until that client cancels, or until it has
  been READY or IDLE for 10 minutes with no write from that client, after which the slot may be recycled for a
  new client (the LRU one goes first). A client that finds its slot gone sees IDLE and repeats its request.
- One OPEN body for the whole drive (32 KB, `MB_BODY_MAX`), shared by the mailbox and all channel clients, as
  today: an OPEN by any client replaces the body; READ reads the current body. This is a limit of the plain
  ESP32's memory, not of the protocol; the identification text says `body 1`.

## 9. The requests (VERIFIED: ESProFile_FujiNet.ino 351-420; nothing new)

The payload is the request text, the reply the text or bytes that `mbServe` produces today, unchanged:
`HELLO`, `ECHO <text>`, `STATUS` (gets ` ch <slots used> chbad <n>` appended), `WIFI`, `SCAN`, `CLOCK`,
`TRACE`, `OPEN <url>`, `READ <offset> <count>` (count <= 492, bytes), `CLOSE`. So a tool switching from the
mailbox route to the driver route changes the transport and nothing in what it says or expects.

## 10. Detection, compatibility and the range-check fix (VERIFIED facts, PROPOSED rules)

What a read of $FFFEF8 gives on each firmware (Emulator 580-595, SdFat FatFile.cpp 1170-1192):

| Firmware | Image size | Read $FFFEF8 | Write $FFFEF8 |
|---|---|---|---|
| stock ESProFile, ESPFuji 1.0 / 1.1 | up to 16,776,952 bytes (31,535 blocks; every 5 MB and 10 MB image) | "out of range": 532 zero bytes | "out of range": nothing written |
| stock ESProFile, ESPFuji 1.0 / 1.1 | larger (Tim's Macintosh XL image: 65,520 blocks = 34,856,640 bytes) | the byte check passes (`blockNum < disk.fileSize()` compares a block number with a byte count), `seekSet(blockNum*532)` FAILS past the end of the file (FatFile.cpp 1189) and the position stays where the previous command left it: the read returns THE BLOCK AFTER THE LAST ONE ACCESSED (harmless) | the same failed seek, then `disk.write` OVERWRITES THE BLOCK AFTER THE LAST ONE ACCESSED (destructive) |
| ESPFuji 1.2 (this spec) | any | the channel block | the channel |

Rules that follow:
1. PROBE BY READ, NEVER BY WRITE. A driver reads $FFFEF8 first and treats the drive as having a channel only
   when the reply carries `LISAFUJI`, version 3, kind 2. Zeros or a stray data block mean "no channel". No
   write goes to $FFFEF8 before a probe has succeeded on this drive since the driver started. (The Lisa
   driver's dcode for the channel refuses writes until its probe flag is set.)
2. The range check is fixed in the same firmware version: `blockNum < disk.fileSize() / 532` in readDrive and
   writeDrive, and any block at or above $FFFE00 that is not a known reserved block answers zeros on a read
   and is dropped on a write, with the "out of range" console line. (The note to Alex about this, drafted
   1 Nov, still stands for the stock firmware.)
3. Identification text (the IDLE payload): `ESPFuji <version> channel 3 slots <n> body 1`. The driver shows it
   to the user and may check `channel 3`.
4. OPTIONAL, to discuss with Alex: bytes 48-63 of the spare table ($FFFFFF), which are $FF padding today
   (Emulator 230-232), could say `ESPFUJI CHANNEL ` so a program can tell before touching $FFFEF8. Alex's
   48-byte table and the Selector's bytes 32-47 (`Cameo/Aphid 0001`) would not change. Not needed for rule 1.

## 11. Coexistence (VERIFIED facts)

- The file mailbox (versions 1 and 2) is untouched: `mbOnWrite` / `mbOnRead` stay in the ordinary read and
  write paths; the channel branches sit BEFORE them, beside the Selector branches, and return without
  touching the image. ESPFuji XL and the Lisa tools of today keep working.
- The Selector's blocks and Alex's handling of them are untouched. A Selector running on an ESPFuji 1.2 drive
  never sends $FFFEF8.
- On the Lisa, HDISK keeps one sorted queue per drive and one command on the cable at a time
  (SOURCE-HDISK 642-682, OPTION_A Q4): the OS's own requests and the channel's are simply interleaved;
  nothing on the drive side needs a lock. The ESP32 side is single-threaded on the interface core; the
  slots are the only shared state with the network task, with one flag each way as today.
- No state of Apple's driver changes (OPTION_A Q3), and the drive stores nothing, so a restart of either
  side leaves no trace except the slots, which recycle.

## 12. Firmware changes, by file (PROPOSED; ESPFuji 1.2, flag `ESPFUJI_CHANNEL` beside `FUJINET_MAILBOX`)

| File | Change |
|---|---|
| `ESProFile_Emulator.ino` readDrive | new branch before the ordinary read: `else if (cmd[1]==0xFF && cmd[2]==0xFE && cmd[3]==0xF8) chOnRead(blockData + readStatusOffset);` fills 532 bytes (20 zeros + data). Console line ` - ESPFuji channel: read for client C: idle/pending/ready/refused`. LED amber like a Selector command. |
| `ESProFile_Emulator.ino` writeDrive | new branch before the ordinary write: `chOnWrite(blockData)`; the block is NOT stored. Console line ` - ESPFuji channel: request/poll/cancel client C seq S (len)`. |
| `ESProFile_Emulator.ino` both | range check in blocks (section 10 rule 2). |
| `mailbox_core.h` | `MB_VERSION_CH 3`; `mb_parse` accepts version 3 (and `mb_xl_*`, `mbOnWrite`, `mbOnRead` reject it, so a version 3 page can never be taken for a mailbox page); `ch_build_reply(data, seq, client, status, payload, len)`; kinds 3 POLL and 4 CANCEL; status constants. |
| `ESProFile_FujiNet.ino` | `struct ChSlot { client, seq, state, reqlen, req[492], replen, rep[492], lastMs }` x CH_SLOTS on the heap; `chOnWrite`, `chOnRead`, `chLastClient`, `chBad`; `mbTask` takes the oldest pending slot or the mailbox request in turn and calls the same `mbServe`; STATUS and HELLO texts gain the channel counters; `ESPFUJI_VERSION "ESPFuji 1.2"`. |
| `tests/channel_test.c` | PC test with pretend 6-byte commands: probe (IDLE), request, pending, ready, repeat ignored, new sequence replaces, two clients and POLL, CANCEL, bad block ignored, `v_flag` read-back idempotent, a version 3 block offered to `mbOnWrite` is rejected. Also keep `mailbox_test.c` passing. |
| bench mode | a typed `CH <client> <text>` sends a request through `chOnWrite` and prints what a read would give, so the channel is tried on the USB port before a Lisa. |
| `README.md`, `CHANGELOG.md` | the channel, the fixed range check, the version. No AI attribution anywhere (FujiNet rule). |

Memory on the plain ESP32: 4 slots about 4 KB on the heap, next to the 32 KB body; the 0.5 soaks ran with
about 150 KB free heap (HELLO text), so this fits; the host test and a soak on the SPARE ESP32 (never the
2/10's internal ESProFile) confirm before any Lisa sees it.

## 13. The Lisa side, in brief (what this spec asks of ESPFDEMO REV D and FnBus)

- The driver keeps its block read of REV C (dcode 3/7) and adds: dcode 9 = probe (read $FFFEF8, check the
  signature, set the probe flag, return the identification), dcode 10 = request (build the REQUEST block from
  the caller's text, write $FFFEF8 through Apple's driver with `operatn := 0`, one block, raw_io, tag = 24
  zeros), dcode 11 = read the channel (one read, return status and payload), dcode 12 = cancel. The write path
  is the untested half of Option A (reads are proven, 5-8 Nov); LisaEm can try it only against a plain image
  ($FFFEF8 is "out of range" there too: the write must come back with status 0 and change nothing).
- Writes to the channel are refused by the driver until the probe has succeeded; the driver refuses to work at
  all when `v_flag` is on, until a real Lisa has shown the verify read harmless (section 7).
- FnBus gets route fnViaDrv: probe through the driver first, then the mailbox file, then serial; the header
  builder is the mailbox's with version 3 and the client id; the reader ignores the echo field; the wait and
  the 492-byte READ loop stay.

## 14. Test plan (PROPOSED order)

1. PC: `tests/channel_test.c` (section 12).
2. Bench on the spare ESP32: the `CH` command on the USB port; HELLO, WIFI, OPEN/READ/CLOSE through the slots.
3. LisaEm: ESPFDEMO REV D's write path against a plain image (status 0, image unchanged, dcodes return).
4. Tim's 2/10 with the SPARE ESProFile running ESPFuji 1.2: probe, HELLO, CLOCK, OPEN/READ through the driver
   while the boot volume works normally; then the two-tools case (POLL).
5. Only then FnBus fnViaDrv in the tools and the all-apps round (Tim's rule).

## 15. Open questions

1. stepleton: $FFFEF8 and the version-3 page (one layout for file mailbox and channel) acceptable? Client ids
   without a registry acceptable?
2. Alex: the range-check fix in the stock firmware; the optional spare-table marker (section 10, rule 4).
3. Whether MacWorks's ProFile driver passes a block number like $FFFEF8 at all (a Macintosh XL driver would
   otherwise go through the 6522 directly, as lisa_io does).
4. Whether four slots are enough or two would do (fewer slots, more free heap for the body).
5. Whether the drive should also answer the channel on the SECOND parallel port of a Lisa 2/10 (internal and
   external ESProFile are separate drives; each has its own channel, each with its own Wi-Fi): nothing in the
   protocol prevents it; the Lisa driver's "which disk" choice (REV C dcode 4, N page) already exists.

## Appendix A. The two remap tables (VERIFIED for LOS, measured for MacWorks)

    low 4 bits asked for:        0 1 2 3 4 5 6 7 8 9 A B C D E F
    LOS puts on the cable:       0 5 A F 4 9 E 3 8 D 2 7 C 1 6 B   (INT1TAB, 5n mod 16)
    LOS: to reach X, ask for:    0 D A 7 4 1 E B 8 5 2 F C 9 6 3   (13n mod 16)
    MacWorks puts on the cable:  0 D A 7 4 1 E B 8 5 2 F C 9 6 3   (13n mod 16, measured order 0,13,10,7,4,1,14 ...)
    fixed points of both:        0       4       8       C

## Appendix B. Block numbers in decimal (for Lisa Pascal constants, no $ in the Workshop's longint literals)

    $FFFEF8 = 16776952   channel
    $FFFEFD = 16776957   ESProFile status (blkstat in ESPFDEMO)
    $FFFFFF = 16777215   spare table (blkspare)
    5 MB image  = 5,175,296 bytes =  9,728 blocks;  10 MB image = 10,350,592 bytes = 19,456 blocks
    byte-check threshold = 16,776,952 bytes = 31,535 blocks (images above it: see section 10)

## 16. Draft 0.2 (9 November 2026): what writing and testing the code changed

The drive side now exists as code in a PRIVATE copy of the firmware (a private working copy, nothing flashed,
nothing pushed) and is checked on the PC: `tests/channel_test.c` (100 checks, the list of section 12 and more) and
`tests/channel_stress.c` (the two cores as two threads). Sections 1 to 15 stand, with these changes and additions:

1. NEW RULE, found by the test: A CLIENT STARTS ITS SESSION WITH A CANCEL (after the probe, before its first
   request). Without it, a tool that is started again with the same client number and counts its sequence numbers
   from 1 again sends (client, 1) while the slot still holds the old session's (client, 1): by the rule of section
   7 that is a REPEAT, it is ignored, and the read returns the OLD answer, marked READY. The Lisa driver therefore
   sends CANCEL for a client when it hands the client number out (attach), as well as when it takes it back.
2. A written block is "not a channel block" (ignored, `chbad` goes up) also when: the kind is not 1, 3 or 4; a
   REQUEST has sequence 0 (sequence 0 means IDLE in a reply); bytes 18-19 are not 0 (section 3 said "must be 0";
   the drive now enforces it, so a later use of those bytes cannot be half understood by an older drive).
3. `mb_parse` is NOT changed (section 12 said it would accept version 3). The channel has its own parser,
   `ch_parse`, which takes the 512 data bytes and accepts version 3 only; `mb_parse` keeps rejecting version 3, so
   no mailbox path can take a channel block for a mailbox page, by construction. A version 3 block written to an
   ORDINARY block number is plain data: stored, not a request.
4. The slots and every rule of sections 3 to 8 are in a new pure file, `channel_core.h` (no hardware), not in
   `ESProFile_FujiNet.ino`: the PC tests run the very code the drive runs. The .ino keeps two hooks (`chOnWrite`,
   `chOnRead`), the network task's `chServeOne`, the set-up and the bench lines.
5. REFUSED "no slot": the drive remembers ONE refusal (the last client refused). That client reads REFUSED with
   its sequence number until it writes again; a second refused client that polls later reads IDLE and simply asks
   again. Only READY slots are recycled (10 minutes with no write from their client, the longest idle first; a
   POLL counts as a write and keeps a slot); a PENDING slot is never taken away.
6. A request that replaces one, or a cancel, while the network task is working on the old request: the old answer
   is DROPPED when it arrives (counted, `drop`), as section 7 said; a replaced request goes to the back of the
   "oldest first" queue.
7. TWO CORES WITHOUT A LOCK (section 11 said "one flag each way"): each slot field has one writer. The interface
   core owns the request and a generation number that goes up by 2 with every change and is odd while it changes;
   the network task copies a request only under an even, unchanged generation, and owns the reply and the number
   of the generation it answered, written last. The reply is READY exactly when the two numbers are equal. The
   stress test checks every reply against the request it claims to answer over 20 million operations; two
   deliberately broken versions (reply announced before it is written; no generation check after the copy) fail
   it within a second, which the single-threaded test cannot see. A PC is not an ESP32: evidence, not proof.
8. STATUS gains ` ch <slots used> wait <not answered yet> chbad <n> chreq <n> rpt <n> rd <n> drop <n>` (section 9
   had `ch` and `chbad` only). HELLO's `req` counts mailbox and channel requests together.
9. Bench lines: `CH` (a read as it stands: the probe), `CH <client>` (POLL, then read), `CH <client> CANCEL`,
   `CH <client> <request>` (REQUEST, then reads until not PENDING, 40 s). NOT offered on the USB port of a running
   drive: that port is read by the network task, and the slots must have one writer on each side.
10. The range check (section 10, rule 2) is `blockNum < fileSize / 532` in both paths (`ch_block_in_image`). Its
    second half (unknown blocks at or above $FFFE00 answer zeros) needs no code: no FAT32 file reaches 16.7 million
    blocks. Worth saying to Alex in general terms: the stock check lets through EVERY block number between the
    image's block count and its byte count (block 20,000 of a 10 MB image as well), not only the reserved ones on
    large images; the failed seek then does the rest.
11. A development build calls itself `ESPFuji 1.2d1` (identification `ESPFuji 1.2d1 channel 3 slots 4 body 1`), so
    that it cannot be mistaken for a release.
12. Not changed, still open: the questions of section 15; the Lisa side (section 13) is still on paper; the
    verify read with a zero tag is still INFERRED.
13. MEMORY ON THE PLAIN ESP32, measured by the linker (section 12 guessed "4 KB on the heap, this fits"): ESPFuji
    1.1's STATIC RAM is full to within 8 bytes (segment `dram0_0_seg` 124,580 bytes, 1.1 ends exactly at its end).
    With the channel's state as ordinary statics the link failed ("overflowed by 200 bytes"), with one 4-byte
    pointer it failed too ("by 8 bytes"). So the channel adds NO static byte: its state, the four slots and the
    network task's two buffers (about 5.2 KB together) are the tail of the heap block that already holds the 32 KB
    page body, found through the existing pointer. Cost of the channel on the plain ESP32: flash +2,336 bytes
    (1,257,552 to 1,259,888), static RAM +0, heap about 5.2 KB of the roughly 150 KB free. Any later feature that
    needs a static variable on the plain ESP32 has to free one first (the diagnostic's 64 KB buffer is the big one).
14. BUILT, NOT RUN: the private copy compiles and links with arduino-esp32 3.3.12 for the plain ESP32, the
    ESP32-S3 and the bench build (see DRIVER_PROGRESS_2026-11-09.md for the outcome of each). No ESP32 has run it:
    test plan step 2 (bench on the SPARE ESP32) is the next proof and needs Tim's go-ahead for a flash.

## 17. The Lisa side exists: ESPFDEMO REV D (9 November 2026, night; LisaEm)

Section 13 is no longer a sketch. ESPFDEMO REV D (built and tested in LisaEm 9 Nov 23:20,
first build; DRIVER_PROGRESS_2026-11-09b.md) has these DEVICE_CONTROL codes on top of REV C's 1 to 8, all with
`ar10[0]` = device as before (-1 = the built-in port's disk, 0..39 = configinfo index):

| dcode | Name | In | What it does | Out |
|---|---|---|---|---|
| 9 | PROBE | device | reads $FFFEF8 through Apple's driver (dcode 3's path); the drive has a channel only if the 512 data bytes say `LISAFUJI`, version 3, kind 2; then the device is remembered as probed, else the memory is cleared | ar10[0] stage (0 good), [1] 1 = channel / 0 = none, [2] status, [3] payload length, [4] client, [5] sequence, [6] ticks, [7] devrec, [8] restarts, [9] block handed over ($FFFEF8). Payload in the buffer from byte 44 (dcode 5 fetches it) |
| 10 | REQUEST | device, client 1..65535, sequence 1..32767, length 0..492; payload already stored at buffer byte 44.. by dcode 14 | writes a kind 1 block to $FFFEF8: 24 zero header bytes (the tag), then `LISAFUJI`, 0 3, seq, 0 1, len, client, 0 0, payload, zeros | as dcode 3; stage 12 = no channel probed on THIS device, 11 = write-verify is on, 13 = bad client/sequence/length |
| 11 | READ CHANNEL | device | the read of dcode 9 without the probe memory | as dcode 9 |
| 12 | CANCEL | device, client, sequence | writes a kind 4 block | as dcode 10 |
| 13 | POLL | device, client, sequence | writes a kind 3 block | as dcode 10 |
| 14 | STORE | offset 0..500, 36 bytes in ar10[1..9] | puts bytes into the buffer (the mirror of dcode 5); 623 while a transfer is under way | - |
| 15 | STATE | - | ar10[0] probed devrec, [1] last client written, [2] channel writes so far, [3] reads, [4] REV (4), [5] 1 in the TESTWR build, [6] last sequence, [7] last kind | - |
| 16 | TESTWR build only | device, ordinary block | writes the buffer to an ordinary block; LisaEm only, never for a Lisa; the ordinary build answers 623 | as dcode 3 |

Rules the code keeps, as this spec asks (sections 7, 10, 13, 16): the only block the ordinary build ever WRITES is
$FFFEF8 (`EFALLOW`); no write before a probe has succeeded on that very device (`probed` = the devrec address, so
the internal and the external ESProFile of a 2/10 are probed separately); no write while `v_flag` (write-verify) is
on; the request block is built in the driver, the caller supplies only client, sequence, length and the payload
bytes; a request's sequence 0 is refused (13). What the Lisa side does NOT do yet: hand out client ids (the tool
chooses; spec 8 suggests 1 per attached tool), send the CANCEL of 16.1 by itself (the tool does it after the probe:
dcode 12 with its client and sequence 1), or POLL before a read on its own (the tool knows whether another client
wrote last: dcode 15's `lastcli`). FnBus's `fnViaDrv` route is still to be written.

What LisaEm showed (DRVTEST5, results in DRVTEST5_REV_D_TESTWR_results_2026-11-09.txt):

1. THE WRITE HALF OF OPTION A WORKS. With the TESTWR build, block 19400 (free in the bitmap, low 4 bits 8 = a fixed
   point) was read, 36 data bytes replaced, written (stage 0, `reqsuccess` true, 512 bytes, about 30,000 MICROTIMER
   ticks, the same as a read), read back with the marker, written back with the original and read back again; the
   disk image's block, data and 20-byte tag, is byte-identical to the original image afterwards. The request differs
   from the read only in `operatn := 0`, `read_flag := false` and `CVT_BUFF_ADDR (er, false, ...)`, as GENIO's DISKIO
   does it (`read_f` passed to all three). So the channel REQUEST, CANCEL and POLL writes will reach a drive; only the
   drive's answer to them is untested (no channel exists yet outside the PC).
2. A PLAIN DRIVE'S ANSWER TO $FFFEF8 IS NOT ZEROS. LisaEm's ProFile returns, for a block it does not have, a raw
   header that echoes the six command bytes (`00 FF FE F8 0A 03`) and 512 data bytes of SOME OTHER BLOCK (once
   catalog text, once the test's own output file as it was being written): the "block after the last one accessed"
   behaviour section 10 predicts for the stock ESProFile firmware on a big image. The probe said "no channel" both
   times, every channel write was refused with stage 12, and the driver's write counter stayed 0. Consequences:
   (a) rule 1 of section 10 (probe by READ, never by write) is confirmed as necessary, not just prudent; (b) a tool
   must ignore the status/length/client/sequence fields when the probe says `chan 0` (they are garbage from the stray
   block: status 20290, client 22574 ...); (c) the identification text of 10.3 is the only thing a user should see.
3. The 24-byte raw header round trip: what the driver read back after its own write was the same header as before
   (`00000000 7FFF0200 00004BA2 00000022 00004BA3 00004BA1`) and the image tag was unchanged, so Apple's WRHDR
   rebuilt the tag the drive already had (bit 7 of byte 6 was set already, the byte-11 checksum came out equal). For
   the channel the request tag is 24 zeros from us and whatever WRHDR makes of them on the cable: the drive ignores
   it (section 5), so nothing more is needed.
4. Devices: the probe of a disk that is not Apple's ProFile driver (LisaEm's device 36, the Sony drive) is refused
   with stage 7 before any read; a channel write to a non-disk (37) is refused with 12 (no probe) before anything
   else; CDKILL / CDMAKE / CDKILL leave the position empty (751) as in REV C.

Test plan (section 14) after this: step 3 is DONE as far as LisaEm allows (write path proven on an ordinary block;
the channel write itself refused on a plain image, as it must be). Step 2 (bench on the SPARE ESP32) still needs
Tim's go-ahead; step 4 can use the spare ESProFile Tim ordered (expected 16 to 23 November).

## 18. The tool side: ESPFuji Driver REV F / REV G, the channel from an Office System tool (10 November 2026, night; LisaEm)

The Office System tool "ESPFuji Driver" (419327) now drives the channel dcodes of section 17. Two keys (and menu
items) were added to REV E (tool REV F, then REV G):

- **C, probe**: dcode 9 on the chosen disk. The page shows the stage line as the read pages do ("Stage 0: read done
  and good; handed over as $FFFEF8, ticks N"), then either `NO CHANNEL on this drive: it answered $FFFEF8 with some
  other block (a plain ESProFile or ProFile; X shows it). The driver writes nothing.` or `CHANNEL FOUND: reply IDLE
  for client 0, sequence 0, payload 38 bytes:` followed by the payload as text (the identification of 10.3). The
  parsed status / client / sequence / length are shown ONLY when `chan = 1`, as section 17 point 2 demands; when
  `chan = 0` nothing of the stray block is interpreted, but the block IS fetched so that X shows it as hex (REV G;
  REV F fetched it only when a channel was found, which left the 2/10 tester with "No read yet" for the one block
  worth recording). The page ends with the driver's state (dcode 15): writes, reads, last client / sequence / kind,
  probed or not.
- **H, HELLO**: the client session of section 8 and 16.1 from the tool's side. Client id 1 (one tool), sequence
  numbers counted by the tool from 1, wrapping at 30000. The first H after an attach sends CANCEL (dcode 12,
  sequence 1); a later H says "CANCEL: sent earlier in this session". Then the payload `HELLO` is stored at buffer
  byte 44 (dcode 14) and dcode 10 writes the REQUEST (sequence 2, length 5). Then dcode 11 reads the channel until
  the status word is not PENDING (1), at most 20 reads, and the page says `Reply after N reads: READY, sequence 2,
  NN bytes:` with the text, or `Still PENDING after 20 reads: C reads the block again.` (C is then a plain read of
  the same block, since the probe is idempotent too), or the stage text when a write or read fails. Every write is
  reported with its stage: on a plain drive the CANCEL already comes back `stage 12: no channel probed on this
  device` and nothing else is tried; the state line's write counter stays 0. A re-attach (K, A) starts a new session
  (sequence from 1, CANCEL due again), which is exactly the case 16.1 was written for.

What LisaEm showed (REV F, 02:33, built first time; PC check first, see below): the home page with the new C/H line,
C = "NO CHANNEL ... ticks 36841", H = "CANCEL (client 1, sequence 1): stage 12: no channel probed on this device"
and "Driver state: writes 0, reads 1, ..., probed none", S as before (the status block $FFFEFD, handed over as
$FFFEF9), a second C (reads 3, writes 0), help, Save & Put Away clean. So the tool side of sections 4, 8, 16.1 and
17 runs on the real OS against the real driver; only the drive's half of the conversation is missing. REV G (the X
fix) went through the same test and became the floppy ESPFUJI_DRIVER_0_1_REVG_TEST.dc42 (tool REV G + driver REV D;
the floppy's open on a clean boot disk is the copy-path test of the 5,632-byte REV D driver file). REV G passed the same LisaEm run (02:50: C, then X shows the 512 bytes LisaEm answered for $FFFEF8, a block of file-system data, not text; H refused, writes 0) and the floppy opened on the clean boot disk twce_clean: 'Copied it from the tool's own volume: 5632 bytes ... the second try worked', 'Driver: ESPFDEMO REV 4 at #15#3', the copied file byte-identical to the REV D driver. The hand-over for the Lisa 2/10 was the folder ESPFuji Driver 0.1 REV G test 2026-11-10, since replaced by the REV H folder (section 20).

The PC check (`efd_rev_g/pc_check`, FPC with a stand-in SysCall) imitates TWO drives: the plain one (a read of
$FFFEF8 answers a stray block, as LisaEm did) and one with channel firmware (IDLE identification before any write,
PENDING at the first read after a REQUEST, READY with a HELLO text at the second, CANCEL forgets). Against the second
the H page ran the whole exchange (CANCEL, HELLO, two reads, READY, 64 bytes; a second H without CANCEL; C showing the
READY block; K + A + H sending CANCEL again) before any Lisa build, and X showed the block with the layout of section
3 byte for byte (`4C495341 46554A49 0003 0003 0002 0040 0001 0002` ...). That is the only place the request / reply
half has run so far; the drive's code (section 16) and the tool's code have each been tested against an imitation of
the other, never against each other. Test plan step 2 and 4 remain, and need Tim's go-ahead for a flash.

Still for the driver (unchanged from 17): hand out client ids and send the 16.1 CANCEL itself, POLL before a read
when another client wrote last; and FnBus's `fnViaDrv`.

## 19. Sessions in the driver: ESPFDEMO REV E (10 November 2026; LisaEm)

Section 16.1 said the Lisa driver sends a client's CANCEL "when it hands the client number out (attach), as well as
when it takes it back", and section 17 left both for later. ESPFDEMO REV E (test program
DRVTEST6, build `drv_build_q.sh <tag> test|prod`, check `verify_rev_e.py`; DRIVER_PROGRESS_2026-11-10b.md) does it,
with one refinement that came out of the Device Drivers Manual:

1. **Client ids come from the driver**: dcode 17 "open a session" hands out the lowest free id of 1..8 (eight
   sessions, one per tool; stage 14 when all are open) and dcode 18 "close a session" takes it back. Both are
   bookkeeping in the control block; dcode 17 does no I/O at all, so a tool may open its session before it has
   probed any disk. A caller that manages its own id (the REV G tool uses 1 that way) keeps using dcodes 10-13 as
   in REV D: ids above 8 are not the driver's, and the two kinds do not collide if such callers stay above 8
   (section 8's "LOS driver from 1" now means 1..8 for the driver's own pool).
2. **The opening CANCEL is sent by the driver, but DEFERRED to the session's first REQUEST**: a session's first
   dcode 10 is preceded by a kind-4 block for that client (one more write; if that write fails its stage is returned
   and the request is not sent). So the CANCEL always comes after the probe (writes are gated on it) and before the
   first request, whatever order the caller used, and a caller cannot forget it. An explicit dcode 12 counts as the
   opening CANCEL too. The driver remembers per session whether the drive has heard of it (state `sssent`) and the
   last sequence it sent.
3. **The closing CANCEL is sent by dcode 18**, with the session's last sequence number, to the probed device, and
   only when the drive has heard of the session (a session that never wrote costs the drive nothing). The session
   is closed whatever the write's outcome (the drive recycles the slot after 10 minutes anyway, section 8).
4. **ddown sends nothing.** The manual (3.3.3, "Must Disable Interrupts: Yes", "return with an error if the driver is
   waiting for I/O") has ddown run with interrupts off and not wait for anything; a BLK_REQ there, waiting for the
   disk interrupt, would never wake. So CDKILL does not cancel on the drive: a tool closes its sessions (dcode 18)
   before CDKILL or Put Away, as the ESPFuji Driver tool already detaches in its put-away branch (LOS note 93).
   This replaces 16.1's "as well as when it takes it back" for the CDKILL case; the drive's 10-minute recycling is
   the safety net for a tool that dies without closing.
5. A re-attach (CDKILL + CDMAKE) starts with an empty pool (nsopen 0): no session survives the driver.

What LisaEm showed (23:16-23:22, the TEST build, first build, `verify_rev_e.py` 0 problems): eight opens gave ids 1..8,
the ninth was refused with stage 14, closing 3 and opening again gave 3, closing an id that is not open gave 15; on
the plain image the probe said no channel and all six channel writes (a session id, client 0, a wrong device, a
non-session id) were refused with stage 12, write counter 0. Then dcode 19 (test build only) pretended the probe had
found a channel, and the session paths ran against LisaEm: a session's first request cost TWO writes (CANCEL kind 4,
then REQUEST kind 1), its second one, its close one (kind 4, the last sequence, 28,000-41,000 ticks); an explicit
CANCEL + request + a non-session request + close = 8; open again, POLL, request (two writes), close = 12; nsopen back
to 0; after CDKILL + CDMAKE nsopen 0 and the next open gave 1. All of it as the PC run against a stand-in predicted.

Two facts about LisaEm's drive for the record: a WRITE of $FFFEF8 is "block number invalid" in LisaEm (profile.c sets
bit 6 of status byte 3 and returns before any image access), and Apple's ProFile driver TOOK those writes: stage 0,
reqsuccess 1, hard_error 0, 512 bytes, about the ticks of a read. So that status bit is not one Apple's driver fails
a request on (the same was seen for reads on 9 Nov). The image was untouched: block 19400 equals the original after
the REV D write test, 416 blocks differ (the build's own files) and none of them holds a raw LISAFUJI version-3
block. Section 5's "all four status bytes 0" stays the rule for the firmware; the LisaEm bit is a curiosity, not a
licence.

The REV E driver file is 6,656 bytes (REV D 5,632). The tool side (REV G) does not use dcodes 17/18 yet: tool REV H
will (open at attach, close before CDKILL and at Put Away, no CANCEL of its own), and FnBus's `fnViaDrv` route is
designed on the same calls in FNBUS_DRIVER_ROUTE_DESIGN_2026-11-10.md (driver research folder). Still for the driver:
POLL before a read when another client wrote last (the design note's open point 5).

## 20. The tool takes its session from the driver (REV H), and a fault in REV E's deferred CANCEL (11 November 2026; PC and LisaEm)

Tool REV H does what section 19 asked of it: at attach it opens a session with dcode 17
and shows the client id on the home page ("Session: client 1, opened by the driver at attach; K or Put Away closes
it."); H sends HELLO with that id and no CANCEL of its own (the page says "The driver sends the session's opening
CANCEL before this first request." the first time); K, Save & Put Away and quit close the session with dcode 18
BEFORE CDKILL, and the Detached page says what the close did ("no CANCEL needed (it never wrote to a drive)" or "a
CANCEL went to the probed drive, stage 0 (taken)"); the state line ends with "sessions N". A driver before REV E
answers 623 to dcode 17: the home page says "Session: NONE: this driver has no sessions (REV before E); H is
refused" and nothing is sent.

**The fault.** Writing the tool's PC stand-in for the sessions meant imitating EFWRITE and EFWRCH byte for byte,
and that is where it showed: EFWRCH builds every channel block in the driver's one buffer and zeroes the buffer
AFTER the block's own payload. The deferred opening CANCEL (section 19.2) has no payload, so it zeroed bytes 44..535,
and the REQUEST that followed in the same dcode-10 call went out with its stored length (5) and a payload of zeros.
On the imitated channel drive the request arrived as `"     "`; the reply came anyway (the imitation does not read
the text), so the TOOL could not see it, only the drive's log could, and the 10 Nov TESTCH run in LisaEm counted
writes without looking at the buffer. REV F (one change in EFWRITE: the `len` payload
bytes are copied to a local array before the CANCEL and put back after it) fixes it. DRVTEST7 (= DRVTEST6 + a
fetch of the buffer after the session's first request) says `PAYLOAD after CANCEL+REQUEST: KEPT (HELLO still at
44)` with REV F and `WIPED` with the REV E behaviour (both seen on the PC; `verify_rev_f.py` fails on WIPED).
Rule for every caller, now in the driver's header: store the payload (dcode 14) right before the request (dcode
10); any other channel write (12, 13, a close) rebuilds the buffer. Note 103 in LOS_PROGRAMMING_NOTES.

**Nothing for the firmware changes**: the drive side (section 16) never saw a REV E block, and a request whose
payload is zeros is a legal request with the wrong text, not a protocol matter.

**What LisaEm showed (11 November, 02:16-02:48, one background chain).** Driver REV F, test build: DRVTEST7 repeats
every result of section 19 (pool, refused writes, the write counts [0, 2, 3, 8, 12], the closing CANCELs, block 19400
written and restored, image untouched) and adds `PAYLOAD after CANCEL+REQUEST: KEPT (HELLO still at 44)`: after the
session's first request (two writes, both taken) the buffer's bytes 44..79 are HELLO and zeros. Ordinary build: writes
0 everywhere, 623 for dcodes 16 and 19, driver file 6,144 bytes (as REV E; the test build 6,656). Tool REV H, built
first time (Obj 24,576 bytes), on the plain image: home page with "Session: client 1 ..."; C = NO CHANNEL, ticks
38,336, state line "... probed no, sessions 1"; H = "The driver sends the session's opening CANCEL before this first
request." then "HELLO (client 1, sequence 1): stage 12: no channel probed on this device", writes 0; K = "Session 1
closed: no CANCEL needed (it never wrote to a drive).", CDKILL 0; A = session 1 again; H refused again; Save & Put
Away clean. The REV H floppy (tool REV H + driver REV F, 6,144 bytes) opened from a clean disk: the copy line and the session line both on the 15-line home page, the copied driver file byte-identical (02:47-02:58).

The hand-over for the Lisa 2/10 is the folder ESPFuji Driver 0.1 REV H test 2026-11-11 (floppy = tool REV H + driver
REV F; the REV G folder is marked superseded). Still for the driver: POLL before a read when another client wrote
last (the design note's open point 5); still for Tim: the 2/10 run, the flash go-ahead, section 6 of the design note.

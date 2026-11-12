# Development notes: what was tested, what was found, what is open

Everything that is not the protocol, the driver or the firmware design: the record behind the other documents in
this folder. Marks used in the earlier drafts: VERIFIED = read in the cited source or seen in LisaEm; INFERRED =
reasoned, not tested; PROPOSED = a choice to agree on. The day-by-day working document, draft 0.2 of the
specification with its sections 16 to 20, is kept unchanged as `ESPFUJI_CHANNEL_SPEC.md` beside this file.

## 1. Timeline

| Date (2026) | What |
|---|---|
| 30 Oct | stepleton proposes reserved blocks plus a driver instead of the file mailbox (LisaList2 topic 897). |
| 31 Oct | Research: Apple's ProFile driver has no raw-block door; lisa-share shows run-time loading of a driver; the Selector's reserved blocks are already in the ESProFile firmware. |
| 1 Nov | Option A (a second driver calling Apple's ProFile driver) judged feasible from the OS source; stepleton's two questions answered: no lasting change to Apple's driver's state, one queue per drive so no collision. Alex agrees to the magic-block approach and leaves the driver to Tim. Design draft 0.1. |
| 5 Nov | Driver REV A: a configurable driver with no hardware attached at run time in LisaEm; the console position, channel 3, works; an empty expansion slot is refused (609). REV B: a block read through Apple's driver works while that driver serves the boot volume. |
| 6 Nov | REV C: the guards (Apple's ProFile driver only, no Widget), the remap undone in the driver, a status read without a block number. Tool REV A: the same calls from an Office System tool. |
| 7-8 Nov | Tool REV C-E: a floppy that copies the driver file to the boot volume when the OS reports 794; the kept first error; the hex page. |
| 8 Nov | Firmware-side specification draft 0.1 ($FFFEF8, the version 3 block, the exchange, the rules). |
| 9 Nov | Draft 0.2: the drive side written as `channel_core.h` in a private copy, PC tests (100 checks) and a two-thread stress test pass; the opening cancel found necessary by the test. Driver REV D: the channel calls (probe, request, read, cancel, poll, store, state); the write path proven in LisaEm on an ordinary block with a test build. |
| 10 Nov | Tool REV F/G: probe and HELLO from the tool. Driver REV E: sessions (open, close, the deferred opening cancel, nothing in `ddown`). |
| 11 Nov | Tool REV H takes its session from the driver. Driver REV F fixes a fault in REV E's deferred cancel. The design published on GitHub (this folder) and the status posted on LisaList2 (topic 905). stepleton's review asks for the documentation to be split, which this folder now is. |

## 2. What LisaEm showed

LisaEm has no channel: its ProFile answers a block it does not have with a stray block. What it can show is the
Lisa side.

- **Reads through Apple's driver work** (5 Nov): block 0 came back as block 0; a request for `$FFFFF3` arrived as
  the spare table (the 5n mod 16 remap, seen); a read asked for as `$FFFEF9` went out on the emulated cable as
  "read `$FFFEFD`, retry 10, sparing 3". Apple's driver's state afterwards: unchanged but for its counters.
- **The tag warnings** (5 and 6 Nov): the spare table's text in its tag gave warning -626 and the request completed
  "unsuccessfully" with data. That is where the zero-tag rule for replies comes from.
- **The write path works** (9 Nov, test build only): a free block of a scratch disk was read, altered, written,
  read back with the change, written back and read back again; the image's block, data and tag, was
  byte-identical to the original afterwards. The write differs from the read only in the operation code, the read
  flag and the direction given to `CVT_BUFF_ADDR`, as the File System's DISKIO does it. About 30,000 `MICROTIMER`
  ticks per transfer, read or write.
- **A plain drive's answer to `$FFFEF8` is not zeros** (9 Nov): LisaEm returned a raw header echoing the six
  command bytes and 512 data bytes of some other block (catalog text once, the test's own output file once). The
  probe said "no channel" both times and every write was refused. So probing by read is necessary, not merely
  prudent, and a tool must not interpret the fields of a failed probe.
- **LisaEm marks a write of `$FFFEF8` "block number invalid"** (bit 6 of status byte 3) and Apple's driver took the
  write anyway with success reported. That status bit is not one Apple's driver fails a request on. A curiosity
  of the emulator, not a licence: the firmware answers zero status bytes.
- **Sessions** (10 and 11 Nov, test build with a pretended probe): eight opens gave numbers 1 to 8, the ninth was
  refused, a closed number was handed out again; a session's first request cost two writes (cancel, then request),
  the second one, the close one; after `CDKILL` and `CDMAKE` the pool was empty.
- **The tool** (10 and 11 Nov): the probe page, the HELLO page, the hex page, attach and detach, the floppy's copy
  of the driver file to a clean boot disk, all on the real Office System in LisaEm, with every channel write
  refused with stage 12 because no channel exists there.

## 3. What the PC showed

- `channel_test.c`: 100 checks pass, with and without optimisation. `channel_stress.c`: 20 million operations, every
  reply matching its request; two deliberately broken variants (reply announced before it is written; no
  generation check after the copy) fail within a second. `mailbox_test.c` still passes.
- The firmware compiles and links with arduino-esp32 3.3.12 for the plain ESP32, the ESP32-S3 and the bench build.
  Cost on the plain ESP32: flash +2,336 bytes (1,257,552 to 1,259,888), static RAM +0, heap about 5.2 KB of the
  roughly 150 KB free during the 0.5 soak tests. ESPFuji 1.1's static RAM was found full to within 8 bytes
  (`dram0_0_seg` 124,580 bytes): with the channel's state as statics the link failed by 200 bytes, with a single
  pointer by 8 bytes. Any later feature that needs a static variable on the plain ESP32 must free one first; the
  diagnostic's 64 KB buffer is the large one.
- The tool's PC stand-in (a Free Pascal build of the tool against an imitation of `SysCall`) imitates two drives:
  a plain one and one with the channel. Against the second the whole exchange ran (cancel, HELLO, two reads, READY,
  a second HELLO without a cancel, re-attach and cancel again) before any Lisa build. The drive's code and the
  tool's code have each been tested against an imitation of the other, never against each other.
- **The REV E fault** (11 Nov): writing the stand-in's imitation of the driver's channel write, byte for byte,
  showed that the deferred opening cancel, built in the driver's one buffer with an empty payload, zeroed the
  buffer after it, so the request that followed in the same call went out with its stored length and a payload of
  zeros. The LisaEm run of 10 Nov counted writes and could not see it. REV F saves the payload around the cancel;
  the test program now fetches the buffer after the first session request and reports KEPT or WIPED. Nothing for
  the firmware: a request with a zero payload is a legal request with the wrong text.

## 4. Facts from Apple's source that shaped the protocol

- `SOURCE-PROFILEASM`, table INT1TAB: the low four bits of a block number are replaced by 5n mod 16 for every
  ProFile-type drive (`source-PROFILE` turns the remap off only for a Widget). A multi-block request is remapped
  block by block, so channel requests are single blocks.
- `source-PROFILE`: retry count and sparing threshold are set once per drive (10 and 3; 11 after `DCONTROL` 21) and
  cannot be chosen per request through Apple's driver. The channel therefore ignores them, unlike the Selector's
  blocks, which use them as parameters.
- `SOURCE-PROFILEASM` RDHDR: a reply whose tag byte 6 has bit 7 set must XOR to zero over all 532 bytes, else
  checksum error and retries. `SOURCE-HDISK`: tag byte 2 bits 7 and 6 come back as warnings -626 and -625.
- `source-PROFILE` and `SOURCE-PROFILEASM`: a command that does not complete within about 10 to 12 seconds times out,
  and `SOURCE-HDISK` keeps one queue per drive, so the whole disk waits meanwhile. Hence: the drive answers at bus
  speed and the host polls.
- `SOURCE-HDISK`: after an error Apple's driver re-sends the same command up to four more times. Hence repeats are
  keyed by (client, sequence) and ignored.
- `SOURCE-HDISK`, `v_flag`: with write-verify on, every write is followed by a read of the same block "to compute
  checksum only". That read reaches the channel and gets PENDING or READY; harmless because reads are idempotent,
  and the zero tag passes the checksum logic. INFERRED: LisaEm cannot try it, and the driver refuses to write while
  verify is on until a Lisa has.
- `SOURCE-GENIO` DISKIO: the only range check on a block number is DISKIO's own, which a request built directly
  for `dskio` skips; there is no block cache below the File System.
- The Device Drivers Manual 3.3.3: `ddown` runs with interrupts off and must not wait for I/O. Hence the driver's
  shut-down sends no cancel and programs close their sessions first.

## 5. The stock firmware's range check

`ESProFile_Emulator.ino` admits an ordinary read or write when `blockNum < disk.fileSize()`, a block number against
a byte count. Consequences, by image size: up to 16,776,952 bytes (31,535 blocks: every 5 MB and 10 MB image) an
unknown reserved block is "out of range" and a read returns zeros, a write does nothing. On a larger image (a
Macintosh XL image of 65,520 blocks, for instance) the check passes, the seek past the end of the file fails and
leaves the position where the previous command left it, so a read returns the block after the last one accessed
(harmless) and a write overwrites it (destructive). The same holds for every block number between the block count
and the byte count of any image, block 20,000 of a 10 MB image included. The channel firmware fixes the check in
blocks; the fix is worth passing to the stock firmware's author in these general terms, independently of the
channel.

## 6. Open questions

Answered or withdrawn since the first drafts:

- `$FFFEF8`: fine (stepleton, 11 Nov).
- Client number ranges per operating system (1.., 256.., 4096.. in the early drafts): withdrawn. The drive does not
  care who a client is; only clients on the same drive at the same time need distinct numbers, and the Lisa
  driver's pool and the opening cancel cover the restart case.
- A second parallel port: dropped as a protocol question. Each ESProFile is its own drive with its own channel;
  the Lisa driver probes drives one at a time and a tool chooses which.

Still open:

1. A channel-wide reset for an operating system to send at start-up without knowing which clients exist
   (stepleton, 11 Nov): a kind 5 RESET with client 0 is the obvious shape. To be agreed and then added to the
   protocol, the firmware and the driver together.
2. Whether to retire the file mailbox on the Lisa Office System once the driver route is proven, which would
   require every tool to be re-released at once (stepleton favours it; the Macintosh XL keeps the mailbox until
   it has a route of its own).
3. Four slots or two.
4. Whether MacWorks' ProFile driver passes a high block number through (see `MACWORKS.md`).
5. The release driver id, and whether tools carry the driver file (see `CLIENTS.md`, 1.7).
6. The write-verify read on a real Lisa.
7. A ProFile on a parallel card, never seen.

## 7. Test plan and where it stands

1. PC tests of the drive side: done, 9 Nov.
2. The bench build on a spare ESP32 (the `CH` lines on the USB port; HELLO, WIFI, OPEN/READ/CLOSE through the
   slots): waiting for a go-ahead to flash.
3. LisaEm: the Lisa side as far as LisaEm allows: done, 9 to 11 Nov.
4. A Lisa 2/10 with a spare ESProFile running the channel firmware: probe, HELLO, CLOCK, OPEN/READ through the
   driver while the boot volume works normally; then two tools at once (the POLL case). Waiting for the spare
   ESProFile's ESP32 and the flash.
5. Only then the FnBus route in the tools and the all-tools round.

The floppy for step 4 (tool REV H with driver REV F) is built and its test list written; it has not been run on a
Lisa yet.

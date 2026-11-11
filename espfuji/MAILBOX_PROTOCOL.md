# The ESPFuji mailbox protocol

How a Lisa program talks to the network through its ProFile. This is the write-up of what the ESPFuji firmware
(`sw/ESProFile/mailbox_core.h`, `sw/ESProFile/ESProFile_FujiNet.ino`) and the Lisa tools (`lisa-tool/EFJ-MBOX.TEXT`)
actually do, for anyone who wants to answer the mailbox from something other than an ESProFile: an emulator such as
BMLisa or LisaEm, another drive, a PC standing in for the drive. Firmware 1.1, 11 November 2026.

## 1. The idea

A Lisa Office System program cannot talk to a drive. It can only read and write files. So the program creates an
ordinary file, `MAILBOX.DATA`, and writes a request into one of its 512-byte pages. The drive sees every page the
Lisa writes, recognises the request by a signature at the start of the page, does the network work on its own
processor, and when the Lisa next reads a page of that file, hands back the reply instead of the stored page. The
Lisa's operating system never knows: it wrote a file and read a file. The ProFile emulation itself is unchanged, and
the disk image is never altered by a reply.

Everything needed to implement the drive side is two hooks: one on every block written, one on every block read.

## 2. A page on the bus

The ProFile transfers 532 bytes per block: a 20-byte tag, then 512 data bytes. The Lisa Office System puts file
bookkeeping in the tag, and the drive side of the mailbox uses two of its fields.

| Tag bytes | Meaning (Lisa Office System) |
|-----------|------------------------------|
| 0-1 | version |
| 2-3 | volume ID |
| 4-5 | **file ID** (the S-file number of the file this page belongs to) |
| 6-7 | data used; bit 7 of byte 6 says a checksum is present |
| 8-10 | absolute page |
| 11 | **checksum**: with it, the XOR of all 532 bytes is 0 |
| 12-13 | relative page (the page's position in its file) |
| 14-16 | forward link |
| 17-19 | backward link |

The Lisa refuses a page it reads unless the XOR of all 532 bytes is 0 (OS source `SOURCE-PROFILEASM`, lines
591-598). Whoever replaces the data of a page must therefore recompute tag byte 11. All numbers in this protocol
are big-endian, as the 68000 writes them.

## 3. The mailbox page

The 512 data bytes of a request, a reply, or an idle page:

| Data bytes | Field |
|------------|-------|
| 0-7 | signature, the ASCII text `LISAFUJI` |
| 8-9 | protocol version: 1 (Lisa Office System) or 2 (MacWorks, the Macintosh XL) |
| 10-11 | sequence number |
| 12-13 | kind: 0 idle, 1 request, 2 reply, 3 status (unused) |
| 14-15 | payload length, 0 to 492 |
| 16-19 | reserved; a reply echoes a number here (section 6), a version 2 page carries k and n here (section 7) |
| 20-511 | payload, at most 492 bytes; the rest zero |

A page is a mailbox page when the signature matches, the version is 1 or 2, and the length is at most 492.
Anything else on the drive is an ordinary block and is left alone.

## 4. The exchange, seen from the Lisa (version 1)

This is what the Lisa tools do, in `EFJ-MBOX.TEXT`.

1. **The file.** If `MAILBOX.DATA` is not beside the tool, make it: 256 whole pages of zeros, written with
   whole-page I/O (`WRITE_DATA`, absolute offsets), then `FLUSH`. 256 pages so that a long run of polls never
   wraps round onto a page the OS still holds in its cache.
2. **The request.** Take the next sequence number (1 to 30000, then back to 1). Build the page: signature, version
   1, the sequence number, kind 1, the length, the request text. Write it with `WRITE_DATA` to page k of the file
   (k is a running index that every request and every poll advances), then `FLUSH` the file so the OS does not
   hold the page back.
3. **The reply.** Read the pages after k, one at a time, with `READ_DATA`: k+1, k+2, ... (wrapping at 256). Three
   reads, then a 250 ms nap, for up to 40 seconds. A page comes back as the reply when it carries the signature,
   kind 2, and the same sequence number. Any other content, including a page the OS served from its cache, is
   simply skipped; the next page is tried. The reply payload is the answer.
4. **Failure.** No reply in 40 seconds means the drive is not an ESPFuji, or it is busy. A reply whose text
   begins `ERROR` is a refusal with a reason.

The polling reads are not wasted traffic to the drive: each one is a chance for it to answer. Nothing about the
request page needs to be read back.

## 5. The exchange, seen from the drive (version 1)

Two hooks, both on the path the drive already has for every block.

**On every block the Lisa writes** (before, or as, it is stored):
- Parse the data as a mailbox page. Not a mailbox page, or not kind 1: store the block as usual and do nothing.
- A request: remember the **file ID from tag bytes 4-5** (that is the mailbox file from now on), the sequence number,
  the block number, and the payload. Clear any reply still waiting: it is not the answer to this. Hand the request
  to the worker. A request that arrives before the worker took the previous one simply replaces it.
- Store the block all the same. The request page is an ordinary part of the file.

**On every block the Lisa reads** (after it is read from the image, before it goes to the host):
- No reply waiting: pass the block through.
- A reply is waiting and the block's tag file ID equals the remembered mailbox file ID: replace the 512 data bytes
  with the reply page (signature, version 1, the request's sequence number, kind 2, length, echo, payload), keep the
  20-byte tag, set tag byte 11 to 0 and then to the XOR of the other 531 bytes so the whole page XORs to 0. Pass it on.
- The reply **stays on offer** after it has been served. It is cleared only by the next request. The Lisa's OS
  sometimes reads a page of the file on its own; if that read swallowed the reply, the tool would wait 40 seconds for
  nothing (seen on a Lisa 2/10, 24 October 2026).

**The worker** does the network work away from the ProFile timing (on the ESProFile, a task on the other CPU core)
and finishes every request within 30 seconds, so a reply always beats the Lisa's 40 second wait. The reply carries:
the request's sequence number; kind 2; the payload; and in bytes 16-19 either the offset a READ answered or, for
every other request, the block number the request was written to.

A drive that stores the file ID in RAM only forgets the mailbox at power-up. That is fine: the first request after
power-up teaches it again. Tag bytes 4-5 are the only thing that links a read to the mailbox; a drive answering
version 1 must have the Lisa's tags, which an emulator that keeps 532-byte blocks does.

## 6. Requests and replies

Requests are ASCII text in the payload, one per page, no terminator. Replies are text, except READ, whose reply is
raw bytes. The Lisa tools treat a reply beginning `ERROR` as a refusal and show the rest as the reason.

| Request | Reply |
|---------|-------|
| `HELLO` | `ESPFuji 1.1 up <seconds>s <wifi word> <ip> req <count> heap <free>k/<min>k` |
| `ECHO <text>` | `OK <text>` |
| `STATUS` | `requests <n> replies <n> served <n> body <bytes> open <0 or 1>` |
| `WIFI` | `ssid <name> status <word> ip <a.b.c.d> rssi <dBm> reason <n>` |
| `CLOCK` | `OK yyyy mm dd hh mi ss` (SNTP time, local when `timezone=` is set, else UTC), or `ERROR no time yet (...)` |
| `OPEN <url>` | `OK <bytes> <http code>` with ` truncated` appended when the body was cut at 32 KB, or `ERROR <why>` |
| `READ <offset> <count>` | the bytes themselves, `count` at most 492, clipped to the body; bytes 16-19 echo `offset` |
| `CLOSE` | `OK closed` |
| `TRACE` | timing counters, text (diagnostic) |
| `SCAN` | `<n> networks: [<ssid> ch<n> <dBm>] ...` (diagnostic; takes a few seconds) |
| anything else | `ERROR unknown request` |

The wifi word is one of `connected`, `idle`, `no ssid`, `failed`, `lost`, `disconnected`, or similar. HELLO is what the
tools send first, and a reply of any text to it is what tells them the route is there. The ESPFuji tool shows the
text as it comes; an implementation that is not an ESProFile can answer HELLO with its own name and version.

**OPEN** fetches the whole body now, GET, up to two redirects (a redirect from http to https is followed with a
secure client), an 8 second connection timeout, certificates not checked (there is no certificate store on the
drive; the FujiNet does the same), the body kept up to 32 KB. The Lisa then takes it in pieces with READ.
**READ** `offset count` returns `count` bytes from `offset`; `offset` past the end gives 0 bytes. The echo of the
offset in bytes 16-19 means a READ retried after a lost reply is harmless. **CLOSE** drops the body. Only one body
is open at a time; a second OPEN replaces the first.

A fetch, as the tools do it (`MbGet` in `EFJ-MBOX.TEXT`):

```
OPEN http://example.org/page          ->  OK 2731 200
READ 0 492                            ->  492 bytes
READ 492 492                          ->  492 bytes
...                                   ->  until the bytes received reach 2731, or a READ returns 0 bytes
CLOSE                                 ->  OK closed
```

The ESPFuji tool keeps 8 KB of a page; LisaFuji and the Browser keep more; a Lisa picture from the LisaWeb gateway
is about 13 KB.

## 7. Version 2: a Lisa running MacWorks (the Macintosh XL)

MacWorks puts the Mac File Manager's tag in the ProFile tag: file number in bytes 0-3, flags in 4-5, the logical
block in 6-7, the date in 8-11, zeros, and its own checksum in **byte 19** (the 532 bytes XOR to 0). So tag bytes
4-5 are not a file ID there, and a version 1 request from MacWorks must never be made: a drive would remember a flag
word shared by thousands of blocks and hand the reply to reads of unrelated files.

A version 2 page says where it sits instead:
- bytes 8-9 = 2;
- bytes 16-17 = **k**, this page's index in the mailbox file, from 0;
- bytes 18-19 = **n**, the number of pages in the file (at most 1024).

The Mac application creates `MAILBOX.DATA` contiguous (64 pages, `AllocContig`), writes every page once as an idle
version 2 page (kind 0, with its k and n), reads and writes it with the cache bypassed, and gives up after 45
seconds. The drive, on a version 2 request written to block b, remembers n; on a read it serves the reply only when
the **stored page is itself a version 2 mailbox page with the same n**. (The first beta used the block range b-k ..
b-k+n-1; MacWorks interleaves logical blocks on the ProFile, so a file contiguous to the Mac is not a run of
consecutive drive blocks, and the range caught other files' blocks. Found on a real Macintosh XL, 2 November 2026.)
The swap keeps the Mac tag and, if the stored page XORed to 0, recomputes byte 19 so it still does. The reply page is
version 2 and carries the same echo in bytes 16-19 as a version 1 reply (section 5); the Mac application matches a
reply by its sequence number only.

A version 1 drive (ESPFuji 1.0) rejects every version 2 page and never answers one, so the Mac application says
"this drive needs ESPFuji 1.1 or later" after its wait. Version 1 and version 2 do not interfere: a drive serving
both keeps one request slot and one reply slot, and the version of the request decides which rule the read hook uses.

## 8. Implementing the drive side in an emulator

The checklist, for BMLisa, LisaEm, or any ProFile emulation that sees the 532-byte blocks:

1. In the block-write path: `mb_parse` on the 512 data bytes; kind 1 means a request. Version 1: keep tag bytes 4-5
   as the mailbox file ID. Version 2: keep n from bytes 18-19. Keep the sequence number, block number and payload;
   drop any waiting reply; queue the request. Store the block as usual.
2. In the block-read path, when a reply is waiting: version 1, the block's tag bytes 4-5 equal the file ID; version
   2, the stored page parses as a version 2 mailbox page with the same n. If so, replace the data with the reply
   page and fix the checksum byte (11 for version 1; 19 for version 2, only if the page XORed to 0 before). Leave the
   reply on offer until the next request.
3. The worker: answer the requests of section 6. OPEN must finish within 30 seconds; a Lisa tool gives up at 40, the
   Mac application at 45. Keep one body of up to 32 KB; READ returns at most 492 bytes.
4. Never write a reply into the image. The reply lives only in the read path.
5. Nothing stops an implementation from answering faster than the ESProFile. The Lisa polls every 250 ms after three
   immediate reads, so a reply ready within a few milliseconds is picked up by the first poll.

`mailbox_core.h` is plain C with no hardware in it and can be lifted whole: parsing, building, the two swaps and the
version 2 checks. `tests/mailbox_test.c` exercises it on a PC, and `macxl-tool/tests/xl_end_to_end.c` runs the
version 2 rules against a pretend MacWorks ProFile.

To test a new implementation from the Lisa side: the ESPFuji tool (0.2) on the demo disk or its own floppy; H sends
HELLO and shows the reply, W sends WIFI. Then LisaFuji, LisaFuji Browser, LisaCheckers, LisaSalvo, LisaFetch,
LisaSolstice and LisaTwinClock, which all use this same exchange and need nothing else from the drive.
The mailbox file is created by whichever tool runs first.

## 9. Versions

- **1** (ESPFuji 0.1 to 1.1, October 2026): the Lisa Office System, mailbox found by the tag's file ID.
- **2** (ESPFuji 1.1b1, 30 October 2026; corrected in 1.1b2, 2 November): MacWorks, mailbox found by the page's own
  k and n. ESPFuji 1.1 serves both.

The firmware's own version is what HELLO prints. The request set has not changed since 0.5 (CLOCK) and is the same
for both page versions.

Tim O'Connor (GyroPilot), with Alex Anderson-McLeod's agreement for the ESProFile. GPL-3.0, like the firmware.
Questions: open an issue on github.com/GyroPilot/ESPFuji.

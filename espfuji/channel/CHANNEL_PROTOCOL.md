# The ESPFuji channel protocol

Draft 0.3, 11 November 2026. Status: proposed. The drive side exists as code and passes its tests on a PC; no
ESProFile has run it and no Lisa has talked to it yet.

This document defines what travels between a host and an ESPFuji drive through the channel block. It says nothing
about how any particular host reaches the block; that is the business of the per-platform documents beside it.
Reading it requires three things from elsewhere: the ProFile command set (a 6-byte command, a 532-byte block made
of a 20-byte tag and 512 data bytes), the request texts of the ESPFuji mailbox (`MAILBOX_PROTOCOL.md`, one folder
up, section 6), and the FujiNet idea of a drive that does network work on the host's behalf.

## 1. Terms

- **Drive**: the ProFile-compatible device that implements this protocol. Today that is an ESProFile running the
  ESPFuji firmware; an emulator or any other device that answers the ProFile bus can implement it.
- **Host**: whatever sends ProFile commands to the drive: a Lisa running its Office System, a Lisa running
  MacWorks, a bare-metal program, a test rig.
- **Channel block**: block number `$FFFEF8`. Reading or writing it never touches the disk image.
- **Request**: a 512-byte data block the host writes to the channel block, carrying one request text.
- **Reply**: the 512-byte data block the drive returns when the host reads the channel block.
- **Client**: one user of the channel, identified by a 16-bit number the host chooses. A client has at most one
  request outstanding at a time.
- **Session**: the life of one client from its opening cancel to its closing cancel.
- **Slot**: the drive's memory for one client: the request it holds and the reply to it.
- **Sequence number**: a 16-bit number the client attaches to each request so that the drive can tell a new
  request from a repeat of the last one.

## 2. The channel block

The drive recognises the channel by the three block-number bytes of the command, `FF FE F8`, on reads and writes
alike. The retry-count and sparing-threshold bytes of the command carry nothing and are ignored.

Every exchange is a single-block command. A multi-block command must not be used.

Why `$FFFEF8`: popular Lisa operating systems rearrange the low four bits of a block number before it reaches
the cable, to suit the ProFile's sector interleave. The Lisa Office System sends 5n mod 16; MacWorks sends the
inverse, 13n mod 16. Both leave the digits 0, 4, 8 and C alone, so `$FFFEF8` arrives at the drive unchanged from
either, and from a program that drives the port directly. The neighbouring reserved blocks `$FFFEFC` to `$FFFFFF`
belong to the Cameo/Aphid Selector protocol and are not touched. `$FFFEF4` and `$FFFEF0` are free fixed points if a
second channel is ever needed.

## 3. Block layout

The 512 data bytes of a request and of a reply have the same layout. All numbers are big-endian.

| Bytes | Field | In a request (host writes) | In a reply (host reads) |
|---|---|---|---|
| 0-7 | signature | the ASCII text `LISAFUJI` | `LISAFUJI` |
| 8-9 | protocol version | 3 | 3 |
| 10-11 | sequence number | the client's own, never 0 | the sequence number this reply answers; 0 when idle |
| 12-13 | kind | 1 REQUEST, 3 POLL, 4 CANCEL | 2 REPLY |
| 14-15 | payload length | 0 to 492 | 0 to 492 |
| 16-17 | client | 1 to 65535; 0 is never a client | the client this reply is for; 0 = nobody |
| 18-19 | status | must be 0 | 0 IDLE, 1 PENDING, 2 READY, 3 REFUSED |
| 20-511 | payload | the request text, no terminator | the reply (section 6) |

Bytes after the payload are zero.

This is the ESPFuji mailbox page with protocol version 3. Versions 1 and 2 are pages of a file (the mailbox);
version 3 is the channel. A drive serving both never confuses them: a version 3 block is never a mailbox page,
and a version 3 block written to an ordinary block number is plain data and is stored like any other.

A block written to the channel is **not a request** and is ignored by the drive (nothing is stored, nothing
changes, a counter goes up) when any of these holds: the signature or the version is wrong; the length is over
492; the client is 0; the status field is not 0; the kind is not 1, 3 or 4; the kind is REQUEST and the sequence
number is 0.

## 4. The tag and the status bytes

- The 20-byte tag of a block the host writes is ignored. The host may send zeros or whatever its operating
  system puts there.
- The 20-byte tag of every reply is all zeros. The reasons are host-side: a set bit 7 in tag byte 6 makes the
  Lisa Office System demand a checksum of the whole block, and set top bits in tag byte 2 come back as warnings.
  A zero tag triggers neither.
- The drive answers every channel command with four zero status bytes (no error, not busy). It never reports
  an error through the ProFile status for a channel command; refusals travel in the reply block. An error status
  would make a host driver retry and then fail, which is slower and says less.

## 5. The exchange

1. The host writes a REQUEST block (kind 1) for client C with sequence number S and the request text as payload.
   The drive stores it in C's slot, marks it pending, and hands it to its worker. The write completes at once;
   nothing waits for the network.
2. The host reads the channel block. The drive answers **for the client of the most recent write** to the
   channel (request, poll or cancel), with one of:
   - PENDING, sequence S, no payload: the request is taken, the answer is not there yet;
   - READY, sequence S, payload = the answer;
   - REFUSED, sequence S, payload = the reason in words;
   - IDLE, sequence 0, client C: nothing is outstanding for C (never asked, cancelled, or the slot was
     recycled); payload = the drive's identification (section 7).
3. The host reads again after a short pause until the status is no longer PENDING.
4. A read **does not consume the reply**. The reply stays until the same client writes a new request or a
   cancel, or the slot is recycled. A repeated read, whoever makes it, gets the same answer.
5. A POLL block (kind 3) for client C changes nothing except "the client of the most recent write": the next
   read answers for C. A host with one client never needs it. A host whose clients interleave writes a POLL for
   client C before reading on C's behalf when some other client wrote last.
6. A CANCEL block (kind 4) for client C forgets C's request and reply and frees the slot. A request already being
   worked on finishes on the drive and its answer is dropped.
7. **A client starts its session with a CANCEL** and ends it with a CANCEL. The opening cancel matters when a
   program restarts with the same client number and counts its sequence numbers from the beginning again:
   without it the first request would repeat the (client, sequence) of the previous session's first request,
   be taken for a repeat, and the read would return the old session's answer marked READY.

A worked example, HELLO from client 1 with sequence number 7, data bytes 0-23 in hex (the rest 0):

    request  4C 49 53 41 46 55 4A 49  00 03  00 07  00 01  00 05  00 01  00 00  48 45 4C 4C 4F ("HELLO")
    pending  4C 49 53 41 46 55 4A 49  00 03  00 07  00 02  00 00  00 01  00 01
    ready    4C 49 53 41 46 55 4A 49  00 03  00 07  00 02  00 3E  00 01  00 02  45 53 50 46 75 6A 69 ... (62 bytes of text)
    idle     4C 49 53 41 46 55 4A 49  00 03  00 00  00 02  00 24  00 00  00 00  "ESPFuji 1.2 channel 3 slots 4 body 1"

## 6. Requests and replies

The payload of a request is one request text of the ESPFuji mailbox protocol: `HELLO`, `ECHO <text>`, `STATUS`,
`WIFI`, `SCAN`, `CLOCK`, `TRACE`, `OPEN <url>`, `READ <offset> <count>`, `CLOSE`. The payload of a READY reply is
what the mailbox answers to the same text: text for every request, raw bytes for `READ`. A reply that begins
`ERROR` is the worker's refusal of the request (an unknown request, a failed fetch); a REFUSED status is the
channel's refusal of the transport (section 8). The request set and the reply texts are defined in
`MAILBOX_PROTOCOL.md`, section 6, and do not change with the transport. `STATUS` gains channel counters at the end
of its text.

A `READ` is answered by its sequence number alone; the channel has no echo field. A client that re-sends a READ
after a lost reply uses a new sequence number.

One **body** (the result of the last `OPEN`, up to 32 KB) exists per drive and is shared by every client and by
the mailbox. An `OPEN` by any client replaces it; `READ` reads the current one. Clients that fetch at the same time
on the same drive will interfere; a host with several clients must not interleave one client's `OPEN`/`READ` run
with another's. This is a limit of the drive's memory, not of the protocol, and the identification text says
`body 1`.

## 7. Detection and identification

A host finds out whether a drive has a channel by **reading** the channel block and checking the reply for the
signature `LISAFUJI`, version 3 and kind 2. Anything else means there is no channel: a drive without one answers
a block of zeros, or, on some firmware and image sizes, some unrelated block of the image.

**A host never writes the channel block to a drive that has not answered a channel read.** On a drive whose
firmware checks a block number against the image size in bytes instead of blocks, an unknown high block number
on a large image passes the check and a write lands on a real block of the image.

Before any client has written since power-up, a read answers IDLE for client 0 with the identification text:

    ESPFuji <version> channel 3 slots <n> body 1

`channel 3` is the protocol version, `slots <n>` the number of clients the drive can hold at once, `body 1` the
number of fetched bodies it keeps. A host may show the text to its user and may check `channel 3`.

## 8. Clients, repeats and slots

- **Client numbers** are chosen by the host, 1 to 65535. The drive does not interpret the value. Two clients
  using one drive at the same time must have different numbers; nothing else is required, and there is no
  registry. How a host hands out numbers to its programs is the host's affair and is described in its own
  document.
- **Sequence numbers** are chosen by the client, 1 to 65535, never 0, and should change with every request. The
  drive compares them for equality only.
- **Repeats**: a REQUEST whose (client, sequence) equals that of the request already in the client's slot,
  pending or answered, is ignored and the slot is untouched. This makes the host's own retries harmless: a Lisa
  disk driver re-sends a command several times after an error.
- **Replacement**: a REQUEST with a new sequence number from a client that has one in its slot replaces it. If
  the old one was being worked on, its answer is dropped when it arrives.
- **Slots**: the drive has a small number of slots (four on a plain ESP32). A new client takes a free slot. If
  none is free, the drive may recycle a slot whose client has been answered and has not written for ten
  minutes, the longest idle first; a pending slot is never taken away. If that fails too, the request is REFUSED
  with the reason `no slot`. The drive remembers one refusal: the refused client reads REFUSED with its sequence
  number until it writes again, while any other client without a slot reads IDLE.
- A client whose slot was recycled, or whose write was lost on the cable, reads IDLE and simply sends its
  request again with a new sequence number.
- A POLL counts as a write for the purpose of keeping a slot.

## 9. Timing

- The drive answers every channel command at bus speed, from memory. It never holds the bus busy while the
  network works. (On the Lisa Office System a command that takes more than about ten seconds stalls the whole disk
  queue, the boot volume included.)
- The network work is done by a worker apart from the bus, one request at a time, oldest first, taking turns with
  the mailbox when both have requests waiting. A fetch is bounded (30 seconds on the ESPFuji firmware), so a
  client waits at most about one fetch for its turn plus one for its own request.
- The host polls. The interval and the overall wait are the host's choice; the ESPFuji Lisa tools pause 250 ms
  between reads and give up after 40 seconds.

## 10. Coexistence

- The drive stores nothing in the disk image for the channel. A restart of either side leaves no trace except
  the slots, which recycle.
- The file mailbox (versions 1 and 2) is unaffected; a drive may serve both.
- The Selector's reserved blocks are unaffected; a Selector never sends `$FFFEF8`.
- Nothing on the drive side needs a lock between the host's disk traffic and the channel: the host's disk driver
  puts one command on the cable at a time, and channel commands are simply interleaved with the rest.

## 11. Versions

- Version 3, draft 0.1, 8 November 2026: this protocol. Draft 0.2, 9 November: the opening cancel, the stricter
  block validation (status field, kinds, sequence 0), the single remembered refusal, pending slots never
  recycled. Draft 0.3, 11 November: this write-up, with the client-number ranges of the earlier drafts withdrawn
  (the drive does not care who a client is) and the open points below.

Open points, to settle before the first release:

1. A way to reset the whole channel (every slot cancelled) without knowing which clients exist, for an operating
   system to use at start-up. A possible form: kind 5 RESET with client 0, which today is rejected as "not a
   request", so an older drive ignores it and a newer one acts on it.
2. Whether the file mailbox should be retired on the Lisa Office System once the driver route is proven.
3. The number of slots (four, or two with more memory for the body).

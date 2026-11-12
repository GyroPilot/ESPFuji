# The channel in the ESProFile firmware

What changes in the ESPFuji firmware (itself a fork of Alex Anderson-McLeod's ESProFile emulator) to serve the
channel of `CHANNEL_PROTOCOL.md`. The code exists in a private working copy as development build `ESPFuji 1.2d1`;
it compiles and links for the plain ESP32, the ESP32-S3 and the bench build, passes its tests on a PC, and has
not run on an ESP32. It will be published after its first run on a spare ESProFile.

## 1. Where the channel sits

The ESProFile firmware is three Arduino files. `ESProFile_Emulator.ino` is the ProFile: it runs on one core with
interrupts off, takes the 6-byte command from the Lisa, and reads or writes the 532-byte block in the image on
the SD card; reserved block numbers (`$FFFEFC` to `$FFFFFF`) are matched on the three command bytes before the
ordinary read or write and served from memory. `ESProFile_FujiNet.ino` is ESPFuji's addition: a task on the other
core that keeps the Wi-Fi up and answers mailbox requests, and two hooks in the block paths that recognise a
mailbox page. `mailbox_core.h` is plain C without hardware: the page layout, its parser and builders, lifted whole
by the PC tests.

The channel follows the same pattern as the Selector's reserved blocks and the mailbox:

- one more match in the emulator's read path and one in its write path, on `FF FE F8`, placed before the
  ordinary read and write and beside the Selector's matches, so that nothing reaches the image;
- a new pure file, `channel_core.h`, holding the slots and every rule of the protocol, with no hardware in it;
- in `mailbox_core.h`, the version 3 block: its parser, its builder and the constants for kinds and statuses;
- in the FujiNet file, two thin hooks for the emulator to call, one call in the network task to serve a channel
  request, the set-up of the slots, the counters in `STATUS` and `HELLO`, and a bench command.

The whole addition is behind one switch, `#define ESPFUJI_CHANNEL` at the top of `ESProFile.ino`; with it commented
out the firmware is ESPFuji 1.1.

## 2. The block paths

On a **read** whose command names the channel block, the emulator fills the 532 bytes it is about to send from
the channel's `ch_on_read`: 20 zero tag bytes and the reply block for the client of the most recent write (or
the identification when there is none). The LED shows amber as for a Selector command. The image is not read.

On a **write** to the channel block, the emulator hands the 512 data bytes of the received block to `ch_on_write`
and stores nothing; the write is acknowledged with four zero status bytes like any other. The write hook
classifies the block (not a channel block, request, repeat, poll, cancel, no slot) and puts a line on the USB
console saying which.

Both hooks run on the interface core with interrupts off and must be quick: a write costs one parse and a copy of
up to 492 bytes into a slot; a read costs building 532 bytes from a slot. Neither waits for anything.

**The range check.** The stock firmware admits an ordinary read or write when the block number is below the
image's size *in bytes*. Every block number between the image's block count and its byte count passes that
check; the seek to `block * 532` then fails past the end of the file and the position stays where the previous
command left it, so the read returns, and a write overwrites, the block after the last one accessed. On a 10 MB
image that admits block 20,000; on an image over about 16.7 MB it admits the reserved blocks as well. With the
channel the check becomes "block number below the image's size in bytes divided by 532" in both paths
(`ch_block_in_image`). A block number past the end is then refused with the existing "out of range" console line.

## 3. The version 3 block in `mailbox_core.h`

The mailbox's parser `mb_parse` accepts versions 1 and 2 only and is not changed: a version 3 block can never be
taken for a mailbox page by any path. The channel has its own parser, `ch_parse`, which takes the 512 data bytes,
accepts version 3 and nothing else, and fills a `ch_header_t` (sequence, kind, length, client, status, a pointer
to the payload). `ch_build` writes a version 3 block of any kind; `ch_build_reply` writes a whole 532-byte reply
as it goes on the bus, 20 zero tag bytes first. The constants: `CH_REQUEST` 1, `CH_REPLY` 2, `CH_POLL` 3,
`CH_CANCEL` 4; `CH_ST_IDLE` 0, `CH_ST_PENDING` 1, `CH_ST_READY` 2, `CH_ST_REFUSED` 3.

## 4. `channel_core.h`: the slots

The channel's state is a `ch_state_t`: a pointer to the slots and their number, the client of the most recent
write, the one remembered refusal (client and sequence), the arrival counter, the counters reported by `STATUS`,
and the identification text. A slot, `ch_slot_t`, holds the request (client, sequence, length, text, arrival
order, time of the client's last write) and the reply (length, text), plus three numbers that order the two
cores (section 5).

The interface core's functions:

- `ch_is_cmd(cmd)`: is this 6-byte command for the channel block (bytes 1 to 3 equal `FF FE F8`; retry and sparing
  ignored).
- `ch_on_write(state, data, now)`: the protocol's rules for a written block. Rejects what section 3 of the
  protocol rejects; a POLL updates the most-recent client and the slot's last-write time; a CANCEL frees the
  client's slot and clears a remembered refusal for it; a REQUEST that repeats the slot's (client, sequence) is
  ignored; otherwise the request goes into the client's slot, or a free one, or a recycled one (answered, ten
  minutes without a write from its client, the longest idle first), or is refused and remembered as the one
  refusal. Returns which of these happened.
- `ch_on_read(state, page)`: fills 532 bytes for the client of the most recent write: READY with the reply when
  the reply's generation equals the request's, else PENDING; REFUSED for the remembered refused client; IDLE
  with the identification otherwise. Changes nothing.

The network task's functions:

- `ch_take(state, ticket, payload, len)`: the oldest request not yet taken, copied out under a stable generation,
  with a ticket naming the slot and the generation. Returns 0 when there is nothing, or when the interface core
  was changing a slot at that instant (try again later).
- `ch_finish(state, ticket, out, len)`: stores the answer in the ticket's slot and marks it done, unless the
  request has been replaced or cancelled meanwhile, in which case the answer is dropped and counted.

`ch_used` and `ch_waiting` count slots in use and requests not yet answered, for `STATUS`.

## 5. Two cores without a lock

The interface core may not block and the network task may not stop it, so the slots are shared without a lock.
Every field of a slot has exactly one writer. The interface core owns the request fields and a **generation
number** per slot that goes up by two with every change of what the slot holds and is odd while the change is
being made. The network task reads a request only under an even generation that is the same before and after
the copy, and owns the reply fields and two numbers: the generation it has taken and the generation whose answer
is in the reply, written last, after the reply bytes. A reply is READY exactly when the answered generation
equals the current one. A request replaced or cancelled while the network task works on it has moved its
generation on, so the answer is dropped when it arrives. The three numbers are read and written with the
compiler's atomic load and store, a plain 32-bit move on the ESP32 plus the ordering guarantee.

## 6. The FujiNet file

- `chOnWrite(block)` and `chOnRead(block)`: the hooks the emulator calls, wrapping `ch_on_write` and `ch_on_read`
  with a console line each. They are called from the block paths on the interface core and from nowhere else: in
  particular not from the network task's reading of the USB port, because the slots have one writer per side.
- `chServeOne()`: in the network task's loop, takes the oldest channel request with `ch_take`, answers it with the
  same `mbServe` that answers a mailbox request, and stores the answer with `ch_finish`. When the mailbox and the
  channel both have requests waiting they take turns, one request each.
- Set-up: the slots are initialised before the interface core starts, with the identification text
  `ESPFuji <version> channel 3 slots <n> body 1`.
- **Memory.** On the plain ESP32 ESPFuji 1.1 fills its static RAM to within a few bytes, so the channel adds no
  static variable, not even a pointer: its state, the slots and the network task's two buffers are the tail of
  the heap block that already holds the 32 KB page body, found through the existing body pointer
  (`chMem`). If that block could not be allocated there is no channel: a read of the channel block gives zeros and
  writes are ignored, and the console says so at start-up. Four slots on the plain ESP32 (`CH_SLOTS`), about
  1 KB each.
- `STATUS` appends ` ch <slots used> wait <not answered> chbad <n> chreq <n> rpt <n> rd <n> drop <n>`; `HELLO`'s
  request count includes channel requests.
- The version string of a build with the channel is `ESPFuji 1.2d1` until it is released, so that a development
  build can never be mistaken for a release.
- The bench build (the firmware without the emulator, driven from the USB port) gets the lines `CH` (a read of
  the channel as it stands), `CH <client>` (a POLL, then a read), `CH <client> CANCEL`, and `CH <client> <request>`
  (a REQUEST, then reads until the status is no longer PENDING, up to 40 s). They go through the same two hooks
  as the bus would. They are not offered on a running drive's USB port.

## 7. Tests

- `tests/channel_test.c`: the rules of the protocol one by one, with pretend 6-byte commands: the probe, a
  request, pending then ready, a repeat ignored, a new sequence replacing, two clients and a POLL, a CANCEL, bad
  blocks ignored, the read-back of a write-verify idempotent, a version 3 block offered to the mailbox hook
  rejected, slot recycling, the remembered refusal.
- `tests/channel_stress.c`: the two cores as two threads, every reply checked against the request it claims to
  answer, over millions of operations; versions with the generation checks removed fail it within a second. A
  PC is not an ESP32: this is evidence for the ordering, not proof.
- `tests/mailbox_test.c`: unchanged and still passing, which shows the mailbox is unaffected.

Both channel tests include `channel_core.h` and `mailbox_core.h` directly: the PC runs the very code the drive
runs.

## 8. Not done

- No ESP32 has run the build: the bench build on a spare ESP32 is the next step, then a spare ESProFile with a
  Lisa.
- The firmware's `README.md` and `CHANGELOG.md` are updated with the channel, the range check and the version
  when the code is published.

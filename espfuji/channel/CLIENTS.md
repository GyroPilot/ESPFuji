# Writing a FujiNet client for the channel

What a program must do, beyond what the driver documents say, to use the ESPFuji network service through the
channel. The requests and replies themselves are those of `MAILBOX_PROTOCOL.md`, section 6: a program that
already talks to the mailbox changes only its transport.

## 1. On the Lisa Office System

The ESPFuji Lisa tools share one unit, FnBus, that hides the transport: today a serial FujiNet or the file
mailbox. A third route through the driver is designed and not yet built; this section is that design, and the
"ESPFuji Driver" tool is the working example of every driver call in it.

### 1.1 Route order

Driver first, then the file mailbox, then serial. The driver route is tried by attaching the driver, probing the
disks, opening a session and sending `HELLO` with a short wait; any failure falls through to the next route
without a message, so a tool on a Lisa without the driver file, or against a drive without the channel, behaves
exactly as today. The probe costs one block read.

### 1.2 Coming up

1. Find or make the driver's entry: `CDINFO` on the console positions 3 to 7; an entry with the agreed driver id
   is reused, else the first free position gets `CDMAKE` (driver name `ESPFDEMO`, not permanent, device type
   `seqdev`). Failure: no driver route.
2. Identify (code 1) through `DEVICE_CONTROL` on the entry: this loads the driver. 794 means no driver file on the
   boot volume: no driver route (the tools do not install the driver; the ESPFuji Driver tool does). A revision
   below 5 has no sessions: no driver route.
3. Choose the disk: the one remembered in the tool's settings file, else the built-in port (-1), else each
   `configinfo` entry that code 8 reports as Apple's ProFile driver with a ProFile-type drive. Probe each (code
   9) until one reports a channel. A Lisa 2/10 can have two ESProFiles, each with its own channel and its own
   Wi-Fi; the first found wins unless the settings file names the other.
4. Open a session (code 17) and keep the client number. Stage 14 (eight sessions open) is a failure.
5. `HELLO` through the request procedure below with the short probe wait. A reply of any text means the route is
   up; anything else closes the session (code 18) and falls through.

The driver sends the session's opening CANCEL before this first request by itself; the tool sends none.

### 1.3 One request

1. Take the next sequence number (1 to 30000, then back to 1).
2. Store the payload with code 14, 36 bytes a call, at buffer offsets 44, 80, 116 and so on. The last call may
   carry bytes past the payload; the driver zeroes the rest of the block itself. 623 means the driver is busy
   with another program's transfer: pause 250 ms and try again, a few times.
3. Send the request (code 10) with the device, the client number, the sequence number and the length. Stage 0
   means the drive took the write. Stage 12 means the probe was lost (the driver was re-attached by another
   program): probe once more, then give up. Stage 11 (write-verify on) is a failure with a message for the user;
   the only cure is Apple's `DCONTROL` code 21, which a tool should not use unasked. Stage 6 is a hard error from
   Apple's driver (keep the error for the user). Stage 10 (busy): pause and retry as in step 2.
4. Read the channel (code 11) until the wait is used up:
   - stage 0, channel found, status PENDING with our sequence number: pause 250 ms and read again;
   - READY with our sequence number: fetch the payload with code 5, 36 bytes a call from offset 44, the length
     from `ar10[3]` of the read (at most 492). A payload beginning `ERROR` is the worker's refusal; show the rest.
   - REFUSED with our sequence number: the payload is the reason (`no slot`); report it.
   - IDLE for our client: our slot was recycled, or a cancel crossed the request. Send the request again, once,
     with the next sequence number.
   - a reply for some other client: another program wrote in between. Send a POLL (code 13, our client and
     sequence) and read again.
   - channel not found: the drive lost its channel (a power cycle, other firmware). Fail; the next coming-up
     probes again.
5. Wait used up: no reply. The drive bounds a fetch at 30 seconds, so a 40 second wait is enough.

Bracket every `DEVICE_CONTROL` that reaches the drive with `Sched_Class(false)` and `Sched_Class(true)`.

### 1.4 Fetching a page

`OPEN <url>`, then `READ <offset> 492` repeatedly until the bytes received reach the length `OPEN` reported or a
`READ` returns no bytes, then `CLOSE`, exactly as over the mailbox. The drive keeps one body for everyone: do not
interleave your `OPEN`/`READ` run with another program's, and expect the body to be gone if another program ran an
`OPEN` meanwhile (the next `READ` returns the other body or nothing; the safe response is to `OPEN` again).

### 1.5 Going down

At quit and at Put Away (a tool's process stays resident after Put Away): close the session (code 18); then ask
the driver how many sessions are still open (code 15, `ar10[8]`); `CDKILL` the entry only when the answer is 0,
otherwise leave the driver attached for the other program. Forget the route so the next coming-up starts over.

### 1.6 Settings

One more word in the tool's settings file: which disk had the channel, so the next launch probes that disk first.

### 1.7 Decisions still open

The release driver id (one for all tools, so that they share one entry); whether each tool carries the driver file
on its floppy or relies on the ESPFuji Driver tool's one-time install; whether the POLL of 1.3 step 4 should be
done by the driver instead, which knows who wrote last. All tools adopt the route in one round, after the channel
has run end to end on hardware.

## 2. Under MacWorks

Not available: see `MACWORKS.md`. A Macintosh XL program uses the file mailbox, protocol version 2.

## 3. On the bare machine

A program that owns the parallel port (a boot program, a program started from the Selector) talks to the drive
directly, as stepleton's `lisa_io` does, with no interleave remap and no driver in the way. It must:

- read `$FFFEF8` first and treat the drive as having a channel only when the reply carries `LISAFUJI`, version 3
  and kind 2; never write the block otherwise;
- choose a client number (1 to 65535) and send a CANCEL for it before its first request, and after its last;
- send single-block writes with a zero tag (or any tag; it is ignored) and the request block in the data bytes;
- read the block back until the status is no longer PENDING, pausing between reads; a read is harmless and may
  be repeated;
- use a new sequence number for every request, including a retry of a `READ`;
- send a POLL before reading only if something else on the same drive may have written since its own last
  write, which on the bare machine is never.

The retry-count and sparing-threshold bytes of the command are free for the program's own use with other
reserved blocks; the channel ignores them.

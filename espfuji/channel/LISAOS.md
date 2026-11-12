# The channel from the Lisa Office System: the driver

How a program running under the Lisa Office System reaches the channel block of `CHANNEL_PROTOCOL.md`. The
driver described here is `ESPFDEMO`, revision F (11 November 2026); it runs in LisaEm and has not run on a real
Lisa. Sources to read beside this: the Lisa Device Drivers Manual (Apple, 1984; chapters 1, 2.5 to 2.7, 3.5 and 6),
the Lisa Office System source (`SOURCE-PROFILEASM`, `source-PROFILE`, `SOURCE-HDISK`, `SOURCE-GENIO`, `SOURCE-CD`,
`source-DRIVERDEFS`, `source-driversubs`), and pappadf's lisa-share, the first run-time-loaded driver on this
system. The driver's own source is `SOURCE-ESPFDEMO.TEXT`; the Office System tool that drives it is "ESPFuji Driver"
(unit `EfdDrv` in `EFD-DRV.TEXT`), the working example of every call below.

## 1. The problem

A program under the Office System cannot send a block number to a drive: the only way to a disk is through the
File System, which deals in files. The ProFile driver that Apple supplies has no door for raw blocks either; its
DEVICE_CONTROL knows three codes (last error, disk status, sparing on or off) and everything else goes to the
volume. Reserved blocks such as `$FFFEF8` are therefore out of reach without a driver of one's own.

Two shapes were possible: a replacement for Apple's ProFile driver with the reserved blocks added, or a second
driver that asks Apple's driver to do the transfer. The second is the one built. It is the least invasive: Apple's
driver keeps the port and the boot volume, and the new driver owns no hardware at all.

## 2. Loading the driver

The Office System loads a **configurable driver** from the boot volume at run time. The mechanism, worked out by
lisa-share:

1. The driver's code file sits on the boot volume as `SYSTEM.CD_<name>`; here `SYSTEM.CD_ESPFDEMO`. The loader
   recognises a driver by its contents, and the name, having no brace, is invisible on the desktop.
2. A program chooses a free **configuration position** and makes an entry for the driver there with `CDMAKE`,
   giving a driver id, the driver name, "not permanent" and the device type `seqdev`. The position used is the
   console slot, `cd_console` (15), channel 3 to 7, device 0; the entry becomes device `#15#3` and the program
   talks to it by the path `-#15#3`. An empty expansion slot cannot be used: the OS refuses to start a driver
   there (error 609). `CDINFO` on a position answers 751 when it is free and returns the entry when it is not, so a
   program can find an entry left by an earlier run (same driver id) and reuse it.
3. The driver is loaded by the OS at the first call made to the entry. If the code file is not on the boot
   volume the call fails with 794. The "ESPFuji Driver" tool then copies the file from its own volume to the boot
   volume (`MAKE_FILE`, `OPEN`, `READ_DATA` and `WRITE_DATA` in 512-byte pieces, `TRUNCATE`, `CLOSE_OBJECT`; 848 and
   956 from `READ_DATA` both mean the end of the file) and tries once more. Other programs expect the file to be
   there.
4. `CDKILL` on the position runs the driver's shut-down code and unloads it. No existing driver is taken out or
   put back; the position was free.

The driver is written in Workshop 3.0 Pascal with Apple's five driver support files, built and linked the way
Apple links its own drivers. It keeps the rules of the Device Drivers Manual, chapter 6: one unit, blank code
segment only, no writable globals (its state lives in a block got with `GETSPACE` whose address is kept in the
device record's `cb_addr`), no strings, no sets, no `WRITELN`, no multiply or divide, kernel services only
through the driver jump table. Names differ in their first eight characters.

The driver answers the function codes `dinit` (allocate and clear the control block), `ddown` (free it),
`dcontrol` (everything below), and `dinterrupt`, `dalarms`, `ddiscon` (nothing to do). `READ_DATA` and
`WRITE_DATA` on the device are refused with 656: the driver is not a byte stream, it is driven entirely by
`DEVICE_CONTROL`.

## 3. Reaching the drive through Apple's driver

A `DEVICE_CONTROL` call that needs the drive runs this sequence inside the driver:

1. **Find the device record.** The caller names the disk with one number: -1 for the disk on the built-in
   parallel port, whose device record pointer is in the system global area (`port_cb_ptrs.via1`), or 0 to 39 for
   an entry of the `configinfo` table (a disk on a parallel card). A pointer that does not fall inside the system
   global area is treated as "no device".
2. **Check that it is a hard disk served by Apple's HDISK layer** (the record has an entry point, type `diskdev`,
   a control block and an extension in the system global area, and the extension names HDISK's entry point).
   Then **check that the driver behind it is Apple's ProFile driver with a ProFile-type drive**: the HDISK control
   block points back at this device record; its extension holds the 6522 addresses Apple's ProFile driver computes
   for this position (built-in port: `$FCD801` with the status register `$400` above it; a card: `$FC2001` plus
   `$4000` per slot plus `$800` per channel); and the drive type is 0 or 1 (ProFile 5 MB or 10 MB). The Sony and
   Priam drivers also use HDISK and are refused this way; so is a Widget, because its driver does not remap block
   numbers.
3. **Allow only certain blocks.** For a read: any block of the volume, the spare table `$FFFFFF`, the ESProFile
   status block `$FFFEFD` and the channel `$FFFEF8`. For a write: the channel, and nothing else, ever. A write is
   also refused while the drive's write-verify flag is on (Apple's driver would follow it with a read of the same
   block; harmless by the protocol's rules but not yet shown on hardware), and until a probe (section 5) has found
   a channel on this very device.
4. **Undo the interleave remap.** Apple's ProFile driver replaces the low four bits n of every block number by
   5n mod 16 before the number goes on the cable, for every ProFile-type drive. The caller names the block as the
   drive is to receive it, and the driver asks Apple's driver for the number whose remap gives that: 13n mod 16
   on the low four bits, when the drive's `remap_interleave` flag is set. `$FFFEF8` is a fixed point and is handed
   over unchanged; `$FFFEFD` is asked for as `$FFFEF9`.
5. **Build a request block the way the File System's own DISKIO builds one**, without DISKIO's range check and
   volume offset: a `reqblk` plus a `disk_extend` from `GETSPACE`, cleared; `LINK_TO_PCB`; operation 1 for a read
   or 0 for a write; the device record as `cfigptr`; the list chain linked to itself; in the extension the read
   flag, the block number, `io_mode = raw_io`, one chunk; the buffer address converted with `CVT_BUFF_ADDR` for a
   536-byte buffer and the I/O count adjusted with `ADJ_IO_CNT`.
6. **Call Apple's driver**: `CALLDRIVER` on the device record with function code `dskio` and the request. The
   request joins the one queue HDISK keeps for that drive, behind whatever the OS has asked for, and the calling
   process sleeps in `BLK_REQ` until the transfer is done. Then the results are copied out (`reqsuccess_f`,
   `hard_error`, the transfer count, the service flag, the restarts Apple's driver needed) and the request is
   freed with `CANCEL_REQ`.

With `raw_io` the 20 tag bytes of a block arrive in the buffer expanded to 24 header bytes, followed by the 512
data bytes: hence the 536-byte buffer. On a write the driver supplies 24 zero header bytes; Apple's driver may
rebuild the tag it puts on the cable, which the drive ignores.

Nothing in Apple's driver's state changes because of these requests beyond its own counters, and the OS's
requests and the driver's are simply interleaved on the drive's queue. Each request takes about the time of one
block read.

## 4. The interface: DEVICE_CONTROL

A program calls `DEVICE_CONTROL(ec, '-#15#3', dc)` with the `dctype` record of the `SysCall` unit: `dcversion` must
be 1 (else 623), `dccode` is the function below, `dcdata[0..9]` (called `ar10` in the driver) carries ten
longints in and out. Unknown codes answer 623.

Every result that involves the drive comes back with a **stage** in `ar10[0]`: 0 means done and good, anything
else says where the call stopped:

| Stage | Meaning |
|---|---|
| 1 | no such device |
| 2 | not a hard disk served by HDISK |
| 3 | no space in the system global area for the request |
| 4 | `CVT_BUFF_ADDR` refused (its error in `ar10[1]`) |
| 5 | Apple's driver refused the request (its error in `ar10[1]`) |
| 6 | the request ended unsuccessfully (error or warning in `ar10[3]`) |
| 7 | the disk's driver is not Apple's ProFile driver on a parallel port |
| 8 | the drive is a Widget or of an unknown type |
| 9 | block number not allowed |
| 10 | busy: another transfer is under way |
| 11 | write-verify is on (writes only) |
| 12 | no channel probed on this device (writes only) |
| 13 | client, sequence or length not allowed (writes only) |
| 14 | no free session |
| 15 | not an open session |

Where a stage of 0 is reported for a transfer, `ar10[2]` is the success flag, `[3]` the hard error, `[4]` the
transfer count, `[5]` the service flag, `[6]` the `MICROTIMER` ticks the transfer took, `[7]` the device record
address, `[8]` the restarts, `[9]` the block number handed to Apple's driver.

The functions:

| Code | Name | In | Out |
|---|---|---|---|
| 1 | identify | - | `'ESPF'`, `'DEMO'`, revision (6 = F), calls so far, slot, channel, system global base, request block size, control block address, reads so far |
| 2 | echo | `ar10[0]` | `ar10[1] = ar10[0] + 1` |
| 3 | read a block | device; block number as the drive receives it | stage and transfer results; the block in the buffer |
| 4 | device table entry | device | device record fields (entry point, control block, slot, channel, device number, type, block count) |
| 5 | fetch buffer bytes | offset 0 to 500 | 36 bytes of the buffer in `ar10[1..9]` |
| 6 | disk driver state | device | HDISK control block fields (extension, remap flag, drive type, size, restarts, queue length, verify flag, retry limit, hardware base) |
| 7 | ESProFile status | device | as 3, the block `$FFFEFD` |
| 8 | is it Apple's ProFile driver? | device | verdict as stages 0, 1, 2, 7, 8; hardware addresses; error counters, retry and sparing settings, remap flag, drive type |
| 9 | PROBE | device | stage; `ar10[1]` 1 = channel found, 0 = not; status, length, client and sequence of the reply; ticks; the device is remembered as probed when found |
| 10 | REQUEST | device, client, sequence, length; payload already in the buffer | stage; the request block (kind 1) written |
| 11 | read the channel | device | as 9, without the probe memory |
| 12 | CANCEL | device, client, sequence | stage; a kind 4 block written |
| 13 | POLL | device, client, sequence | stage; a kind 3 block written |
| 14 | store buffer bytes | offset 0 to 500; 36 bytes in `ar10[1..9]` | 623 while a transfer is under way |
| 15 | channel state | - | probed device record, last client written, writes, reads, revision, build flags, last sequence, last kind, open sessions |
| 17 | open a session | - | `ar10[1]` = client number from the driver's pool (1 to 8); stage 14 when all eight are open; no I/O |
| 18 | close a session | client number | the closing CANCEL's results when one went out; stage 15 if not open |

Codes 16 and 19 exist only in test builds (a write of an ordinary block; a pretended probe) and answer 623 in the
ordinary build. They are never to run on a real Lisa.

**The buffer** is the driver's one 536-byte block: 24 header bytes, then 512 data bytes. In a channel block the
20-byte channel header occupies data bytes 0 to 19, so a payload starts at buffer byte 44. A program stores a
request's payload there with code 14, 36 bytes a call, and fetches a reply's payload from there with code 5, 36
bytes a call. The payload length of a reply is in `ar10[3]` of the read.

A channel write (codes 10, 12, 13, 18) builds the whole block in that buffer from the call's numbers: the program
supplies only the payload bytes and the three numbers. The probe and the channel read (9, 11) take the reply apart
into `ar10[2..5]`; when the probe says "no channel" those fields are garbage from whatever block the drive
answered with and must not be interpreted.

## 5. The probe

Code 9 reads `$FFFEF8` through Apple's driver and looks at the 512 data bytes: the signature `LISAFUJI`, version 3
and kind 2 mean the drive has a channel, and the driver remembers that device record as probed. Any other
content, zeros or a stray block, clears the memory. A probe is per device: the internal and the external drive
of a Lisa 2/10 are probed separately, and the driver remembers one probed device at a time.

Until a probe has succeeded on a device, every channel write to it is refused with stage 12. That is the rule
of the protocol's section 7 made mechanical: no program can write the channel block to a drive that has not
identified itself, whatever the program does.

## 6. Sessions

The driver hands out client numbers and takes care of the opening and closing cancels of the protocol.

- Code 17 opens a session: the lowest free number of 1 to 8 is handed out, eight programs at once at most. No
  I/O is done, so a program may open its session before it has probed any disk.
- The session's **opening CANCEL is sent by the driver, deferred to the session's first REQUEST**: the first code
  10 for that client is preceded by a kind 4 block for it (one more block write). So the cancel always follows
  the probe (writes are gated on it) and precedes the first request, whatever order the program used, and a
  program cannot forget it. An explicit code 12 counts as the opening cancel too. The driver keeps, per session,
  whether the drive has heard of it and the last sequence number it sent.
- Code 18 closes a session: when the session has written to the probed drive, a CANCEL with the session's last
  sequence number goes to that drive first. The session is closed whatever the write's outcome.
- The driver's shut-down code (`ddown`, run by `CDKILL`) **sends nothing**. The Device Drivers Manual has `ddown`
  run with interrupts off and not wait for I/O; a block request there, waiting for the disk interrupt, would
  never wake. A program therefore closes its sessions before it kills the driver. The drive's ten-minute slot
  recycling is the safety net for a program that dies without closing.
- Client numbers above 8 are not the driver's. A program that manages its own number may use codes 10 to 13 with
  it directly, sending its own cancels; such numbers do not collide with the pool.
- `CDKILL` followed by `CDMAKE` starts with an empty pool: no session survives the driver.

## 7. Rules for programs using the driver

- **Store the payload immediately before the request.** Every channel write builds its block in the one buffer
  and zeroes the rest of it; a deferred opening cancel inside code 10 is handled by the driver (it saves and
  restores the payload around it), but any other channel write between a store and its request destroys the
  stored bytes.
- **Do not interpret the reply fields of a failed probe** (`ar10[1] = 0`): they come from a stray block.
- **Close sessions before `CDKILL`**: in the quit path, in the put-away path (a tool's process stays resident
  after Put Away, so the put-away branch must detach), and before any `CDKILL` for any reason.
- **Leave the driver attached when another program has sessions open**: code 15 reports the number of open
  sessions; a program that finds it above its own should close its session and not `CDKILL`.
- **Bracket each `DEVICE_CONTROL` that reaches the drive with `Sched_Class(false)` and `Sched_Class(true)`**, as
  the ESPFuji tools do around their file calls: the calling process sleeps in `BLK_REQ` for the duration of one
  block transfer.
- **Expect 623 from a store or a request while a transfer is under way** (another program's call is in the
  driver), wait a moment and try again.
- **Expect 751 from any call when the driver is not attached**, and 794 at the first call when the code file is
  missing from the boot volume.
- **Never cancel a request from the driver's queue**: a queued HDISK request cannot be cancelled; the driver
  waits for every transfer to complete before freeing it, and so must any program that bypasses it.
- **Use one driver id for all programs** that are to share one entry; the test tool uses 100, from the range 1 to
  128 left free for testing. The release id is still to be chosen.
- Nothing in the driver changes the write-verify flag, the sparing setting or any other setting of Apple's
  driver, and programs should not change them unasked either.

## 8. What has not been shown

- Nothing has run on a real Lisa or against a real ESProFile: the timing, the error path (LisaEm never refuses a
  block) and the real status block are unknown.
- A ProFile on a parallel card: the driver finds it through the `configinfo` table, reasoned from the source and
  not yet seen.
- A write-verify read of the channel block: the protocol says it is harmless, the driver refuses to write while
  verify is on until a Lisa has shown it so.
- A run-time-loaded driver calling another driver's `dskio` had not been done before this one; it works in LisaEm.

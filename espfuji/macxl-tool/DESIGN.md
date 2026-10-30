# ESPFuji XL: the ESPFuji mailbox for a Macintosh XL (MacWorks)

Tim, 30 Oct 2026: "Can you start working on a Macintosh XL version of the ESPFuji please?"
A Lisa running MacWorks (XL 3.0, Plus, Plus II) is a Mac. Its ESProFile is an HFS (or MFS) volume, and the
Lisa Office System tools cannot run. ESPFuji XL is two pieces:

1. **ESPFuji XL**, a small 68000 Mac application (Retro68, System 6 and 7, Plus and up, so MacWorks Plus and
   Plus II; MacWorks XL 3.0 with System 3.x is a later check). It talks to the drive through a file,
   MAILBOX.DATA, exactly as the Lisa tools do, and offers Hello, Wi-Fi, Set Clock, and Open Page.
2. **Mailbox protocol version 2**, a small firmware change, because MacWorks tags its blocks differently.

## What MacWorks writes on a ProFile (measured, 30 Oct)

A MacWorks Plus II (System 7.1) ESProFile image, 65,520 blocks:
- The 20-byte ProFile tag holds the **Mac File Manager tag** in bytes 0-11: file number (0-3), flags and fork
  (4-5), logical block in the file (6-7), modification date (8-11). Bytes 12-18 are zero.
- **Byte 19 is a checksum**: the whole 532-byte page XORs to 0 (17,105 of 17,186 Mac-tagged blocks).
- Blocks never written by MacWorks carry the formatter's fill ('5555...' tags, data 0x35).

So under MacWorks, tag bytes 4-5, where firmware 1.0 looks for the Lisa file ID, hold a flag word shared by
thousands of blocks (0x8100 on 732 blocks, 0x0300 on 1,293 ...). Firmware 1.0 would remember that word and then
hand the reply to reads of unrelated Mac files. A version 1 request from a Mac must never happen.

## Protocol version 2

Same page as version 1 (signature "LISAFUJI", sequence, kind, length, 492 bytes of payload), with:
- bytes 8-9 = **2**;
- bytes 16-17 = **k**, this page's index in the mailbox file (0-based);
- bytes 18-19 = **n**, the number of pages in the mailbox (the file is one contiguous run of blocks).

The drive, on a version 2 request written to block b: the mailbox is blocks b-k .. b-k+n-1. It serves the reply
on a read of any block in that range, whatever the tag says. On the swap it keeps the page acceptable: if the
stored page XORed to 0, it fixes tag byte **19** (MacWorks) so it still does. The reply page is version 2 and
echoes k/n in bytes 16-19. Version 1 behaves exactly as in 1.0 (tag file ID, byte 11).

Firmware 1.0 rejects any page whose version is not 1 (`mb_parse`), so an XL request reaching an old drive is
simply stored and never answered: the application says "no answer: this drive needs ESPFuji 1.1 or later". No
risk of a wrong swap.

Contiguity: the application creates MAILBOX.DATA with AllocContig (one run of allocation blocks), writes every
page once as an idle version 2 page (kind 0), and refuses to use a file it cannot prove contiguous. MacWorks maps
the volume's 512-byte blocks to ProFile blocks one to one, so a contiguous file is a contiguous block range.

## The exchange (the application)

- Request: next sequence number, next page k (round robin over n = 64 pages), write the 512 bytes at file
  offset k x 512 with the File Manager, cache bypassed (`noCacheMask`), then flush the volume's writes.
- Reply: read the other pages of the mailbox in turn, cache bypassed, until one comes back as kind 2 with the same
  sequence number; give other applications time between reads (WaitNextEvent), give up after 45 s (an OPEN can
  take 30 s on the drive). A page still in a cache comes back as the stored idle or request page and is skipped.
- OPEN, then READ offset count in 492-byte pieces into a text window, then CLOSE. CLOCK sets the Mac's clock
  (SetDateTime). HELLO and WIFI show the drive's own lines.

## Where it lives

- Firmware: `sw/ESProFile/mailbox_core.h` (version 2 parsing, `mb_xl_range`, `mb_build_data_v`, `mb_swap_data_xl`),
  `ESProFile_FujiNet.ino` (the request and reply carry the version; `mbXlFirst`/`mbXlCount`; `mbOnRead(blk, blockNum)`),
  and the emulator's read hook, which now passes the block number. Version 1 paths are unchanged.
- The Mac application: `espfuji/macxl-tool` (this folder). Host test: `tests/xl_end_to_end.c`, a pretend MacWorks
  ProFile with the firmware's version 2 logic and the application's page code (13 checks).

## Not yet known (needs the real XL)

- Whether MacWorks's ProFile driver checks the byte-19 checksum on read (the swap keeps it right either way).
- Whether the System 6 cache honours noCacheMask under MacWorks (the round robin covers a stale page anyway).
- MacWorks XL 3.0 (System 3.x, MFS): AllocContig and noCacheMask are not there; a later version may use the
  ProFile driver directly.

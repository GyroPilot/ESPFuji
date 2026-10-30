# ESPFuji XL (beta): the ESPFuji network on a Macintosh XL

**Not supported, and not yet tried on a real Macintosh XL.** This is the first public test build. ESPFuji questions and
bug reports go to this repository's Issues, never to Alex Anderson-McLeod.

A Lisa running MacWorks is a Mac, and its ESProFile holds a Mac volume, so the Lisa Office System tools cannot run there.
ESPFuji XL is a small Mac application that talks to the same ESPFuji drive through a file, `MAILBOX.DATA`, which it makes
in its own folder. It needs:

- **MacWorks Plus or MacWorks Plus II** (Mac Plus ROM or later, System 6 or 7), booting from an ESProFile;
- **ESPFuji firmware 1.1b1 or later** on that ESProFile (firmware 1.0 never answers a Macintosh XL request);
- about 32 KB free in one piece on that volume.

MacWorks XL 3.0 (System 3.x) is not covered yet.

## Install

1. Flash ESPFuji 1.1b1 onto the ESProFile as in [the main README](../README.md) (back up the stock firmware first),
   with `espfuji.ini` on its SD card as before.
2. Copy **ESPFuji XL** onto the ESProFile's Mac volume: `ESPFuji XL 0.1d1.bin` from the Releases page is MacBinary
   (StuffIt Expander or any MacBinary tool turns it into the application); the `.dsk` is an 800K disk with it on.
3. Open it. Its first line should read *MAILBOX.DATA: 64 pages in one piece, ready*.

## Use

Network menu: **Hello** (the drive's version, uptime, Wi-Fi, address), **Wi-Fi**, **Set Clock from the Network** (sets the
Mac's clock: UTC, or local time with `timezone=` in espfuji.ini), **Open Page...** (any http or https address, up to
32 KB, shown as text), **News Headlines**. Command-period stops a request; a request with no answer gives up after 45 s.

## How it works

See [DESIGN.md](DESIGN.md). In short: MacWorks writes the Mac's own file tags on the ProFile, so the drive cannot find
the mailbox by the Lisa file ID as it does for the Lisa tools. A Macintosh XL request (mailbox protocol version 2) says
which page of MAILBOX.DATA it is on and how many pages the file has, and the drive answers on that range of blocks only.

## Build

[Retro68](https://github.com/autc04/Retro68): `RETRO68_TOOLCHAIN=/path/to/toolchain bash build.sh`
Host test (any C compiler): `cd tests && cc -I../src -I../../../sw/ESProFile -o xl_end_to_end xl_end_to_end.c ../src/xlproto.c && ./xl_end_to_end`

## Tested so far (30 October 2026)

- Host test: 13 checks pass (the mailbox range, no reply on another file's blocks, MacWorks's checksum byte kept,
  firmware 1.0 ignores version 2 pages). The firmware's own Lisa mailbox test still passes.
- System 7.6.1 in QEMU (Quadra 800): the application starts, makes MAILBOX.DATA in one piece with 64 valid pages,
  and sends a correct HELLO; with no ESPFuji drive there it reports "no answer in 45 s" as it should.
- Not yet: a real Lisa running MacWorks with an ESProFile on 1.1b1. Reports welcome.

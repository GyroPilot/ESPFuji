# ESPFuji (private, not yet published)

FujiNet-style network access for the Apple Lisa through an **ESProFile** (Alex Anderson-McLeod's ESP32 ProFile
emulator). This is a fork of https://github.com/alexthecat123/ESProFile; the ESProFile hardware and the original
firmware are Alex's work. **Not the official ESProFile firmware.**

**Status: private.** It becomes public only after Alex confirms (asked 26 Oct 2026; his earlier email said a
forked/separate repo under GPL 3 is fine). Until then nothing here is published or announced.

## What it does

- Firmware (branch history from 72495a1 on top of Alex's 7ee7ee4; files `sw/ESProFile/ESProFile_FujiNet.ino`,
  `mailbox_core.h`, small hooks in the emulator): the ProFile emulation is unchanged. A "mailbox" file on the Lisa's
  ProFile volume carries requests (HELLO, WIFI, STATUS, OPEN url, READ, CLOSE) written by the Lisa and replies read
  back; a network task on the ESP32's second core joins Wi-Fi (espfuji.ini on the SD card, never stored in flash)
  and fetches web pages over HTTP/HTTPS.
- `espfuji/lisa-tool/`: the Lisa Office System tool ESPFuji (Lisa Pascal sources, REV C, tool number 419324).
- `espfuji/tools/`: bench and soak scripts (USB serial), the flasher wrapper, and dc42_to_image.py (LisaEm ProFile
  .dc42 to ESProFile raw .image, with the ProFile 5:1 interleave).

Proven on a real Lisa 2/10, 25 Oct 2026: a web page fetched through the drive (HTTP 200). Overnight soak, 1,500
fetches: no heap leak; an occasional 45 s missing reply to fix.

## Licence

GPL 3 intended (per Alex's email); a LICENSE file is added when the repo goes public.

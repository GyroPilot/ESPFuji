# ESPFuji

FujiNet-style network access for the Apple Lisa through an **ESProFile** (Alex Anderson-McLeod's ESP32 ProFile
emulator). This is a fork of https://github.com/alexthecat123/ESProFile; the ESProFile hardware and the original
firmware are Alex's work. **Not the official ESProFile firmware.**

**Status (27 Oct 2026): beta, proven on a real Lisa 2/10.** Alex Anderson-McLeod, the ESProFile's author,
agreed by email on 27 Oct 2026 to these changes living in a separate repository of their own, with the firmware
and the Lisa-side tool kept together. Nothing here is an official ESProFile release; questions about the hardware
go to Alex, questions about ESPFuji to this repository.

## What it does

- Firmware (branch history from 72495a1 on top of Alex's 7ee7ee4; files `sw/ESProFile/ESProFile_FujiNet.ino`,
  `mailbox_core.h`, small hooks in the emulator): the ProFile emulation is unchanged. A "mailbox" file on the Lisa's
  ProFile volume carries requests (HELLO, WIFI, STATUS, OPEN url, READ, CLOSE) written by the Lisa and replies read
  back; a network task on the ESP32's second core joins Wi-Fi (espfuji.ini on the SD card, never stored in flash)
  and fetches web pages over HTTP/HTTPS.
- `espfuji/lisa-tool/`: the Lisa Office System tool ESPFuji (Lisa Pascal sources, REV C, tool number 419324).
- `espfuji/tools/`: bench and soak scripts (USB serial), the flasher wrapper, and dc42_to_image.py (LisaEm ProFile
  .dc42 to ESProFile raw .image, with the ProFile 5:1 interleave).

Proven on a real Lisa 2/10, 25 Oct 2026: a web page fetched through the drive (HTTP 200). Firmware 0.3 is on
`main`. Firmware 0.4 (branch `fw-0.4`, work in progress) bounds every fetch so the drive always answers within
30 s and adds a TRACE command; it still has a known crash on servers that answer after several seconds, so stay
on 0.3 for now.

## Licence

GNU GPL version 3 (see LICENSE at the repository root), as agreed with Alex. The ESProFile hardware design and the
original emulator firmware are Alex's work (https://github.com/alexthecat123/ESProFile), used here with his
permission; the ESPFuji additions (the mailbox firmware, the Lisa tool, the tools) are by Tim O'Connor (GyroPilot).

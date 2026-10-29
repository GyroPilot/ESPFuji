# ESPFuji

Network access for the Apple Lisa through an **ESProFile**, Alex Anderson-McLeod's ESP32 ProFile emulator
(https://github.com/alexthecat123/ESProFile). No serial adapter, no extra box: the drive the Lisa already boots
from joins your Wi-Fi and fetches web pages for Lisa Office System programs.

This repository is a fork of Alex's, published with his agreement (27 Oct 2026). The ESProFile hardware and the
original emulator firmware are Alex's work; questions about the hardware go to him, questions about ESPFuji here.
**ESPFuji is not the official ESProFile firmware.** Beta: it works on one Lisa 2/10 and has been soaked for hours
on the bench, and that is the extent of the testing so far.

## How it works

The Lisa cannot talk to a drive except through files, so the drive watches for one. A Lisa program writes a
request into a page of `MAILBOX.DATA` on the ProFile volume (for example `OPEN http://...`); the firmware sees the
signed page as it is written, fetches the page on its second CPU core, and the next time the Lisa reads the file
it gets the reply instead of the stored page. The ProFile emulation itself is unchanged. Requests: `HELLO`,
`WIFI`, `STATUS`, `CLOCK`, `OPEN <url>`, `READ <offset> <count>`, `CLOSE`, `TRACE`. Pages up to 32 KB; HTTP and
HTTPS; every fetch ends within 30 s so the Lisa never waits in vain.

## What you need

- An ESProFile (external or internal) with a **plain ESP32** module ("ESP32 Dev Module" in Alex's instructions).
  An image for the ESP32-S3 module is built too, but it has not been tried on real hardware yet.
- Its SD card, a 2.4 GHz Wi-Fi network (the ESP32 does not do 5 GHz) and a computer with Python 3 for the
  one-time flash.
- A Lisa running the Lisa Office System (3.0 or 3.1), booting from that ESProFile or with it as a second drive.

## Install, once

1. **Back up the drive's firmware.** With the ESProFile's USB cable in and the Lisa off:
   `python -m pip install esptool`, then `python espfuji/tools/flash_espfuji_release.py backup COM7`
   (your port name instead of COM7; see the script's header for macOS and Linux). Keep the file: it puts Alex's
   firmware back any time with `restore`.
2. **Flash ESPFuji:** `python espfuji/tools/flash_espfuji_release.py flash COM7 ESPFuji_<version>_esp32.merged.bin`
   (the image is on the Releases page).
3. **Tell it your network:** copy `sw/ESProFile/espfuji.ini.example` to the root of the SD card as `espfuji.ini`
   and fill in `ssid=` and `password=`. The drive reads it at power-up; the password is never written into the
   ESP32's flash. Upper and lower case in the network name need not match: the drive uses the name as broadcast.
4. **Power up** and watch the USB serial port at 115200 if you like: it prints `ESPFuji: on <network> as <address>`.

The drive works exactly as before for everything else: boot, LOS, Xenix, MacWorks. If no Lisa program ever writes
a mailbox page, nothing changes.

## Use, on the Lisa

- **ESPFuji test tool** (`espfuji/lisa-tool`, floppy on the Releases page): copy it to the ESProFile's volume and
  open it. It makes `MAILBOX.DATA` beside itself the first time. **H** asks the drive HELLO (version, uptime, Wi-Fi,
  address, free memory); **W** the Wi-Fi state; **G** fetches a web page and shows it; **S** the counters.
- **LisaFuji 1.2 REV V** (news, Wikipedia, the space station, weather; https://github.com/GyroPilot/Apple-LisaFuji)
  finds the drive by itself: put it on the ESProFile's volume, run the test tool once so the mailbox file exists,
  and its first line reads "the ESProFile" instead of a serial port. Every page then comes through the drive.
  The other FujiNet tools follow.

## Bench, without a Lisa

The same requests can be typed on the drive's USB serial port (115200): `python espfuji/tools/bench_talk.py COM7`
resets the drive and sends HELLO, WIFI, a fetch and READ. `soak.py` repeats fetches for hours and watches the heap
(point it at your own web server, not somebody else's). `dc42_to_image.py` converts a LisaEm ProFile `.dc42` to
the raw `.image` the ESProFile uses, both ways.

## Status and versions

- **0.5** (Releases): bounded fetches (30 s), CLOCK from SNTP, TRACE timings, and the fix for a crash the 0.3/0.4
  firmware had on servers that answered after several seconds (0 restarts in 275 such fetches; before, about 3%).
- 0.5.2 (source; images follow the next Lisa test): a fetch without an address fails at once and the drive joins
  the network again (0.5 waited for three failures); an idle keepalive; a page is read to its full length (0.5
  could cut a long HTTPS page short); a request typed on the USB port can no longer take a Lisa's reply.
- 0.3: the first build proven on the Lisa 2/10 (25 Oct 2026).

Known limits: one page open at a time; HTTPS without certificate checks (like the FujiNet); no POST yet; the
Lisa reads a page in 492-byte pieces, so a 30 KB page takes a few seconds to arrive.

## Building it yourself

Arduino CLI with the esp32 core 3.3.x and SdFat; `FUJINET_MAILBOX` is defined in `sw/ESProFile/build_opt.h`.
Plain ESP32: `arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app sw/ESProFile`.
ESP32-S3: `--fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,PartitionScheme=huge_app`. The `.merged.bin` in the output
is the file the flasher writes at address 0. `mailbox_core.h` has a PC test (`sw/ESProFile/tests`).

## Licence

GNU GPL version 3 (see LICENSE at the repository root), as agreed with Alex. The ESProFile hardware design and the
original emulator firmware are Alex's (https://github.com/alexthecat123/ESProFile), used here with his
permission; the ESPFuji additions (the mailbox firmware, the Lisa tool, the tools) are by Tim O'Connor (GyroPilot).

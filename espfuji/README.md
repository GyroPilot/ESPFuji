# ESPFuji

Network access for the Apple Lisa through an **ESProFile**, Alex Anderson-McLeod's ESP32 ProFile emulator
(https://github.com/alexthecat123/ESProFile). No serial adapter, no extra box: the drive the Lisa already boots
from joins your Wi-Fi and fetches web pages for Lisa Office System programs.

**ESPFuji is not the official ESProFile firmware, and this is not the official ESProFile repository.** Alex has approved of this being published, but he does not support the ESPFuji firmware in any way: the mailbox code, the Lisa tools and the flasher are Tim O'Connor's, and questions or bugs about them go to this repository's Issues, never to Alex. The ESProFile boards and the stock emulator firmware remain Alex's work and his to support.
Tested on one Lisa 2/10 and two ESProFile boards; the soaks and the Lisa runs are listed under Status below.

## How it works

The Lisa cannot talk to a drive except through files, so the drive watches for one. A Lisa program writes a
request into a page of `MAILBOX.DATA` on the ProFile volume (for example `OPEN http://...`); the firmware sees the
signed page as it is written, fetches the page on its second CPU core, and the next time the Lisa reads the file
it gets the reply instead of the stored page. The ProFile emulation itself is unchanged. Requests: `HELLO`,
`WIFI`, `STATUS`, `CLOCK`, `OPEN <url>`, `READ <offset> <count>`, `CLOSE`, `TRACE`. Pages up to 32 KB; HTTP and
HTTPS; every fetch ends within 30 s so the Lisa never waits in vain.

## What you need

- An ESProFile with a **plain ESP32** module: look at the module, its metal can says ESP32-WROOM-32 ("ESP32 Dev
  Module" in Alex's instructions). An image for the ESP32-S3 module (the can says ESP32-S3) is built too, but it
  has not been tried on real hardware yet. Tested so far on one external ESProFile; an internal one is the same
  board, untested.
- Its SD card, a 2.4 GHz Wi-Fi network (the ESP32 does not do 5 GHz) and a computer with Python 3 for the
  one-time flash.
- A Lisa running the Lisa Office System 3.1 (3.0 should work; untested), booting from that ESProFile or with it as a
  second drive.

## Install, once

Everything below comes from the Releases page: the image, `flash_espfuji_release.py`, `espfuji.ini.example`
(no need to clone the repository).

1. **Install esptool once**, in a venv (macOS and Linux say `python3`, Windows `python`):
   `python3 -m venv espfuji-env` then `espfuji-env/bin/python -m pip install esptool`
   (Windows: `espfuji-env\Scripts\python`). The script works with esptool 4 and 5.
2. **Back up the drive's firmware.** With the ESProFile's USB cable in and the Lisa OFF (the USB cable powers the
   ESP32 for flashing): `espfuji-env/bin/python flash_espfuji_release.py backup /dev/cu.usbserial-XXXX`
   (Windows: `COM7`; the script's header explains the port names). The backup lands in the folder you ran it from.
3. **Flash ESPFuji:** `espfuji-env/bin/python flash_espfuji_release.py flash <port> ESPFuji_<version>_esp32.merged.bin`.
   Flashing does not touch the SD card: your disk image stays as it is.
4. **Going back:** `flash_espfuji_release.py restore <port> esprofile_backup_<date>.bin` puts Alex's firmware back
   any time.
5. **Tell it your network:** copy `espfuji.ini.example` to the root of the SD card as `espfuji.ini` and fill in
   `ssid=` and `password=` (and `timezone=` if you want CLOCK in local time). The drive reads it at power-up; the
   password is never written into the ESP32's flash, but it sits in plain text on the card, so a guest or IoT
   network is a reasonable choice. Upper and lower case in the network name need not match.
6. **Power up.** To watch it join: a serial terminal on the USB port at 115200 (`screen /dev/cu.usbserial-XXXX
   115200` on macOS, PuTTY on Windows) prints `ESPFuji: on <network> as <address>`.

The drive works exactly as before for everything else: boot, LOS, Xenix, MacWorks. If no Lisa program ever writes
a mailbox page, nothing changes.

## Use, on the Lisa

The test tool is the diagnostic (hello, Wi-Fi, one raw page, counters) and makes the mailbox file; LisaFuji and LisaCheckers are the
applications, and they find the drive by themselves.

- **ESPFuji test tool** (`espfuji/lisa-tool`; the `.dc42` floppy image on the Releases page goes onto a Lisa through a
  Floppy Emu, a BlueSCSI, LisaEm, or a real drive written from a Mac): copy it to the ESProFile's volume and open it. It makes `MAILBOX.DATA` beside itself the first time. **H** asks the drive HELLO (version, uptime, Wi-Fi,
  address, free memory); **W** the Wi-Fi state; **G** fetches a web page and shows it; **S** the counters.
- **LisaFuji 1.2** (30 October 2026 or later; news, Wikipedia, the space station, weather, the clock;
  https://github.com/GyroPilot/Apple-LisaFuji/releases) finds the drive by itself: put it on the ESProFile's volume, run
  the test tool once so the mailbox file exists, and its first line reads "the ESProFile" instead of a serial port.
  Every page then comes through the drive. **LisaCheckers 1.2**, **LisaSalvo 1.2**, **LisaTwinClock 1.3** and the
  **LisaFuji Browser 1.2 beta** (all at https://github.com/GyroPilot, 30 Oct 2026) do the same (first complete
  Checkers game through an ESProFile: 29 Oct 2026). **Earlier releases of both (LisaFuji 1.1, the
  1.2 beta of 23 October, LisaCheckers 1.1) do not know the drive and only talk to a serial FujiNet: update them.**
  **Every tool must sit in the same ProFile window as the test tool**: a Lisa with two ESProFiles shows two
  windows both named ProFile, and a tool looks for MAILBOX.DATA only on its own volume.

## Use, on a Macintosh XL (MacWorks), beta

**ESPFuji XL** (`espfuji/macxl-tool`, 1.1b1 pre-release): a Mac application for MacWorks Plus and Plus II that talks
to the same drive (Hello, Wi-Fi, set the Mac's clock, open a page, news). It needs firmware **1.1b1 or later**: 1.0
never answers it. Not yet tried on a real Macintosh XL; see [its README](macxl-tool/README.md).

## Bench, without a Lisa

The same requests can be typed on the drive's USB serial port (115200): `python espfuji/tools/bench_talk.py COM7`
resets the drive and sends HELLO, WIFI, a fetch and READ. `soak.py` repeats fetches for hours and watches the heap
(point it at your own web server, not somebody else's). `dc42_to_image.py` converts a LisaEm ProFile `.dc42` to
the raw `.image` the ESProFile uses, both ways.

## Status and versions

- **0.5** (Releases): bounded fetches (30 s), CLOCK from SNTP, TRACE timings, and the fix for a crash the 0.3/0.4
  firmware had on servers that answered after several seconds (0 restarts in 275 such fetches; before, about 3%).
- **0.5.2** (Releases, 28 Oct 2026): a fetch without an address fails at once and the drive joins the network
  again (0.5 waited for three failures); an idle keepalive; a page is read to its full length (0.5 could cut a
  long HTTPS page short); a request typed on the USB port can no longer take a Lisa's reply. Idle soak 8 h clean;
  LisaFuji's four services, CLOCK and a complete LisaCheckers game through the drive on a Lisa 2/10.
- **1.0** (Releases, 30 Oct 2026): the 0.5.3 code with the version changed. 0.5.3 added `timezone=` in espfuji.ini, so
  CLOCK answers local time (checked on a second board flashed from macOS, then a day of idle soak with CLOCK every
  15 minutes, 0 cut, 0 lost). Nothing else moved between the last test and the release.
- The flasher works with esptool 4 and 5 (esptool 4 is what a Python 3.9, e.g. Apple's, installs) and falls back
  from 460800 to 115200 baud when the link drops. Tested 29 Oct 2026 on macOS 15.8.1 (Apple's Python 3.9, esptool 4)
  with a second ESProFile: backup, flash, boot; and on Windows 10 (Python 3.12, esptool 5).
- **1.1b1** (pre-release, 30 Oct 2026): the Macintosh XL (mailbox protocol version 2) and ESPFuji XL 0.1d1.
  The Lisa side is unchanged from 1.0. Host-tested and run in an emulator only.
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

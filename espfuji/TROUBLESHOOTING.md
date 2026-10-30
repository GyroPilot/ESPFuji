# ESPFuji: when something does not work

Everything here was seen on real boards in October 2026. Check the first line of HELLO (over USB, 115200) before
anything else: it names the firmware and says whether espfuji.ini was read.

**The status LED is red and the Lisa does not see the drive.** The board has no SD card it can use. Alex's firmware
halts without a card. Seat the card and press reset (or unplug and replug the USB cable). Seen after every card swap.

**HELLO says "espfuji.ini read: ssid no".** The file is missing or misnamed on the card: it must be `espfuji.ini` in
the card's root, plain text, `ssid=` and `password=` lines (see espfuji.ini.example). The card is FAT32.

**WIFI says "status connected" but "ip 0.0.0.0", or a fetch says "ERROR no wifi (no address)".** The board joined the
network but got no address (DHCP). 0.5.2 and later rejoin after 20 s; on older firmware press reset. If it keeps
happening, give the board a reservation in your router.

**The ssid in espfuji.ini differs from the network's name by case.** The board says so in HELLO ("espfuji.ini says X,
the network is Y: using the broadcast name") and joins anyway.

**A page arrives short, or STATUS says "truncated".** 0.5.1 and earlier cut bodies at the first read; 0.5.2 reads to
Content-Length. If it still happens, the server closed early; TRACE shows the last fetch.

**CLOCK answers UTC.** Add `timezone=` to espfuji.ini (0.5.3) with a POSIX TZ string such as `EST5EDT,M3.2.0,M11.1.0`
(US Eastern) or `GMT0BST,M3.5.0/1,M10.5.0` (UK). CLOCK answers "ERROR no time yet (SNTP waiting)" for the first
minute after power-up.

**The Lisa tool says "no mailbox on -#13- (ec 948)".** The tool is running from a volume that is not the ESPFuji
image. On a Lisa with two ProFiles both windows are named "ProFile": put the tool (LisaFuji, LisaCheckers ...) beside
the ESPFuji tool on the ESProFile's volume, or rename the volume so you can tell them apart.

**The Lisa tool says "mailbox did not answer: ESPFuji?"** The drive answered nothing within the wait. Check HELLO
over USB; with firmware before 0.5.2, never read the USB console while a Lisa is using the drive (a reply could go to
the console instead of the Lisa).

**Flashing from a Mac fails every other time, or "Invalid head of packet".** The USB link on some Macs (a 2018 Mac
mini) drops bytes at 460800 baud and fails connects. The shipped flasher tries 460800 with 20 connect attempts and
then 115200 (6 minutes for a backup, 80 seconds for a flash). Use the USB cable alone to power the board; the Lisa
off.

**esptool says "unrecognized command" (flash-id, read-flash).** esptool 4 (Apple's Python 3.9 gets it) uses the
underscore names. The shipped flasher asks `esptool version` and picks the right names; run the flasher, not esptool.

**Nothing on the USB console at all.** 115200 baud, 8N1. On macOS the port is /dev/cu.usbserial-XXXX (plain ESP32)
or /dev/cu.usbmodemXXXX (ESP32-S3); on Windows see Device Manager, Ports. Opening the port on macOS pulses the reset
line: the board reboots and prints its banner, which is normal.

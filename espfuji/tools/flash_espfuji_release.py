"""flash_espfuji_release.py -- put ESPFuji on an ESProFile from any computer (Windows, macOS, Linux).

Needs Python 3 (3.8 or later) and esptool.  The one-time set-up, in a folder of your choice:

   Windows:      python -m venv espfuji-env
                 espfuji-env\\Scripts\\python -m pip install esptool
   macOS, Linux: python3 -m venv espfuji-env
                 espfuji-env/bin/python -m pip install esptool

(A "venv" keeps esptool out of the system Python; on macOS 15 installing outside one can be refused or take ten
minutes building a wheel.  Apple's pip may say it is old: harmless.)  Then, with the ESProFile's USB cable in and
the Lisa OFF (the USB cable alone powers the ESP32 for flashing; the Lisa must not be powering it), run the script
with that Python -- "python" below means espfuji-env\\Scripts\\python on Windows, espfuji-env/bin/python elsewhere:

   python flash_espfuji_release.py backup  COM7            save what is on the board now (ALWAYS do this first)
   python flash_espfuji_release.py flash   COM7 ESPFuji_0.5.3_esp32.merged.bin
   python flash_espfuji_release.py restore COM7 esprofile_backup_<date>.bin

The port: Windows COMn (Device Manager, Ports); macOS /dev/cu.usbserial-XXXX for a plain ESP32 board (or
/dev/cu.SLAB_USBtoUART with Silicon Labs' own driver) and /dev/cu.usbmodemXXXX for an ESP32-S3; Linux /dev/ttyUSB0
or /dev/ttyACM0.  "ls /dev/cu.*" before and after plugging the cable in shows which name appeared.

Which image: a plain ESP32 module (its metal can says ESP32-WROOM-32) takes ESPFuji_<ver>_esp32.merged.bin; an
ESP32-S3 module (the can says ESP32-S3) takes ESPFuji_<ver>_esp32s3.merged.bin.  If unsure, run "backup" first:
its first lines name the chip.  The merged image is the whole flash (bootloader, partition table, firmware),
written at address 0, so one file is all a board needs.  Flashing does not touch the SD card: your disk image and
espfuji.ini stay as they are.  The backup file is written in the folder you run the script from; keep it, "restore"
puts Alex's original firmware back any time.

Works with esptool 4 (the version a Python 3.9 gets, e.g. Apple's) and esptool 5.  Tried at 460800 baud first;
if the link drops (seen on a Mac mini: every other connect fails, and faster than 115200 loses bytes), the same
operation is repeated at 115200, which took 6 minutes for a backup and 80 seconds for a flash on macOS 15.
"""
import subprocess, sys, time


def esptool_major():
    """4 or 5: esptool 5 renamed its operations (flash-id, read-flash ...); 4 knows only flash_id, read_flash ..."""
    try:
        out = subprocess.run([sys.executable, "-m", "esptool", "version"], capture_output=True, text=True, timeout=60)
        text = (out.stdout + out.stderr).strip().splitlines()
        for line in reversed(text):
            tok = line.strip().split()
            if tok and tok[-1][0].isdigit():
                return int(tok[-1].split(".")[0])
    except Exception:
        pass
    return 4                                            # the safe guess: esptool 5 still accepts the old names


def op(name):
    return name if MAJOR >= 5 else name.replace("-", "_")


def esptool(*args):
    cmd = [sys.executable, "-m", "esptool"] + list(args)
    print("+", " ".join(cmd), flush=True)
    return subprocess.call(cmd)


def esptool_retry(port, *args):
    """The operation at 460800, then once more at 115200 if the link dropped (macOS CP210x, 29 Oct 2026)."""
    for baud in ("460800", "115200"):
        rc = esptool("--port", port, "--baud", baud, "--connect-attempts", "20", *args)
        if rc == 0:
            return 0
        if baud == "460800":
            print("... that failed; trying again at 115200 (slower, more robust)", flush=True)
            time.sleep(2)
    return rc


def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    what, port = sys.argv[1], sys.argv[2]
    print("esptool %d.x" % MAJOR, flush=True)
    if what == "backup":
        out = time.strftime("esprofile_backup_%Y%m%d_%H%M.bin")
        info = subprocess.run([sys.executable, "-m", "esptool", "--port", port, "--connect-attempts", "20", op("flash-id")],
                              capture_output=True, text=True)
        text = info.stdout + info.stderr
        print(text)
        if info.returncode != 0:
            print("FAILED: esptool could not talk to the board on", port, "(see above)"); return info.returncode
        size = "4MB"
        for line in text.splitlines():
            if "flash size" in line.lower():
                size = line.split(":")[-1].strip().split()[0]
        units = {"KB": 1024, "MB": 1024 * 1024}
        nbytes = int(size[:-2]) * units[size[-2:].upper()] if size[-2:].upper() in units else int(size, 0)
        print("backing up", size, "(%d bytes) to" % nbytes, out, "in this folder", flush=True)
        time.sleep(2)                                   # the probe's reset: give the port a moment (macOS)
        rc = esptool_retry(port, op("read-flash"), "0", str(nbytes), out)
        print("SAVED" if rc == 0 else "FAILED", out); return rc
    if what in ("flash", "restore"):
        if len(sys.argv) < 4:
            print("give the image file"); return 2
        img = sys.argv[3]
        rc = esptool_retry(port, op("write-flash"), "0", img)
        print("DONE: unplug and replug the board (or press its reset button)" if rc == 0 else "FAILED"); return rc
    print(__doc__); return 2


MAJOR = esptool_major()

if __name__ == "__main__":
    sys.exit(main())

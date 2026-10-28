"""flash_espfuji_release.py -- put ESPFuji on an ESProFile from any computer (Windows, macOS, Linux).

Needs Python 3 and esptool:   python -m pip install esptool
Then, with the ESProFile's USB cable in and the Lisa OFF:

   python flash_espfuji_release.py backup  COM7            save what is on the board now (ALWAYS do this first)
   python flash_espfuji_release.py flash   COM7 ESPFuji_0.5_esp32.merged.bin
   python flash_espfuji_release.py restore COM7 esprofile_backup_<date>.bin

On macOS the port is /dev/cu.SLAB_USBtoUART (plain ESP32 boards) or /dev/cu.usbmodem* (ESP32-S3 boards);
on Linux /dev/ttyUSB0 or /dev/ttyACM0.  The merged image is the whole flash (bootloader, partition table,
firmware), written at address 0, so one file is all a board needs.  Which image: the plain ESP32 module
(ESP32 Dev Module, as in Alex's instructions) takes ESPFuji_<ver>_esp32.merged.bin; an ESP32-S3 module takes
ESPFuji_<ver>_esp32s3.merged.bin.  If unsure, run "backup" first: its first line names the chip.
"""
import subprocess, sys, time

def esptool(*args):
    cmd = [sys.executable, "-m", "esptool"] + list(args)
    print("+", " ".join(cmd), flush=True)
    return subprocess.call(cmd)

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    what, port = sys.argv[1], sys.argv[2]
    if what == "backup":
        out = time.strftime("esprofile_backup_%Y%m%d_%H%M.bin")
        info = subprocess.run([sys.executable, "-m", "esptool", "--port", port, "flash-id"],
                              capture_output=True, text=True)
        text = info.stdout + info.stderr
        print(text)
        size = "4MB"
        for line in text.splitlines():
            if "flash size" in line.lower():
                size = line.split(":")[-1].strip().split()[0]
        print("backing up", size, "to", out, flush=True)
        rc = esptool("--port", port, "--baud", "460800", "read-flash", "0", size, out)
        print("SAVED" if rc == 0 else "FAILED", out); return rc
    if what in ("flash", "restore"):
        if len(sys.argv) < 4:
            print("give the image file"); return 2
        img = sys.argv[3]
        rc = esptool("--port", port, "--baud", "460800", "write-flash", "0", img)
        print("DONE: unplug and replug the board (or press its reset button)" if rc == 0 else "FAILED"); return rc
    print(__doc__); return 2

if __name__ == "__main__":
    sys.exit(main())

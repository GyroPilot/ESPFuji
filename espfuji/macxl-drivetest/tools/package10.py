#!/usr/bin/env python3
"""package10.py: Drive Test XL 1.0 release candidate -> release/share/<folder> (ExtFS layout for put_releases.sh),
release/dc42/<folder>.dc42 (800K Disk Copy 4.2 with the app and READ ME), release/bin/<folder>.bin (MacBinary II),
release/SHA256SUMS.txt.  Same layout as src/moof-setup/package_xl11.py.   ~/serialenv/bin/python3 tools/package10.py"""
import os, struct, binascii, hashlib, shutil, machfs, macresources
H = os.path.dirname(os.path.abspath(__file__)) + "/.."; OUT = H + "/release"; name = "Drive Test XL"; folder = name + " 1.0"
README = ("Drive Test XL 1.0 (22 November 2026), for the Macintosh XL (MacWorks) with an ESProFile running ESPFuji.\r\r"
"It checks the drive you start from: it writes a file of known data, reads it back and compares every byte, while the drive is busy on Wi-Fi, and shows the drive's own error counters.\r\r"
"How to use it\r"
"1. Copy \"Drive Test XL\" to the ESProFile volume and open it there.\r"
"2. Wait about a minute after power-up, so the drive has joined Wi-Fi. Network > Wi-Fi shows whether it has.\r"
"3. Test > Quick Test (Apple-T): 5 passes, about three minutes on a 16 MHz XL. Test > Long Test (Apple-L): 50 passes, about half an hour. Apple-period stops a test.\r"
"4. Read the last line: RESULT: PASS or RESULT: FAIL.\r\r"
"What a pass does\r"
"Each pass writes a 256 KB file (DRIVETEST.DATA) to the volume, reads it back past the system's disk cache and compares it byte by byte. While that goes on, the drive fetches a small web page, so its radio is sending. The file is deleted when the test ends.\r\r"
"What the lines mean\r"
"\"N bytes differ\": bytes that came back different from what was written. It should be 0.\r"
"\"drive: ...\": the drive's own STATUS line before and after. \"refused\" counts writes the drive did not accept because a byte was missing; \"odd\" counts reads the Macintosh did not take in full. Both should stay as they were.\r"
"FAIL means a byte differed, the system reported a disk error, or refused/odd went up.\r"
"PASS with the note \"no counters\": the firmware is older than ESPFuji 1.1.3 and reports no counters; the byte comparison still counts.\r"
"PASS \"but no page was fetched\": the disk was fine but the radio was not busy; check the Wi-Fi settings on the card.\r"
"Test > Fetch a Page During Each Pass can be switched off to test the disk alone.\r\r"
"Notes\r"
"For MacWorks (Plus, Plus II) only; it does not run under the Lisa Office System.\r"
"It uses the same MAILBOX.DATA file as ESPFuji XL: run one of them at a time. The drive needs ESPFuji 1.1b2 or later; the counters need 1.1.3.\r"
"If a test FAILS: stop using the drive for work you care about, update the ESProFile to the newest ESPFuji, and test again.\r"
"Network > Hello, Wi-Fi and Drive Status show the drive's own lines.\r\r"
"1.0: the first release. Tested on a real Macintosh XL (MacWorks Plus II, System 7.5.5, 16 MHz XLerator, ESPFuji 1.1.3); starts on System 6.0.8 with 1 MB in an emulator.\r"
"By GyroPilot (Tim O'Connor), github.com/GyroPilot.\r").encode("mac_roman")
def dc42(img, label):
    c = 0
    for (w,) in struct.iter_unpack(">H", img): c = (c + w) & 0xFFFFFFFF; c = ((c >> 1) | (c << 31)) & 0xFFFFFFFF
    n = label.encode("mac_roman"); return bytes([len(n)]) + n.ljust(63, b"\0") + struct.pack(">IIIIBBH", len(img), 0, c, 0, 1, 0x22, 0x0100) + img
shutil.rmtree(OUT, ignore_errors=True)
for sub in ("share", "dc42", "bin"): os.makedirs(os.path.join(OUT, sub))
b = bytearray(open(H + "/build/DriveTestXL.bin", "rb").read()); dl, rl = struct.unpack(">II", b[83:91]); dp = (dl + 127) // 128 * 128
data, rsrc = bytes(b[128:128+dl]), bytes(b[128+dp:128+dp+rl]); types = {r.type for r in macresources.parse_file(rsrc)}
assert b"0.1d" not in rsrc and b"(test)" not in rsrc and b"Drive Test XL 1.0" in rsrc and {b"BNDL", b"ICN#", b"FREF", b"vers"} <= types
nb = name.encode("mac_roman"); b[1] = len(nb); b[2:65] = nb.ljust(63, b"\0")
typ, cre = bytes(b[65:69]), bytes(b[69:73]); flags = (((b[73] << 8) | b[101]) | 0x2000) & ~0x0100
rmname = "READ ME - %s.txt" % name
D = os.path.join(OUT, "share", folder); os.makedirs(D + "/.rsrc"); os.makedirs(D + "/.finf")
open(f"{D}/{name}", "wb").write(data); open(f"{D}/.rsrc/{name}", "wb").write(rsrc)
open(f"{D}/.finf/{name}", "wb").write(typ + cre + struct.pack(">H", flags) + bytes(22))
open(f"{D}/{rmname}", "wb").write(README); open(f"{D}/.finf/{rmname}", "wb").write(b"TEXTttxt" + bytes(24))
v = machfs.Volume(); v.name = folder
f = machfs.File(); f.type, f.creator, f.flags, f.data, f.rsrc = typ, cre, flags, data, rsrc; v[name] = f
r = machfs.File(); r.type, r.creator, r.data = b"TEXT", b"ttxt", README; v["READ ME"] = r
img = v.write(size=800*1024, align=512, desktopdb=True, bootable=False)
open(os.path.join(OUT, "dc42", folder + ".dc42"), "wb").write(dc42(img, folder))
b[73] = flags >> 8; b[101] = flags & 0xFF; b[122] = b[123] = 129; b[124:126] = struct.pack(">H", binascii.crc_hqx(bytes(b[:124]), 0))
open(os.path.join(OUT, "bin", folder + ".bin"), "wb").write(b)
open(os.path.join(OUT, "READ ME - Drive Test XL (PC text).txt"), "wb").write(README.decode("mac_roman").replace("\r", "\r\n").encode("utf-8"))
with open(os.path.join(OUT, "SHA256SUMS.txt"), "w") as s:
    for sub, fn in (("dc42", folder + ".dc42"), ("bin", folder + ".bin")):
        s.write("%s  %s/%s\n" % (hashlib.sha256(open(os.path.join(OUT, sub, fn), "rb").read()).hexdigest(), sub, fn))
print("%s: app %s/%s flags %04x rsrc %d, READ ME %d bytes" % (folder, typ.decode(), cre.decode(), flags, rl, len(README)))

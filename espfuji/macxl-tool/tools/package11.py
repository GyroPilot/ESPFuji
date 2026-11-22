#!/usr/bin/env python3
"""package11.py: ESPFuji XL 1.1 -> release/share/<folder> (ExtFS layout for put_releases.sh), release/dc42/<folder>.dc42
(800K Disk Copy 4.2 with the app and READ ME), release/bin/<folder>.bin (MacBinary II), release/SHA256SUMS.txt.
~/serialenv/bin/python3 tools/package11.py   (after bash build.sh)"""
import os, struct, binascii, hashlib, shutil, machfs, macresources
H = os.path.dirname(os.path.abspath(__file__)) + "/.."; OUT = H + "/release"; name = "ESPFuji XL"; folder = name + " 1.1"
README = ("ESPFuji XL 1.1 (23 November 2026), for the Macintosh XL with an ESProFile running ESPFuji 1.1b2 or later.\r\r"
"1.1: new: Network > Drive Status (Apple-D) shows the drive's own STATUS line: its requests and, with ESPFuji 1.1.3 or later, its disk counters (\"disk wr .. refused .. rd .. odd ..\"; refused and odd should be 0). To test a drive, use Drive Test XL.\r"
"1.0: the first release. Tested on a real Macintosh XL (MacWorks Plus II, System 7.5.5) and in emulators (System 6.0.2 to 7.5.5); earlier test versions (0.1d) can be thrown away.\r\r"
"Copy \"ESPFuji XL\" to the ESProFile volume and open it there (it can replace 1.0; MAILBOX.DATA stays).\r"
"Network: Hello, Wi-Fi, Drive Status, Set Clock, Open Page (pages under about 8 KB until the drive's large-page bug is fixed), News.\r"
"Apple-period (or Escape) stops a request.\r"
"Runs on System 6.0.2 and later; aimed at a 1 MB XL.\r"
"1.1 was tested on a real Macintosh XL (MacWorks Plus II, System 7.5.5, 16 MHz XLerator, ESPFuji 1.1.3) and in emulators (System 6.0.8 and 7.6.1).\r"
"By GyroPilot (Tim O'Connor), github.com/GyroPilot.\r").encode("mac_roman")
def dc42(img, label):
    c = 0
    for (w,) in struct.iter_unpack(">H", img): c = (c + w) & 0xFFFFFFFF; c = ((c >> 1) | (c << 31)) & 0xFFFFFFFF
    n = label.encode("mac_roman"); return bytes([len(n)]) + n.ljust(63, b"\0") + struct.pack(">IIIIBBH", len(img), 0, c, 0, 1, 0x22, 0x0100) + img
shutil.rmtree(OUT, ignore_errors=True)
for sub in ("share", "dc42", "bin"): os.makedirs(os.path.join(OUT, sub))
b = bytearray(open(H + "/build/ESPFujiXL.bin", "rb").read()); dl, rl = struct.unpack(">II", b[83:91]); dp = (dl + 127) // 128 * 128
data, rsrc = bytes(b[128:128+dl]), bytes(b[128+dp:128+dp+rl]); types = {r.type for r in macresources.parse_file(rsrc)}
assert b"1.1d" not in rsrc and b"(test" not in rsrc and b"ESPFuji XL 1.1" in rsrc and b"Drive Status" in rsrc and {b"BNDL", b"ICN#", b"FREF", b"vers"} <= types
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
open(os.path.join(OUT, "READ ME - ESPFuji XL 1.1 (PC text).txt"), "wb").write(README.decode("mac_roman").replace("\r", "\r\n").encode("utf-8"))
with open(os.path.join(OUT, "SHA256SUMS.txt"), "w") as s:
    for sub, fn in (("dc42", folder + ".dc42"), ("bin", folder + ".bin")):
        s.write("%s  %s/%s\n" % (hashlib.sha256(open(os.path.join(OUT, sub, fn), "rb").read()).hexdigest(), sub, fn))
print("%s: app %s/%s flags %04x rsrc %d, READ ME %d bytes" % (folder, typ.decode(), cre.decode(), flags, rl, len(README)))

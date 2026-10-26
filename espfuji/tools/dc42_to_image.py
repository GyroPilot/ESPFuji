"""dc42_to_image.py  24-Oct-2026: a LisaEm ProFile image (DC42: 84-byte header, all data blocks, then all 20-byte tags,
both in logical order) -> the ESProFile's raw profile.image (per block: 20-byte tag then 512 data, in the ProFile's
5:1 interleave order, as TorZidan's LisaFileSystemTool README section 2.2 describes).  The reverse direction too.
usage: py -3.12 dc42_to_image.py in.dc42 out.image      or      py -3.12 dc42_to_image.py in.image out.dc42
"""
import struct, sys

DELTA = (0, 4, 8, 12, 0, 4, 8, -4, 0, 4, -8, -4, 0, -12, -8, -4)
def interleave5(n): return n + DELTA[n % 16]

def dc42_to_image(src, dst):
    d = open(src, "rb").read()
    datasize, tagsize = struct.unpack(">II", d[0x40:0x48])
    magic = struct.unpack(">H", d[0x52:0x54])[0]
    assert magic == 0x0100, "not a DC42 file"
    nblk = datasize // 512
    assert tagsize == nblk * 20, f"tag size {tagsize} is not 20 bytes per block"
    data = d[84:84 + datasize]; tags = d[84 + datasize:84 + datasize + tagsize]
    out = bytearray(nblk * 532)
    for n in range(nblk):
        p = interleave5(n) * 532
        out[p:p + 20] = tags[n * 20:(n + 1) * 20]
        out[p + 20:p + 532] = data[n * 512:(n + 1) * 512]
    open(dst, "wb").write(out)
    return nblk

def image_to_dc42(src, dst, name=b"profile"):
    raw = open(src, "rb").read()
    nblk = len(raw) // 532
    assert nblk * 532 == len(raw), "not a whole number of 532-byte blocks"
    data = bytearray(nblk * 512); tags = bytearray(nblk * 20)
    for n in range(nblk):
        p = interleave5(n) * 532
        tags[n * 20:(n + 1) * 20] = raw[p:p + 20]
        data[n * 512:(n + 1) * 512] = raw[p + 20:p + 532]
    hdr = bytearray(84)
    hdr[0] = len(name); hdr[1:1 + len(name)] = name
    struct.pack_into(">II", hdr, 0x40, len(data), len(tags))
    def cks(b):
        c = 0
        for i in range(0, len(b) - 1, 2):
            c = (c + ((b[i] << 8) | b[i + 1])) & 0xffffffff
            c = ((c >> 1) | ((c & 1) << 31)) & 0xffffffff
        return c
    struct.pack_into(">IIBBH", hdr, 0x48, cks(data), cks(tags), 0x03, 0x96, 0x0100)
    open(dst, "wb").write(bytes(hdr) + bytes(data) + bytes(tags))
    return nblk

if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    if dst.lower().endswith(".dc42"): n = image_to_dc42(src, dst)
    else: n = dc42_to_image(src, dst)
    print(f"{src} -> {dst}: {n} blocks ({n * 512 // 1024} KB)")

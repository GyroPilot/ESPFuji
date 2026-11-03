#!/usr/bin/env python3
"""mkicon.py: ESPFuji XL's icon (original art, 3 Nov 2026): the Macintosh XL with the Fuji on its screen and
Wi-Fi, plus a cat's head sitting on top of the case beside the waves (Tim: "Maybe a cat head next to the Wi-Fi
symbol on top of lisa"), and the cat is for Alex, who made the ESProFile (Tim, 3 Nov 2026). FujiNet XL has the same drawing without the cat.  Writes src/icons.r ('ICN#' 128: icon, then mask) and icon_preview.png (8x).
   python3 tools/mkicon.py"""
import math
W = 32
px = [[0] * W for _ in range(W)]
def put(x, y):
    if 0 <= x < W and 0 <= y < W: px[y][x] = 1
def hline(x0, x1, y):
    for x in range(x0, x1 + 1): put(x, y)
def vline(x, y0, y1):
    for y in range(y0, y1 + 1): put(x, y)
def rect(x0, y0, x1, y1): hline(x0, x1, y0); hline(x0, x1, y1); vline(x0, y0, y1); vline(x1, y0, y1)
# the XL: a wide case (two pixels thick), the screen at the left, the floppy slot at the right, the keyboard below
rect(1, 11, 30, 27); rect(2, 12, 29, 26)
rect(4, 14, 17, 23)
hline(21, 27, 20); hline(21, 27, 21)
hline(3, 28, 29); hline(3, 28, 30)
# the Fuji on the screen: flat crater top, white snow cap with a jagged snow line, dark slopes
hline(9, 12, 16)
put(8, 17); put(13, 17)
for x in (7, 8, 10, 13, 14): put(x, 18)
for y, x0, x1 in ((19, 6, 15), (20, 6, 15), (21, 5, 16), (22, 5, 16)): hline(x0, x1, y)
# Wi-Fi over the slot side
cx, cy = 24.0, 10.0
for r in (3.0, 6.0, 9.0):
    for y in range(W):
        for x in range(W):
            dx, dy = x + 0.5 - cx, cy - (y + 0.5)
            if abs(math.hypot(dx, dy) - r) <= 0.75 and 45 <= math.degrees(math.atan2(dy, dx)) <= 135: put(x, y)
# the cat: a head sitting on the case, ears up, eyes and nose left white
CAT = [".#.....#.",
       ".##...##.",
       "#########",
       "##.###.##",
       "#########",
       "####.####",
       ".#######."]
for dy, row in enumerate(CAT):
    for dx, c in enumerate(row):
        if c == "#": put(7 + dx, 4 + dy)
# mask: the case and the keyboard solid, the waves as drawn
mask = [row[:] for row in px]
for y in range(11, 28):
    for x in range(1, 31): mask[y][x] = 1
def hexrows(g): return ["".join("%02X" % sum(g[y][xb * 8 + k] << (7 - k) for k in range(8)) for xb in range(4)) for y in range(W)]
rows = hexrows(px) + hexrows(mask)
with open("src/icons.r", "w") as f:
    f.write("/* icons.r: ESPFuji XL's icon, by tools/mkicon.py (original art: a Macintosh XL with a Fuji on its screen, Wi-Fi and a cat). */\n")
    f.write("data 'ICN#' (128, \"ESPFuji XL\") {\n")
    for i in range(0, 64, 4): f.write('\t$"' + "".join(rows[i:i + 4]) + '"\n')
    f.write("};\n")
try:
    from PIL import Image
    im = Image.new("L", (W, W), 255)
    for y in range(W):
        for x in range(W):
            if px[y][x]: im.putpixel((x, y), 0)
    im.resize((W * 8, W * 8), Image.NEAREST).save("icon_preview.png")
except ImportError:
    pass

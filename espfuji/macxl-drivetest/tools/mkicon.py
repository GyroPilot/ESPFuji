#!/usr/bin/env python3
"""mkicon.py [A|B|C]: Drive Test XL's icon (original art, 22 Nov 2026; Tim: "make an icon for it").
Three proposals in the family of ESPFuji XL's icon (the same Macintosh XL case and Wi-Fi waves, no cat):
  A  a check mark on the XL's screen                 B  a ProFile drive under a magnifying glass, a check mark in the glass
  C  a test trace (pulse line) on the XL's screen
With no argument: writes icon/proposals.png (each at 8x and at real size).  With a letter: writes src/icons.r."""
import math, sys, os
W = 32
def blank(): return [[0] * W for _ in range(W)]
def mk(g):
    def put(x, y, v=1):
        if 0 <= x < W and 0 <= y < W: g[y][x] = v
    def hline(x0, x1, y, v=1):
        for x in range(x0, x1 + 1): put(x, y, v)
    def vline(x, y0, y1, v=1):
        for y in range(y0, y1 + 1): put(x, y, v)
    def rect(x0, y0, x1, y1): hline(x0, x1, y0); hline(x0, x1, y1); vline(x0, y0, y1); vline(x1, y0, y1)
    return put, hline, vline, rect
def wifi(put, cx, cy, radii=(3.0, 6.0, 9.0)):
    for r in radii:
        for y in range(W):
            for x in range(W):
                dx, dy = x + 0.5 - cx, cy - (y + 0.5)
                if abs(math.hypot(dx, dy) - r) <= 0.75 and 45 <= math.degrees(math.atan2(dy, dx)) <= 135: put(x, y)
def xl_case(g):
    put, hline, vline, rect = mk(g)
    rect(1, 11, 30, 27); rect(2, 12, 29, 26)      # the case, two pixels thick
    rect(4, 14, 17, 23)                           # the screen
    hline(21, 27, 20); hline(21, 27, 21)          # the floppy slot
    hline(3, 28, 29); hline(3, 28, 30)            # the keyboard
    wifi(put, 24.0, 10.0)
    return put, hline, vline, rect
def case_mask(g):
    m = [r[:] for r in g]
    for y in range(11, 28):
        for x in range(1, 31): m[y][x] = 1
    return m
def design_A():
    g = blank(); put, hline, vline, rect = xl_case(g)
    for x, y in ((6, 19), (7, 20), (8, 21), (9, 22), (10, 21), (11, 20), (12, 19), (13, 18), (14, 17), (15, 16)):   # a bold check mark
        put(x, y); put(x, y - 1)
    return g, case_mask(g)
def design_C():
    g = blank(); put, hline, vline, rect = xl_case(g)
    pts = [(5, 19), (6, 19), (7, 19), (8, 18), (9, 16), (10, 21), (11, 22), (12, 17), (13, 19), (14, 19), (15, 19), (16, 19)]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        for y in range(min(y0, y1), max(y0, y1) + 1): put(x1, y)
        put(x0, y0)
    return g, case_mask(g)
def design_B():
    g = blank(); put, hline, vline, rect = mk(g)
    rect(1, 18, 30, 29); rect(2, 19, 29, 28)      # the ProFile: a low wide box
    hline(5, 20, 24)                              # its front groove
    rect(24, 22, 26, 24)                          # the ready lamp
    hline(3, 28, 31)                              # its shadow / feet
    cx, cy, r = 13.5, 9.5, 7.5                    # the magnifying glass above it
    for y in range(W):
        for x in range(W):
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            if abs(d - r) <= 0.9: put(x, y)
    for k in range(7):                            # its handle, down to the right
        put(19 + k, 15 + k); put(20 + k, 15 + k); put(19 + k, 16 + k)
    for x, y in ((9, 10), (10, 11), (11, 12), (12, 13), (13, 12), (14, 11), (15, 10), (16, 9), (17, 8), (18, 7)):   # a check mark in the glass
        put(x, y); put(x, y - 1)
    m = [row[:] for row in g]
    for y in range(18, 30):
        for x in range(1, 31): m[y][x] = 1
    for y in range(W):
        for x in range(W):
            if math.hypot(x + 0.5 - cx, y + 0.5 - cy) <= r: m[y][x] = 1
    return g, m
D = {"A": design_A, "B": design_B, "C": design_C}
here = os.path.dirname(os.path.abspath(__file__)) + "/.."
def hexrows(g): return ["".join("%02X" % sum(g[y][xb * 8 + k] << (7 - k) for k in range(8)) for xb in range(4)) for y in range(W)]
if len(sys.argv) > 1:
    g, m = D[sys.argv[1]](); rows = hexrows(g) + hexrows(m)
    with open(here + "/src/icons.r", "w") as f:
        f.write("/* icons.r: Drive Test XL's icon, by tools/mkicon.py %s (original art, 22 Nov 2026). */\n" % sys.argv[1])
        f.write("data 'ICN#' (128, \"Drive Test XL\") {\n")
        for i in range(0, 64, 4): f.write('\t$"' + "".join(rows[i:i + 4]) + '"\n')
        f.write("};\n")
    print("wrote src/icons.r, design", sys.argv[1])
else:
    from PIL import Image, ImageDraw
    S = 8; im = Image.new("L", (3 * (W * S + 40) + 40, W * S + 150), 255); dr = ImageDraw.Draw(im)
    for i, k in enumerate("ABC"):
        g, m = D[k](); x0 = 40 + i * (W * S + 40)
        dr.rectangle([x0 - 1, 49, x0 + W * S, 50 + W * S], outline=160)
        for y in range(W):
            for x in range(W):
                if g[y][x]: dr.rectangle([x0 + x * S, 50 + y * S, x0 + x * S + S - 1, 50 + y * S + S - 1], fill=0)
        dr.text((x0 + W * S // 2 - 4, 20), k, fill=0)
        for y in range(W):                      # real size, on white and on the Finder's grey
            for x in range(W):
                if g[y][x]: im.putpixel((x0 + 40 + x, 60 + W * S + 20 + y), 0)
        for y in range(-4, W + 4):
            for x in range(-4, W + 4):
                gx, gy = x0 + 140 + x, 60 + W * S + 20 + y
                inside = 0 <= x < W and 0 <= y < W
                v = 0 if (inside and g[y][x]) else (255 if (inside and m[y][x]) else (255 if (x + y) % 2 else 0))
                im.putpixel((gx, gy), v)
    im.save(here + "/icon/proposals.png"); print("wrote icon/proposals.png")

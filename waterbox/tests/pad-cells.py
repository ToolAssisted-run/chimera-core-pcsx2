#!/usr/bin/env python3
"""Which of padtest.elf's readouts changed.

padtest.elf (tests/own) draws a DualShock 2 as the MACHINE sees it: a grid of
cells, one per button, each showing that button's pressure. Hold one button and
exactly one cell changes - so pressing the button a package declares as "Circle"
and watching which cell moves is the only honest way to ask whether the wire
from the frontend to the pad is the one the names promise.

Usage: pad-cells.py <idle.tga> <held.tga>
prints the names of the cells that differ, one per line.

The two frames must have the same PARITY. A PS2 in an interlaced mode scans out
half a picture per frame and the deinterlacer reconstructs the rest, so frames
an odd number apart differ everywhere and the answer is noise.
"""
import sys

# The cell each readout occupies at 640x448, measured from the program's own
# output. x is the number beside the icon, not the icon: an icon is drawn the
# same whether or not the button is down, and only the number moves.
CELLS = {
    "Select":   (78, 88, 27, 37),
    "L3":       (208, 218, 27, 37),
    "R3":       (338, 348, 27, 37),
    "Start":    (468, 478, 27, 37),
    "Up":       (78, 144, 49, 67),
    "Right":    (208, 274, 49, 67),
    "Down":     (338, 404, 49, 67),
    "Left":     (468, 534, 49, 67),
    "L2":       (78, 144, 73, 91),
    "R2":       (208, 274, 73, 91),
    "L1":       (338, 404, 73, 91),
    "R1":       (468, 534, 73, 91),
    "Triangle": (78, 144, 95, 115),
    "Circle":   (208, 274, 95, 115),
    "Cross":    (338, 404, 95, 115),
    "Square":   (468, 534, 95, 115),
}


def read_tga(path):
    d = open(path, "rb").read()
    w = d[12] | (d[13] << 8)
    h = d[14] | (d[15] << 8)
    off = 18 + d[0]
    px = d[off:off + w * h * 4]
    rows = [px[y * w * 4:(y + 1) * w * 4] for y in range(h)]
    if not (d[17] & 0x20):
        rows.reverse()   # TGA rows are bottom-up unless the origin bit says otherwise
    return w, h, rows


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    w, h, idle = read_tga(sys.argv[1])
    w2, h2, held = read_tga(sys.argv[2])
    if (w, h) != (w2, h2):
        sys.exit(f"frames differ in size: {w}x{h} vs {w2}x{h2}")
    for name, (x0, x1, y0, y1) in CELLS.items():
        if y1 >= h or x1 >= w:
            continue
        changed = any(held[y][x * 4:x * 4 + 3] != idle[y][x * 4:x * 4 + 3]
                      for y in range(y0, y1 + 1) for x in range(x0, x1 + 1))
        if changed:
            print(name)


if __name__ == "__main__":
    main()

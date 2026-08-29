#!/usr/bin/env python3
# How much the picture changes from one frame to the next, averaged over a
# directory of the gate harness's .tga dumps.
#
# It exists for one leg: an interlaced machine hands over one FIELD per frame,
# and a core that does not put the fields back together shows a picture that
# climbs and falls every frame. That is not something a hash can see - every
# frame differs from the last either way - so the question has to be asked in
# pixels: how FAR apart are consecutive frames.
#
# usage: frame-motion.py <dir of f*.tga>   -> prints the mean absolute
#        difference between consecutive frames, 0 to 255
import glob
import os
import struct
import sys


def read_tga(path):
    """The harness writes uncompressed 32-bit bottom-up TGA and nothing else."""
    with open(path, "rb") as f:
        data = f.read()
    idlen, cmaptype, imgtype = data[0], data[1], data[2]
    width, height = struct.unpack_from("<HH", data, 12)
    depth = data[16]
    if imgtype != 2 or depth != 32:
        raise SystemExit(f"{path}: not an uncompressed 32-bit TGA")
    start = 18 + idlen
    pixels = data[start:start + width * height * 4]
    # luminance is enough, and cheap: this measures movement, not colour
    return width, height, bytes(pixels[i] for i in range(1, len(pixels), 4))


def main():
    files = sorted(glob.glob(os.path.join(sys.argv[1], "f*.tga")))
    if len(files) < 2:
        raise SystemExit("need at least two frames")
    frames = [read_tga(p) for p in files]
    total = 0.0
    for a, b in zip(frames, frames[1:]):
        if a[0] != b[0] or a[1] != b[1]:
            raise SystemExit("frames differ in size")
        total += sum(abs(x - y) for x, y in zip(a[2], b[2])) / len(a[2])
    print(f"{total / (len(frames) - 1):.4f}")


if __name__ == "__main__":
    main()

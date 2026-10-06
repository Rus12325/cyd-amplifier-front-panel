#!/usr/bin/env python3
"""Print a drawing/crop as ASCII art so it can be read without an image viewer.

    python probe_cyd_dim.py IMG [x0 y0 x1 y1] [cols]

The image is converted to greyscale, averaged down to `cols` columns, and each
cell is turned into a character by ink density.  Dense strokes (lines, digits)
come out as '#'/'%', blank paper as spaces.
"""
import sys

import numpy as np
from PIL import Image

RAMP = " .:-=+*#%@"


def art(path, box=None, cols=150):
    im = Image.open(path).convert("L")
    if box:
        im = im.crop(box)
    w, h = im.size
    rows = max(1, int(round(cols * h / w / 2.1)))     # chars are ~2x tall
    a = np.asarray(im, dtype=float)
    # block-average down to the character grid
    ys = np.linspace(0, h, rows + 1).astype(int)
    xs = np.linspace(0, w, cols + 1).astype(int)
    out = []
    for j in range(rows):
        line = []
        for i in range(cols):
            blk = a[ys[j]:ys[j + 1] or ys[j] + 1, xs[i]:xs[i + 1] or xs[i] + 1]
            ink = 1.0 - blk.mean() / 255.0 if blk.size else 0.0
            line.append(RAMP[min(len(RAMP) - 1, int(ink * 12))])
        out.append("".join(line).rstrip())
    return out


if __name__ == "__main__":
    src = sys.argv[1]
    box = tuple(int(v) for v in sys.argv[2:6]) if len(sys.argv) > 5 else None
    cols = int(sys.argv[6]) if len(sys.argv) > 6 else 150
    print("image %s  crop %s  cols %d" % (src, box, cols))
    for n, line in enumerate(art(src, box, cols)):
        print("%3d|%s" % (n, line))

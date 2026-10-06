#!/usr/bin/env python3
"""Independent audit: compare front_140.step (source) with front_140_cyd.step.

Reads both files from disk and reports, with no reliance on cyd_front.py's own
logging:

  * volume / bbox / face count of each
  * the front face at y = Y1: how much skin, how many separate openings, and
    the actual X x Z size of the opening
  * how much material the new part adds and removes relative to the source
  * a probe grid across the window: is it really open all the way through
"""
import os
import sys

import FreeCAD
import Part


def arg(i, default):
    """argv[i] if it is a real, readable file; otherwise the default."""
    if len(sys.argv) > i and os.path.isfile(sys.argv[i]):
        return sys.argv[i]
    return default


SRC = arg(1, r"D:\Downloads\front_140.step")
DST = arg(2, r"D:\Downloads\front_140_cyd.step")

Y1 = 5.6


def load(path):
    s = Part.read(path)
    return s.Solids[0] if len(s.Solids) == 1 else s


src, dst = load(SRC), load(DST)


def report(tag, s):
    bb = s.BoundBox
    print("%-8s vol %10.3f  faces %4d  solids %d  valid %s" %
          (tag, s.Volume, len(s.Faces), len(s.Solids), s.isValid()))
    print("         size %.2f x %.2f x %.2f   x %.2f..%.2f  y %.2f..%.2f  "
          "z %.2f..%.2f" %
          (bb.XLength, bb.YLength, bb.ZLength, bb.XMin, bb.XMax, bb.YMin,
           bb.YMax, bb.ZMin, bb.ZMax))


print("=== models")
report("source", src)
report("new", dst)

add = dst.cut(src).Volume
rem = src.cut(dst).Volume
print("=== difference")
print("material added by the new part : %10.3f mm^3" % add)
print("material removed               : %10.3f mm^3" % rem)
print("net volume change              : %+10.3f mm^3" % (dst.Volume - src.Volume))

# ---- the front face: skin area and its openings
print("=== front face at y = %.1f" % Y1)


def front_openings(s):
    """area of the front skin and the bbox of each hole in it"""
    skin = [f for f in s.Faces
            if f.Surface.__class__.__name__ == "Plane"
            and abs(f.Surface.Axis.y - 1.0) < 1e-9
            and abs(f.Surface.Position.y - Y1) < 1e-9]
    holes = []
    for f in skin:
        for w in f.Wires:
            if not f.isInside(w.CenterOfMass, 1e-6, True):
                bb = w.BoundBox
                holes.append((bb.XLength, bb.ZLength))
    return sum(f.Area for f in skin), holes


for tag, s in (("source", src), ("new", dst)):
    area, holes = front_openings(s)
    print("%-8s skin %9.1f mm^2   opening(s) %d" % (tag, area, len(holes)))
    for w, h in sorted(holes, key=lambda t: -t[0] * t[1])[:6]:
        print("            opening %.2f x %.2f mm" % (w, h))

# ---- is the window really open, and how big is it, measured by probing
print("=== window probe (rays along y through the panel, y = -10 .. 10)")
W, H = 57.6, 43.2


def open_at(x, z, s):
    """is there a clear channel through the part at (x, z)?"""
    col = Part.makeBox(0.2, 30.0, 0.2,
                       FreeCAD.Vector(x - 0.1, -10.0, z - 0.1))
    return s.common(col).Volume < 1e-6


for tag, s in (("source", src), ("new", dst)):
    xs = [x / 2.0 for x in range(-80, 81, 4)]
    zs = [z / 2.0 for z in range(-60, 61, 4)]
    grid = [(x, z) for x in xs for z in zs]
    clear = [p for p in grid if open_at(p[0], p[1], s)]
    if not clear:
        print("%-8s no through opening at all" % tag)
        continue
    cx = [p[0] for p in clear]
    cz = [p[1] for p in clear]
    print("%-8s through-opening %.1f x %.1f mm   %d/%d rays clear" %
          (tag, max(cx) - min(cx) + 2, max(cz) - min(cz) + 2, len(clear),
           len(grid)))

# ---- the active area of the display must see daylight
print("=== display window (active area %.1f x %.1f, centred)" % (W, H))
for tag, s in (("source", src), ("new", dst)):
    blocked = 0
    n = 0
    for i in range(-7, 8):
        for j in range(-5, 6):
            x, z = i * (W / 14.0), j * (H / 10.0)
            n += 1
            if not open_at(x, z, s):
                blocked += 1
    print("%-8s %d/%d rays blocked through the active area" % (tag, blocked, n))

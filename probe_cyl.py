#!/usr/bin/env python3
"""List Y-axis cylindrical features of a part (candidate bosses / holes).

    freecadcmd probe_cyl.py part.step [min_radius]
"""
import sys

import Part

args = [a for a in sys.argv[1:] if not a.lower().endswith((".py", ".pyc"))]
shape = Part.read(args[0])
rmin = float(args[1]) if len(args) > 1 else 0.6

print("file        : %s" % args[0])
print("solids      : %d" % len(shape.Solids))
bb = shape.BoundBox
print("bbox mm     : x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f"
      % (bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
print("faces       : %d" % len(shape.Faces))
hits = []
for i, f in enumerate(shape.Faces):
    s = f.Surface
    if s.__class__.__name__ != "Cylinder":
        continue
    ax = s.Axis
    if abs(ax.y) < 0.9 or s.Radius < rmin:
        continue
    c = s.Center
    fb = f.BoundBox
    hits.append((c.x, c.z, s.Radius, fb.YMin, fb.YMax))
print("Y-axis cylinders (r >= %.2f): %d" % (rmin, len(hits)))
for x, z, r, y0, y1 in sorted(hits):
    print("   x %8.2f  z %8.2f  r %.2f (d %.2f)  y %.2f..%.2f  len %.2f"
          % (x, z, r, 2 * r, y0, y1, y1 - y0))

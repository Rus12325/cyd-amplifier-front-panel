#!/usr/bin/env python3
"""Map a part's outer face from its mesh and count the openings in it.

    python skin_check.py part.stl [y_front] [window W H] [pitch]

The flat top of the part (the visible face) is rasterised on a fine grid: a
point is "face" if the mesh crosses the plane just under y_front there.  The
openings in that face are the background patches fully enclosed by it, so the
outside contour does not count.  There must be exactly one - the screen window,
at the expected place - for the face to be closed everywhere else.
"""
import sys

import numpy as np
import trimesh
from scipy import ndimage

args = sys.argv[1:]
PATH = args[0]
Y1 = float(args[1]) if len(args) > 1 else 5.6
WW, WH = (float(args[2]), float(args[3])) if len(args) > 3 else (60.6, 46.2)
STEP = float(args[4]) if len(args) > 4 else 0.5

mesh = trimesh.load(PATH)
T = np.asarray(mesh.triangles, dtype=float)
print("mesh              : %s  %d triangles  watertight %s"
      % (PATH, len(T), mesh.is_watertight))
bb = mesh.bounds
print("bbox mm           : x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f"
      % (bb[0][0], bb[1][0], bb[0][1], bb[1][1], bb[0][2], bb[1][2]))

# only the triangles that reach the outer plane can bound a point of it
band = T[T[:, :, 1].max(axis=1) >= Y1 - 0.05]
print("triangles on the face plane : %d" % len(band))


def hits(p0, p1, tri):
    d = p1 - p0
    e1 = tri[:, 1] - tri[:, 0]
    e2 = tri[:, 2] - tri[:, 0]
    h = np.cross(d, e2)
    a = np.einsum("ij,ij->i", e1, h)
    live = np.abs(a) > 1e-12
    if not live.any():
        return False
    inv = np.where(live, 1.0 / np.where(live, a, 1.0), 0.0)
    s = p0 - tri[:, 0]
    u = inv * np.einsum("ij,ij->i", s, h)
    live &= (u >= -1e-9) & (u <= 1 + 1e-9)
    q = np.cross(s, e1)
    dd = np.broadcast_to(d, e2.shape)
    v = np.einsum("ij,ij->i", dd, q) * inv
    live &= (v >= -1e-9) & (u + v <= 1 + 1e-9)
    t = np.einsum("ij,ij->i", e2, q) * inv
    live &= (t >= -1e-9) & (t <= 1 + 1e-9)
    return bool(live.any())


xs = np.arange(bb[0][0], bb[1][0] + STEP / 2, STEP)
zs = np.arange(bb[0][2], bb[1][2] + STEP / 2, STEP)
face = np.zeros((len(zs), len(xs)), dtype=bool)
for i, z in enumerate(zs):
    p0 = np.array([0.0, Y1 - 0.05, z])
    p1 = np.array([0.0, Y1 + 0.05, z])
    for j, x in enumerate(xs):
        p0[0] = p1[0] = x
        face[i, j] = hits(p0, p1, band)

area = face.sum() * STEP * STEP
print("flat outer face   : %.1f mm^2  x %.1f..%.1f  z %.1f..%.1f"
      % (area, xs[face.any(axis=0)].min(), xs[face.any(axis=0)].max(),
         zs[face.any(axis=1)].min(), zs[face.any(axis=1)].max()))

lab, n = ndimage.label(~face)
border = set(lab[0, :]) | set(lab[-1, :]) | set(lab[:, 0]) | set(lab[:, -1])
holes = []
for k in range(1, n + 1):
    if k in border:
        continue
    ys, xr = np.where(lab == k)
    holes.append((int((lab == k).sum()), xs[xr.min()], xs[xr.max()],
                  zs[ys.min()], zs[ys.max()]))
holes.sort(key=lambda h: -h[0])
print("openings in it    : %d" % len(holes))
good = len(holes) == 1
for size, x0, x1, z0, z1 in holes[:10]:
    ok = (abs(x1 - x0 - WW) < 2 * STEP and abs(z1 - z0 - WH) < 2 * STEP
          and abs(x0 + x1) < 2 * STEP and abs(z0 + z1) < 2 * STEP)
    good = good and ok
    print("   %6.1f mm^2  x %7.2f..%7.2f  z %7.2f..%7.2f  %s"
          % (size * STEP * STEP, x0, x1, z0, z1,
             "= the window" if ok else "<- NOT the window"))
print("\nRESULT:", "PASS - the outer face has exactly one opening, the window"
      if good else "FAIL - unexpected opening(s) in the outer face")
sys.exit(0 if good else 1)

#!/usr/bin/env python3
"""
Front panel for the 1.4x scaled enclosure, written as a real ISO-10303-21
(AP214) B-rep solid with millimetre units.

Geometry
--------
    plate   150 x 10 x 50 mm, scaled by FACTOR  ->  210 x 14 x 70 mm
    recess  SW x SH x SD mm for the CYD 2432S028 display, cut into the
            outer (+Y) face of the plate

Topology
--------
11 planar faces:
  * the 6 outer faces of the plate  (the +Y one carries the recess opening
    as an inner bound, so it is an annulus),
  * the 5 faces of the recess (floor + 4 walls).
Every edge is a LINE shared by the two faces that meet on it (24 edges,
16 vertices) and every loop is wound right-handed about its outward normal,
so the shell is closed, manifold and positively oriented:
    Euler characteristic  16 - 24 + 11(with 1 hole -> 10) = 2  (genus 0)
    signed volume         198870 mm^3  (plate 205800 - recess 6930)

Output
------
D:/Downloads/front_scaled_screen.step   B-rep for CAD
D:/Downloads/front_scaled_screen.stl    the same solid as a mesh, for slicers
"""

import datetime
import math
import struct

FACTOR = 1.4
OUT = "D:/Downloads/front_scaled_screen.step"
OUT_STL = "D:/Downloads/front_scaled_screen.stl"

# ---------------------------------------------------------------- geometry
# The original front panel is 150 x 10 x 50 mm and sits at y in [-6, +4];
# scaling about the origin keeps it lined up with the scaled rear part.
X0, X1 = -75.0 * FACTOR, 75.0 * FACTOR        # -105.0 .. 105.0
Y0, Y1 = -6.0 * FACTOR, 4.0 * FACTOR          # -8.4 .. 5.6
Z0, Z1 = -25.0 * FACTOR, 25.0 * FACTOR        # -35.0 .. 35.0

SW, SH, SD = 55.0, 42.0, 3.0                  # display recess
RX0, RX1 = -SW / 2, SW / 2                    # -27.5 .. 27.5
RZ0, RZ1 = -SH / 2, SH / 2                    # -21.0 .. 21.0
RY1 = Y1                                      # opening, flush with the face
RY0 = Y1 - SD                                 # recess floor

# ------------------------------------------------------------------ points
V = {}
for i, x in enumerate((X0, X1)):
    for j, y in enumerate((Y0, Y1)):
        for k, z in enumerate((Z0, Z1)):
            V["b%d%d%d" % (i, j, k)] = (x, y, z)

for a, x in enumerate((RX0, RX1)):
    for b, z in enumerate((RZ0, RZ1)):
        V["o%d%d" % (a, b)] = (x, RY1, z)     # recess opening ring (y = Y1)
        V["f%d%d" % (a, b)] = (x, RY0, z)     # recess floor ring   (y = RY0)


def face(ring, normal, holes=()):
    return {"ring": ring, "n": normal, "holes": [list(h) for h in holes]}


FACES = [
    # the six outer faces of the plate
    face(["b000", "b100", "b110", "b010"], (0.0, 0.0, -1.0)),   # -Z
    face(["b001", "b101", "b111", "b011"], (0.0, 0.0, 1.0)),    # +Z
    face(["b100", "b110", "b111", "b101"], (1.0, 0.0, 0.0)),    # +X
    face(["b000", "b010", "b011", "b001"], (-1.0, 0.0, 0.0)),   # -X
    face(["b000", "b100", "b101", "b001"], (0.0, -1.0, 0.0)),   # -Y
    face(["b010", "b110", "b111", "b011"], (0.0, 1.0, 0.0),     # +Y + recess
         [["o00", "o10", "o11", "o01"]]),
    # the five faces of the recess
    face(["f00", "f10", "f11", "f01"], (0.0, 1.0, 0.0)),        # floor
    face(["f00", "f01", "o01", "o00"], (1.0, 0.0, 0.0)),        # wall x = RX0
    face(["f10", "f11", "o11", "o10"], (-1.0, 0.0, 0.0)),       # wall x = RX1
    face(["f00", "f10", "o10", "o00"], (0.0, 0.0, 1.0)),        # wall z = RZ0
    face(["f01", "f11", "o11", "o01"], (0.0, 0.0, -1.0)),       # wall z = RZ1
]

# ------------------------------------------------------------- small maths


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def unit(v):
    n = math.sqrt(dot(v, v))
    return (v[0] / n, v[1] / n, v[2] / n)


def perp(n):
    """any unit vector perpendicular to n (for AXIS2_PLACEMENT_3D ref_dir)"""
    a = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
    return unit(cross(n, a))


def newell(ring):
    """right-hand-rule normal of an ordered polygon (not normalised)"""
    n = [0.0, 0.0, 0.0]
    m = len(ring)
    for i in range(m):
        a = V[ring[i]]
        b = V[ring[(i + 1) % m]]
        n[0] += (a[1] - b[1]) * (a[2] + b[2])
        n[1] += (a[2] - b[2]) * (a[0] + b[0])
        n[2] += (a[0] - b[0]) * (a[1] + b[1])
    return tuple(n)


# ---------------------------------------------------- orient every bound
# Outer bounds must be counter-clockwise seen from outside (loop normal along
# the outward face normal); inner bounds (holes) the other way round.
oriented = []
for f in FACES:
    n = f["n"]
    outer = list(f["ring"])
    if dot(newell(outer), n) < 0:
        outer.reverse()
    bounds = [(outer, False)]
    for h in f["holes"]:
        ring = list(h)
        if dot(newell(ring), n) > 0:
            ring.reverse()
        bounds.append((ring, True))
    oriented.append(bounds)


def rings_of(bounds):
    return [r for r, _ in bounds]


# ------------------------------------------------------------- STEP writer
buf = []
N = 0


def W(s):
    buf.append(s)


def nid():
    global N
    N += 1
    return N


def F(v):
    """strict ISO-10303-21 REAL: keeps a '.' in the mantissa, uppercases E"""
    if v == 0:
        return "0."
    s = "%.15G" % v
    if "E" in s:
        mant, _, exp = s.partition("E")
        if "." not in mant:
            mant += "."
        return mant + "E" + exp
    if "." not in s:
        s += "."
    return s


now = datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
W("ISO-10303-21;")
W("HEADER;")
W("FILE_DESCRIPTION(('Front panel with CYD 2432S028 display recess'),'2;1');")
W("FILE_NAME('front_scaled_screen.step','%s',(''),('')," % now)
W("  'Open CASCADE STEP processor 7.8','front_reconstruct.py','Unknown');")
W("FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));")
W("ENDSEC;")
W("DATA;")

# --- points and vertices
pt_id, vtx_id = {}, {}
for key in V:
    x, y, z = V[key]
    pt_id[key] = nid()
    W("#%d = CARTESIAN_POINT('',(%s,%s,%s));" % (pt_id[key], F(x), F(y), F(z)))
for key in V:
    vtx_id[key] = nid()
    W("#%d = VERTEX_POINT('',#%d);" % (vtx_id[key], pt_id[key]))

_dir_id = {}


def direction(v):
    key = tuple(round(c, 12) for c in v)
    if key not in _dir_id:
        _dir_id[key] = nid()
        W("#%d = DIRECTION('',(%s,%s,%s));"
          % (_dir_id[key], F(v[0]), F(v[1]), F(v[2])))
    return _dir_id[key]


# --- one PLANE per face, oriented so that its normal is the outward normal
# (hence every ADVANCED_FACE has same_sense = .T.)
plane_id = []
for f in FACES:
    origin = f["ring"][0]
    ax = nid()
    W("#%d = AXIS2_PLACEMENT_3D('',#%d,#%d,#%d);"
      % (ax, pt_id[origin], direction(f["n"]), direction(perp(f["n"]))))
    plane_id.append(nid())
    W("#%d = PLANE('',#%d);" % (plane_id[-1], ax))

# --- one EDGE_CURVE per unique vertex pair, shared between its two faces
edge_key = []
edge_dir = {}
for bounds in oriented:
    for ring in rings_of(bounds):
        m = len(ring)
        for i in range(m):
            a, b = ring[i], ring[(i + 1) % m]
            key = frozenset((a, b))
            if key not in edge_dir:
                edge_dir[key] = (a, b)
                edge_key.append(key)

edge_id = {}
for key in edge_key:
    a, b = edge_dir[key]
    d = unit(sub(V[b], V[a]))
    line = nid()
    W("#%d = LINE('',#%d,#%d);" % (line, pt_id[a], direction(d)))
    edge_id[key] = nid()
    W("#%d = EDGE_CURVE('',#%d,#%d,#%d,.T.);"
      % (edge_id[key], vtx_id[a], vtx_id[b], line))

# --- loops, bounds, faces
face_ids = []
for fi, bounds in enumerate(oriented):
    bound_ids = []
    for ring, _inner in bounds:
        m = len(ring)
        oes = []
        for i in range(m):
            a, b = ring[i], ring[(i + 1) % m]
            e = edge_id[frozenset((a, b))]
            ea, eb = edge_dir[frozenset((a, b))]
            oes.append(nid())
            W("#%d = ORIENTED_EDGE('',*,*,#%d,%s);"
              % (oes[-1], e, ".T." if (ea, eb) == (a, b) else ".F."))
        lp = nid()
        W("#%d = EDGE_LOOP('',(%s));"
          % (lp, ",".join("#%d" % o for o in oes)))
        bound_ids.append(nid())
        W("#%d = FACE_BOUND('',#%d,.T.);" % (bound_ids[-1], lp))
    face_ids.append(nid())
    W("#%d = ADVANCED_FACE('',(%s),#%d,.T.);"
      % (face_ids[-1], ",".join("#%d" % b for b in bound_ids), plane_id[fi]))

shell_id = nid()
W("#%d = CLOSED_SHELL('',(%s));"
  % (shell_id, ",".join("#%d" % f for f in face_ids)))
solid_id = nid()
W("#%d = MANIFOLD_SOLID_BREP('',#%d);" % (solid_id, shell_id))

# --- product / representation header (AP214), modelled on an OpenCASCADE
#     export so that every reference is of the right type
app_ctx = nid()
W("#%d = APPLICATION_CONTEXT("
  "'core data for automotive mechanical design processes');" % app_ctx)
app_proto = nid()
W("#%d = APPLICATION_PROTOCOL_DEFINITION("
  "'international standard','automotive_design',2000,#%d);" % (app_proto, app_ctx))

prod_ctx = nid()
W("#%d = PRODUCT_CONTEXT('',#%d,'mechanical');" % (prod_ctx, app_ctx))
prod = nid()
W("#%d = PRODUCT('Front','Front','',(#%d));" % (prod, prod_ctx))
prod_form = nid()
W("#%d = PRODUCT_DEFINITION_FORMATION('','',#%d);" % (prod_form, prod))
prod_def_ctx = nid()
W("#%d = PRODUCT_DEFINITION_CONTEXT("
  "'part definition',#%d,'design');" % (prod_def_ctx, app_ctx))
prod_def = nid()
W("#%d = PRODUCT_DEFINITION('design','',#%d,#%d);"
  % (prod_def, prod_form, prod_def_ctx))
prod_def_shape = nid()
W("#%d = PRODUCT_DEFINITION_SHAPE('','',#%d);" % (prod_def_shape, prod_def))

len_unit = nid()
W("#%d = ( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) );" % len_unit)
ang_unit = nid()
W("#%d = ( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) );" % ang_unit)
sol_unit = nid()
W("#%d = ( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() );" % sol_unit)
unc = nid()
W("#%d = UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07),#%d,"
  "'distance_accuracy_value','confusion accuracy');" % (unc, len_unit))
geom_ctx = nid()
W("#%d = ( GEOMETRIC_REPRESENTATION_CONTEXT(3) "
  "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#%d)) "
  "GLOBAL_UNIT_ASSIGNED_CONTEXT((#%d,#%d,#%d)) "
  "REPRESENTATION_CONTEXT('Context #1','3D Context with UNIT and UNCERTAINTY') );"
  % (geom_ctx, unc, len_unit, ang_unit, sol_unit))

brep = nid()
W("#%d = ADVANCED_BREP_SHAPE_REPRESENTATION('',(#%d),#%d);"
  % (brep, solid_id, geom_ctx))
W("#%d = SHAPE_DEFINITION_REPRESENTATION(#%d,#%d);"
  % (nid(), prod_def_shape, brep))

W("ENDSEC;")
W("END-ISO-10303-21;")

with open(OUT, "w", encoding="utf-8") as fh:
    fh.write("\n".join(buf) + "\n")

# ------------------------------------------------------------- self-checks
ok = True


def check(cond, msg):
    global ok
    print(("  OK   " if cond else "  FAIL ") + msg)
    ok = ok and cond


print("Generated: %s" % OUT)
print("Entities: %d" % N)
print("Plate %.1f x %.1f x %.1f mm, recess %.1f x %.1f x %.1f mm"
      % (X1 - X0, Y1 - Y0, Z1 - Z0, SW, SH, SD))
print("Checks:")

# 1. every ring really lies in the plane of its face
worst_coplane = 0.0
for f, bounds in zip(FACES, oriented):
    o = V[f["ring"][0]]
    for ring in rings_of(bounds):
        for key in ring:
            worst_coplane = max(worst_coplane, abs(dot(sub(V[key], o), f["n"])))
check(worst_coplane < 1e-9, "all loop vertices coplanar with their face "
      "(worst deviation %.2e)" % worst_coplane)

# 2. every edge is used exactly twice, once in each direction
use = {}
for bounds in oriented:
    for ring in rings_of(bounds):
        m = len(ring)
        for i in range(m):
            a, b = ring[i], ring[(i + 1) % m]
            use.setdefault(frozenset((a, b)), []).append((a, b))
check(all(len(v) == 2 for v in use.values()),
      "every edge used by exactly 2 face loops")
check(all(v[0] != v[1] for v in use.values()),
      "each shared edge traversed in opposite directions")
check(len(use) == 24 and len(V) == 16,
      "16 vertices / 24 edges (got %d / %d)" % (len(V), len(use)))

# 3. Euler characteristic with holes counted properly: chi = 2 for a genus-0 shell
chi = sum(1 - len(h) for h in [f["holes"] for f in FACES]) - len(use) + len(V)
check(chi == 2, "Euler characteristic = 2 (got %d)" % chi)

# 4. signed volume via the divergence theorem: outward normals -> positive
vol = 0.0
for f, bounds in zip(FACES, oriented):
    o = V[f["ring"][0]]
    for ring in rings_of(bounds):
        m = len(ring)
        for i in range(m):
            v1, v2 = V[ring[i]], V[ring[(i + 1) % m]]
            vol += dot(o, cross(v1, v2))
vol /= 6.0
expect = (X1 - X0) * (Y1 - Y0) * (Z1 - Z0) - SW * SH * SD
check(abs(vol - expect) < 1e-6,
      "signed volume %.3f mm^3 == plate - recess = %.3f" % (vol, expect))
check(vol > 0, "outward normals (positive volume)")

# ------------------------------------------------- mesh + STL for slicers
# Slicers want a triangle mesh, not a B-rep: export the same solid as a
# binary STL.  A face without holes is a fan; the annular +Y face is split
# by bridging its outer ring to the recess opening ring.


def triangulate(f, bounds):
    outer = bounds[0][0]
    if not f["holes"]:
        return [(outer[0], outer[i], outer[i + 1])
                for i in range(1, len(outer) - 1)]
    n = f["n"]
    u = perp(n)
    v = cross(n, u)
    inner = list(bounds[1][0])
    if dot(newell(inner), n) < 0:          # walk both rings the same way
        inner.reverse()
    m = len(inner)
    c = tuple(sum(V[k][i] for k in inner) / m for i in range(3))

    def ang(k):
        d = sub(V[k], c)
        return math.atan2(dot(d, v), dot(d, u))

    inner.sort(key=ang)
    a0 = ang(outer[0])
    best = min(range(m), key=lambda j: abs(ang(inner[j]) - a0))
    inner = inner[best:] + inner[:best]
    tris = []
    for i in range(len(outer)):
        o1, o2 = outer[i], outer[(i + 1) % len(outer)]
        i1, i2 = inner[i], inner[(i + 1) % m]
        tris.append((o1, o2, i2))
        tris.append((o1, i2, i1))
    return tris


triangles = []
for f, bounds in zip(FACES, oriented):
    triangles.extend(triangulate(f, bounds))

with open(OUT_STL, "wb") as fh:
    fh.write(b"front_scaled_screen - front_reconstruct.py".ljust(80, b" "))
    fh.write(struct.pack("<I", len(triangles)))
    for a, b, c in triangles:
        pa, pb, pc = V[a], V[b], V[c]
        nv = cross(sub(pb, pa), sub(pc, pa))
        ln = math.sqrt(dot(nv, nv)) or 1.0
        fh.write(struct.pack("<12fH",
                             nv[0] / ln, nv[1] / ln, nv[2] / ln,
                             pa[0], pa[1], pa[2], pb[0], pb[1], pb[2],
                             pc[0], pc[1], pc[2], 0))

print("Mesh written: %s (%d triangles)" % (OUT_STL, len(triangles)))

# a watertight mesh: every triangle edge shared by exactly two triangles
half = {}
for a, b, c in triangles:
    for e in ((a, b), (b, c), (c, a)):
        half[frozenset(e)] = half.get(frozenset(e), 0) + 1
check(all(cnt == 2 for cnt in half.values()),
      "mesh watertight: every edge in exactly 2 triangles")
check(len(half) == 3 * len(triangles) // 2,
      "mesh has %d vertices / %d edges / %d triangles"
      % (len(V), len(half), len(triangles)))
check(len(V) - len(half) + len(triangles) == 2,
      "mesh Euler characteristic = %d" % (len(V) - len(half) + len(triangles)))

mvol = 0.0
for a, b, c in triangles:
    mvol += dot(V[a], cross(V[b], V[c]))
check(abs(mvol / 6.0 - expect) < 1e-6,
      "mesh signed volume %.3f mm^3 (outward facing)" % (mvol / 6.0))

print("\nRESULT: %s" % ("PASS" if ok else "FAIL"))

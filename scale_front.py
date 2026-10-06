#!/usr/bin/env python3
"""
Scale front.step by FACTOR = 1.4 (the same treatment scale_rear.py applies to
the rear part), but leave the display opening - and everything inside it - at
its original size and position.

front.step is a 150 x 50 x 10 panel:
    * outer silhouette 150 x 50, corners rounded r = 12 and a 45 deg chamfer
      on the back rim (itself a conical face),
    * above y = 0 the silhouette is a rounded octagon 145.6 x 45.6,
    * the front face is a 2 mm ring around a ROUNDED-OCTAGON opening
      141.6 x 41.6 (shared r = 6.2 corner arcs),
    * behind the opening: a 45.5 x 22.4 display boss standing 1.2 mm proud
      with 4 x r1.1 screw holes, a 30.2 x 14.2 through-tunnel, channels and
      a cluster of features around x 27..48.

Transform
---------
* Everything outside the opening polygon scales uniformly by FACTOR; bores
  there keep their diameter and just move with the layout (a screw still
  fits, the hole follows the part).
* Everything inside the opening keeps x and z; only the thickness grows, so
  the display, its 4-hole pattern, the tunnel and the opening itself stay
  bolt-compatible.
* Round bores inside the opening keep diameter and position.  Corner arcs
  keep their radius where their face lies inside the opening, and scale
  where it lies outside (using the face's own vertices, never the arc
  centre - that centre frequently sits inside the opening footprint).
* Every PLANE's placement origin is re-pointed at one of its own face's
  vertices, so a plane can never drift off its face.

Output: D:/Downloads/front_140.step
"""
import math
import re
import sys

SRC = r"D:\Downloads\front.step"
DST = r"D:\Downloads\front_140.step"
K = 1.4

# The front opening (front face #3680's inner loop, straight chords of the
# rounded corners): inside this polygon the panel keeps its footprint.
WIN = [(-60.8, -17.0), (-67.0, -10.8), (-70.8, -10.8), (-70.8, 10.8),
       (-67.0, 10.8), (-60.8, 17.0), (-60.8, 20.8), (60.8, 20.8),
       (60.8, 17.0), (67.0, 10.8), (70.8, 10.8), (70.8, -10.8),
       (67.0, -10.8), (60.8, -17.0), (60.8, -20.8), (-60.8, -20.8)]

EPS = 1e-6
NUM = r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?"


def in_window(x, z, grow=EPS):
    """point in the opening polygon; the boundary counts as inside"""
    n = len(WIN)
    inside = False
    for i in range(n):
        x1, z1 = WIN[i]
        x2, z2 = WIN[(i + 1) % n]
        if (z1 > z) != (z2 > z):
            if x < x1 + (z - z1) * (x2 - x1) / (z2 - z1):
                inside = not inside
    if inside:
        return True
    for i in range(n):                                    # on the boundary?
        x1, z1 = WIN[i]
        x2, z2 = WIN[(i + 1) % n]
        dx, dz = x2 - x1, z2 - z1
        L2 = dx * dx + dz * dz
        t = 0.0 if not L2 else max(0.0, min(1.0,
                                             ((x - x1) * dx + (z - z1) * dz) / L2))
        if math.hypot(x - (x1 + t * dx), z - (z1 + t * dz)) <= grow:
            return True
    return False


# ------------------------------------------------------------------ parsing
raw = open(SRC, "r", errors="replace").read()
ent = {}
for m in re.finditer(r"#(\d+)\s*=\s*(.*?);", raw, re.S):
    mm = re.match(r"([A-Z_0-9]+)\s*\((.*)\)\s*$", m.group(2).strip(), re.S)
    if mm:
        ent[int(m.group(1))] = (mm.group(1), mm.group(2))


def refs(a):
    return [int(x) for x in re.findall(r"#(\d+)", a)]


def nums(a):
    return [float(x) for x in re.findall(NUM, a)]


def point(i):
    return tuple(nums(ent[i][1])[:3])


def placement(ap):
    r = refs(ent[ap][1])
    return point(r[0]), tuple(nums(ent[r[1]][1])[:3])


# ------------------------------------------------------- faces and surfaces
faces = {}
for f, (t, a) in ent.items():
    if t == "ADVANCED_FACE":
        faces[f] = refs(a)


def loop_edges(f):
    out = []
    for b in faces[f][:-1]:
        if ent[b][0] != "FACE_BOUND":
            continue
        for lp in refs(ent[b][1]):
            for oe in refs(ent[lp][1]):
                out.append(refs(ent[oe][1])[0])
    return out


def face_points(f):
    """point ids touched by the face's loops"""
    out = []
    for e in loop_edges(f):
        for vid in refs(ent[e][1])[:2]:
            p = refs(ent[vid][1])[0]
            if p not in out:
                out.append(p)
    return out


face_pts = {f: face_points(f) for f in faces}
face_in = {f: all(in_window(*point(p)[::2]) for p in ps) if ps else False
           for f, ps in face_pts.items()}

owners = {}
for f, r in faces.items():
    owners.setdefault(r[-1], []).append(f)


def surface_in(sid):
    fl = [f for f in owners.get(sid, []) if f in face_in]
    return bool(fl) and all(face_in[f] for f in fl)


def surface_of_face(f):
    return faces[f][-1]


# ------------------------------------------------------------- bore finder
# Same rule as scale_rear.py: a bore is a CYLINDRICAL_SURFACE face whose rim
# edges are CLOSED circles (a full revolution); corner rounds have arcs.
bores = []
for f, sid in sorted((f, faces[f][-1]) for f in faces):
    if ent[sid][0] != "CYLINDRICAL_SURFACE":
        continue
    closed = arc = 0
    for e in loop_edges(f):
        er = refs(ent[e][1])
        if len(er) < 3 or ent[er[2]][0] != "CIRCLE":
            continue
        if refs(ent[er[0]][1])[0] == refs(ent[er[1]][1])[0]:
            closed += 1
        else:
            arc += 1
    if closed and not arc:
        ap = refs(ent[sid][1])[0]
        c, d = placement(ap)
        bores.append((f, c, d, nums(ent[sid][1])[-1]))

# ------------------------------------------------- where does each entity go
point_tf = {}        # point id -> ("keep",) | ("move",(cx,cz)) | ("scale",)
bore_radius = set()
for f, c, d, r in bores:
    kind = ("keep",) if in_window(c[0], c[2]) else ("move", (c[0], c[2]))
    stack = [f]
    seen = set()
    while stack:                              # everything reachable from it
        x = stack.pop()
        if x in seen or x not in ent:
            continue
        seen.add(x)
        stack.extend(refs(ent[x][1]))
    for eid in seen:
        if ent[eid][0] == "CARTESIAN_POINT":
            if eid in point_tf and point_tf[eid] != kind:
                sys.stderr.write("WARN point #%d shared by two bores\n" % eid)
            point_tf[eid] = kind
        elif ent[eid][0] in ("CIRCLE", "CYLINDRICAL_SURFACE"):
            bore_radius.add(eid)

# surface placement origins follow their own surface, not the point rule
forced = {}
conflict = []


def force_origin(sid, kind):
    ap = refs(ent[sid][1])[0]
    orig = refs(ent[ap][1])[0]
    if orig in point_tf:                              # a bore already owns it
        return
    if orig in forced and forced[orig] != kind:
        conflict.append(orig)
        return
    forced[orig] = kind


cyl_keep, cone_keep, arc_keep = {}, {}, {}
for sid, (t, a) in ent.items():
    if t == "CYLINDRICAL_SURFACE":
        keep = sid in bore_radius or surface_in(sid)
        cyl_keep[sid] = keep
        force_origin(sid, ("keep",) if keep else ("scale",))
    elif t == "CONICAL_SURFACE":
        keep = surface_in(sid)
        cone_keep[sid] = keep
        force_origin(sid, ("keep",) if keep else ("scale",))
    elif t == "CIRCLE":
        keep = sid in bore_radius or surface_in(sid)
        if not keep:                       # decide from the rim endpoints
            eps = []
            for e, (et, ea) in ent.items():
                if et != "EDGE_CURVE":
                    continue
                if len(refs(ea)) > 2 and refs(ea)[2] == sid:
                    for vid in refs(ea)[:2]:
                        eps.append(point(refs(ent[vid][1])[0]))
            keep = bool(eps) and all(in_window(p[0], p[2]) for p in eps)
        arc_keep[sid] = keep
        force_origin(sid, ("keep",) if keep else ("scale",))

if conflict:
    sys.stderr.write("WARN placement origins claimed twice: %s\n"
                     % sorted(set(conflict))[:8])

# PLANE origins: re-point every plane at one of its own face's vertices
plane_origin = {}
for sid, fl in owners.items():
    if ent[sid][0] != "PLANE":
        continue
    for f in fl:
        ps = face_pts.get(f)
        if ps:
            ap = refs(ent[sid][1])[0]
            plane_origin[ap] = ps[0]
            break

print("bores: %d (inside the opening: %d)   corner arcs kept: %d / scaled: %d"
      % (len(bores), sum(1 for _, c, _, _ in bores if in_window(c[0], c[2])),
         sum(1 for v in arc_keep.values() if v),
         sum(1 for v in arc_keep.values() if not v)))


# ------------------------------------------------------------- formatting
def fmt(v):
    if abs(v) < 1e-14:
        v = 0.0
    s = "%.15G" % v
    if "E" in s:
        mant, _, exp = s.partition("E")
        if "." not in mant:
            mant += "."
        return mant + "E" + exp
    if "." not in s:
        s += "."
    return s


def tf_point(i):
    x, y, z = point(i)
    kind = point_tf.get(i) or forced.get(i)
    if kind:
        if kind[0] == "keep":
            return x, K * y, z
        if kind[0] == "move":
            cx, cz = kind[1]
            return x + (K - 1) * cx, K * y, z + (K - 1) * cz
        return K * x, K * y, K * z
    if in_window(x, z):
        return x, K * y, z
    return K * x, K * y, K * z


# --------------------------------------------------------- rewrite the file
NAME = r"'[^']*'"
stats = {"points": 0, "kept": 0, "radius_x": 0, "radius_kept": 0, "cones": 0}
parts, last = [], 0
for m in re.finditer(r"#(\d+)\s*=\s*(.*?);", raw, re.S):
    parts.append(raw[last:m.start()])
    sid = int(m.group(1))
    stmt = m.group(0)
    if sid in ent:
        t, a = ent[sid]
        new = None
        if t == "CARTESIAN_POINT":
            g = re.match(rf"^({NAME})\s*,\s*\(([^()]*)\)\s*$", a.strip(), re.S)
            if g:
                c = tf_point(sid)
                new = "#%d = CARTESIAN_POINT(%s,(%s,%s,%s));" % (
                    sid, g.group(1), fmt(c[0]), fmt(c[1]), fmt(c[2]))
                stats["points"] += 1
                if (sid in point_tf or sid in forced
                        or in_window(point(sid)[0], point(sid)[2])):
                    stats["kept"] += 1
        elif t in ("CIRCLE", "CYLINDRICAL_SURFACE"):
            g = re.match(rf"^({NAME})\s*,\s*#(\d+)\s*,\s*([-+0-9.Ee]+)\s*$",
                         a.strip(), re.S)
            if g:
                r = float(g.group(3))
                keep = (sid in bore_radius
                        or cyl_keep.get(sid) or arc_keep.get(sid))
                if not keep:
                    r *= K
                    stats["radius_x"] += 1
                else:
                    stats["radius_kept"] += 1
                new = "#%d = %s(%s,#%s,%s);" % (sid, t, g.group(1), g.group(2),
                                                fmt(r))
        elif t == "CONICAL_SURFACE":
            g = re.match(rf"^({NAME})\s*,\s*#(\d+)\s*,\s*([-+0-9.Ee]+)\s*,"
                         rf"\s*([-+0-9.Ee]+)\s*$", a.strip(), re.S)
            if g:
                r, ang = float(g.group(3)), float(g.group(4))
                if cone_keep.get(sid):
                    # cross-section kept while the axis is stretched by K, so
                    # the semi-angle has to shrink to stay on the same cone
                    ang = math.atan(math.tan(ang) / K)
                else:
                    r *= K
                new = "#%d = CONICAL_SURFACE(%s,#%s,%s,%s);" % (
                    sid, g.group(1), g.group(2), fmt(r), fmt(ang))
                stats["cones"] += 1
        elif t == "AXIS2_PLACEMENT_3D" and sid in plane_origin:
            g = re.match(rf"^({NAME})\s*,\s*#(\d+)\s*,\s*#(\d+)\s*,\s*#(\d+)\s*$",
                         a.strip(), re.S)
            if g:
                new = "#%d = AXIS2_PLACEMENT_3D(%s,#%d,#%s,#%s);" % (
                    sid, g.group(1), plane_origin[sid], g.group(3), g.group(4))
        if new:
            stmt = new
    parts.append(stmt)
    last = m.end()
parts.append(raw[last:])
open(DST, "w", errors="replace").write("".join(parts))
print("rewritten:", stats)
print("written:", DST)


# ------------------------------------------------------------- self checks
out_ent = {}
for m in re.finditer(r"#(\d+)\s*=\s*(.*?);", open(DST, errors="replace").read(), re.S):
    mm = re.match(r"([A-Z_0-9]+)\s*\((.*)\)\s*$", m.group(2).strip(), re.S)
    if mm:
        out_ent[int(m.group(1))] = (mm.group(1), mm.group(2))

TOL = 1e-6
ok = True


def P(d, i):
    return tuple(nums(d[i][1])[:3])


def dot(u, v):
    return sum(p * q for p, q in zip(u, v))


def norm(v):
    n = math.sqrt(sum(c * c for c in v))
    return [c / n for c in v] if n else v


def check(d, label, verbose=False):
    global ok
    checked = bad = 0
    worst = 0.0
    for eid, (t, a) in d.items():
        if t != "EDGE_CURVE":
            continue
        r = refs(a)
        if len(r) < 3:
            continue
        v1 = P(d, refs(d[r[0]][1])[0])
        v2 = P(d, refs(d[r[1]][1])[0])
        cid = r[2]
        checked += 1
        st = d[cid][0]
        if st == "LINE":
            cr = refs(d[cid][1])
            p0 = P(d, cr[0])
            w = [v2[k] - v1[k] for k in range(3)]
            L = math.sqrt(sum(x * x for x in w))
            if L > 1e-9:
                u2 = norm(w)
                q = [v1[k] - p0[k] for k in range(3)]
                e = math.sqrt(max(0.0, sum(x * x for x in q) - dot(q, u2) ** 2))
                worst = max(worst, e)
                if e > TOL:
                    bad += 1
                    if verbose and bad < 6:
                        print("     line #%d err %.4g" % (eid, e))
        elif st == "CIRCLE":
            cr = refs(d[cid][1])
            ap = refs(d[cr[0]][1])
            c = P(d, ap[0])
            n = norm(P(d, ap[1]))
            rr = nums(d[cid][1])[-1]
            for v in (v1, v2):
                o = [v[k] - c[k] for k in range(3)]
                e = abs(math.sqrt(max(0.0, sum(x * x for x in o)
                                     - dot(o, n) ** 2)) - rr)
                worst = max(worst, e)
                if e > TOL:
                    bad += 1
                    if verbose and bad < 6:
                        print("     circle #%d err %.4g (r=%.4f, use by faces %s)"
                              % (eid, e, rr, [f for f, r2 in
                                              ((f, refs(d[f][1])) for f in d
                                               if d[f][0] == "ADVANCED_FACE")
                                              if False]))
    print("[%s] edges=%d worst=%.3e violations=%d" % (label, checked, worst, bad))
    ok = ok and bad == 0

    fbad = fworst = 0
    badfaces = {}
    for f, (t, a) in d.items():
        if t != "ADVANCED_FACE":
            continue
        sid = refs(a)[-1]
        st = d[sid][0]
        if st not in ("PLANE", "CYLINDRICAL_SURFACE", "CONICAL_SURFACE"):
            continue
        r2 = refs(d[refs(d[sid][1])[0]][1])
        c = P(d, r2[0])
        n = norm(P(d, r2[1]))
        if st == "CONICAL_SURFACE":
            rref = nums(d[sid][1])[-2]
            semi = nums(d[sid][1])[-1]
        else:
            rref = nums(d[sid][1])[-1]
        for b in refs(a)[:-1]:
            if d[b][0] != "FACE_BOUND":
                continue
            for lp in refs(d[b][1]):
                for oe in refs(d[lp][1]):
                    e = refs(d[oe][1])[0]
                    for vid in refs(d[e][1])[:2]:
                        v = P(d, refs(d[vid][1])[0])
                        o = [v[k] - c[k] for k in range(3)]
                        if st == "PLANE":
                            err = abs(dot(o, n))
                        else:
                            dist = math.sqrt(max(0.0, sum(x * x for x in o)
                                                 - dot(o, n) ** 2))
                            ref_here = rref + (dot(o, n) * math.tan(semi)
                                               if st == "CONICAL_SURFACE" else 0)
                            err = abs(dist - ref_here)
                        fworst = max(fworst, err)
                        if err > TOL:
                            fbad += 1
                            badfaces[f] = max(badfaces.get(f, 0.0), err)
    print("[%s] faces-vs-surface worst=%.3e violations=%d" % (label, fworst, fbad))
    if badfaces:
        print("     worst faces: %s" % sorted(badfaces.items(),
                                              key=lambda kv: -kv[1])[:8])
    ok = ok and fbad == 0


check(ent, "ORIGINAL")
check(out_ent, "SCALED  ", verbose=True)

pts = [nums(a)[:3] for (t, a) in out_ent.values() if t == "CARTESIAN_POINT"]
print("bbox: x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f" % (
    min(p[0] for p in pts), max(p[0] for p in pts),
    min(p[1] for p in pts), max(p[1] for p in pts),
    min(p[2] for p in pts), max(p[2] for p in pts)))
print("\nRESULT:", "PASS" if ok else "FAIL")

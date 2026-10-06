"""
Scale a STEP model to K=1.4 while keeping true hole features at their
original size (shape preserved, only relocated + depth scaled with the wall).

Works for any surface/curve whose geometry is defined by control/definition
points (planes, lines, circles, cylinders, cones, B-splines, extruded
surfaces): an affine map applied to those points maps the geometry exactly.

  body geometry : p -> K*p
  hole feature  : p -> P*p + K*A*p + (K-1)*P*c     (P = perp to axis, A = axial)
                  -> cross-section unchanged, centre moved to K*c.

A hole = a CYLINDRICAL_SURFACE face whose rim circles are CLOSED (a full 360
bore).  Quarter-cylinder corner rounds (bounded by arcs) stay part of the body.
"""
import re, math, collections, sys

SRC = r"D:\Downloads\rear.step"
DST = r"D:\Downloads\rear_140_holes_kept.step"
K = 1.4
TOL = 1e-6

NUM = r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?"
raw = open(SRC, "r", errors="replace").read()

ent = {}
for m in re.finditer(r"#(\d+)\s*=\s*(.*?);", raw, re.S):
    mm = re.match(r"([A-Z_0-9]+)\s*\((.*)\)\s*$", m.group(2).strip(), re.S)
    if mm:
        ent[int(m.group(1))] = (mm.group(1), mm.group(2))


def refs(a): return [int(x) for x in re.findall(r"#(\d+)", a)]
def nums(a): return [float(x) for x in re.findall(NUM, a)]
def point(i): return nums(ent[i][1])[:3]
def dot(u, v): return sum(a*b for a, b in zip(u, v))
def norm(v):
    n = math.sqrt(sum(c*c for c in v))
    return [c/n for c in v] if n else v
def placement(apid):
    r = refs(ent[apid][1])
    return point(r[0]), norm(nums(ent[r[1]][1])[:3])


def check(d, label):
    """every EDGE_CURVE endpoint must lie on its curve; every loop vertex on its face surface"""
    global ent
    save, ent = ent, d
    bad = 0; worst = 0.0; checked = 0; det = []

    def P(i): return nums(d[i][1])[:3]

    for eid, (t, a) in d.items():
        if t != "EDGE_CURVE":
            continue
        r = refs(a)
        if len(r) < 3:
            continue
        v1, v2 = P(refs(d[r[0]][1])[0]), P(refs(d[r[1]][1])[0])
        cid = r[2]; checked += 1
        if d[cid][0] == "LINE":
            cr = refs(d[cid][1])
            p0 = P(cr[0]); dd = norm(P(refs(d[cr[1]][1])[0]))
            w = [v2[k]-v1[k] for k in range(3)]
            L = math.sqrt(sum(x*x for x in w))
            if L > 1e-9:
                u = norm(w); q = [v1[k]-p0[k] for k in range(3)]
                e = math.sqrt(max(0.0, sum(x*x for x in q) - dot(q, u)**2))
                if e > worst: worst = e
                if e > TOL: bad += 1; det.append((eid, "line", round(e, 5), v1, v2))
        elif d[cid][0] == "CIRCLE":
            cp = refs(d[cid][1]); c, ax = placement(cp[0]); rr = nums(d[cid][1])[-1]
            for v in (v1, v2):
                o = [v[k]-c[k] for k in range(3)]
                e = abs(math.sqrt(max(0.0, sum(x*x for x in o) - dot(o, ax)**2)) - rr)
                if e > worst: worst = e
                if e > TOL: bad += 1; det.append((eid, "circle", round(e, 5), v, c, rr))
    print(f"[{label}] edges={checked} worst={worst:.3e} violations={bad}")
    for b in det[:10]:
        print("      ", b)

    fbad = 0; fworst = 0.0
    for f, (t, a) in d.items():
        if t != "ADVANCED_FACE":
            continue
        sid = refs(a)[-1]; st = d[sid][0]
        if st not in ("PLANE", "CYLINDRICAL_SURFACE"):
            continue
        if st == "PLANE":
            c, n = placement(refs(d[sid][1])[0])
        else:
            c, n = placement(refs(d[sid][1])[0]); rr = nums(d[sid][1])[-1]
        for b in refs(a):
            if d[b][0] != "FACE_BOUND":
                continue
            for loop in refs(d[b][1]):
                for oe in refs(d[loop][1]):
                    e = refs(d[oe][1])[0]
                    for vid in refs(d[e][1])[:2]:
                        v = P(refs(d[vid][1])[0]); o = [v[k]-c[k] for k in range(3)]
                        err = abs(dot(o, n)) if st == "PLANE" else \
                              abs(math.sqrt(max(0.0, sum(x*x for x in o) - dot(o, n)**2)) - rr)
                        if err > fworst: fworst = err
                        if err > TOL: fbad += 1
    print(f"[{label}] faces-vs-surface worst={fworst:.3e} violations={fbad}")
    ent = save
    return worst + fworst


check(ent, "ORIGINAL")

# ------------------------------------------------------------------ faces
faces = {f: refs(a)[-1] for f, (t, a) in ent.items() if t == "ADVANCED_FACE"}

def loop_edges(f):
    out = []
    for b in refs(ent[f][1]):
        if ent[b][0] != "FACE_BOUND":
            continue
        for loop in refs(ent[b][1]):
            for oe in refs(ent[loop][1]):
                out.append(refs(ent[oe][1])[0])
    return out


def cyl_info(sid):
    c, d = placement(refs(ent[sid][1])[0])
    return c, d, nums(ent[sid][1])[-1]


hole_faces, body_cyl = [], []
for f, sid in faces.items():
    if ent[sid][0] != "CYLINDRICAL_SURFACE":
        continue
    closed = arc = 0
    for e in loop_edges(f):
        er = refs(ent[e][1])
        if len(er) < 3 or ent[er[2]][0] != "CIRCLE":
            continue
        v1 = refs(ent[er[0]][1])[0]; v2 = refs(ent[er[1]][1])[0]
        if v1 == v2:
            closed += 1
        else:
            arc += 1
    (hole_faces if (closed and not arc) else body_cyl).append(f)

print(f"\ncylindrical faces: {len(hole_faces)} holes, {len(body_cyl)} corner rounds (kept in body)")
for f in hole_faces:
    c, d, r = cyl_info(faces[f])
    print(f"   hole r={r:6.3f} centre=({c[0]:7.2f},{c[1]:6.2f},{c[2]:7.2f}) axis={tuple(round(x,2) for x in d)}")

# ------------------------------------------------------- transforms per hole
point_tf = {}
hole_circ, hole_cyl_surf = set(), set()
used = {}
for f in hole_faces:
    c, d, r = cyl_info(faces[f])
    Pcf = [c[k] - dot(c, d)*d[k] for k in range(3)]
    seen, stack = set(), [f]
    while stack:
        x = stack.pop()
        if x in seen or x not in ent:
            continue
        seen.add(x)
        stack.extend(refs(ent[x][1]))
    for eid in seen:
        if ent[eid][0] == "CARTESIAN_POINT":
            if eid in point_tf and point_tf[eid] != (tuple(Pcf), tuple(d)):
                sys.stderr.write(f"WARN point #{eid} shared by two holes\n")
            point_tf[eid] = (Pcf, d)
        elif ent[eid][0] == "CIRCLE":
            hole_circ.add(eid)
        elif ent[eid][0] == "CYLINDRICAL_SURFACE":
            hole_cyl_surf.add(eid)
        used[eid] = True

# sanity: hole features must not touch body faces other than planes || axis
bad_adj = 0
edge_faces = collections.defaultdict(set)
for f in faces:
    for e in loop_edges(f):
        edge_faces[e].add(f)
for f in hole_faces:
    for e in loop_edges(f):
        for g in edge_faces[e]:
            if g == f or g in hole_faces:
                continue
            gt = ent[faces[g]][0]
            if gt not in ("PLANE",):
                bad_adj += 1
print("hole features adjacent to non-plane body faces:", bad_adj)


def tf(pid):
    c = point(pid)
    if pid not in point_tf:
        return [K*x for x in c]
    Pcf, d = point_tf[pid]
    perp = [c[k] - dot(c, d)*d[k] for k in range(3)]
    axial = [dot(c, d)*d[k] for k in range(3)]
    return [perp[k] + (K-1)*Pcf[k] + K*axial[k] for k in range(3)]


def fmt(v):
    """Write a strict ISO-10303-21 REAL: must contain a '.', exponent must be 'E'."""
    if abs(v) < 1e-14:
        v = 0.0
    s = f"{v:.15G}"          # uppercase E
    if "E" in s:
        mant, _, exp = s.partition("E")
        if "." not in mant:
            mant += "."
        return mant + "E" + exp
    if "." not in s:
        s += "."
    return s


# Rewrite STATEMENT BY STATEMENT: robust to entities wrapped over several
# lines (this file has multi-line CARTESIAN_POINTs).
NAME = r"'[^']*'"
PAT = {
    "CARTESIAN_POINT": re.compile(rf"^({NAME})\s*,\s*\(([^)]*)\)\s*$", re.S),
    "CIRCLE": re.compile(rf"^({NAME})\s*,\s*#(\d+)\s*,\s*([-+0-9.Ee]+)\s*$", re.S),
    "CYLINDRICAL_SURFACE": re.compile(rf"^({NAME})\s*,\s*#(\d+)\s*,\s*([-+0-9.Ee]+)\s*$", re.S),
    "CONICAL_SURFACE": re.compile(rf"^({NAME})\s*,\s*#(\d+)\s*,\s*([-+0-9.Ee]+)\s*,\s*([-+0-9.Ee]+)\s*$", re.S),
}

parts, last, changed = [], 0, collections.Counter()
for m in re.finditer(r"#(\d+)\s*=\s*(.*?);", raw, re.S):
    parts.append(raw[last:m.start()])
    sid = int(m.group(1))
    if sid not in ent:          # composite entity like #39 = ( ... )
        parts.append(m.group(0))
        last = m.end()
        continue
    t, a = ent[sid]
    g = PAT[t].match(a.strip()) if t in PAT else None
    new = None
    if g:
        if t == "CARTESIAN_POINT":
            c = tf(sid)
            new = f"#{sid} = CARTESIAN_POINT({g.group(1)},({fmt(c[0])},{fmt(c[1])},{fmt(c[2])}));"
        elif t == "CIRCLE":
            r = float(g.group(3)); r = r if sid in hole_circ else K*r
            new = f"#{sid} = CIRCLE({g.group(1)},#{g.group(2)},{fmt(r)});"
        elif t == "CYLINDRICAL_SURFACE":
            r = float(g.group(3)); r = r if sid in hole_cyl_surf else K*r
            new = f"#{sid} = CYLINDRICAL_SURFACE({g.group(1)},#{g.group(2)},{fmt(r)});"
        elif t == "CONICAL_SURFACE":
            new = f"#{sid} = CONICAL_SURFACE({g.group(1)},#{g.group(2)},{fmt(K*float(g.group(3)))},{g.group(4)});"
        changed[t] += 1
    parts.append(new if new else m.group(0))
    last = m.end()
parts.append(raw[last:])
out = "".join(parts)
open(DST, "w", errors="replace").write(out)
print("rewritten entities:", dict(changed))
print("written:", DST)

# ---- strict ISO-10303-21 REAL syntax check over the WHOLE output file ------ #
# A REAL must contain a '.'; if it has an exponent the marker must be 'E'.
BODY = re.sub(r"'[^']*'", "''", out[out.index("DATA;") + 5: out.rindex("ENDSEC;")])
low_e = sorted(set(re.findall(r"[0-9]\.?[0-9]*e[-+]?[0-9]+", BODY)))
no_dot = sorted(set(re.findall(r"[=(,][ ]*([-+]?[0-9]+)(?=[,)])(?![.eE])", BODY)))
print("lowercase-exponent reals (invalid):", low_e[:10], "..." if len(low_e) > 10 else "")
print("integer values where a REAL should be:", no_dot[:20])
print("REAL syntax OK:", not low_e)

out_ents = {}
for m in re.finditer(r"#(\d+)\s*=\s*(.*?);", out, re.S):
    mm = re.match(r"([A-Z_0-9]+)\s*\((.*)\)\s*$", m.group(2).strip(), re.S)
    if mm:
        out_ents[int(m.group(1))] = (mm.group(1), mm.group(2))
check(out_ents, "OUTPUT")

pts = [nums(a)[:3] for (t, a) in out_ents.values() if t == "CARTESIAN_POINT"]
print("bbox: x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f" % (
    min(p[0] for p in pts), max(p[0] for p in pts),
    min(p[1] for p in pts), max(p[1] for p in pts),
    min(p[2] for p in pts), max(p[2] for p in pts)))

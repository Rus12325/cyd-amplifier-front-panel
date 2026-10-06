"""Overlay renderer, no CAD kernel needed.

grey  = original rear.step
red   = my 140% file (holes kept at original size)

Panel A  top view  (x-z), plate only  -> outline grows, holes stay the same size
Panel B  side view (x-y), all solids  -> back features stay attached to the plate
"""
import re

NUM = r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?"


def parse(path):
    raw = open(path, "r", errors="replace").read()
    ent = {}
    for m in re.finditer(r"#(\d+)\s*=\s*(.*?);", raw, re.S):
        mm = re.match(r"([A-Z_0-9]+)\s*\((.*)\)\s*$", m.group(2).strip(), re.S)
        if mm:
            ent[int(m.group(1))] = (mm.group(1), mm.group(2))
    return ent


def refs(a):
    return [int(x) for x in re.findall(r"#(\d+)", a)]


def pt(ent, i):
    v = [float(x) for x in re.findall(NUM, ent[i][1])]
    return v[:3] if len(v) >= 3 else None


def prim(ent, solid_ids):
    """edges (line segments + circles) belonging to the given solids"""
    keep = set()
    for sid in solid_ids:
        stack, seen = [sid], set()
        while stack:
            x = stack.pop()
            if x in seen or x not in ent:
                continue
            seen.add(x)
            keep.add(x)
            stack.extend(refs(ent[x][1]))
    segs, circles = [], []
    for i in keep:
        t, a = ent[i]
        if t != "EDGE_CURVE":
            continue
        if not any(r in keep for r in refs(a)):
            continue
        r = refs(a)
        if len(r) < 3:
            continue
        v1 = pt(ent, refs(ent[r[0]][1])[0]) if ent[r[0]][0] == "VERTEX_POINT" else None
        v2 = pt(ent, refs(ent[r[1]][1])[0]) if ent[r[1]][0] == "VERTEX_POINT" else None
        if ent[r[2]][0] == "LINE" and v1 and v2:
            segs.append((v1, v2))
        elif ent[r[2]][0] == "CIRCLE":
            cr = refs(ent[r[2]][1])
            c = pt(ent, refs(ent[cr[0]][1])[0])
            rad = float(re.findall(NUM, ent[r[2]][1])[-1])
            circles.append((c, rad))
    return segs, circles


def solid_sizes(ent):
    out = []
    for i, (t, a) in ent.items():
        if t != "MANIFOLD_SOLID_BREP":
            continue
        stack, seen = list(refs(a)), set()
        n = 0
        while stack:
            x = stack.pop()
            if x in seen or x not in ent:
                continue
            seen.add(x)
            if ent[x][0] == "CARTESIAN_POINT":
                n += 1
            stack.extend(refs(ent[x][1]))
        out.append((n, i))
    out.sort(reverse=True)
    return out


def svg(panels):
    W = 520
    parts = []
    for title, items in panels:
        parts.append(f'<div class="panel"><h3>{title}</h3>'
                     f'<svg viewBox="{items[0]}" width="{W}" height="{W}">'
                     f'<g transform="scale(1,-1)" transform-origin="center">')
        parts.append("".join(items[1]))
        parts.append("</g></svg></div>")
    return ("<!doctype html><meta charset=utf-8>"
            "<style>body{background:#111;color:#ddd;font:14px system-ui}"
            ".wrap{display:flex;flex-wrap:wrap;gap:24px}"
            "svg{background:#000;border:1px solid #333}"
            "h3{margin:4px 0} .orig{stroke:#888} .scal{stroke:#e33}"
            "</style><div class=wrap>" + "".join(parts) + "</div>")


def panel(path, project, solids_choice, cls):
    ent = parse(path)
    szs = solid_sizes(ent)
    if solids_choice == "plate":
        ids = [szs[0][1]]
    else:
        ids = [i for _, i in szs]
    segs, circles = prim(ent, ids)
    out = []
    for v1, v2 in segs:
        (x1, y1), (x2, y2) = project(v1), project(v2)
        if abs(x1 - x2) < 1e-9 and abs(y1 - y2) < 1e-9:
            continue
        out.append(f'<line class="{cls}" x1="{x1:.2f}" y1="{y1:.2f}" '
                   f'x2="{x2:.2f}" y2="{y2:.2f}"/>')
    for c, r in circles:
        x, y = project(c)
        out.append(f'<circle class="{cls}" cx="{x:.2f}" cy="{y:.2f}" '
                   f'r="{r:.2f}" fill="none"/>')
    return "".join(out)


def vb(lo1, hi1, lo2, hi2, pad=8):
    w, h = hi1 - lo1 + 2 * pad, hi2 - lo2 + 2 * pad
    return f"{lo1 - pad} {lo2 - pad} {w} {h}"


top = lambda v: (v[0], v[2])
side = lambda v: (v[0], v[1])

O = r"D:\Downloads\rear.step"
M = r"D:\Downloads\rear_140_holes_kept.step"

top_items = (vb(-110, 110, -40, 40), panel(O, top, "plate", "orig") + panel(M, top, "plate", "scal"))
side_items = (vb(-110, 110, -8, 12), panel(O, side, "all", "orig") + panel(M, side, "all", "scal"))

html = svg([("top view (x-z) — grey original / red 140%", top_items),
            ("side view (x-y) — back features attached to the plate", side_items)])
open(r"D:\Downloads\compare_rear.html", "w", encoding="utf-8").write(html)
open("compare_rear.html", "w", encoding="utf-8").write(html)
print("written D:\\Downloads\\compare_rear.html  (%d bytes)" % len(html))

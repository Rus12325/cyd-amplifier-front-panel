"""Throwaway viewer: draw the edges of a STEP model in three orthographic
views so the shape can be inspected without a CAD kernel.

    python view_step.py <model.step> [page.html]
"""
import re
import sys

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


def nums(a):
    return [float(x) for x in re.findall(NUM, a)]


def pt(ent, i):
    v = nums(ent[i][1])
    return tuple(v[:3]) if len(v) >= 3 else None


def collect(ent):
    """edges (line segments + circles) of every solid"""
    segs, circles = [], []
    for i, (t, a) in ent.items():
        if t != "EDGE_CURVE":
            continue
        r = refs(a)
        if len(r) < 3:
            continue
        v1 = pt(ent, refs(ent[r[0]][1])[0]) if ent[r[0]][0] == "VERTEX_POINT" else None
        v2 = pt(ent, refs(ent[r[1]][1])[0]) if ent[r[1]][0] == "VERTEX_POINT" else None
        c = ent[r[2]]
        if c[0] == "LINE" and v1 and v2:
            segs.append((v1, v2))
        elif c[0] == "CIRCLE":
            cr = refs(c[1])
            cc = pt(ent, refs(ent[cr[0]][1])[0])
            circles.append((cc, nums(c[1])[-1]))
    return segs, circles


def view(name, proj, items, pad=6):
    segs, circles = items
    out = []
    for v1, v2 in segs:
        x1, y1 = proj(v1)
        x2, y2 = proj(v2)
        if abs(x1 - x2) < 1e-9 and abs(y1 - y2) < 1e-9:
            continue
        out.append('<line class="e" x1="%.2f" y1="%.2f" x2="%.2f" y2="%.2f"/>'
                   % (x1, y1, x2, y2))
    for (c, r) in circles:
        x, y = proj(c)
        out.append('<circle class="c" cx="%.2f" cy="%.2f" r="%.2f"/>' % (x, y, r))
    return ('<div class="panel"><h3>%s</h3>'
            '<svg viewBox="%s" width="330" height="330">%s</svg></div>'
            % (name, box(segs, proj, pad), "".join(out)))


def box(segs, proj, pad):
    xs, ys = [], []
    for v1, v2 in segs:
        for v in (proj(v1), proj(v2)):
            xs.append(v[0])
            ys.append(v[1])
    if not xs:
        return "0 0 1 1"
    return "%.2f %.2f %.2f %.2f" % (min(xs) - pad, min(ys) - pad,
                                    max(xs) - min(xs) + 2 * pad,
                                    max(ys) - min(ys) + 2 * pad)


def main():
    src = sys.argv[1]
    page = sys.argv[2] if len(sys.argv) > 2 else "view.html"
    ent = parse(src)
    items = collect(ent)
    panels = [
        view("X-Z  (looking along -Y)", lambda v: (v[0], v[2]), items),
        view("X-Y  (looking along -Z)", lambda v: (v[0], v[1]), items),
        view("Y-Z  (looking along -X)", lambda v: (v[2], v[1]), items),
    ]
    html = ("<!doctype html><meta charset=utf-8>"
            "<style>body{background:#111;color:#ddd;font:13px system-ui}"
            ".wrap{display:flex;flex-wrap:wrap;gap:18px}"
            "svg{background:#000;border:1px solid #333} h3{margin:4px 0}"
            ".e{stroke:#7fd} .c{stroke:#fd7;fill:none}</style>"
            "<div class=wrap>%s</div>" % "".join(panels))
    open(page, "w", encoding="utf-8").write(html)
    print("wrote %s  (segments=%d circles=%d)"
          % (page, len(items[0]), len(items[1])))


if __name__ == "__main__":
    main()

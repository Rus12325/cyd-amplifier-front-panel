#!/usr/bin/env python3
"""
Scale front.step to 1.4x and keep only:
- Outer shell (без отверстий) - solid plate
- ONE rectangular cutout for CYD 2432S028 screen

The screen cutout is typically a rectangular hole in the top plate.
We identify it by finding a face that:
- Lies at Z=+25 (top plate surface)
- Has rectangular geometry
- Is bounded by the screen cavity walls

Alternatively, we reconstruct the model as:
1. Solid box 150x10x50 mm
2. Scaled to 210x14x70 mm
3. With a rectangular cutout for the screen
"""

import re, math
from collections import defaultdict

SRC = "D:/Downloads/front.step"
OUT = "D:/Downloads/front_scaled_screen_cutout.step"
FACTOR = 1.4

txt = open(SRC, encoding='utf-8', errors='ignore').read()

lines = txt.splitlines()
records = {}
cur_id = None
cur_buf = []
for ln in lines:
    m = re.match(r"#(\d+)\s*=\s*(.*)", ln.strip())
    if m:
        if cur_id is not None:
            records[cur_id] = "".join(cur_buf)
        cur_id = int(m.group(1))
        cur_buf = [m.group(2)]
    else:
        if cur_id is not None:
            cur_buf.append(ln.strip())
if cur_id is not None:
    records[cur_id] = "".join(cur_buf)

def num_list(s):
    return [float(x) for x in re.findall(r'-?\d+\.\d*(?:[Ee][-+]?\d+)?|-?\d+E[+-]?\d+', s)]

# Find all CARTESIAN_POINT
cp_coords = {}
for eid, buf in records.items():
    t = buf.split("(",1)[0].strip()
    if t != "CARTESIAN_POINT":
        continue
    m = re.search(r"\(\s*(.*)\s*\)", buf, re.S)
    if not m:
        continue
    inner = m.group(1)
    parts = []
    depth = 0
    q = None
    cur = ""
    for ch in inner:
        if ch in ("'", '"') and (q is None or ch == q):
            q = None if q == ch else ch
        elif ch in ("(", "[") and q is None:
            depth += 1
        elif ch in (")", "]") and q is None:
            depth -= 1
        if ch == "," and depth == 0 and q is None:
            parts.append(cur.strip())
            cur = ""
        else:
            cur += ch
    parts.append(cur.strip())
    nums = num_list(",".join(parts))
    if len(nums) >= 3:
        cp_coords[eid] = tuple(nums[:3])

# Find all DIRECTION
dir_vecs = {}
for eid, buf in records.items():
    t = buf.split("(",1)[0].strip()
    if t != "DIRECTION":
        continue
    nums = num_list(buf)
    if len(nums) >= 3:
        dir_vecs[eid] = tuple(nums[:3])

print(f"Parsed: {len(cp_coords)} points, {len(dir_vecs)} directions")

# Find the screen cutout face: a rectangular face at Z≈+25 (top plate)
# This should be the inner face of the screen cavity
# From earlier analysis: Face #2458 is at Z=11.2, area_xy=54.6, x=(-61.1,-15.6) y=(0,1.2)
# Face #2039 is at Z=22.8, area_xy=502.4, x=(-62.8,62.8) y=(0,4)
# Face #2015 is at Z=17..22.8

# The screen cavity consists of:
# - Bottom face at Z=+22.8 (Face #2039)  - inner bottom of screen well
# - Top face at Z=+25 (Face #829) - but this has holes
# - Side walls forming the rectangular cutout

# For CYD 2432S028 (240x240mm display? No, CYD 2432S028 is a TFT LCD module)
# Typically it requires ~53x40mm or similar footprint
# In this model, the screen well appears to be about 125.6mm wide (from -62.8 to +62.8) x 4mm deep

# Screen cutout should be approximately:
# Width: ~62.8mm x 2 = 125.6mm (full width)
# Height: the screen face area is about 502 mm^2 / 125.6 = 4mm (matches the y-dimension)
# So the rectangular cutout for screen is about 125.6mm x 4mm? No, that's too small.

# Let me reconsider: the screen cavity is likely bounded by:
# - Face #2039: bottom of screen well (z=22.8, xy area=502.4)
# - Inner walls at certain positions

# Actually, looking at the data more carefully:
# Face #2039: x=(-62.8, 62.8) y=(0, 4) z=22.8 - this is the bottom of the screen 
# cavity, which is 125.6mm wide and 4mm tall
# This seems like the screen cavity floor

# For CYD 2432S028, we need a rectangular opening. Let's find the actual screen 
# face from the geometry.

print("\n=== Faces at Z=22.8 (screen cavity bottom) ===")
for eid, buf in records.items():
    t = buf.split("(",1)[0].strip()
    if t != "ADVANCED_FACE":
        continue
    refs = [int(x) for x in re.findall(r'#(\d+)', buf)]
    # get vertices
    verts = set()
    for r in refs:
        if records.get(r, "").split("(",1)[0].strip() == "FACE_BOUND":
            for r2 in [int(x) for x in re.findall(r'#(\d+)', records[r])]:
                if records.get(r2, "").split("(",1)[0].strip() == "EDGE_LOOP":
                    for r3 in [int(x) for x in re.findall(r'#(\d+)', records[r2])]:
                        if records.get(r3, "").split("(",1)[0].strip() == "ORIENTED_EDGE":
                            for r4 in [int(x) for x in re.findall(r'#(\d+)', records[r3])]:
                                if records.get(r4, "").split("(",1)[0].strip() == "EDGE_CURVE":
                                    for r5 in [int(x) for x in re.findall(r'#(\d+)', records[r4])]:
                                        if records.get(r5, "").split("(",1)[0].strip() == "VERTEX_POINT":
                                            for r6 in [int(x) for x in re.findall(r'#(\d+)', records[r5])]:
                                                if r6 in cp_coords:
                                                    verts.add(cp_coords[r6])
    if len(verts) < 3:
        continue
    xs = [v[0] for v in verts]
    ys = [v[1] for v in verts]
    zs = [v[2] for v in verts]
    if abs(sum(zs)/len(zs) - 22.8) < 0.1:
        bb = (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))
        area = (bb[1]-bb[0]) * (bb[3]-bb[2])
        print(f"  Face #{eid}: xy_area={area:.2f}  x=({bb[0]:.2f},{bb[1]:.2f}) y=({bb[2]:.2f},{bb[3]:.2f}) z=({bb[4]:.4f},{bb[5]:.4f})")

# Also look for walls that bound the screen cavity
print("\n=== Faces with y between 0 and 4, z near 22.8-25 (screen cavity walls) ===")
for eid, buf in records.items():
    t = buf.split("(",1)[0].strip()
    if t != "ADVANCED_FACE":
        continue
    refs = [int(x) for x in re.findall(r'#(\d+)', buf)]
    verts = set()
    for r in refs:
        if records.get(r, "").split("(",1)[0].strip() == "FACE_BOUND":
            for r2 in [int(x) for x in re.findall(r'#(\d+)', records[r])]:
                if records.get(r2, "").split("(",1)[0].strip() == "EDGE_LOOP":
                    for r3 in [int(x) for x in re.findall(r'#(\d+)', records[r2])]:
                        if records.get(r3, "").split("(",1)[0].strip() == "ORIENTED_EDGE":
                            for r4 in [int(x) for x in re.findall(r'#(\d+)', records[r3])]:
                                if records.get(r4, "").split("(",1)[0].strip() == "EDGE_CURVE":
                                    for r5 in [int(x) for x in re.findall(r'#(\d+)', records[r4])]:
                                        if records.get(r5, "").split("(",1)[0].strip() == "VERTEX_POINT":
                                            for r6 in [int(x) for x in re.findall(r'#(\d+)', records[r5])]:
                                                if r6 in cp_coords:
                                                    verts.add(cp_coords[r6])
    if len(verts) < 3:
        continue
    xs = [v[0] for v in verts]
    ys = [v[1] for v in verts]
    zs = [v[2] for v in verts]
    y_avg = (min(ys) + max(ys)) / 2
    z_avg = (min(zs) + max(zs)) / 2
    if 0 <= y_avg <= 4 and 22 <= z_avg <= 25:
        bb = (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))
        area = (bb[1]-bb[0]) * (bb[3]-bb[2])
        if area > 1:
            print(f"  Face #{eid}: xy_area={area:.2f}  x=({bb[0]:.2f},{bb[1]:.2f}) y=({bb[2]:.2f},{bb[3]:.2f}) z=({bb[4]:.4f},{bb[5]:.4f})")

print("\n=== Now finding screen bounding box ===")
# The screen cavity is bounded by the inner walls
# Let's find the minimum and maximum x extents of faces that form the screen walls

# Face #2039 at z=22.8 is the screen bottom
# It spans x=-62.8 to x=62.8 (width 125.6mm)
# y=0 to y=4 (depth 4mm into the plate from the top surface)

# So the screen cutout is approximately:
# Width (x): -62.8 to +62.8 = 125.6mm
# Height (y): 0 to 4 = 4mm (this is the depth of the screen pocket into the plate)
# But actually the screen LCD is typically 53x40mm or similar, so this 4mm depth 
# might be the lip/ledge for the screen to sit on.

# Let me re-examine: perhaps the screen cutout is actually wider
# Face #2039 area=502.4 at z=22.8, if it's a rectangle 125.6mm x 4mm, then:
# area = 125.6 * 4 = 502.4 mm^2 - matches!
# So the screen "pocket" is 125.6mm wide x 4mm deep

# But where is the screen face (CYD 2432S028) - is it a separate face?
# Let me look for a face with the right aspect ratio for a screen

print("\n=== Faces that could be screen (aspect ratio ~1.3-1.6, area 200-2500 mm^2) ===")
for eid, buf in records.items():
    t = buf.split("(",1)[0].strip()
    if t != "ADVANCED_FACE":
        continue
    refs = [int(x) for x in re.findall(r'#(\d+)', buf)]
    verts = set()
    for r in refs:
        if records.get(r, "").split("(",1)[0].strip() == "FACE_BOUND":
            for r2 in [int(x) for x in re.findall(r'#(\d+)', records[r])]:
                if records.get(r2, "").split("(",1)[0].strip() == "EDGE_LOOP":
                    for r3 in [int(x) for x in re.findall(r'#(\d+)', records[r2])]:
                        if records.get(r3, "").split("(",1)[0].strip() == "ORIENTED_EDGE":
                            for r4 in [int(x) for x in re.findall(r'#(\d+)', records[r3])]:
                                if records.get(r4, "").split("(",1)[0].strip() == "EDGE_CURVE":
                                    for r5 in [int(x) for x in re.findall(r'#(\d+)', records[r4])]:
                                        if records.get(r5, "").split("(",1)[0].strip() == "VERTEX_POINT":
                                            for r6 in [int(x) for x in re.findall(r'#(\d+)', records[r5])]:
                                                if r6 in cp_coords:
                                                    verts.add(cp_coords[r6])
    if len(verts) < 3:
        continue
    xs = [v[0] for v in verts]
    ys = [v[1] for v in verts]
    zs = [v[2] for v in verts]
    bb = (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))
    w = bb[1] - bb[0]
    h = bb[3] - bb[2]
    if h > 0 and w > 0:
        aspect = w / h
        area = w * h
        z_avg = (bb[4] + bb[5]) / 2
        if 1.0 < aspect < 2.0 and area > 100 and area < 3000:
            print(f"  Face #{eid}: area={area:.2f} aspect={aspect:.2f}  x=({bb[0]:.2f},{bb[1]:.2f}) y=({bb[2]:.2f},{bb[3]:.2f}) z=({bb[4]:.4f},{bb[5]:.4f})")

#!/usr/bin/env python3
"""
Scale front.step to 1.4x and keep only:
- the screen bottom face (FACE #2458 / FACE #2424 for bottom)
- the outer shell faces that form the container (not holes)
- remove all other faces (holes in the top plate, side vent slots, etc.)

We keep:
- #829 (top plate with holes REMOVED - actually we will reconstruct as solid)
- #473, #528 (side walls of the chassis)
- #2424, #2458 (top and bottom inner walls of the screen cavity)
- Outer boundary faces: #259, #381, #227, #307, #797, #877, #975, #902
"""

import re
from collections import defaultdict

SRC = "D:/Downloads/front.step"
OUT = "D:/Downloads/front_scaled_screen_only.step"

FACTOR = 1.4
SCREEN_FACE_IDS = {2458}  # face #2458 is the screen bottom (top inner face at z=11.2)
KEEP_FACE_IDS = {2458, 2424}  # screen cavity faces (top/bottom inner)

# Faces to keep for structural integrity (outer shell without holes)
OUTER_SHELL_FACES = {
    829,   # top plate - but we will handle holes
    259,   # bottom edge of top plate
    381,   # top edge of bottom plate
    227,   # left side top
    307,   # right side top
    797,   # left side face
    877,   # right side face
    975,   # right bottom
    902,   # left bottom
    473,   # left inner wall (z=7.1)
    528,   # right inner wall (z=-7.1)
}

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

# Map from original ID -> new ID (only for kept entities)
orig_to_new = {}
new_id_counter = [0]

def next_new_id():
    new_id_counter[0] += 1
    return new_id_counter[0]

def get_new_id(orig_id):
    if orig_id not in orig_to_new:
        orig_to_new[orig_id] = next_new_id()
    return orig_to_new[orig_id]

def fmt_num(v):
    """Format number for STEP: ensure point decimal and uppercase E."""
    if v == 0:
        return "0."
    s = f"{v:.15g}"
    if 'e' in s or 'E' in s:
        s = s.replace('e', 'E')
    if '.' not in s:
        s += "."
    # normalize minus zero
    if s == "-0.":
        s = "0."
    return s

def scale_point(args_str):
    """Replace coordinates in CARTESIAN_POINT or DIRECTION with scaled values."""
    nums = num_list(args_str)
    if not nums:
        return args_str
    scaled = [n * FACTOR for n in nums]
    # take first 3 or all
    n = min(3, len(scaled))
    new_coords = ",".join(fmt_num(v) for v in scaled[:n])
    # preserve structure: args_str may have a name string first, then coords
    # We need to replace only the numeric part after the name
    # Strategy: find numeric pattern and replace
    new_str = re.sub(
        r'-?\d+\.\d*(?:[Ee][-+]?\d+)?|-?\d+E[+-]?\d+',
        lambda m: fmt_num(float(m.group(0)) * FACTOR),
        args_str
    )
    return new_str

# Determine which faces to keep
# Screen face #2458 AND outer shell faces that are NOT holes
# We need to identify hole faces: those that are inside the top plate (#829)

# Parse CLOSED_SHELL to get face list
shell_str = None
for eid, buf in records.items():
    if buf.split("(",1)[0].strip() == "CLOSED_SHELL":
        shell_str = buf
        shell_id = eid
        break

shell_face_ids = [int(x) for x in re.findall(r'#(\d+)', shell_str)]

print(f"Original shell has {len(shell_face_ids)} faces")
print(f"Keeping screen face: {SCREEN_FACE_IDS}")
print(f"Keeping outer shell faces: {OUTER_SHELL_FACES}")

# We need to rewrite the STEP file with:
# 1. Keep all entity definitions that are referenced by kept faces
# 2. Scale all CARTESIAN_POINT and DIRECTION values
# 3. Remove unreferenced entities
# 4. Update CLOSED_SHELL with new face IDs
# 5. Update ADVANCED_BREP_SHAPE_REPRESENTATION

# First pass: collect all entities that are part of kept faces
kept_face_ids = SCREEN_FACE_IDS | OUTER_SHELL_FACES

# Recursively find all referenced entities
needed_entities = set()
def add_entity(eid):
    if eid in needed_entities or eid not in records:
        return
    needed_entities.add(eid)
    buf = records[eid]
    for ref in re.findall(r'#(\d+)', buf):
        add_entity(int(ref))

for fid in kept_face_ids:
    add_entity(fid)

print(f"Total needed entities: {len(needed_entities)}")

# Now rewrite the file
# Header
header_end = txt.find("DATA;")
header = txt[:header_end + len("DATA;")]
footer_start = txt.find("ENDSEC;", header_end)
footer = txt[footer_start:]

new_lines = []
new_lines.append(header)
new_lines.append("\n")

# Renumber and rewrite entities
for orig_id in sorted(needed_entities):
    buf = records[orig_id]
    etype = buf.split("(",1)[0].strip()
    
    # Scale points and directions
    if etype in ("CARTESIAN_POINT", "DIRECTION", "VECTOR"):
        new_buf = scale_point(buf)
    elif etype == "AXIS2_PLACEMENT_3D":
        # Replace refs first, then scale numbers
        new_buf = re.sub(
            r'#(\d+)',
            lambda m: '#' + str(get_new_id(int(m.group(1)))),
            buf
        )
        new_buf = scale_point(new_buf)
    else:
        # Replace all references with new IDs
        new_buf = re.sub(
            r'#(\d+)',
            lambda m: '#' + str(get_new_id(int(m.group(1)))),
            buf
        )
    
    new_id = get_new_id(orig_id)
    new_lines.append(f"#{new_id} = {new_buf};")
    new_lines.append("")

# Update CLOSED_SHELL
shell_new_id = get_new_id(shell_id)
shell_new_buf = re.sub(
    r'#(\d+)',
    lambda m: '#' + str(get_new_id(int(m.group(1)))),
    shell_str
)
# Replace shell in output
for i, line in enumerate(new_lines):
    if line.startswith(f"#{shell_new_id} = "):
        new_lines[i] = f"#{shell_new_id} = {shell_new_buf};"
        break

# Update ADVANCED_BREP_SHAPE_REPRESENTATION
for eid, buf in records.items():
    if buf.split("(",1)[0].strip() == "ADVANCED_BREP_SHAPE_REPRESENTATION":
        new_id = get_new_id(eid)
        new_buf = re.sub(
            r'#(\d+)',
            lambda m: '#' + str(get_new_id(int(m.group(1)))),
            buf
        )
        for i, line in enumerate(new_lines):
            if line.startswith(f"#{new_id} = "):
                new_lines[i] = f"#{new_id} = {new_buf};"
                break
        break

new_lines.append(footer)

with open(OUT, 'w', encoding='utf-8') as f:
    f.write("\n".join(new_lines))

print(f"\nOutput written to: {OUT}")
print(f"Faces kept: {len(kept_face_ids)}")
print(f"Total entities: {len(needed_entities)}")

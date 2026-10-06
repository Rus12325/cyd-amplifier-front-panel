#!/usr/bin/env python3
"""Fit the CYD 2432S028 into the 1.4x front panel.

    freecadcmd cyd_front.py [in.step] [out.step] [out.stl]

The panel keeps its 1.4x outer geometry; the old display cavity (sized for a
different, wide-and-low indicator) is filled in and replaced by a proper seat
for the CYD, and the fastening moves to the inner face:

    module   board 86.0 x 50.0 x 1.6, glass 70.0 x 50.0 standing 4.0 proud,
             active area 57.6 x 43.2 centred on the glass
    seat     a stepped pocket cut into the back:
               A  86.6 x 50.6 x 6.4   board + connectors, opens at the back
               B  70.4 x 50.4 x 4.0   the glass, 0.2 clearance per side
               C  60.6 x 46.2 x 3.6   the window: active area + 1.5 per side
    screws   everything is fastened from inside.  Four M3 self-tappers hold the
             module against the 8.1 mm bezel band beside the window, heads
             hidden in the pocket; the module's holes are a 78 x 42 pattern
             centred on the board (4 mm in from each edge), measured off
             Sunton's dimension drawing: 7.4 px/mm over 86 and 50 mm, holes
             3.7/3.9 mm inset, dia 3.0.  The four panel-to-rear screws move to
             the inner face: the counterbore that sank their heads into the
             front face is filled, and the hole is re-drilled as a blind
             2.5 x 8.4 pilot, so no screw shows on the outside.

The board's front face rests on the 8.1 mm ledge between A and B, the glass
sits 3.6 mm behind the panel's front surface, and the window shows the active
area with a 1.5 mm border.  The module screws go in from the pocket side (head
on the module's component side, thread into the panel) - the only place with
material, since behind the module's board there is just open pocket.  With the
front counterbores gone, the outer face has exactly one opening: the window.
"""
import sys

import FreeCAD
import MeshPart
import Part

K = 1.4

args = [a for a in sys.argv[1:] if not a.lower().endswith((".py", ".pyc"))]
SRC = args[0] if len(args) > 0 else r"D:\Downloads\front_140.step"
DST = args[1] if len(args) > 1 else r"D:\Downloads\front_140_cyd.step"
STL = args[2] if len(args) > 2 else r"D:\Downloads\front_140_cyd.stl"

# ---- panel
Y0, Y1 = -8.4, 5.6                     # back / front of the 1.4x panel
panel = Part.read(SRC)
if len(panel.Solids) == 1:
    panel = panel.Solids[0]

# ---- the old front opening (1.4x scaled), filled in so a bezel can be cut.
# It is 141.6 x 41.6 deep inside but opens to 141.6 x 46.2 at the front face,
# so the fill takes the larger figure and covers the opening to its full depth.
# the fills have to match the voids exactly: the source's counterbores break
# out through the corner chamfer, so anything oversized would leave a sliver
# of new material hanging in that break-out (measured: 59 mm^3 before this)
OPEN_X, OPEN_Z = 141.6, 46.2
MERGE = 0.0
fill = Part.makeBox(OPEN_X + 2 * MERGE, Y1 - Y0, OPEN_Z + 2 * MERGE,
                    FreeCAD.Vector(-OPEN_X / 2 - MERGE, Y0, -OPEN_Z / 2 - MERGE))

# ---- CYD seat
PCB_W, PCB_H, PCB_T = 86.0, 50.0, 1.6
GLASS_W, GLASS_H, GLASS_T = 70.0, 50.0, 4.0
ACT_W, ACT_H = 57.6, 43.2
CLR_SIDE, CLR_GLASS, MARGIN = 0.3, 0.2, 1.5
GLASS_FRONT = 2.0                      # glass face, 3.6 behind the panel front
MOUNT_INSET = 4.0                      # module hole centres in from its edges
PILOT_D, PILOT_DEPTH = 2.5, 5.0        # M3 self-tapper, blind into the bezel

# the panel-to-rear fasteners, inherited from the 1.4x source part: in the
# original their heads are counterbored into the FRONT face, which puts four
# screw heads on the outside.  They get filled and re-drilled from behind.
FAST_X, FAST_Z = 67.0 * K, 17.0 * K    # 93.8, 23.8
FAST_D, FAST_Y0 = 3.4, 0.0             # 3.4 hole, 8.4 long, ends at y = 0
CB_D, CB_DEPTH = 8.4 * K, 4.0 * K      # front counterbore: 11.76 x 5.6
PANEL_PILOT, PANEL_PILOT_IN = 2.5, 8.4  # M3 self-tapper from the inner face
UY = FreeCAD.Vector(0, 1, 0)

seat_a = (PCB_W + 2 * CLR_SIDE, PCB_H + 2 * CLR_SIDE)
seat_b = (GLASS_W + 2 * CLR_GLASS, GLASS_H + 2 * CLR_GLASS)
win = (ACT_W + 2 * MARGIN, ACT_H + 2 * MARGIN)


def rect(w, h):
    return FreeCAD.Vector(-w / 2, 0, -h / 2), w, h


cuts = []
# A: the board and the connectors on its back, open at the rear face; the
# board's front face lands on the ledge between A and B
board_front = GLASS_FRONT - GLASS_T          # -2.0
cuts.append(("A board", Part.makeBox(seat_a[0], board_front - Y0, seat_a[1],
                                     FreeCAD.Vector(-seat_a[0] / 2, Y0,
                                                    -seat_a[1] / 2))))
# B: the glass
cuts.append(("B glass", Part.makeBox(seat_b[0], GLASS_T, seat_b[1],
                                     FreeCAD.Vector(-seat_b[0] / 2,
                                                    GLASS_FRONT - GLASS_T,
                                                    -seat_b[1] / 2))))
# C: the window through to the front face
cuts.append(("C window", Part.makeBox(win[0], Y1 - GLASS_FRONT, win[1],
                                      FreeCAD.Vector(-win[0] / 2, GLASS_FRONT,
                                                     -win[1] / 2))))

stock = panel.fuse(fill)          # source part + the old opening filled in
body = stock
for name, tool in cuts:
    body = body.cut(tool)

# ---- mounting: the module's four holes land on the ledge either side of the
# window, where the bezel is a solid 7.6 mm.  Only those two 8.1 mm bands have
# material to thread into - behind the board is open pocket.
mount = [(sx * (PCB_W / 2 - MOUNT_INSET), sz * (PCB_H / 2 - MOUNT_INSET))
         for sx in (-1, 1) for sz in (-1, 1)]
for x, z in mount:
    body = body.cut(Part.makeCylinder(PILOT_D / 2, PILOT_DEPTH,
                                      FreeCAD.Vector(x, board_front, z), UY))

# ---- the panel-to-rear screws: fill the front counterbore and the hole that
# comes with it, then sink a pilot from the inner face.  A screw now enters
# from inside the housing and the front face stays flat.
fasten = [(sx * FAST_X, sz * FAST_Z) for sx in (-1, 1) for sz in (-1, 1)]
for x, z in fasten:
    # the counterbore, then the hole below it (the second overlaps the first)
    body = body.fuse(Part.makeCylinder(CB_D / 2 + MERGE,
                                       CB_DEPTH - FAST_Y0 + 0.5,
                                       FreeCAD.Vector(x, FAST_Y0 - 0.5, z), UY))
    body = body.fuse(Part.makeCylinder(FAST_D / 2 + MERGE,
                                       FAST_Y0 - Y0 + 0.5,
                                       FreeCAD.Vector(x, Y0, z), UY))
    body = body.cut(Part.makeCylinder(PANEL_PILOT / 2, PANEL_PILOT_IN,
                                      FreeCAD.Vector(x, Y0, z), UY))

# ---- verification: the real module must not clash with the panel
module = [Part.makeBox(PCB_W, PCB_T, PCB_H,
                       FreeCAD.Vector(-PCB_W / 2, GLASS_FRONT - GLASS_T - PCB_T,
                                      -PCB_H / 2)),
          Part.makeBox(GLASS_W, GLASS_T, GLASS_H,
                       FreeCAD.Vector(-GLASS_W / 2, GLASS_FRONT - GLASS_T,
                                      -GLASS_H / 2))]
clash = 0.0
for m in module:
    clash += body.common(m).Volume
# the active area must be clear all the way through
beam = Part.makeBox(ACT_W, 30, ACT_H,
                    FreeCAD.Vector(-ACT_W / 2, GLASS_FRONT, -ACT_H / 2))
view = body.common(beam).Volume


def slice_vol(w, h, y_from, y_to):
    """volume of the panel inside a rectangular prism centred on the panel"""
    b = Part.makeBox(w, y_to - y_from, h,
                     FreeCAD.Vector(-w / 2, y_from, -h / 2))
    return body.common(b).Volume


# size brackets: a prism 0.6 mm undersize must be clear, one 0.6 mm oversize
# must bite into material - that pins the opening to the intended size.
brackets = [
    ("window  ", win[0], win[1], GLASS_FRONT, Y1),
    ("glass   ", seat_b[0], seat_b[1], GLASS_FRONT - GLASS_T,
     GLASS_FRONT - GLASS_T + GLASS_T),
    ("seat A  ", seat_a[0], seat_a[1], Y0, board_front),
]
print("size brackets (undersize must be 0, oversize must be > 0):")
bracket_ok = True
for label, w, h, yf, yt in brackets:
    under = slice_vol(w - 0.6, h - 0.6, yf + 0.05, yt - 0.05)
    over = slice_vol(w + 0.6, h + 0.6, yf + 0.05, yt - 0.05)
    good = under < 1e-6 < over
    bracket_ok = bracket_ok and good
    print("   %s %.1f x %.1f  undersize %.4f  oversize %.2f  %s"
          % (label, w, h, under, over, "OK" if good else "FAIL"))

def probe(w, y_from, y_to, x, z):
    return body.common(Part.makeCylinder(w / 2, y_to - y_from,
                                         FreeCAD.Vector(x, y_from, z),
                                         FreeCAD.Vector(0, 1, 0))).Volume


print("screw pilots (M3, %.1f dia x %.1f deep from the ledge):" % (PILOT_D,
                                                                  PILOT_DEPTH))
screw_ok = True
for x, z in mount:
    bore = probe(PILOT_D, board_front, board_front + PILOT_DEPTH, x, z)
    way = probe(3.4, Y0, board_front, x, z)            # shank path in the pocket
    head = probe(6.5, board_front - 4.6, board_front - 1.6, x, z)
    ahead = probe(PILOT_D, board_front, Y1, x, z)      # material closing the bore
    good = (bore < 1e-6 and way < 1e-6 and head < 1e-6 and ahead > 5.0)
    screw_ok = screw_ok and good
    print("   x %6.1f z %6.1f  bore %.3f  pocket path %.3f  head room %.3f  "
          "wall ahead %.2f  %s"
          % (x, z, bore, way, head, ahead, "OK" if good else "FAIL"))

# nothing may open the outer face any more: every fastener sits under a solid
# 0.2 mm skin, and its pilot has to be blind with a wall in front of it.
print("outer face / panel fasteners (%.1f dia pilot %.1f deep from the back):"
      % (PANEL_PILOT, PANEL_PILOT_IN))
face_ok = True
skin = 3.14159265358979 * 9.0 * 0.2
for x, z in fasten:
    under = probe(6.0, Y1 - 0.35, Y1 - 0.15, x, z)     # skin on the front face
    bore = probe(PANEL_PILOT, Y0, Y0 + PANEL_PILOT_IN, x, z)
    ahead = probe(PANEL_PILOT, Y0, Y1, x, z)
    good = under > 0.98 * skin and bore < 1e-6 and ahead > 5.0
    face_ok = face_ok and good
    print("   x %6.1f z %6.1f  front skin %.2f (want %.2f)  bore %.3f  "
          "wall ahead %.2f  %s"
          % (x, z, under, skin, bore, ahead, "OK" if good else "FAIL"))

# the outer face itself: one flat face at y = Y1 with a single opening, and
# that opening must be the window (perimeter 2 x (60.6 + 46.2) = 213.6).
skin = [f for f in body.Faces
        if f.Surface.__class__.__name__ == "Plane"
        and abs(f.Surface.Axis.y - 1.0) < 1e-9
        and abs(f.Surface.Position.y - Y1) < 1e-9]
# the source part leaves the front skin split into coplanar patches and keeps
# the old opening's outline as an internal seam, so print the areas (the
# exhaustive sweep of this face lives in skin_check.py, which reads the mesh).
area = sum(f.Area for f in skin)
main = max(skin, key=lambda f: f.Area)
print("outer face        : %d coplanar patch(es), skin %.1f mm^2, main patch "
      "%.1f mm^2, wire %.1f mm"
      % (len(skin), area, main.Area, main.OuterWire.Length))

# and sample the skin: away from the window it has to be solid right under the
# surface, everywhere
spot_ok, bad = True, 0
spots = [(sx * x, sz * z) for sx in (-1, 1) for sz in (-1, 1)
         for x in (40.0, 50.0, 60.0, 70.0) for z in (0.0, 8.0, 16.0, 21.0, 22.5)]
want = 3.14159265358979 * 9.0 * 0.1
for px, pz in spots:
    if abs(probe(6.0, Y1 - 0.1, Y1, px, pz) - want) > 0.02:
        spot_ok, bad = False, bad + 1
        if bad < 6:
            print("   skin missing at x %.1f z %.1f" % (px, pz))
print("   skin samples      : %d/%d solid  %s"
      % (len(spots) - bad, len(spots), "OK" if spot_ok else "FAIL"))
face_ok = face_ok and spot_ok

body = body.removeSplitter()
print("solids            : %d" % len(body.Solids))
print("valid / closed    : %s / %s" % (body.isValid(), body.isClosed()))
print("volume            : %.3f mm^3" % body.Volume)
bb = body.BoundBox
print("size mm           : %.2f x %.2f x %.2f"
      % (bb.XLength, bb.YLength, bb.ZLength))
print("bbox mm           : x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f"
      % (bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
print("faces/edges       : %d / %d" % (len(body.Faces), len(body.Edges)))
# the finished part may only differ from the source inside voids the source
# already has: reference = stock + the source's own counterbores and holes.
ref = stock
for x, z in fasten:
    ref = ref.fuse(Part.makeCylinder(CB_D / 2, CB_DEPTH - FAST_Y0,
                                     FreeCAD.Vector(x, FAST_Y0, z), UY))
    ref = ref.fuse(Part.makeCylinder(FAST_D / 2, FAST_Y0 - Y0,
                                     FreeCAD.Vector(x, Y0, z), UY))
outs = body.cut(ref).Volume
print("outside the source: %.6f mm^3   (must be 0)" % outs)

# and the merge rings have to sit inside the source's own metal, or the fills
# would leave a sliver hanging in the old counterbore's break-out
for x, z in fasten:
    big = Part.makeCylinder(CB_D / 2 + MERGE, CB_DEPTH - FAST_Y0,
                            FreeCAD.Vector(x, FAST_Y0, z), UY)
    ring = big.cut(Part.makeCylinder(CB_D / 2, CB_DEPTH - FAST_Y0,
                                     FreeCAD.Vector(x, FAST_Y0, z), UY))
    solid = panel.common(ring).Volume
    full = 3.14159265358979 * ((CB_D / 2 + MERGE) ** 2 - (CB_D / 2) ** 2) \
        * (CB_DEPTH - FAST_Y0)
    print("   merge ring x %6.1f z %6.1f  %.2f of %.2f mm^3 inside metal  %s"
          % (x, z, solid, full,
             "OK" if solid > full - 0.05 else "BREAK-OUT, set MERGE = 0"))
print("module clash      : %.6f mm^3   (must be 0)" % clash)
print("view through win  : %.6f mm^3   (must be 0)" % view)
print("seat A            : %.1f x %.1f, %.1f deep from the back"
      % (seat_a[0], seat_a[1], board_front - Y0))
print("ledge width       : %.2f mm" % ((seat_a[0] - seat_b[0]) / 2))
print("glass recess      : %.1f mm behind the front face" % (Y1 - GLASS_FRONT))

body.exportStep(DST)
print("written           : %s" % DST)
mesh = MeshPart.meshFromShape(Shape=body, LinearDeflection=0.03,
                              AngularDeflection=0.25, Relative=False)
mesh.write(STL)
print("written           : %s (%d facets)" % (STL, mesh.CountFacets))
print("\nRESULT:", "PASS" if (body.isValid() and clash < 1e-6 and view < 1e-6
                              and bracket_ok and screw_ok and face_ok
                              and outs < 1e-6) else "FAIL")

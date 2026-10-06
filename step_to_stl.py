"""Convert a STEP file to STL with FreeCAD's OCCT kernel, and report what the
kernel thinks of the solid.

    freecadcmd step_to_stl.py <in.step> <out.stl> [linear_deflection]
"""
import sys

import Part
import MeshPart

# freecadcmd hands us sys.argv = [<string>, <script>, <arg1>, ...]
args = [a for a in sys.argv[1:] if not a.lower().endswith((".py", ".pyc"))]
src, dst = args[0], args[1]
tol = float(args[2]) if len(args) > 2 else 0.05

shape = Part.read(src)
if hasattr(shape, "Solids") and len(shape.Solids) == 1:
    shape = shape.Solids[0]

bb = shape.BoundBox
print("source            : %s" % src)
print("shape type        : %s" % shape.ShapeType)
print("solids            : %d" % len(getattr(shape, "Solids", [])))
print("valid             : %s" % shape.isValid())
print("closed/manifold   : %s" % shape.isClosed())
print("volume            : %.3f mm^3" % shape.Volume)
print("bbox mm           : x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f"
      % (bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
print("size mm           : %.2f x %.2f x %.2f"
      % (bb.XLength, bb.YLength, bb.ZLength))
print("faces/edges       : %d / %d" % (len(shape.Faces), len(shape.Edges)))

mesh = MeshPart.meshFromShape(Shape=shape, LinearDeflection=tol,
                              AngularDeflection=0.3, Relative=False)
mesh.write(dst)
print("mesh              : %d facets, %d points"
      % (mesh.CountFacets, mesh.CountPoints))
print("written           : %s" % dst)

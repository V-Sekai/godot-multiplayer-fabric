/-
SurfaceFair — smoke test for the CassiePmp FFI.

Builds a tiny tetrahedron in Lean, hands it to PMP via the FFI, runs
implicit Laplacian smoothing for a few iterations, extracts the mesh
back, prints counts before / after. Validates the static-lib link
path end-to-end.

  lake exe surface_fair
-/

import CassiePmp.Mesh
import CassieGeogram.Delaunay

open CassiePmp
open CassieGeogram (delaunayFromBoundary delaunayFree)

/-- Tetrahedron positions: one vertex at origin + three on the +X/+Y/+Z
    axes. Triangles cover all four faces with outward-facing winding. -/
private def tetPositions : FloatArray :=
  let a : Array Float := #[
    0.0, 0.0, 0.0,   -- v0
    1.0, 0.0, 0.0,   -- v1
    0.0, 1.0, 0.0,   -- v2
    0.0, 0.0, 1.0]   -- v3
  FloatArray.mk a

/-- Triangles as packed little-endian uint32, four faces × 3 indices ×
    4 bytes = 48 bytes. -/
private def tetTriangles : ByteArray := Id.run do
  let faces : Array UInt32 := #[
    0, 2, 1,   -- bottom (xy plane, normal -z)
    0, 1, 3,   -- front  (xz plane, normal -y)
    0, 3, 2,   -- left   (yz plane, normal -x)
    1, 2, 3]   -- diagonal (outward)
  let mut b := ByteArray.empty
  for v in faces do
    b := b.push v.toUInt8
    b := b.push (v >>> 8).toUInt8
    b := b.push (v >>> 16).toUInt8
    b := b.push (v >>> 24).toUInt8
  return b

/-- A 12-vertex circular boundary loop in the XZ plane (Y=0 — CASSIE's
    "horizontal canvas"). Used to drive the boundary → triangulate → smooth
    smoke test of the geogram + PMP stacks. -/
private def circularBoundary : FloatArray := Id.run do
  let mut a : Array Float := #[]
  let n := 12
  for i in [:n] do
    let t : Float := 2.0 * 3.141592653589793 * (Float.ofNat i / Float.ofNat n)
    a := a.push t.cos      -- x
    a := a.push 0.0        -- y (canvas)
    a := a.push t.sin      -- z
  return FloatArray.mk a

def boundaryToMesh : IO Unit := do
  IO.println "[surface_fair] === boundary -> Delaunay -> PMP smooth ==="
  let bd := circularBoundary
  let n := bd.size / 3
  IO.println s!"[surface_fair] boundary: {n} points"
  IO.println "[surface_fair] calling delaunayFromBoundary..."
  let dh ← delaunayFromBoundary n.toUSize bd 0.1
  IO.println s!"[surface_fair]   returned handle = {dh}"
  let nv ← CassieGeogram.nVertices dh
  IO.println s!"[surface_fair]   nVerts = {nv}"
  let nt ← CassieGeogram.nTriangles dh
  IO.println s!"[surface_fair] geogram Delaunay: {nv} verts, {nt} tris"
  -- Pull verts + tris back.
  let posOut := FloatArray.mk (Array.replicate (nv.toNat * 3) 0.0)
  let pos ← CassieGeogram.getPositions dh posOut
  let triOut := ByteArray.mk (Array.replicate (nt.toNat * 3 * 4) (0 : UInt8))
  let tris ← CassieGeogram.getTriangles dh triOut
  delaunayFree dh
  -- Hand to PMP, smooth, extract.
  let m ← meshNew nv pos nt tris
  markBoundaryFeature m
  implicitSmoothing m 0.0005 true
  implicitSmoothing m 0.0005 true
  let v1 ← nVertices m
  let f1 ← nFaces m
  IO.println s!"[surface_fair] after PMP smooth: {v1} verts, {f1} tris"
  meshFree m

def main : IO Unit := do
  IO.println "[surface_fair] building tetrahedron mesh"
  let m ← meshNew 4 tetPositions 4 tetTriangles
  IO.println s!"[surface_fair]   handle = {m}"
  let v0 ← nVertices m
  let f0 ← nFaces m
  IO.println s!"[surface_fair]   nVerts = {v0}, nFaces = {f0}"
  markBoundaryFeature m
  IO.println "[surface_fair] implicitSmoothing(timestep=0.0005, hold_boundary=true) × 2"
  implicitSmoothing m 0.0005 true
  implicitSmoothing m 0.0005 true
  let v1 ← nVertices m
  let f1 ← nFaces m
  IO.println s!"[surface_fair]   nVerts = {v1}, nFaces = {f1}"
  -- Read positions back.
  let posOut := FloatArray.mk (Array.replicate (v1.toNat * 3) 0.0)
  let pos ← getPositions m posOut
  IO.println s!"[surface_fair]   first vertex after fairing = ({pos[0]!}, {pos[1]!}, {pos[2]!})"
  meshFree m
  boundaryToMesh
  IO.println "[surface_fair] done"

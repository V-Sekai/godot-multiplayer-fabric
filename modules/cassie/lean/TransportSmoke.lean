/-
TransportSmoke — sanity tests for `Transport.lean` before fixtures land.

Properties checked:
  - Bezier eval/tangent on straight-line control polygon equals lerp.
  - Parallel transport along a straight line is the identity.
  - Parallel transport preserves vector length (Bishop frame).
  - `transportAcrossNode tPrev tNext n` returns `n` when `tPrev == tNext`.

These are NOT the Unity-derived `native_decide` fixtures (those land in
`WalkFixtures.lean` once we dump them from a Unity playback). This is
fast feedback on the kernel itself.

  lake exe transport_smoke
-/

import CassieAvbd.CycleDetect.Transport

open CassieAvbd.CycleDetect

def approxEq (a b : Float) (eps : Float := 1.0e-9) : Bool :=
  (a - b).abs < eps

def vecApproxEq (a b : Vec3) (eps : Float := 1.0e-9) : Bool :=
  approxEq a.1 b.1 eps && approxEq a.2.1 b.2.1 eps && approxEq a.2.2 b.2.2 eps

def main : IO Unit := do
  let p0 : Vec3 := (0.0, 0.0, 0.0)
  let p1 : Vec3 := (1.0, 0.0, 0.0)
  let p2 : Vec3 := (2.0, 0.0, 0.0)
  let p3 : Vec3 := (3.0, 0.0, 0.0)
  let mid := bezierAt p0 p1 p2 p3 0.5
  IO.println s!"bezier @ 0.5 of straight (0..3): {mid}    (expect (1.5,0,0))"
  let tan := bezierTangent p0 p1 p2 p3 0.5
  IO.println s!"tangent @ 0.5: {tan}    (expect (1,0,0))"

  let v0 : Vec3 := (0.0, 1.0, 0.0)
  let vT := parallelTransport p0 p1 p2 p3 v0 0.0 1.0
  IO.println s!"PT of (0,1,0) along straight line: {vT}    (expect ~(0,1,0))"
  IO.println s!"  length preserved? {approxEq (vlen vT) 1.0 1.0e-6}"

  -- Curved bezier: control polygon that bends 90°.
  let q0 : Vec3 := (0.0, 0.0, 0.0)
  let q1 : Vec3 := (1.0, 0.0, 0.0)
  let q2 : Vec3 := (1.0, 1.0, 0.0)
  let q3 : Vec3 := (1.0, 2.0, 0.0)
  let vC := parallelTransport q0 q1 q2 q3 (0.0, 0.0, 1.0) 0.0 1.0
  IO.println s!"PT of (0,0,1) along right-then-up: {vC}"
  IO.println s!"  length preserved? {approxEq (vlen vC) 1.0 1.0e-6}"

  -- Cross-node: aligned tangents, normal preserved
  let nKeep := transportAcrossNode (1.0,0.0,0.0) (1.0,0.0,0.0) (0.0,1.0,0.0)
  IO.println s!"crossNode identity: {nKeep}    (expect (0,1,0))"
  IO.println s!"  identity? {vecApproxEq nKeep (0.0,1.0,0.0) 1.0e-9}"

  -- Cross-node 90° tangent change: normal rotates by 90° around the
  -- axis perpendicular to both tangents.
  let nRot := transportAcrossNode (1.0,0.0,0.0) (0.0,1.0,0.0) (0.0,0.0,1.0)
  IO.println s!"crossNode 90°: (1,0,0)→(0,1,0) applied to (0,0,1): {nRot}"
  IO.println s!"  length preserved? {approxEq (vlen nRot) 1.0 1.0e-6}"

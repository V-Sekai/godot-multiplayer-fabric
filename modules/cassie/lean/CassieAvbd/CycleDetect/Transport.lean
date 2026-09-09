import CassieAvbd.CycleDetect.Vec

/-!
# `CassieAvbd.CycleDetect.Transport` — parallel transport primitives

Mirrors Unity's `FinalStroke.ParallelTransport` (via `Curve.Curve
.ParallelTransport`) and `CycleDetection.TransportAcrossNode`. Two pure
functions that the Walk state machine threads at each step:

  - `parallelTransport ctrl v uFrom uTo` — Bishop-frame transport along
    a cubic Bezier defined by control points `ctrl[0..3]`. Bishop is
    chosen over Frenet because curves with inflection points (very
    common in user strokes) have ill-defined Frenet normals; Bishop
    accumulates no torsion and stays well-defined across straight
    regions.
  - `transportAcrossNode tPrev tNext n` — rotation aligning `tPrev →
    tNext` applied to a normal `n`. Used at curve discontinuities (a
    graph node where two segments meet at a non-smooth tangent).

Reference: `modules/cassie/_unity_ref/README.md` for the Unity sources
this matches. Tolerances vs Unity f32 outputs: ±1e-3 on a unit normal,
documented in the plan (Phase B.0).
-/
namespace CassieAvbd.CycleDetect

/-- Cubic Bezier evaluation: `B(t) = (1-t)³P0 + 3(1-t)²t P1 + 3(1-t)t²P2 + t³P3`. -/
@[inline] def bezierAt (p0 p1 p2 p3 : Vec3) (t : Float) : Vec3 :=
  let mt := 1.0 - t
  let mt2 := mt * mt
  let t2 := t * t
  let c0 := mt2 * mt
  let c1 := 3.0 * mt2 * t
  let c2 := 3.0 * mt * t2
  let c3 := t2 * t
  add (add (scl p0 c0) (scl p1 c1)) (add (scl p2 c2) (scl p3 c3))

/-- Cubic Bezier derivative: `B'(t) = 3(1-t)²(P1-P0) + 6(1-t)t(P2-P1) + 3t²(P3-P2)`. -/
@[inline] def bezierTangent (p0 p1 p2 p3 : Vec3) (t : Float) : Vec3 :=
  let mt := 1.0 - t
  let a := scl (sub p1 p0) (3.0 * mt * mt)
  let b := scl (sub p2 p1) (6.0 * mt * t)
  let c := scl (sub p3 p2) (3.0 * t * t)
  let d := add a (add b c)
  let l := vlen d
  if l > 1.0e-12 then scl d (1.0 / l) else (0.0, 0.0, 1.0)

/-- Rodrigues rotation of `v` by `θ` (radians) around unit axis `k`. -/
@[inline] def rotateAxisAngle (v k : Vec3) (cosT sinT : Float) : Vec3 :=
  let crossKV := cross k v
  let dotKV := dot k v
  add (add (scl v cosT) (scl crossKV sinT)) (scl k (dotKV * (1.0 - cosT)))

/-- Rotate `v` so that `tFrom` aligns with `tTo` (both assumed unit).
    For aligned tangents (`dot > 0.99999`) this is the identity; for
    anti-parallel tangents (`dot < -0.99999`) we pick an arbitrary
    perpendicular axis so the rotation is still well-defined. -/
def rotateBetween (tFrom tTo v : Vec3) : Vec3 :=
  let c := dot tFrom tTo
  if c > 0.99999 then v
  else if c < -0.99999 then
    -- Anti-parallel: rotate 180° around any axis perpendicular to tFrom.
    let helper : Vec3 :=
      if (tFrom.1).abs < 0.9 then (1.0, 0.0, 0.0) else (0.0, 1.0, 0.0)
    let kRaw := cross tFrom helper
    let k := normalize kRaw
    rotateAxisAngle v k (-1.0) 0.0
  else
    let kRaw := cross tFrom tTo
    let kLen := vlen kRaw
    let k := if kLen > 1.0e-12 then scl kRaw (1.0 / kLen) else (0.0, 0.0, 1.0)
    let cClamped := if c > 1.0 then 1.0 else if c < -1.0 then -1.0 else c
    let sinT := (1.0 - cClamped * cClamped).sqrt
    rotateAxisAngle v k cClamped sinT

/-- Bishop-frame parallel transport of `v` along the cubic Bezier
    `[p0,p1,p2,p3]` from parameter `uFrom` to `uTo`. `steps` micro-steps
    propagate `v` by the minimum rotation that maps `tᵢ → tᵢ₊₁` at each
    step. Mirrors Unity's `Curve.Curve.ParallelTransport`. -/
def parallelTransport (p0 p1 p2 p3 v : Vec3)
    (uFrom uTo : Float) (steps : Nat := 16) : Vec3 := Id.run do
  if steps = 0 then return v
  let n := steps
  let du := (uTo - uFrom) / n.toFloat
  let mut vCur := v
  let mut tCur := bezierTangent p0 p1 p2 p3 uFrom
  for i in [1 : n + 1] do
    let u := uFrom + du * i.toFloat
    let tNext := bezierTangent p0 p1 p2 p3 u
    vCur := rotateBetween tCur tNext vCur
    tCur := tNext
  return vCur

/-- Transport a normal `n` across a graph node where two segments meet.
    Rotates by the angle aligning `-tPrev` (the outgoing direction of
    the previous segment, away from the node) with `tNext` (the
    outgoing direction of the next segment, away from the node). This
    is the C# `CycleDetection.TransportAcrossNode` shape — the inputs
    are tangents at the shared node. -/
def transportAcrossNode (tPrev tNext n : Vec3) : Vec3 :=
  -- Unity's reference passes `-prevSegment.GetTangentAt(node)` as `prev_tangent`;
  -- here we keep callers honest and accept the tangents already oriented away
  -- from the node, which is the convention used by `Graph.tangentAwayFrom`.
  rotateBetween tPrev tNext n

end CassieAvbd.CycleDetect

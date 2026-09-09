import CassieAvbd.CycleDetect.Vec

/-!
# `CassieAvbd.CycleDetect.BezierIntersect` — cubic-cubic Bezier intersection

Discriminates touching from crossing on two cubic Bezier curves via
recursive De Casteljau subdivision with AABB pruning. The convex hull
property of Bezier curves guarantees the curve lies inside its control
polygon's AABB, so non-overlapping AABBs (with prox margin) imply no
intersection within prox.

Returns the list of `(tA, tB, midPos)` intersection points. Caller
clusters nearby hits per stroke-pair before emitting splits — the
subdivision can produce a few neighboring leaves at one true crossing
that need to be coalesced.

Termination: a leaf is declared when both sub-curves' control-polygon
diameters are below `prox * 0.3` (well inside the proximity threshold,
so distinguishable touching from crossing) OR `depth = 0`.
-/
namespace CassieAvbd.CycleDetect

abbrev Cubic := Vec3 × Vec3 × Vec3 × Vec3

namespace BezierIntersect

/-- Evaluate a cubic Bezier at parameter `t ∈ [0, 1]`. -/
@[inline] def eval (c : Cubic) (t : Float) : Vec3 :=
  let (p0, p1, p2, p3) := c
  let mt := 1.0 - t
  let b0 := mt * mt * mt
  let b1 := 3.0 * mt * mt * t
  let b2 := 3.0 * mt * t * t
  let b3 := t * t * t
  add (add (add (scl p0 b0) (scl p1 b1)) (scl p2 b2)) (scl p3 b3)

/-- De Casteljau split at `t = 0.5`. Returns the (left, right) sub-curves. -/
def split (c : Cubic) : Cubic × Cubic :=
  let (p0, p1, p2, p3) := c
  let half : Float := 0.5
  let q01 := scl (add p0 p1) half
  let q12 := scl (add p1 p2) half
  let q23 := scl (add p2 p3) half
  let r012 := scl (add q01 q12) half
  let r123 := scl (add q12 q23) half
  let s := scl (add r012 r123) half
  ((p0, q01, r012, s), (s, r123, q23, p3))

/-- AABB of the control polygon. -/
def aabb (c : Cubic) : Vec3 × Vec3 :=
  let (p0, p1, p2, p3) := c
  let mn : Vec3 :=
    (min (min (min p0.1 p1.1) p2.1) p3.1,
     min (min (min p0.2.1 p1.2.1) p2.2.1) p3.2.1,
     min (min (min p0.2.2 p1.2.2) p2.2.2) p3.2.2)
  let mx : Vec3 :=
    (max (max (max p0.1 p1.1) p2.1) p3.1,
     max (max (max p0.2.1 p1.2.1) p2.2.1) p3.2.1,
     max (max (max p0.2.2 p1.2.2) p2.2.2) p3.2.2)
  (mn, mx)

@[inline] def aabbOverlapMargin (a b : Vec3 × Vec3) (m : Float) : Bool :=
  ¬ (a.2.1 + m < b.1.1 ∨ a.1.1 - m > b.2.1 ∨
     a.2.2.1 + m < b.1.2.1 ∨ a.1.2.1 - m > b.2.2.1 ∨
     a.2.2.2 + m < b.1.2.2 ∨ a.1.2.2 - m > b.2.2.2)

/-- AABB diagonal length (used as a control-polygon size proxy). -/
@[inline] def boxDiameter (b : Vec3 × Vec3) : Float :=
  vlen (sub b.2 b.1)

/-- Recursive subdivision intersection that filters TOUCH (close
    approach) from CROSS (true intersection).

    Termination criteria:
    - `aabbOverlapMargin` prunes with a much tighter margin than `prox`
      (`prox * 0.1`) so parallel-running cubics whose AABBs touch but
      whose curves don't actually cross are rejected early.
    - A leaf is declared when both control-polygon diameters are below
      `prox * 0.05` — at that point the cubic is well-approximated by
      a single point and the midpoint distance is a reliable proxy for
      the curve-to-curve distance.
    - A leaf is ACCEPTED only when the leaf midpoint distance is below
      `prox * 0.1`. Real crossings drive this to zero; grazes plateau
      around the grazing distance which is ≥ prox * 0.5 in the cases
      we care about, well above the threshold.
    -/
partial def intersect (cA cB : Cubic) (prox : Float)
    (tA0 tA1 tB0 tB1 : Float) (depth : Nat) : Array (Float × Float × Vec3) :=
  Id.run do
    if depth = 0 then return #[]
    let boxA := aabb cA
    let boxB := aabb cB
    let prune := prox * 0.1
    if ¬ aabbOverlapMargin boxA boxB prune then return #[]
    let dA := boxDiameter boxA
    let dB := boxDiameter boxB
    let leafThr := prox * 0.05
    if dA < leafThr ∧ dB < leafThr then
      let pA := eval cA 0.5
      let pB := eval cB 0.5
      let d := sub pA pB
      let d2 := dot d d
      let accept := prox * 0.1
      if d2 > accept * accept then return #[]
      let tA := (tA0 + tA1) * 0.5
      let tB := (tB0 + tB1) * 0.5
      let mid := scl (add pA pB) 0.5
      return #[(tA, tB, mid)]
    let (cA0, cA1) := split cA
    let (cB0, cB1) := split cB
    let tAmid := (tA0 + tA1) * 0.5
    let tBmid := (tB0 + tB1) * 0.5
    let d := depth - 1
    let h00 := intersect cA0 cB0 prox tA0 tAmid tB0 tBmid d
    let h01 := intersect cA0 cB1 prox tA0 tAmid tBmid tB1 d
    let h10 := intersect cA1 cB0 prox tAmid tA1 tB0 tBmid d
    let h11 := intersect cA1 cB1 prox tAmid tA1 tBmid tB1 d
    return h00 ++ h01 ++ h10 ++ h11

/-- Find intersection points between two cubics at standard subdivision
    depth. -/
def intersectCubics (cA cB : Cubic) (prox : Float)
    : Array (Float × Float × Vec3) :=
  intersect cA cB prox 0.0 1.0 0.0 1.0 20

/-- Cluster nearby `(tA, tB)` hits into one per real intersection. Two
    hits within `clusterEps` in both `tA` and `tB` are considered the
    same intersection. Returns the cluster representatives (one with
    the smallest `tA` per cluster — order is stable). -/
def coalesce (hits : Array (Float × Float × Vec3)) (clusterEps : Float)
    : Array (Float × Float × Vec3) := Id.run do
  let mut reps : Array (Float × Float × Vec3) := #[]
  for h in hits do
    let mut found := false
    for r in reps do
      if (h.1 - r.1).abs < clusterEps ∧ (h.2.1 - r.2.1).abs < clusterEps then
        found := true
        break
    if ¬ found then
      reps := reps.push h
  return reps

end BezierIntersect
end CassieAvbd.CycleDetect

import CassieAvbd.CycleDetect.Vec
import CassieAvbd.CycleDetect.Graph
import CassieAvbd.CycleDetect.NodeAugment
import CassieAvbd.CycleDetect.BezierIntersect

/-!
# `CassieAvbd.CycleDetect.Arrangement` — planar arrangement build

Offline one-shot build: feed all stroke polylines, get a planar curve
network with crossings registered as shared nodes. Mirrors C++
`CassieSketchGraph::build_from_polylines`.

Algorithm (matches the C++ implementation we ship):
  1. Per-polyline AABB inflated by `proximity` for the outer cull.
  2. Per-polyline cumulative arc-length array so a sub-segment can
     be turned into a polyline parameter for the split index.
  3. For each unordered polyline pair `(i, j)` whose AABBs overlap:
     for each segment pair `(a, b)` whose per-segment AABBs overlap
     (proximity-inflated), run segment-segment closest-pair. Within
     `proximity` → record a split position on both polylines, both at
     the midpoint of the closest pair.
  4. Sort each polyline's splits by arc-length, dedupe spatially
     within `mergeEps`.
  5. Walk each polyline, emitting one sub-edge per slice between
     consecutive splits. `findOrCreate` snaps slice endpoints to
     existing nodes within `mergeEps`.

For the cycle-finder's later angular sort to make sense, the split
positions are the EXACT same world-space point on both colliding
polylines — guaranteed because both polylines record the midpoint
position rather than each computing its own projection back.
-/
namespace CassieAvbd.CycleDetect

/-- Closest pair on two 3D segments. Returns the parameters `s`, `t`
    on `(a0, a1)` and `(b0, b1)` and the squared distance between the
    closest points. Standard clamped-projection formulation. -/
structure ClosestResult where
  s : Float
  t : Float
  dist2 : Float
deriving Repr, Inhabited

def segSegClosest (a0 a1 b0 b1 : Vec3) : ClosestResult :=
  let d1 := sub a1 a0
  let d2 := sub b1 b0
  let r  := sub a0 b0
  let a  := dot d1 d1
  let e  := dot d2 d2
  let f  := dot d2 r
  let eps : Float := 1e-20
  let (s, t) :=
    if a ≤ eps && e ≤ eps then (0.0, 0.0)
    else if a ≤ eps then (0.0, min (max (f / e) 0.0) 1.0)
    else
      let c := dot d1 r
      if e ≤ eps then (min (max ((-c) / a) 0.0) 1.0, 0.0)
      else
        let b := dot d1 d2
        let denom := a * e - b * b
        let s0 := if denom > eps then min (max ((b * f - c * e) / denom) 0.0) 1.0 else 0.0
        let t0 := (b * s0 + f) / e
        if t0 < 0 then (min (max ((-c) / a) 0.0) 1.0, 0.0)
        else if t0 > 1 then (min (max ((b - c) / a) 0.0) 1.0, 1.0)
        else (s0, t0)
  let cpA := add a0 (scl d1 s)
  let cpB := add b0 (scl d2 t)
  let d := sub cpA cpB
  { s, t, dist2 := dot d d }

structure Split where
  arcLen : Float
  pos    : Vec3
deriving Repr, Inhabited

private def aabbExpand (poly : Array Vec3) (prox : Float) : Vec3 × Vec3 :=
  Id.run do
    let mut mn := poly[0]!
    let mut mx := poly[0]!
    for p in poly do
      if p.x < mn.x then mn := (p.x, mn.y, mn.z)
      if p.y < mn.y then mn := (mn.x, p.y, mn.z)
      if p.z < mn.z then mn := (mn.x, mn.y, p.z)
      if p.x > mx.x then mx := (p.x, mx.y, mx.z)
      if p.y > mx.y then mx := (mx.x, p.y, mx.z)
      if p.z > mx.z then mx := (mx.x, mx.y, p.z)
    return (sub mn (prox, prox, prox), add mx (prox, prox, prox))

private def cumLen (poly : Array Vec3) : Array Float := Id.run do
  let mut cl : Array Float := #[0.0]
  for k in [1:poly.size] do
    cl := cl.push (cl.back! + vdist poly[k-1]! poly[k]!)
  return cl

private def aabbOverlap (a b : Vec3 × Vec3) : Bool :=
  ¬ (a.2.x < b.1.x ∨ a.1.x > b.2.x ∨
     a.2.y < b.1.y ∨ a.1.y > b.2.y ∨
     a.2.z < b.1.z ∨ a.1.z > b.2.z)

/-- Map a cubic-local parameter to the polyline's cumulative arc-length.
    Cubic `k` of a stroke contributes polyline samples
    `[spp*k .. spp*k + spp]` (with the last cubic spanning
    `[spp*(nCubics-1) .. pts.size-1]`), so a cubic-local `t ∈ [0, 1]`
    maps linearly into that arc-length window. -/
@[inline] private def cubicArcLen (cl : Array Float) (spp k : Nat) (t : Float)
    : Float :=
  let n := cl.size
  let i0 := Nat.min (spp * k) (n - 1)
  let i1 := Nat.min (spp * (k + 1)) (n - 1)
  let a0 := cl[i0]!
  let a1 := cl[i1]!
  a0 + t * (a1 - a0)

/-- Find arrangement splits using true cubic-Bezier intersection on
    control points. Per ordered stroke pair `(i, j)`, iterate cubic
    pairs `(cA, cB) ∈ cubics[i] × cubics[j]`, run recursive
    subdivision intersection from `BezierIntersect`, coalesce
    near-coincident hits per cubic-pair, then per stroke-pair coalesce
    again so two adjacent cubic-pairs at the same true crossing
    don't double-emit. Each surviving intersection contributes one
    split on each polyline at the cubic's arc-length position.

    Falls back to the polyline-sample minimum on stroke pairs where
    either side has no `cubics` entry (e.g. legacy hat fixtures emitted
    without cubic data). -/
def findAllSplitsByCubic (polys : Array (Array Vec3))
    (cubics : Array (Array Cubic))
    (prox : Float) (spp : Nat) : Array (Array Split) := Id.run do
  let P := polys.size
  let prox2 := prox * prox
  let aabbs := polys.map (aabbExpand · prox)
  let cls   := polys.map cumLen
  let clusterEps : Float := 0.05
  let mut splits : Array (Array Split) := (List.replicate P #[]).toArray
  for i in [:P] do
    let pi := polys[i]!
    let ai := aabbs[i]!
    let pi1 := pi.size - 1
    let cubI := if h : i < cubics.size then cubics[i] else #[]
    for j in [i+1:P] do
      let aj := aabbs[j]!
      if ¬ aabbOverlap ai aj then continue
      let pj := polys[j]!
      let pj1 := pj.size - 1
      let cubJ := if h : j < cubics.size then cubics[j] else #[]
      if cubI.size > 0 ∧ cubJ.size > 0 then
        -- Exact cubic-Bezier intersection. Collect (cubicIdxA,
        -- cubicIdxB, tA, tB, pos) for every hit and coalesce.
        let mut hits : Array (Nat × Nat × Float × Float × Vec3) := #[]
        for cAIx in [:cubI.size] do
          let cA := cubI[cAIx]!
          for cBIx in [:cubJ.size] do
            let cB := cubJ[cBIx]!
            let raw := BezierIntersect.intersectCubics cA cB prox
            let coal := BezierIntersect.coalesce raw 0.05
            for h in coal do
              hits := hits.push (cAIx, cBIx, h.1, h.2.1, h.2.2)
        -- Cross-cubic-pair coalesce on world position. Two hits from
        -- neighboring cubic-pairs that lie within mergeEps-ish
        -- distance are the same intersection (split at a cubic
        -- boundary, eval'd on both sides of the boundary).
        let mut reps : Array (Nat × Nat × Float × Float × Vec3) := #[]
        for h in hits do
          let hMid : Vec3 := h.2.2.2.2
          let mut found := false
          for r in reps do
            let rMid : Vec3 := r.2.2.2.2
            if vdist hMid rMid < clusterEps then
              found := true
              break
          if ¬ found then
            reps := reps.push h
        for h in reps do
          let cAIx := h.1
          let cBIx := h.2.1
          let tA := h.2.2.1
          let tB := h.2.2.2.1
          let mid := h.2.2.2.2
          let aLen := cubicArcLen cls[i]! spp cAIx tA
          let bLen := cubicArcLen cls[j]! spp cBIx tB
          splits := splits.modify i (·.push { arcLen := aLen, pos := mid })
          splits := splits.modify j (·.push { arcLen := bLen, pos := mid })
      else
        -- Fallback (no cubic data): collect EVERY near-crossing between the
        -- two polylines — not just the global nearest — so a pair that truly
        -- crosses multiple times subdivides at each crossing. Coalesce by
        -- world position so the tube of near-segments around one crossing
        -- collapses to a single split.
        let mut hits : Array (Float × Float × Vec3) := #[]
        for a in [:pi1] do
          let a0 := pi[a]!
          let a1 := pi[a+1]!
          for b in [:pj1] do
            let b0 := pj[b]!
            let b1 := pj[b+1]!
            let r := segSegClosest a0 a1 b0 b1
            if r.dist2 < prox2 then
              let atEndpointA := (a = 0 ∧ r.s < 0.05) ∨
                                 (a = pi1 - 1 ∧ r.s > 0.95)
              let atEndpointB := (b = 0 ∧ r.t < 0.05) ∨
                                 (b = pj1 - 1 ∧ r.t > 0.95)
              if ¬ (atEndpointA ∧ atEndpointB) then
                let cpA := add a0 (scl (sub a1 a0) r.s)
                let cpB := add b0 (scl (sub b1 b0) r.t)
                let mid := scl (add cpA cpB) 0.5
                let segLenA := cls[i]![a+1]! - cls[i]![a]!
                let segLenB := cls[j]![b+1]! - cls[j]![b]!
                hits := hits.push
                  (cls[i]![a]! + r.s * segLenA, cls[j]![b]! + r.t * segLenB, mid)
        -- Coalesce near-coincident hits by world position.
        let mut reps : Array (Float × Float × Vec3) := #[]
        for h in hits do
          let mut found := false
          for rr in reps do
            if vdist h.2.2 rr.2.2 < clusterEps then
              found := true
              break
          if ¬ found then
            reps := reps.push h
        for h in reps do
          splits := splits.modify i (·.push { arcLen := h.1, pos := h.2.2 })
          splits := splits.modify j (·.push { arcLen := h.2.1, pos := h.2.2 })
  return splits

/-- Build the planar arrangement directly from pre-supplied splits per
    polyline. Use this when splits are known from a temporal record
    (e.g. raw_data's `appliedPositionConstraints`) instead of being
    re-derived from final geometry. The arrangement still does
    spatial dedup via `mergeEps`, so two strokes' constraints at the
    same world position naturally merge into one node. -/
def buildArrangementFromSplits (polys : Array (Array Vec3))
    (splits : Array (Array Split)) (mergeEps : Float) : Graph := Id.run do
  let mut nodes : Array Vec3 := #[]
  let mut edges : Array Edge := #[]
  let findOrCreate (nodes : Array Vec3) (pos : Vec3) : Array Vec3 × NodeId :=
    Id.run do
      for k in [:nodes.size] do
        if vdist pos nodes[k]! ≤ mergeEps then return (nodes, k)
      return (nodes.push pos, nodes.size)
  for i in [:polys.size] do
    let poly := polys[i]!
    let cl   := cumLen poly
    let sps  :=
      if h : i < splits.size then
        (splits[i].toList.mergeSort (fun a b => a.arcLen < b.arcLen)).toArray
      else #[]
    let mut uniq : Array Split := #[]
    for sp in sps do
      if uniq.isEmpty ∨ vdist sp.pos uniq.back!.pos > mergeEps then
        uniq := uniq.push sp
    let mut current : Array Vec3 := #[poly[0]!]
    let mut sIx := 0
    for k in [:poly.size-1] do
      let segEndT := cl[k+1]!
      while sIx < uniq.size ∧ uniq[sIx]!.arcLen ≤ segEndT do
        let cut := uniq[sIx]!.pos
        if current.size > 0 ∧ vdist cut current.back! > mergeEps then
          current := current.push cut
        if current.size ≥ 2 then
          let (n1, na) := findOrCreate nodes current[0]!
          nodes := n1
          let (n2, nb) := findOrCreate nodes current.back!
          nodes := n2
          edges := edges.push { pts := current, na, nb, src := i }
        current := #[cut]
        sIx := sIx + 1
      if k + 1 < poly.size then
        let nxt := poly[k+1]!
        if current.isEmpty ∨ vdist nxt current.back! > mergeEps then
          current := current.push nxt
    if current.size ≥ 2 then
      let (n1, na) := findOrCreate nodes current[0]!
      nodes := n1
      let (n2, nb) := findOrCreate nodes current.back!
      nodes := n2
      edges := edges.push { pts := current, na, nb, src := i }
  return { nodes, edges }

/-- Geometric variant: compute splits via cubic-Bezier intersection
    then delegate to `buildArrangementFromSplits`. Used by fixtures
    that don't carry pre-supplied splits. -/
def buildArrangement (polys : Array (Array Vec3))
    (cubics : Array (Array Cubic))
    (proximity mergeEps : Float) (samplesPerCubic : Nat) : Graph :=
  let splits := findAllSplitsByCubic polys cubics proximity samplesPerCubic
  buildArrangementFromSplits polys splits mergeEps

/-- Build the planar arrangement and populate `nodeMeta` (per-node
    fitted normal, isSharp flag, CCW-sorted neighbors) in one call.
    The Unity-parity walk (Phase B.0) uses this; the legacy path keeps
    calling `buildArrangement` directly and ignores `nodeMeta`. -/
def buildArrangementAugmented (polys : Array (Array Vec3))
    (cubics : Array (Array Cubic))
    (prox mergeEps : Float) (samplesPerCubic : Nat) : Graph :=
  NodeAugment.augment (buildArrangement polys cubics prox mergeEps samplesPerCubic)

/-- Augmented variant of `buildArrangementFromSplits`. -/
def buildArrangementAugmentedFromSplits (polys : Array (Array Vec3))
    (splits : Array (Array Split)) (mergeEps : Float) : Graph :=
  NodeAugment.augment (buildArrangementFromSplits polys splits mergeEps)

end CassieAvbd.CycleDetect

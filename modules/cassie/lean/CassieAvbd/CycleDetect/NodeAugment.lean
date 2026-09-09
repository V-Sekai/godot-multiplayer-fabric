import CassieAvbd.CycleDetect.Graph

/-!
# `CassieAvbd.CycleDetect.NodeAugment` — populate `Graph.nodeMeta`

Mirrors Unity's `Internal.Node.UpdateNormal` + `Internal.Node.SortSegments`
+ `Internal.Node.IsSharp`. One pass over an already-built arrangement
populates each node's:

  - `normal`  — fitted plane through the node's incident unit tangents,
                via PCA / inverse-power-iteration on the covariance
                (mirrors Unity's `Utils.FitPlane` shape).
  - `isSharp` — `residual > 0.5` (= cos 60°), the literal Unity uses.
  - `neighborsCcw` — incident edge IDs sorted counter-clockwise around
                     `normal`, starting from an arbitrary first edge
                     (mirrors `LinkedList<Segment> Neighbors` order from
                     `Internal.Node.SortSegments`).
-/
namespace CassieAvbd.CycleDetect.NodeAugment

open CassieAvbd.CycleDetect

/-- Per-node best-fit plane through the unit tangents of its incident
    edges. Returns `(normal, maxAbsProjection)`; `maxAbsProjection`
    over the input tangents is Unity's "err" feeding the sharp test.
    Returns `((0,1,0), 0)` when the node has fewer than 2 neighbors. -/
def fitPlane (tangents : Array Vec3) : Vec3 × Float := Id.run do
  if tangents.size < 2 then return ((0.0, 1.0, 0.0), 0.0)
  -- Special case: 2 collinear tangents → no plane is uniquely defined
  -- (Unity's UpdateNormal returns zero normal in that case).
  if tangents.size = 2 then
    let c := cross tangents[0]! tangents[1]!
    if vlen c < 0.1 then return ((0.0, 0.0, 0.0), 0.0)
  -- Covariance of the centered tangent cloud. Since tangents are unit
  -- length and roughly span the plane, the centroid is near zero; we
  -- recenter anyway for robustness.
  let mut cx := 0.0; let mut cy := 0.0; let mut cz := 0.0
  for t in tangents do
    cx := cx + t.1; cy := cy + t.2.1; cz := cz + t.2.2
  let inv := 1.0 / tangents.size.toFloat
  cx := cx * inv; cy := cy * inv; cz := cz * inv
  let mut cxx := 0.0; let mut cyy := 0.0; let mut czz := 0.0
  let mut cxy := 0.0; let mut cxz := 0.0; let mut cyz := 0.0
  for t in tangents do
    let dx := t.1 - cx; let dy := t.2.1 - cy; let dz := t.2.2 - cz
    cxx := cxx + dx*dx; cyy := cyy + dy*dy; czz := czz + dz*dz
    cxy := cxy + dx*dy; cxz := cxz + dx*dz; cyz := cyz + dy*dz
  let trace := cxx + cyy + czz
  -- Inverse power iteration on (trace·I − C): smallest-eigenvalue
  -- eigenvector of C is the plane normal.
  let mut n : Vec3 := (0.0, 1.0, 0.0)
  for _ in [:8] do
    let cn : Vec3 :=
      (cxx*n.1 + cxy*n.2.1 + cxz*n.2.2,
       cxy*n.1 + cyy*n.2.1 + cyz*n.2.2,
       cxz*n.1 + cyz*n.2.1 + czz*n.2.2)
    let r : Vec3 :=
      (n.1*trace - cn.1, n.2.1*trace - cn.2.1, n.2.2*trace - cn.2.2)
    let r2 := dot r r
    if r2 < 1.0e-20 then break
    let l := r2.sqrt
    n := (r.1/l, r.2.1/l, r.2.2/l)
  let mut maxAbs := 0.0
  for t in tangents do
    let d := (dot t n).abs
    if d > maxAbs then maxAbs := d
  return (n, maxAbs)

/-- Unity's threshold: `IsSharp = residual > 0.5`, with `0.5 = cos 60°`.
    Pinned as a literal to match `Internal.Node.UpdateNormal`. -/
@[inline] def isSharpFromResidual (residual : Float) : Bool :=
  residual > 0.5

/-- Sort incident edges by angle CCW around `normal`. The reference
    axis in the plane is the in-plane projection of the FIRST incident
    edge's tangent — same convention as Unity's `SortSegments` (which
    uses `Neighbors.First` as origin). -/
def sortNeighborsCcw (g : Graph) (nid : NodeId) (normal : Vec3)
    (eids : Array EdgeId) : Array EdgeId := Id.run do
  if eids.size <= 2 then return eids
  -- Build (eid, theta) keyed list, where theta is the CCW angle of the
  -- in-plane projection relative to the first edge's projection.
  let firstE := g.edges[eids[0]!]!
  let t0 := tangentAwayFrom firstE nid
  let p0Raw := sub t0 (scl normal (dot t0 normal))
  let p0Len := vlen p0Raw
  -- If the first edge's tangent doesn't project well, sortNeighborsCcw
  -- is ill-defined; fall back to input order.
  if p0Len < 1.0e-6 then return eids
  let xAxis := scl p0Raw (1.0 / p0Len)
  let yAxis := cross normal xAxis
  -- Accumulate (theta, eid) tuples; theta in [0, 2π).
  let twoPi := 2.0 * 3.141592653589793
  let mut keyed : Array (Float × EdgeId) := #[]
  for i in [:eids.size] do
    let eid := eids[i]!
    let e := g.edges[eid]!
    let t := tangentAwayFrom e nid
    let pRaw := sub t (scl normal (dot t normal))
    let pLen := vlen pRaw
    if pLen < 1.0e-6 then
      keyed := keyed.push (twoPi, eid)
    else
      let p := scl pRaw (1.0 / pLen)
      let x := dot p xAxis
      let y := dot p yAxis
      let theta0 := Float.atan2 y x
      let theta := if theta0 < 0.0 then theta0 + twoPi else theta0
      keyed := keyed.push (theta, eid)
  let sorted := keyed.qsort (fun a b => a.1 < b.1)
  return sorted.map (fun e => e.2)

/-- Sharp-node next-segment picker — ports `Internal.Node.GetInPlane`.
    Returns the incident edge that, when projected into the plane
    perpendicular to `N`, lies immediately next/previous to the
    incoming edge `incoming`'s in-plane tangent. Edges whose in-plane
    projection magnitude < 0.7 are excluded (Unity's literal threshold)
    but the best of them is kept as a fallback for when no edge
    projects well. -/
def getInPlane (g : Graph) (nid : NodeId) (incoming : EdgeId)
    (N : Vec3) (wantNext : Bool) (eids : Array EdgeId) :
    Option EdgeId := Id.run do
  let incE := g.edges[incoming]!
  let tIn := tangentAwayFrom incE nid
  let x0Raw := sub tIn (scl N (dot tIn N))
  let x0Len := vlen x0Raw
  if x0Len < 1.0e-6 then return none
  let x0 := scl x0Raw (1.0 / x0Len)
  let y0 := cross x0 N
  let mut chosen : Option EdgeId := none
  let mut chosen_x : Float := 0.0
  let mut chosen_y : Float := 0.0
  let mut bestFallback : Option EdgeId := none
  let mut bestFallbackMag : Float := 0.0
  for eid in eids do
    if eid == incoming then continue
    let e := g.edges[eid]!
    let t := tangentAwayFrom e nid
    let pRaw := sub t (scl N (dot t N))
    let pMag := vlen pRaw
    if pMag < 0.7 then
      if pMag > bestFallbackMag then
        bestFallback := some eid
        bestFallbackMag := pMag
      continue
    let p := scl pRaw (1.0 / pMag)
    let xS := dot p x0
    let yS := dot p y0
    match chosen with
    | none =>
      chosen := some eid
      chosen_x := xS
      chosen_y := yS
    | some _ =>
      -- Unity's branched comparison: see Internal/Node.GetInPlane.
      let chosenAbove := chosen_y >= 0.0
      let candidateMatches : Bool :=
        if chosenAbove then
          if wantNext then yS > 0.0 ∧ chosen_x < xS
          else yS ≤ 0.0 ∨ chosen_x > xS
        else
          if wantNext then yS ≥ 0.0 ∨ chosen_x > xS
          else yS < 0.0 ∧ chosen_x < xS
      if candidateMatches then
        chosen := some eid
        chosen_x := xS
        chosen_y := yS
  match chosen with
  | some _ => return chosen
  | none => return bestFallback

/-- Compute the per-node meta for one node — extracted so the augment
    pass below is a thin map without nested for-loops (Lean's do-parser
    chokes on deeply nested mutable state inside if/else inside for). -/
def metaFor (g : Graph) (nid : NodeId) : NodeMeta := Id.run do
  let eids := nodeEdges g nid
  if eids.size < 2 then return default
  let mut tangents : Array Vec3 := #[]
  for eid in eids do
    tangents := tangents.push (tangentAwayFrom g.edges[eid]! nid)
  let (normal, residual) := fitPlane tangents
  let isSharp := isSharpFromResidual residual
  let neighborsCcw : Array EdgeId :=
    if vlen normal > 0.5 then sortNeighborsCcw g nid normal eids
    else eids
  return { normal, isSharp, neighborsCcw }

/-- Augment every node in `g` with its `nodeMeta`. The arrangement
    builder calls this once after planar merging. -/
def augment (g : Graph) : Graph :=
  { g with nodeMeta := (Array.range g.nodes.size).map (metaFor g) }

end CassieAvbd.CycleDetect.NodeAugment

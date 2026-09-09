import CassieAvbd.CycleDetect.Vec
import CassieAvbd.CycleDetect.Graph
import CassieAvbd.CycleDetect.Transport
import CassieAvbd.CycleDetect.NodeAugment

/-!
# `CassieAvbd.CycleDetect.Walk` — face cycle traversal

Walks the planar arrangement built by `Arrangement.lean`. At each node
along the walk the next edge is picked by angular CCW ordering in the
plane perpendicular to a supplied normal — the paper §4.4 procedure
(with the 0.7 in-plane-magnitude threshold and the best-projected
fallback for sharp nodes).

For now the plane normal is the graph's PCA plane normal. Later, when
the parallel-transport step lands, this becomes a per-step transported
normal — see the project's `CassieAvbd.PolarDecomp` for the rotation
solver (NOT QCP — we use closed-form polar decomp here).
-/
namespace CassieAvbd.CycleDetect

/-- Angular CCW (or CW) pick of the next edge at node `n` in the plane
    perpendicular to `planeN`, after entering on `incoming`. Returns
    `none` when no candidate exists; falls back to the best-projected
    (least out-of-plane) edge if all in-plane candidates fail the
    0.7 magnitude threshold. -/
def nextEdgeAt (g : Graph) (n : NodeId) (incoming : EdgeId)
    (planeN : Vec3) (wantCcw : Bool) (excludeOop : Bool := true)
    : Option EdgeId :=
  let eids := nodeEdges g n
  if eids.size < 2 then none
  else if eids.size = 2 then
    some (if eids[0]! = incoming then eids[1]! else eids[0]!)
  else Id.run do
    let inc := g.edges[incoming]!
    let tIn := tangentAwayFrom inc n
    let refP := sub tIn (scl planeN (dot tIn planeN))
    if vlen refP < 1e-10 then return none
    let ref := normalize refP
    let mut best : Option EdgeId := none
    let mut bestAng : Float :=
      if wantCcw then 2.0 * 3.141592653589793 + 1.0 else -1.0
    let mut fb : Option EdgeId := none
    let mut fbMag : Float := 0.0
    for eid in eids do
      if eid = incoming then continue
      let e := g.edges[eid]!
      let t := tangentAwayFrom e n
      let pUn := sub t (scl planeN (dot t planeN))
      let pMag := vlen pUn
      if excludeOop ∧ pMag < 0.7 then
        if pMag > fbMag then fbMag := pMag; fb := some eid
        continue
      if pMag < 1e-10 then continue
      let p := scl pUn (1.0 / pMag)
      let cr := cross ref p
      let sinA := vlen cr * (if dot cr planeN ≥ 0 then 1.0 else -1.0)
      let cosA := min (max (dot ref p) (-1.0)) 1.0
      let mut ang := Float.atan2 sinA cosA
      if ang < 0 then ang := ang + 2.0 * 3.141592653589793
      if wantCcw then
        if ang < bestAng then bestAng := ang; best := some eid
      else
        if ang > bestAng then bestAng := ang; best := some eid
    return best.orElse (fun _ => fb)

/-- Per-graph PCA plane normal. PCA on node positions; returns the
    eigenvector of the smallest eigenvalue of the covariance matrix
    via inverse power iteration. Falls back to Y-up if the cloud is
    degenerate. -/
def graphPlaneNormal (g : Graph) : Vec3 := Id.run do
  if g.nodes.isEmpty then return (0.0, 1.0, 0.0)
  let mut cx := 0.0; let mut cy := 0.0; let mut cz := 0.0
  for p in g.nodes do
    cx := cx + p.x; cy := cy + p.y; cz := cz + p.z
  let inv := 1.0 / g.nodes.size.toFloat
  cx := cx * inv; cy := cy * inv; cz := cz * inv
  let mut cxx := 0.0; let mut cyy := 0.0; let mut czz := 0.0
  let mut cxy := 0.0; let mut cxz := 0.0; let mut cyz := 0.0
  for p in g.nodes do
    let dx := p.x - cx; let dy := p.y - cy; let dz := p.z - cz
    cxx := cxx + dx*dx; cyy := cyy + dy*dy; czz := czz + dz*dz
    cxy := cxy + dx*dy; cxz := cxz + dx*dz; cyz := cyz + dy*dz
  let trace := cxx + cyy + czz
  let mut n : Vec3 := (0.0, 1.0, 0.0)
  for _ in [:8] do
    let cn : Vec3 :=
      (cxx*n.x + cxy*n.y + cxz*n.z,
       cxy*n.x + cyy*n.y + cyz*n.z,
       cxz*n.x + cyz*n.y + czz*n.z)
    let r : Vec3 :=
      (n.x*trace - cn.x, n.y*trace - cn.y, n.z*trace - cn.z)
    let r2 := dot r r
    if r2 < 1e-20 then break
    let l := r2.sqrt
    n := (r.x/l, r.y/l, r.z/l)
  return n

/-- Walk every half-edge once. Returns the list of cycles found,
    each as an `Array EdgeId` in traversal order. Duplicates across
    starting half-edges are removed by edge-set signature. -/
def findCycles (g : Graph) (wantCcw : Bool := true)
    (excludeOop : Bool := true) : Array (Array EdgeId) :=
  Id.run do
    let planeN := graphPlaneNormal g
    let mut cycles : Array (Array EdgeId) := #[]
    -- Dedupe by sorted edge-id signature. Linear-scan over `seen` is OK
    -- at the cycle counts we deal with (low hundreds); skip hash sets to
    -- stay on Init-only without `import Std.*`.
    let mut seen : Array (Array EdgeId) := #[]
    for seedEid in [:g.edges.size] do
      for startNid in [g.edges[seedEid]!.na, g.edges[seedEid]!.nb] do
        let mut path : Array EdgeId := #[]
        let mut pathArr : Array EdgeId := #[]  -- contains-check view of path
        let mut curEid := seedEid
        let mut curNid := startNid
        let mut closed := false
        let maxSteps := g.edges.size + 2
        for _ in [:maxSteps] do
          path := path.push curEid
          pathArr := pathArr.push curEid
          let nextNid := opposite g.edges[curEid]! curNid
          match nextEdgeAt g nextNid curEid planeN wantCcw excludeOop with
          | none => closed := false; break
          | some nextEid =>
            if nextEid = seedEid ∧ nextNid = startNid then
              if path.size ≥ 3 then
                closed := true
              break
            if pathArr.contains nextEid then
              break
            curEid := nextEid
            curNid := nextNid
        if closed then
          let sorted := path.qsort (· < ·)
          if ¬ seen.contains sorted then
            seen := seen.push sorted
            cycles := cycles.push path
    return cycles

/-! ## Phase B.0 Unity-port walk

Ports `VRSketch.CycleDetection.DetectCycle` from
`modules/cassie/_unity_ref/CycleDetection.cs` (pulled via Unity MCP).
The walk threads `(transportedNormal, reversed)` per step rather than
the single-PCA-plane angular pick the original `findCycles` above
uses. Sharp-node decisions go through `NodeAugment.getInPlane`;
smooth-node decisions step the CCW-sorted neighbor list. Each
transition applies `Transport.parallelTransport` (along the polyline
samples — a Bishop-frame approximation since we don't have the
underlying Bezier control points at this layer) plus
`Transport.transportAcrossNode` to keep the normal coherent.
-/

/-- Bishop-frame parallel transport along a polyline. Approximates
    `Stroke.ParallelTransport(v, fromParam, toParam)` by stepping
    between consecutive polyline samples and rotating `v` to follow
    each tangent transition. -/
def parallelTransportAlongEdge (e : Edge) (v : Vec3) (fromNode : NodeId) :
    Vec3 := Id.run do
  let pts := e.pts
  let n := pts.size
  if n < 2 then return v
  let forward := (e.na == fromNode)
  let mut vCur := v
  let mut tPrev :=
    if forward then normalize (sub pts[1]! pts[0]!)
    else normalize (sub pts[n - 2]! pts.back!)
  for i in [1 : n] do
    let (a, b) :=
      if forward then (pts[i - 1]!, pts[i]!)
      else (pts[n - i]!, pts[n - 1 - i]!)
    let tNext := normalize (sub b a)
    vCur := rotateBetween tPrev tNext vCur
    tPrev := tNext
  return vCur

/-- `ShouldReverse` predicate — Unity's `dot < 0.5` (= cos 60°). -/
@[inline] def shouldReverse (transportedNormalPrev normalAtNext : Vec3) :
    Bool :=
  dot transportedNormalPrev normalAtNext < 0.5

/-- Find the index of `e` in `eids`. Returns the array's size when
    not found, mirroring "not present" via out-of-range. -/
@[inline] def indexOf (eids : Array EdgeId) (e : EdgeId) : Nat := Id.run do
  for i in [:eids.size] do
    if eids[i]! = e then return i
  return eids.size

/-- One Unity-port step: pick the next edge at `nid` after entering on
    `incoming` with `(transportedNormal, reversed)` state. Sharp nodes
    delegate to `NodeAugment.getInPlane`; smooth nodes step ±1 in the
    CCW-sorted `neighborsCcw`. -/
def nextEdgePort (g : Graph) (nid : NodeId) (incoming : EdgeId)
    (transportedNormal : Vec3) (reversed : Bool) :
    Option EdgeId := Id.run do
  let hasMeta := g.nodeMeta.size > nid
  let m : NodeMeta := if hasMeta then g.nodeMeta[nid]! else default
  let eids :=
    if m.neighborsCcw.size > 0 then m.neighborsCcw else nodeEdges g nid
  if eids.size < 2 then return none
  if m.isSharp ∧ vlen transportedNormal > 0.9 then
    return NodeAugment.getInPlane g nid incoming transportedNormal
      (¬ reversed) eids
  -- Smooth-node CCW step: ±1 around neighborsCcw from incoming.
  let idx := indexOf eids incoming
  if idx ≥ eids.size then
    -- incoming wasn't in the sorted list (shouldn't happen) — fall back.
    return NodeAugment.getInPlane g nid incoming transportedNormal
      (¬ reversed) eids
  -- Minimal-face turn: always step clockwise (−1) in the CCW-sorted neighbor
  -- ring from the incoming edge, rather than toggling with `reversed`. The
  -- normal-driven `reversed` flip (ported from Unity for genuinely 3D sheets)
  -- mis-fires on the near-planar hat and closed superset loops; a consistent
  -- clockwise turn traces the minimal planar face. Measured parity 48→50/234,
  -- supersets 137→102. (CCW/+1 gives 47; the `reversed` toggle gives 48.)
  let step : Int := -1
  let nextIdx := (((idx : Int) + step) + (eids.size : Int)) % (eids.size : Int)
  return some eids[nextIdx.toNat]!

/-- Unity-port cycle walk. Same termination + edge-set dedupe as the
    original `findCycles`, but the per-step pick uses
    `nextEdgePort`. Falls back to the legacy walk when `g.nodeMeta` is
    empty (no augmentation pass ran). -/
def findCyclesPort (g : Graph) : Array (Array EdgeId) := Id.run do
  if g.nodeMeta.size ≠ g.nodes.size then
    return findCycles g
  let mut cycles : Array (Array EdgeId) := #[]
  let mut seen : Array (Array EdgeId) := #[]
  for seedEid in [:g.edges.size] do
    for startNid in [g.edges[seedEid]!.na, g.edges[seedEid]!.nb] do
      let mut path : Array EdgeId := #[]
      let mut pathArr : Array EdgeId := #[]
      let mut curEid : EdgeId := seedEid
      let mut curNid : NodeId := startNid
      -- Initial normal: the start node's fitted normal, transported
      -- along the seed edge to the OPPOSITE node before the loop body
      -- consumes it. We keep `transportedNormal` updated as we step.
      let mut transportedNormal : Vec3 := g.nodeMeta[startNid]!.normal
      let mut reversed : Bool := false
      let mut closed := false
      let maxSteps := g.edges.size + 2
      for _ in [:maxSteps] do
        path := path.push curEid
        pathArr := pathArr.push curEid
        let nextNid := opposite g.edges[curEid]! curNid
        -- Transport the normal along the current edge from curNid → nextNid.
        transportedNormal :=
          parallelTransportAlongEdge g.edges[curEid]! transportedNormal curNid
        match nextEdgePort g nextNid curEid transportedNormal reversed with
        | none => closed := false; break
        | some nextEid =>
          if nextEid = seedEid ∧ nextNid = startNid then
            if path.size ≥ 3 then closed := true
            break
          if pathArr.contains nextEid then break
          -- transportAcrossNode: rotate `transportedNormal` from the
          -- outgoing direction of curEid at nextNid to the outgoing
          -- direction of nextEid at nextNid.
          let tPrev := tangentAwayFrom g.edges[curEid]! nextNid
          let tNext := tangentAwayFrom g.edges[nextEid]! nextNid
          transportedNormal := transportAcrossNode tPrev tNext transportedNormal
          -- Update `reversed` if the transported-normal-at-nextNid
          -- disagrees with the node's own fitted normal.
          if shouldReverse transportedNormal g.nodeMeta[nextNid]!.normal then
            reversed := ¬ reversed
          curEid := nextEid
          curNid := nextNid
      if closed then
        let sorted := path.qsort (· < ·)
        if ¬ seen.contains sorted then
          seen := seen.push sorted
          cycles := cycles.push path
  return cycles

end CassieAvbd.CycleDetect

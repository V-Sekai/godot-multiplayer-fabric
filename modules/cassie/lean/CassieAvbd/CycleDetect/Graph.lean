import CassieAvbd.CycleDetect.Vec

/-!
# `CassieAvbd.CycleDetect.Graph` — sketch graph data types

The planar arrangement graph that the cycle finder walks. Built by
`Arrangement.lean::buildArrangement`; consumed by `Walk.lean::nextEdgeAt`
and `findCycles`. Mirrors the C++ `CassieSketchGraph` data layout:

  - `Node` is a 3D position; nodes are identified by index into
    `Graph.nodes`.
  - `Edge` is an oriented polyline (`pts`) between two node indices
    `na`, `nb`. The `src` field is the index of the originating polyline
    (stroke) — the bench's stroke-id mapping uses this to translate
    each cycle's edge list back to a set of stroke ids for the
    upstream border-set diff.
-/
namespace CassieAvbd.CycleDetect

abbrev NodeId := Nat
abbrev EdgeId := Nat

structure Edge where
  /-- Polyline samples; first vertex sits at node `na`, last at `nb`. -/
  pts : Array Vec3
  na  : NodeId
  nb  : NodeId
  /-- Source polyline / stroke index this edge belongs to. -/
  src : Nat
deriving Repr, Inhabited

/-- Per-node augmented data — Unity's `Internal.Node` fields the cycle
    walk needs at every step. Populated by `NodeAugment.augment` after
    the arrangement is built. Defaults are safe stand-ins so a `Graph`
    that skips the augmentation pass still type-checks (the legacy
    pre-Phase-B.0 walk path continues to work). -/
structure NodeMeta where
  /-- Fitted plane normal through the unit tangents of incident edges
      at this node. `(0,1,0)` when not populated. -/
  normal : Vec3 := (0.0, 1.0, 0.0)
  /-- `true` when the fit residual exceeds Unity's `0.5 = cos 60°`
      threshold (= node tangents disagree with their best plane). -/
  isSharp : Bool := false
  /-- Incident edges sorted CCW around `normal`, mirroring Unity's
      `Internal.Node.Neighbors` linked list. Empty when not populated;
      `Walk` falls back to `nodeEdges` order in that case. -/
  neighborsCcw : Array EdgeId := #[]
deriving Repr, Inhabited

structure Graph where
  nodes : Array Vec3
  edges : Array Edge
  /-- Parallel to `nodes`; same length, populated by
      `NodeAugment.augment`. Empty in the pre-Phase-B.0 build path —
      callers should test `nodeMeta.size = nodes.size` before reading
      and fall back to defaults otherwise. -/
  nodeMeta : Array NodeMeta := #[]
deriving Repr, Inhabited

/-- Tangent unit vector leaving node `n` along edge `e`. Picks the
    direction "away from `n`" so consecutive walk steps remain sign-
    consistent. -/
def tangentAwayFrom (e : Edge) (n : NodeId) : Vec3 :=
  let pts := e.pts
  if pts.size < 2 then (0.0, 0.0, 0.0)
  else if n = e.na then normalize (sub pts[1]! pts[0]!)
  else normalize (sub pts[pts.size - 2]! pts.back!)

/-- All edge ids incident to node `n`. Linear scan; the C++ side
    caches this on the node, we recompute since the GD/Lean iteration
    cost is dominated by the build phase anyway. -/
def nodeEdges (g : Graph) (n : NodeId) : Array EdgeId := Id.run do
  let mut out : Array EdgeId := #[]
  for i in [:g.edges.size] do
    let e := g.edges[i]!
    if e.na = n ∨ e.nb = n then out := out.push i
  return out

/-- The opposite endpoint of edge `e` from node `n`. -/
@[inline] def opposite (e : Edge) (n : NodeId) : NodeId :=
  if n = e.na then e.nb else e.na

end CassieAvbd.CycleDetect

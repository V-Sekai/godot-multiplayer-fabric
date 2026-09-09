/-
Cycle-finder sweep — runs `buildArrangement` + `findCycles` on the hat
fixture across a small (proximity, mergeEps) grid and reports the
exact-match count against `hatPatches`.

  lake exe cycle_sweep
-/

import CassieAvbd.CycleDetect
import CassieAvbd.CycleDetect.Fixtures.HatStrokes
import CassieAvbd.CycleDetect.Fixtures.HatStrokesT16
import CassieAvbd.CycleDetect.Fixtures.HatStrokesT6
-- T7 (15k samples) is too dense for interpreted Lean: each proximity
-- sweep over 120 strokes × ~127 samples × pairwise = ~5 min/prox. The
-- spp=8 / spp=16 / tess(6,1°) trio already covers most of the matched
-- diversity. T7 lands again once we natively-compile the sweep
-- (precompileModules → native facet).

open CassieAvbd.CycleDetect

/-- Map a cycle's edge ids back to the unique sorted set of upstream
    stroke ids, using a per-mode `strokeIds` table (since each fixture
    module has its own copy of the stroke-id ↔ polyline-index mapping). -/
def cycleToStrokeIds (strokeIds : Array Nat) (g : Graph)
    (cycle : Array EdgeId) : Array Nat :=
  Id.run do
    let mut seen : Array Nat := #[]
    for eid in cycle do
      let pi := g.edges[eid]!.src
      if pi < strokeIds.size then
        let sid := strokeIds[pi]!
        if ¬ seen.contains sid then
          seen := seen.push sid
    return seen.qsort (· < ·)

/-- Count patches whose strokesID set equals one of our cycles' sets. -/
def countExact (strokeIds : Array Nat) (patches : Array (Array Nat))
    (g : Graph) (cycles : Array (Array EdgeId)) : Nat :=
  Id.run do
    let mut detected : Array (Array Nat) := #[]
    for cycle in cycles do
      let ids := cycleToStrokeIds strokeIds g cycle
      if ¬ detected.contains ids then
        detected := detected.push ids
    let mut hits := 0
    for p in patches do
      if detected.contains p then
        hits := hits + 1
    return hits

structure SweepRow where
  prox    : Float
  nodes   : Nat
  edges   : Nat
  cycles  : Nat          -- union of CCW + CW cycle sets, deduped by edge set
  exact   : Nat          -- exact matches against hatPatches
  exactSet : Array (Array Nat) := #[]  -- the per-config matched stroke-id sets
deriving Repr

/-- Union of all 4 walk variants (CCW/CW × exclude/no-exclude),
    deduped by edge-set signature so rotated duplicates don't
    double-count. -/
def cyclesUnion (g : Graph) : Array (Array EdgeId) := Id.run do
  let mut seenSig : Array (Array EdgeId) := #[]
  let mut out : Array (Array EdgeId) := #[]
  for ccw in [true, false] do
    for excl in [true, false] do
      for c in findCycles g ccw excl do
        let sig := c.qsort (· < ·)
        if ¬ seenSig.contains sig then
          seenSig := seenSig.push sig
          out := out.push c
  -- EXPERIMENT (cassie-climb): also union the parallel-transport port walker.
  -- CyclePatch uses findCyclesPort exclusively and calls it a strict superset
  -- of the legacy single-global-PCA-plane findCycles; the sweep was never
  -- updated to it, so it under-enumerates faces of the 3D hat arrangement.
  for c in findCyclesPort g do
    let sig := c.qsort (· < ·)
    if ¬ seenSig.contains sig then
      seenSig := seenSig.push sig
      out := out.push c
  return out

/-- For each cycle, the upstream stroke-id set. Deduped. -/
def cycleStrokeIdSets (strokeIds : Array Nat) (g : Graph)
    (cs : Array (Array EdgeId)) : Array (Array Nat) :=
  Id.run do
    let mut out : Array (Array Nat) := #[]
    for c in cs do
      let ids := cycleToStrokeIds strokeIds g c
      if ¬ out.contains ids then out := out.push ids
    return out

def runOne (strokes : Array (Array Vec3)) (strokeIds : Array Nat)
    (patches : Array (Array Nat)) (prox : Float) : SweepRow :=
  -- cassie-climb: augment so nodeMeta is populated and findCyclesPort engages
  -- (plain buildArrangement leaves nodeMeta empty → port walker silently falls
  -- back to the legacy single-global-PCA-plane walk). Matches CyclePatch.
  let g := buildArrangementAugmented strokes #[] prox prox 8
  let cs := cyclesUnion g
  let sets := cycleStrokeIdSets strokeIds g cs
  let exact := Id.run do
    let mut hits := 0
    for p in patches do
      if sets.contains p then hits := hits + 1
    return hits
  { prox
  , nodes := g.nodes.size
  , edges := g.edges.size
  , cycles := cs.size
  , exact
  , exactSet := sets }

/-- Run the proximity sweep against ONE fixture mode, contributing the
    detected stroke-id sets to the cross-mode grand union. -/
def runMode (label : String) (strokes : Array (Array Vec3))
    (strokeIds : Array Nat) (patches : Array (Array Nat))
    (grand : Array (Array Nat)) : IO (Array (Array Nat)) := do
  IO.println s!"=== {label} : {strokes.size} polylines ==="
  let proxes : List Float :=
    [0.0006, 0.0008, 0.0010, 0.0012, 0.0014, 0.0017, 0.0020, 0.0025]
  let mut g := grand
  for prox in proxes do
    let row := runOne strokes strokeIds patches prox
    IO.println s!"  prox={row.prox}  n={row.nodes}  e={row.edges}  unique={row.cycles}  exact={row.exact}/{patches.size}"
    for s in row.exactSet do
      if ¬ g.contains s then g := g.push s
  return g

def main : IO Unit := do
  let mut grand : Array (Array Nat) := #[]
  -- Each fixture module has the SAME patches table (deterministic from
  -- the same JSON) but DIFFERENT strokes / strokeIds depending on the
  -- tessellation. Run each, contribute to the grand union by-sid.
  grand ← runMode "spp=8       "
            CassieAvbd.CycleDetect.Fixtures.hatStrokes
            CassieAvbd.CycleDetect.Fixtures.hatStrokeIds
            CassieAvbd.CycleDetect.Fixtures.hatPatches grand
  grand ← runMode "spp=16      "
            CassieAvbd.CycleDetect.FixturesT16.hatStrokes
            CassieAvbd.CycleDetect.FixturesT16.hatStrokeIds
            CassieAvbd.CycleDetect.FixturesT16.hatPatches grand
  grand ← runMode "tess(6,1°)  "
            CassieAvbd.CycleDetect.FixturesT6.hatStrokes
            CassieAvbd.CycleDetect.FixturesT6.hatStrokeIds
            CassieAvbd.CycleDetect.FixturesT6.hatPatches grand
  -- Grand union across (mode, prox), by stroke-id set.
  let mut grandExact := 0
  for p in CassieAvbd.CycleDetect.Fixtures.hatPatches do
    if grand.contains p then grandExact := grandExact + 1
  IO.println s!""
  IO.println s!"GRAND-by-sid  unique-sets={grand.size}  exact={grandExact}/{CassieAvbd.CycleDetect.Fixtures.hatPatches.size}"

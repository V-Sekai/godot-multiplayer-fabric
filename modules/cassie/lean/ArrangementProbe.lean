/-
ArrangementProbe — minimal driver that loads a polylines JSON, runs
`buildArrangementAugmented`, and prints the resulting graph's node and
edge counts as a single JSON line on stdout.

Used by `modules/cassie/tools/arrangement_parity_sweep.py` to diff the
Lean port against cassie-data's `sketch_graph/NN-X-Y.json` reference
across all 65 datasets that have one.

Prox and merge_eps are read from the polylines JSON's top-level fields,
which the orchestrator derives from the recorded canvasScale per Unity's
CASSIEParameters defaults:
  prox      = SmallDistance × 2   = 0.04 × canvasScale
  merge_eps = SmallDistance × 0.5 = 0.01 × canvasScale
There is no CLI tuning knob and no per-mesh search — one rule, one value
per session. See `feedback_no-prox-tuning.md` in auto-memory.

The JSON must also carry `samples_per_cubic` — the polyline samples per
cubic Bezier piece used by the orchestrator's flatten step. The
arrangement uses this to recover cubic boundaries for the
single-split-per-stroke-pair detection (mirroring Unity's
`GetClosestProjection` semantics).

  lake exe arrangement_probe --input=<polylines.json>

Output (stdout, single line):
  {"nodes": N, "edges": M, "sharp": S, "strokes": K, "prox": P, "merge_eps": E, "spp": S}

Per-mesh wall time = arrangement build only — no CDT2d, no PMP.
-/

import CassieAvbd.CycleDetect
import CassieAvbd.CycleDetect.NodeAugment
import CassiePolylinesJson

open CassieAvbd.CycleDetect

def main (args : List String) : IO Unit := do
  let mut inputPath : Option String := none
  for a in args do
    if a.startsWith "--input=" then
      inputPath := some (a.drop 8).toString
  let some path := inputPath
    | throw (IO.userError "missing --input=polylines.json")
  let (strokes, _ids, cubics, splits) ← CassiePolylinesJson.loadFile path
  let (proxOpt, mEpsOpt, sppOpt) ← CassiePolylinesJson.loadParams path
  let some prox := proxOpt
    | throw (IO.userError "polylines JSON missing top-level `prox` field — derive from canvasScale, do not hardcode")
  let some mEps := mEpsOpt
    | throw (IO.userError "polylines JSON missing top-level `merge_eps` field — derive from canvasScale, do not hardcode")
  let some spp := sppOpt
    | throw (IO.userError "polylines JSON missing top-level `samples_per_cubic` field — encode from the flatten step")
  if spp = 0 then
    throw (IO.userError "samples_per_cubic must be > 0")
  -- Prefer temporal pre-supplied splits (from raw_data's
  -- appliedPositionConstraints) when ANY stroke has them. They are
  -- the draw-time intersection record Unity actually committed —
  -- much more reliable than re-deriving topology from final geometry.
  let anySplits := Id.run do
    for sps in splits do
      if sps.size > 0 then return true
    return false
  let g :=
    if anySplits then
      buildArrangementAugmentedFromSplits strokes splits mEps
    else
      buildArrangementAugmented strokes cubics prox mEps spp
  let mode := if anySplits then "temporal" else "geometric"
  let mut nSharp := 0
  for i in [:g.nodes.size] do
    if h : i < g.nodeMeta.size then
      if g.nodeMeta[i].isSharp then nSharp := nSharp + 1
  IO.println s!"\{\"nodes\": {g.nodes.size}, \"edges\": {g.edges.size}, \"sharp\": {nSharp}, \"strokes\": {strokes.size}, \"prox\": {prox}, \"merge_eps\": {mEps}, \"spp\": {spp}, \"mode\": \"{mode}\"}"

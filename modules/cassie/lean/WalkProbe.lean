/-
WalkProbe — Phase B.0 sanity check on the hat polyline fixture.

Runs both the legacy `findCycles` and the new `findCyclesPort` on the
augmented arrangement; reports cycle counts side by side plus a few
sampled sharp-node-walk traces (incoming edge → chosen next edge under
the new state machine).

  lake exe walk_probe
-/

import CassieAvbd.CycleDetect
import CassieAvbd.CycleDetect.NodeAugment
import CassieAvbd.CycleDetect.Fixtures.HatStrokes

open CassieAvbd.CycleDetect

def main : IO Unit := do
  let strokes := CassieAvbd.CycleDetect.Fixtures.hatStrokes
  let prox := 0.0017
  let g := buildArrangementAugmented strokes #[] prox prox 8
  IO.println s!"hat — {g.nodes.size} nodes, {g.edges.size} edges"
  IO.println s!"  nodeMeta populated: {g.nodeMeta.size}"
  let mut nSharp := 0
  for i in [:g.nodes.size] do
    if g.nodeMeta[i]!.isSharp then nSharp := nSharp + 1
  IO.println s!"  sharp nodes: {nSharp}"
  let cyclesLegacy := findCycles g
  let cyclesPort := findCyclesPort g
  IO.println s!"findCycles      (legacy, single PCA plane): {cyclesLegacy.size}"
  IO.println s!"findCyclesPort  (Unity B.0 state machine):  {cyclesPort.size}"
  -- Show which cycles are in port but not in legacy (and vice versa) by
  -- edge-set signature.
  let mut legacySigs : Array (Array EdgeId) := #[]
  for c in cyclesLegacy do
    legacySigs := legacySigs.push (c.qsort (· < ·))
  let mut portSigs : Array (Array EdgeId) := #[]
  for c in cyclesPort do
    portSigs := portSigs.push (c.qsort (· < ·))
  let inBoth := portSigs.filter (legacySigs.contains ·)
  let onlyPort := portSigs.filter (fun s => ¬ legacySigs.contains s)
  let onlyLegacy := legacySigs.filter (fun s => ¬ portSigs.contains s)
  IO.println s!"  intersection (same cycle in both):   {inBoth.size}"
  IO.println s!"  only in port (new):                  {onlyPort.size}"
  IO.println s!"  only in legacy (port misses):        {onlyLegacy.size}"

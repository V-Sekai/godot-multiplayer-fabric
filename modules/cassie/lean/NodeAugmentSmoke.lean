/-
NodeAugmentSmoke — validates `NodeAugment.augment` on the hat
polylines fixture. Counts how many nodes are sharp vs smooth, prints
the first few sharp-node IDs + their fitted normals.

  lake exe node_augment_smoke
-/

import CassieAvbd.CycleDetect
import CassieAvbd.CycleDetect.NodeAugment
import CassieAvbd.CycleDetect.Fixtures.HatStrokes

open CassieAvbd.CycleDetect

def main : IO Unit := do
  let strokes := CassieAvbd.CycleDetect.Fixtures.hatStrokes
  let g0 := buildArrangement strokes #[] 0.0017 0.0017 8
  let g := NodeAugment.augment g0
  IO.println s!"nodes: {g.nodes.size}  meta: {g.nodeMeta.size}"
  if g.nodeMeta.size ≠ g.nodes.size then
    IO.println "ERROR: meta size mismatch"
    return
  let mut nSharp := 0
  let mut nSmooth := 0
  let mut nDegenerate := 0
  for i in [:g.nodes.size] do
    let m := g.nodeMeta[i]!
    if m.neighborsCcw.isEmpty then nDegenerate := nDegenerate + 1
    else if m.isSharp then nSharp := nSharp + 1
    else nSmooth := nSmooth + 1
  IO.println s!"  smooth: {nSmooth}   sharp: {nSharp}   degenerate (<2 neighbors): {nDegenerate}"
  -- First 5 sharp node IDs + their normals
  IO.println "first 5 sharp nodes:"
  let mut shown := 0
  for i in [:g.nodes.size] do
    if shown >= 5 then break
    let m := g.nodeMeta[i]!
    if m.isSharp then
      let p := g.nodes[i]!
      IO.println s!"  n{i}: pos=({p.1}, {p.2.1}, {p.2.2}) normal=({m.normal.1}, {m.normal.2.1}, {m.normal.2.2})"
      shown := shown + 1
  -- Sanity: per-node neighborsCcw should match nodeEdges count when populated.
  let mut anyMismatch := false
  for i in [:g.nodes.size] do
    let m := g.nodeMeta[i]!
    if m.neighborsCcw.size > 0 then
      if m.neighborsCcw.size ≠ (nodeEdges g i).size then
        anyMismatch := true
        IO.println s!"  node {i}: neighborsCcw {m.neighborsCcw.size} vs nodeEdges {(nodeEdges g i).size}"
  if !anyMismatch then
    IO.println "all populated neighborsCcw sizes match nodeEdges ✓"

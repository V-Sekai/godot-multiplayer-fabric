-- Bit-compare the baked Hat fixture polylines vs the runtime
-- polylines loaded from hat_polylines.json. If they diverge anywhere,
-- the runtime loader path produces a different arrangement → different
-- cycles → different patch count.

import CassieAvbd.CycleDetect.Fixtures.HatStrokes
import CassiePolylinesJson

open CassieAvbd.CycleDetect

def main : IO Unit := do
  let baked := CassieAvbd.CycleDetect.Fixtures.hatStrokes
  let (loaded, _) ← CassiePolylinesJson.loadFile
    "CassieAvbd/CycleDetect/Fixtures/hat_polylines.json"
  IO.println s!"baked.size  = {baked.size}"
  IO.println s!"loaded.size = {loaded.size}"
  if !(baked.size == loaded.size) then
    IO.println "STROKE COUNT MISMATCH"
    return
  let mut totalSamples := 0
  let mut diffs := 0
  let mut firstDiff : Option (Nat × Nat × Vec3 × Vec3) := none
  for i in [:baked.size] do
    let a := baked[i]!
    let b := loaded[i]!
    if !(a.size == b.size) then
      IO.println s!"stroke {i}: sample count {a.size} vs {b.size}"
      diffs := diffs + 1
      continue
    totalSamples := totalSamples + a.size
    for j in [:a.size] do
      let pa := a[j]!
      let pb := b[j]!
      if !(pa.1 == pb.1) || !(pa.2.1 == pb.2.1) || !(pa.2.2 == pb.2.2) then
        diffs := diffs + 1
        if firstDiff.isNone then
          firstDiff := some (i, j, pa, pb)
  IO.println s!"total samples scanned: {totalSamples}"
  IO.println s!"diffs: {diffs}"
  match firstDiff with
  | some (i, j, pa, pb) =>
    IO.println s!"first diff at stroke {i}, sample {j}:"
    IO.println s!"  baked:  ({pa.1}, {pa.2.1}, {pa.2.2})"
    IO.println s!"  loaded: ({pb.1}, {pb.2.1}, {pb.2.2})"
  | none => IO.println "BIT-IDENTICAL"

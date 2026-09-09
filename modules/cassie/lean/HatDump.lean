/-
HatDump — prints the entire HatRawData fixture as JSON, used by the
roundtrip-soundness check that diffs Lean's output against the source
`thirdparty/cassie-data/raw_data/hat.json`.

  lake exe hat_dump > /tmp/hat_from_lean.json
-/

import CassieAvbd.CycleDetect.Fixtures.HatRawData

def main : IO Unit := do
  IO.println CassieAvbd.CycleDetect.HatRawData.toJson

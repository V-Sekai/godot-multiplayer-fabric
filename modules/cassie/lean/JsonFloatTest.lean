import Lean.Data.Json
open Lean

def main : IO Unit := do
  -- Literal Float vs Float parsed via Json
  -- Use a helper that shifts the float into UInt64-printable space.
  let printBits (label : String) (x : Float) : IO Unit := do
    let scaled := x * 1.0e15
    IO.println s!"{label}: {x}    *1e15 = {scaled}"
  let direct : Float := 0.135022715
  printBits "direct literal " direct
  match Json.parse "0.135022715" with
  | .ok (Json.num n) =>
    let viaJson : Float := n.toFloat
    printBits "via Json.parse" viaJson
    let delta := direct - viaJson
    IO.println s!"delta: {delta}    *1e20 = {delta * 1.0e20}"
    IO.println s!"x==x? {direct == viaJson}"
  | _ => IO.println "parse failed"

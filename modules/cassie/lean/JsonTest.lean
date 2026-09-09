import Lean.Data.Json

def main : IO Unit := do
  let s := "{\"a\":1,\"b\":[2,3]}"
  match Lean.Json.parse s with
  | .ok j => IO.println s!"parsed: {j.compress}"
  | .error e => IO.println s!"error: {e}"

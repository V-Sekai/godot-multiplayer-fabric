import Lean.Data.Json
import CassieAvbd.CycleDetect.Vec
import CassieAvbd.CycleDetect.BezierIntersect
import CassieAvbd.CycleDetect.Arrangement

/-!
# `CassiePolylinesJson` — runtime polylines loader for CyclePatch

Reads a JSON file in the polylines shape used by `arrangement_probe`
and `cycle_patch --input`:

  ```json
  { "strokes": [
      { "id": 0,
        "pts": [[x,y,z], [x,y,z], ...],
        "cubics": [[[x,y,z], [x,y,z], [x,y,z], [x,y,z]], ...]
      },
      ...
    ] }
  ```

`pts` is the flattened polyline carrying edge geometry. `cubics` is the
authoritative cubic Bezier control points used by `BezierIntersect` for
true intersection detection; when absent (legacy hat fixtures), the
loader emits an empty cubic list and the arrangement falls back to
polyline-sample minimum behavior on those strokes.
-/

open Lean (Json)

namespace CassiePolylinesJson

open CassieAvbd.CycleDetect

private def jsonToFloat? : Json → Option Float
  | .num n => some n.toFloat
  | _ => none

/-- Parse `[x, y, z]` into a `Vec3`. -/
private def parseVec3 (j : Json) : Option Vec3 := do
  let arr ← j.getArr?.toOption
  if arr.size < 3 then none
  else
    let x ← jsonToFloat? arr[0]!
    let y ← jsonToFloat? arr[1]!
    let z ← jsonToFloat? arr[2]!
    pure (x, y, z)

/-- Parse one `[[x,y,z], [x,y,z], [x,y,z], [x,y,z]]` cubic. -/
private def parseCubic (j : Json) : Option Cubic := do
  let arr ← j.getArr?.toOption
  if arr.size < 4 then none
  else
    let p0 ← parseVec3 arr[0]!
    let p1 ← parseVec3 arr[1]!
    let p2 ← parseVec3 arr[2]!
    let p3 ← parseVec3 arr[3]!
    pure (p0, p1, p2, p3)

/-- Parse one `{ "arcLen": Float, "pos": [x, y, z] }` split. -/
private def parseSplit (j : Json) : Option Split := do
  let arcF ← (j.getObjVal? "arcLen").toOption
  let arc ← jsonToFloat? arcF
  let posF ← (j.getObjVal? "pos").toOption
  let pos ← parseVec3 posF
  pure { arcLen := arc, pos := pos }

/-- Parse `{ "id", "pts", "cubics"?, "splits"? }`. -/
private def parseStroke (j : Json)
    : Option (Nat × Array Vec3 × Array Cubic × Array Split) := Id.run do
  let some id := (j.getObjValAs? Nat "id").toOption | return none
  let some pts := (j.getObjVal? "pts").toOption | return none
  let some arr := pts.getArr?.toOption | return none
  let mut polyOut : Array Vec3 := #[]
  for v in arr do
    let some p := parseVec3 v | return none
    polyOut := polyOut.push p
  let mut cubOut : Array Cubic := #[]
  match j.getObjVal? "cubics" with
  | .error _ => pure ()
  | .ok cubField =>
    let some carr := cubField.getArr?.toOption | return none
    for cj in carr do
      let some c := parseCubic cj | return none
      cubOut := cubOut.push c
  let mut spOut : Array Split := #[]
  match j.getObjVal? "splits" with
  | .error _ => pure ()
  | .ok splitField =>
    let some sarr := splitField.getArr?.toOption | return none
    for sj in sarr do
      let some sp := parseSplit sj | return none
      spOut := spOut.push sp
  return some (id, polyOut, cubOut, spOut)

/-- Parse the full polylines document. Returns
    `(polylines, ids, cubics, splits)`. -/
def parse (s : String)
    : Except String
        (Array (Array Vec3) × Array Nat ×
         Array (Array Cubic) × Array (Array Split)) := do
  let j ← Json.parse s
  let strokesField ← j.getObjVal? "strokes"
  let strokesArr ← strokesField.getArr?
  let mut polylines : Array (Array Vec3) := #[]
  let mut ids : Array Nat := #[]
  let mut cubicsOut : Array (Array Cubic) := #[]
  let mut splitsOut : Array (Array Split) := #[]
  for js in strokesArr do
    match parseStroke js with
    | some (id, poly, cubs, sps) =>
      polylines := polylines.push poly
      ids := ids.push id
      cubicsOut := cubicsOut.push cubs
      splitsOut := splitsOut.push sps
    | none =>
      throw s!"stroke parse failed at index {polylines.size}"
  pure (polylines, ids, cubicsOut, splitsOut)

/-- Read + parse a polylines JSON file from disk. -/
def loadFile (path : String)
    : IO (Array (Array Vec3) × Array Nat ×
          Array (Array Cubic) × Array (Array Split)) := do
  let contents ← IO.FS.readFile path
  match parse contents with
  | .ok v => pure v
  | .error e => throw (IO.userError s!"CassiePolylinesJson: {e}")

/-- Read a top-level number field, returning `none` if the key is
    absent. Used for `prox` (Unity ProximityThreshold = `0.04 ×
    canvasScale`) and `merge_eps` (Unity mergeConstraintsThreshold =
    `0.01 × canvasScale`). The caller must derive these from the
    recorded canvasScale — see `feedback_no-prox-tuning.md`. -/
private def parseTopFloat (s : String) (key : String)
    : Except String (Option Float) := do
  let j ← Json.parse s
  match j.getObjVal? key with
  | .error _ => pure none
  | .ok v =>
    match jsonToFloat? v with
    | some f => pure (some f)
    | none => throw s!"{key} field is not a number"

def parseProx (s : String) : Except String (Option Float) := parseTopFloat s "prox"
def parseMergeEps (s : String) : Except String (Option Float) := parseTopFloat s "merge_eps"

/-- Optional `samples_per_cubic` top-level field. The orchestrator must
    set this to the number of polyline samples each cubic Bezier piece
    contributes (currently 8 in `arrangement_parity_sweep.py`). Lets
    `findAllSplitsByCubic` partition samples back into the cubic units
    Unity's intersection tests operate on, instead of testing every
    sub-sample pair. -/
def parseSamplesPerCubic (s : String) : Except String (Option Nat) := do
  let j ← Json.parse s
  match j.getObjVal? "samples_per_cubic" with
  | .error _ => pure none
  | .ok v =>
    match v.getNat?.toOption with
    | some n => pure (some n)
    | none => throw "samples_per_cubic field is not a non-negative integer"

/-- Read the optional `prox`, `merge_eps`, and `samples_per_cubic`
    top-level fields. -/
def loadParams (path : String)
    : IO (Option Float × Option Float × Option Nat) := do
  let contents ← IO.FS.readFile path
  let prox ← match parseProx contents with
    | .ok v => pure v
    | .error e => throw (IO.userError s!"CassiePolylinesJson: {e}")
  let mEps ← match parseMergeEps contents with
    | .ok v => pure v
    | .error e => throw (IO.userError s!"CassiePolylinesJson: {e}")
  let spp ← match parseSamplesPerCubic contents with
    | .ok v => pure v
    | .error e => throw (IO.userError s!"CassiePolylinesJson: {e}")
  pure (prox, mEps, spp)

/-- Back-compat. -/
def loadProxAndMergeEps (path : String) : IO (Option Float × Option Float) := do
  let (p, m, _) ← loadParams path
  pure (p, m)

def loadProx (path : String) : IO (Option Float) := do
  let (p, _, _) ← loadParams path
  pure p

end CassiePolylinesJson

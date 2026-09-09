/-
CyclePatch — close the cycle-detect → triangulate → fair loop.

  lake exe cycle_patch
-/

import CassieAvbd.CycleDetect
import CassieAvbd.CycleDetect.Fixtures.HatStrokes
import CassieAvbd.CycleDetect.Fixtures.HatStrokesT16
import CassieAvbd.CycleDetect.Fixtures.HatStrokesT6
import CassiePmp.Mesh
import CassieGeogram.Delaunay
import CassiePolylinesJson

open CassieAvbd.CycleDetect
open CassiePmp
open CassieGeogram (delaunayFromBoundary delaunayFree)

/-! ## Q64.64 fixed-point

A Nat representing `value × 2⁶⁴`. Used to pass sub-Float-ulp-precise
scalar values across the cycle_patch driver↔worker subprocess boundary
without going through any `String.toFloat` (which Lean's stdlib
doesn't ship). -/
namespace Q64

/-- 2⁶⁴ — the denominator implicit in Q64.64. -/
def scale : Nat := 18_446_744_073_709_551_616

/-- Encode an exact rational `num / den` as Q64.64. -/
@[inline] def ofRat (num den : Nat) : Nat := (num * scale) / den

/-- Decode Q64.64 to Float for use at compute sites. The 2⁻⁶⁴ ulp is
    smaller than Float64's epsilon, so this conversion is exact for any
    value Float can represent at all. -/
@[inline] def toFloat (q : Nat) : Float := q.toFloat / scale.toFloat

end Q64

/-- Union of cycle searches, deduped by edge-set. When the graph has
    `nodeMeta` populated (i.e. it came from `buildArrangementAugmented`)
    we use the Unity-port `findCyclesPort` only — it's strictly a
    superset of legacy `findCycles` in steady state. Without
    augmentation we still run all 4 legacy variants for backwards
    compat with the dump baselines. -/
def cyclesUnion (g : Graph) : Array (Array EdgeId) := Id.run do
  let mut seenSig : Array (Array EdgeId) := #[]
  let mut out : Array (Array EdgeId) := #[]
  if g.nodeMeta.size = g.nodes.size then
    -- Union both walks; the port catches the Unity-style sharp
    -- branches and ShouldReverse flips, the legacy fills the 9-or-so
    -- it currently misses (port refinement target).
    let portCs := findCyclesPort g
    let legacyCs := findCycles g
    for c in portCs do
      let sig := c.qsort (· < ·)
      if ¬ seenSig.contains sig then
        seenSig := seenSig.push sig
        out := out.push c
    for c in legacyCs do
      let sig := c.qsort (· < ·)
      if ¬ seenSig.contains sig then
        seenSig := seenSig.push sig
        out := out.push c
  else
    for ccw in [true, false] do
      for excl in [true, false] do
        for c in findCycles g ccw excl do
          let sig := c.qsort (· < ·)
          if ¬ seenSig.contains sig then
            seenSig := seenSig.push sig
            out := out.push c
  return out

/-- IO-monadic boundary walker — produces the same flat xyz polyline as
    a pure version would, but as IO so we can trace + recover if a
    pathological input hits the underlying Lean codegen. -/
def cycleBoundary (g : Graph) (cycle : Array EdgeId) : IO FloatArray := do
  if cycle.size < 2 then return FloatArray.mk #[]
  let e0 := g.edges[cycle[0]!]!
  let e1 := g.edges[cycle[1]!]!
  let shared : NodeId :=
    if e0.nb == e1.na || e0.nb == e1.nb then e0.nb else e0.na
  let mut out : Array Float := #[]
  -- Edge 0: emit toward `shared`.
  let n0 := e0.pts.size
  if e0.nb == shared then
    for i in [:n0] do
      let p := e0.pts[i]!
      out := out.push p.1
      out := out.push p.2.1
      out := out.push p.2.2
  else
    for i in [:n0] do
      let p := e0.pts[n0 - 1 - i]!
      out := out.push p.1
      out := out.push p.2.1
      out := out.push p.2.2
  let mut prevent : NodeId := shared
  -- Continuation edges: skip the duplicated start vertex.
  for k in [1 : cycle.size] do
    let e := g.edges[cycle[k]!]!
    let n := e.pts.size
    if e.na == prevent then
      for i in [1 : n] do
        let p := e.pts[i]!
        out := out.push p.1
        out := out.push p.2.1
        out := out.push p.2.2
      prevent := e.nb
    else
      for i in [1 : n] do
        let p := e.pts[n - 1 - i]!
        out := out.push p.1
        out := out.push p.2.1
        out := out.push p.2.2
      prevent := e.na
  return FloatArray.mk out

def pickLongestCycle (g : Graph) (cs : Array (Array EdgeId)) :
    Option (Array EdgeId) := Id.run do
  let mut bestIdx : Option Nat := none
  let mut bestLen : Nat := 0
  for i in [:cs.size] do
    let c := cs[i]!
    let mut len := 0
    for eid in c do
      len := len + g.edges[eid]!.pts.size
    if len > bestLen then
      bestLen := len
      bestIdx := some i
  match bestIdx with
  | some i => return some cs[i]!
  | none => return none

/-- 2D orientation (cross product sign) on the XZ plane. -/
@[inline] def orient2D (ax az bx bz cx cz : Float) : Float :=
  (bx - ax) * (cz - az) - (bz - az) * (cx - ax)

/-- Strict 2D segment-segment crossing. Endpoints touching don't count
    (the closed-loop sample emission has adjacent segments share an
    endpoint by construction). -/
def segsCross (ax az bx bz cx cz dx dz : Float) : Bool :=
  let o1 := orient2D ax az bx bz cx cz
  let o2 := orient2D ax az bx bz dx dz
  let o3 := orient2D cx cz dx dz ax az
  let o4 := orient2D cx cz dx dz bx bz
  (o1 > 0 && o2 < 0 || o1 < 0 && o2 > 0)
    && (o3 > 0 && o4 < 0 || o3 < 0 && o4 > 0)

/-- True if the XZ-projected boundary polygon self-intersects — any two
    non-adjacent edges cross. CDT2d's exact predicates abort on these,
    so the cycle is unsuitable for triangulation regardless. -/
def boundarySelfIntersects (bd : FloatArray) (n : Nat) : Bool := Id.run do
  for i in [:n] do
    let i' := (i + 1) % n
    let ax := bd[3*i]!
    let az := bd[3*i + 2]!
    let bx := bd[3*i']!
    let bz := bd[3*i' + 2]!
    for j in [i + 2 : n] do
      -- Skip the closing pair (last edge wraps to share endpoint with first).
      if i = 0 && j = n - 1 then continue
      let j' := (j + 1) % n
      let cx := bd[3*j]!
      let cz := bd[3*j + 2]!
      let dx := bd[3*j']!
      let dz := bd[3*j' + 2]!
      if segsCross ax az bx bz cx cz dx dz then
        return true
  return false

/-- Shoelace area of the XZ projection. If ~0 the polygon is degenerate
    (collinear / collapsed-vertical) and CDT2d will abort on it. -/
def xzShoelaceArea (bd : FloatArray) (n : Nat) : Float := Id.run do
  if n < 3 then return 0.0
  let mut s : Float := 0.0
  for i in [:n] do
    let j := (i + 1) % n
    let xi := bd[3*i]!
    let zi := bd[3*i + 2]!
    let xj := bd[3*j]!
    let zj := bd[3*j + 2]!
    s := s + xi * zj - xj * zi
  return s.abs * 0.5

structure Patch where
  boundary : Nat
  verts    : Nat
  tris     : Nat
deriving Repr

structure PatchMesh where
  boundary : Nat
  positions : FloatArray   -- flat xyz xyz ...
  faces : Array UInt32     -- flat a b c a b c ...

/-- Triangulate one cycle and keep the full mesh. `none` when no patch
    comes out (too-small / XZ-degenerate / CDT2d empty). -/
def patchOneMesh (g : Graph) (cycle : Array EdgeId) :
    IO (Option PatchMesh) := do
  let bd ← cycleBoundary g cycle
  let n := bd.size / 3
  if n < 4 then return none
  -- Filters tuned for the CASSIE hat: boundary near-flat in XZ, area
  -- positive. For meshes whose patch boundaries aren't XZ-flat (most
  -- non-hat geometry) these reject viable cycles; subprocess
  -- isolation already catches CDT2d aborts, so let the geometry
  -- decide whether the patch survives triangulation.
  if xzShoelaceArea bd n < 1.0e-12 then return none
  let dh ← delaunayFromBoundary n.toUSize bd 0.0
  let nv ← CassieGeogram.nVertices dh
  let nt ← CassieGeogram.nTriangles dh
  if nt = 0 then
    delaunayFree dh
    return none
  let posOut := FloatArray.mk (Array.replicate (nv.toNat * 3) 0.0)
  let pos ← CassieGeogram.getPositions dh posOut
  let triOut := ByteArray.mk (Array.replicate (nt.toNat * 3 * 4) (0 : UInt8))
  let tris ← CassieGeogram.getTriangles dh triOut
  delaunayFree dh
  -- Unpack little-endian uint32 face indices.
  let mut faces : Array UInt32 := #[]
  for i in [:nt.toNat * 3] do
    let b0 := tris[4*i + 0]!.toUInt32
    let b1 := tris[4*i + 1]!.toUInt32
    let b2 := tris[4*i + 2]!.toUInt32
    let b3 := tris[4*i + 3]!.toUInt32
    faces := faces.push (b0 ||| (b1 <<< 8) ||| (b2 <<< 16) ||| (b3 <<< 24))
  return some { boundary := n, positions := pos, faces := faces }

def patchOne (g : Graph) (cycle : Array EdgeId) :
    IO (Option Patch) := do
  match (← patchOneMesh g cycle) with
  | none => return none
  | some m =>
    return some {
      boundary := m.boundary,
      verts := m.positions.size / 3,
      tris := m.faces.size / 3 }

/-- Resolve mode name to (strokes, strokeIds). Modes mirror the
    fixtures we have generated for the hat: spp=8 (default), spp=16,
    and tess(6,1°). -/
def modeData (mode : String) :
    Array (Array Vec3) × Array Nat :=
  match mode with
  | "16" => (CassieAvbd.CycleDetect.FixturesT16.hatStrokes,
             CassieAvbd.CycleDetect.FixturesT16.hatStrokeIds)
  | "6"  => (CassieAvbd.CycleDetect.FixturesT6.hatStrokes,
             CassieAvbd.CycleDetect.FixturesT6.hatStrokeIds)
  | _    => (CassieAvbd.CycleDetect.Fixtures.hatStrokes,
             CassieAvbd.CycleDetect.Fixtures.hatStrokeIds)

/-- Build the unique sorted stroke-id set for a cycle. -/
def cycleStrokeIds (strokeIds : Array Nat) (g : Graph)
    (cycle : Array EdgeId) : Array Nat := Id.run do
  let mut seen : Array Nat := #[]
  for eid in cycle do
    let pi := g.edges[eid]!.src
    if pi < strokeIds.size then
      let sid := strokeIds[pi]!
      if ¬ seen.contains sid then
        seen := seen.push sid
  return seen.qsort (· < ·)

/-- Serialize one patch mesh to JSON matching the C++ side's
    `_dump_our_patches_json` shape: `{verts: [[x,y,z], ...], faces:
    [a,b,c, ...]}`. Same Blender importer works on both pipelines. -/
def patchToJson (m : PatchMesh) (_sids : Array Nat) : String := Id.run do
  let mut s := "{\"verts\":["
  let nv := m.positions.size / 3
  for v in [:nv] do
    if v > 0 then s := s ++ ","
    let x := m.positions[3*v]!
    let y := m.positions[3*v + 1]!
    let z := m.positions[3*v + 2]!
    s := s ++ s!"[{x},{y},{z}]"
  s := s ++ "],\"faces\":["
  for i in [:m.faces.size] do
    if i > 0 then s := s ++ ","
    s := s ++ toString m.faces[i]!.toNat
  s := s ++ "]}"
  return s

/-- Worker: process a single cycle. When `outPath` is `some p`, writes
    a one-patch JSON to that file *and* prints status on stdout.
    `inputPath` overrides `mode` to load polylines from JSON. -/
def runOneCycle (idx : Nat) (mode : String) (prox : Float)
    (outPath : Option String) (inputPath : Option String := none) :
    IO Unit := do
  let (strokes, strokeIds, cubics, splits) ← match inputPath with
    | some p => CassiePolylinesJson.loadFile p
    | none => do
        let (s, ids) := modeData mode
        pure (s, ids, (#[] : Array (Array Cubic)), (#[] : Array (Array Split)))
  let anySplits := Id.run do
    for sps in splits do
      if sps.size > 0 then return true
    return false
  let g :=
    if anySplits then
      buildArrangementAugmentedFromSplits strokes splits prox
    else
      buildArrangementAugmented strokes cubics prox prox 8
  let cs := cyclesUnion g
  if idx >= cs.size then
    IO.println "missing"
    return
  let c := cs[idx]!
  let sids := cycleStrokeIds strokeIds g c
  let sidStr := ",".intercalate (sids.toList.map toString)
  match (← patchOneMesh g c) with
  | none => IO.println s!"none {sidStr}"
  | some m =>
    if let some p := outPath then
      IO.FS.writeFile p (patchToJson m sids)
    IO.println s!"ok {m.boundary} {m.positions.size / 3} {m.faces.size / 3} {sidStr}"

/-- Parse "sid0,sid1,..." back into an Array Nat. Empty string → #[]. -/
def parseSidList (s : String) : Array Nat := Id.run do
  if s.isEmpty then return #[]
  let parts := s.splitOn ","
  let mut out : Array Nat := #[]
  for p in parts do
    if !p.isEmpty then out := out.push p.toNat!
  return out

structure ModeStats where
  mode      : String
  cycles    : Nat
  patches   : Nat
  noPatch   : Nat
  skipped   : Nat
  totalTris : Nat

/-- Drive one fixture mode: build arrangement, find cycles, spawn one
    worker per cycle. Accumulates unique patch stroke-id sets into the
    cross-mode union and returns per-mode counts. -/
def runMode (mode : String) (proxQ : Nat) (exe : String)
    (union : Array (Array Nat)) :
    IO (ModeStats × Array (Array Nat)) := do
  let prox : Float := Q64.toFloat proxQ
  IO.eprintln s!"=== mode {mode} prox-q={proxQ} ({prox}) ==="
  let (strokes, strokeIds) := modeData mode
  -- spp = 8 — hat fixtures and the runtime polylines loader both flatten
  -- cubic Beziers at 8 samples per piece. Cubics are passed as empty
  -- when consuming hat fixtures (fallback to polyline-sample minimum);
  -- the runtime loader supplies them and the arrangement uses true
  -- Bezier intersection instead.
  let g := buildArrangementAugmented strokes #[] prox prox 8
  IO.eprintln s!"  nodes={g.nodes.size} edges={g.edges.size}"
  let cs := cyclesUnion g
  IO.eprintln s!"  cycles found: {cs.size}"
  let mut ok := 0
  let mut tooSmall := 0
  let mut skipped := 0
  let mut totalTris := 0
  let mut u := union
  for i in [:cs.size] do
    let c := cs[i]!
    -- Skip lollipop topology (cycle revisits a node mid-walk).
    let mut hasDup := false
    if c.size >= 2 then
      let e0 := g.edges[c[0]!]!
      let e1 := g.edges[c[1]!]!
      let mut prev : NodeId :=
        if e0.nb == e1.na || e0.nb == e1.nb then e0.nb else e0.na
      let start : NodeId := if prev == e0.nb then e0.na else e0.nb
      let mut visited : Array NodeId := #[start, prev]
      for k in [1 : c.size] do
        let e := g.edges[c[k]!]!
        let nxt : NodeId := if e.na == prev then e.nb else e.na
        if k + 1 < c.size && visited.any (· == nxt) then hasDup := true
        visited := visited.push nxt
        prev := nxt
    if hasDup then
      skipped := skipped + 1
      continue
    -- Spawn ourselves to isolate the CDT2d abort path per cycle.
    let out ← IO.Process.output {
      cmd := exe,
      args := #[s!"--cycle={i}", s!"--mode={mode}",
                s!"--prox-q={proxQ}"],
      stdin := .null }
    if out.exitCode ≠ 0 then
      skipped := skipped + 1
      continue
    let line := out.stdout.trim
    let sids := cycleStrokeIds strokeIds g c
    if line.startsWith "none" then
      tooSmall := tooSmall + 1
    else if line.startsWith "ok " then
      let parts := ((line.drop 3).toString).splitOn " "
      match parts with
      | n :: _v :: t :: _rest =>
        let nN := n.toNat!
        let tN := t.toNat!
        ok := ok + 1
        totalTris := totalTris + tN
        if !u.contains sids then u := u.push sids
        IO.eprintln s!"  [{i}] {c.size}e {nN}s -> {tN}t  sids={sids.toList}"
      | _ =>
        skipped := skipped + 1
    else
      skipped := skipped + 1
  let stats : ModeStats :=
    { mode, cycles := cs.size, patches := ok,
      noPatch := tooSmall, skipped, totalTris }
  return (stats, u)

/-- Single-mode, single-prox driver that aggregates worker JSON files
    into one Blender-importable artifact. Matches the C++ side's
    `_dump_our_patches_json` shape: `{"label":..., "patches":[{verts,
    faces, sids}, ...]}`. -/
def dumpDriver (mode : String) (proxQ : Nat) (outPath : String) : IO Unit := do
  let exe := (← IO.appPath).toString
  let prox := Q64.toFloat proxQ
  let (strokes, _strokeIds) := modeData mode
  -- spp = 8 — hat fixtures and the runtime polylines loader both flatten
  -- cubic Beziers at 8 samples per piece. Cubics are passed as empty
  -- when consuming hat fixtures (fallback to polyline-sample minimum);
  -- the runtime loader supplies them and the arrangement uses true
  -- Bezier intersection instead.
  let g := buildArrangementAugmented strokes #[] prox prox 8
  let cs := cyclesUnion g
  let tmpDir := outPath ++ ".parts"
  IO.FS.createDirAll tmpDir
  IO.eprintln s!"[dump] mode={mode} prox={prox} cycles={cs.size}"
  IO.eprintln s!"[dump] aggregating into {outPath}"
  let mut kept : Array String := #[]
  for i in [:cs.size] do
    let part := s!"{tmpDir}/p{i}.json"
    let out ← IO.Process.output {
      cmd := exe,
      args := #[s!"--cycle={i}", s!"--mode={mode}",
                s!"--prox-q={proxQ}", s!"--out={part}"], stdin := .null }
    if out.exitCode = 0 ∧ (← System.FilePath.pathExists part) then
      kept := kept.push (← IO.FS.readFile part)
  let body := ",".intercalate kept.toList
  IO.FS.writeFile outPath
    s!"\{\"label\":\"lean-cycle_patch mode={mode} prox={prox}\",\"patches\":[{body}]}"
  IO.eprintln s!"[dump] wrote {kept.size} patches"

def driverMain : IO Unit := do
  if let some out ← IO.getEnv "CASSIE_DUMP_PATCHES_JSON" then
    dumpDriver "8" (Q64.ofRat 17 10000) out
    return
  let exe := (← IO.appPath).toString
  let mut union : Array (Array Nat) := #[]
  let mut allStats : Array ModeStats := #[]
  -- Same prox sweep CycleSweep uses, encoded as Q64.64 to avoid any
  -- Float-roundtrip drift on the driver↔worker boundary.
  let proxes : List Nat := [
    Q64.ofRat 6  10000,  -- 0.0006
    Q64.ofRat 1   1000,  -- 0.0010
    Q64.ofRat 14 10000,  -- 0.0014
    Q64.ofRat 17 10000,  -- 0.0017
    Q64.ofRat 2   1000,  -- 0.0020
    Q64.ofRat 25 10000   -- 0.0025
  ]
  for mode in ["8", "16", "6"] do
    for proxQ in proxes do
      let (stats, u) ← runMode mode proxQ exe union
      union := u
      allStats := allStats.push stats
  -- Compare against upstream's hatPatches set (same across modes, derived
  -- from the same source JSON), counting exact matches.
  let upstream := CassieAvbd.CycleDetect.Fixtures.hatPatches
  let mut grandExact := 0
  for p in upstream do
    if union.contains p then grandExact := grandExact + 1
  IO.eprintln ""
  IO.eprintln "[cycle_patch] === per-mode ==="
  for s in allStats do
    IO.eprintln s!"  mode {s.mode}: {s.patches}/{s.cycles}  noPatch={s.noPatch}  skipped={s.skipped}  tris={s.totalTris}"
  IO.eprintln ""
  IO.eprintln s!"[cycle_patch] GRAND-by-sid union: {union.size} unique patches"
  IO.eprintln s!"[cycle_patch] exact-match vs upstream hatPatches: {grandExact}/{upstream.size}"
  IO.eprintln "[cycle_patch] done"

/-- Single-pass driver for `--input X.json`. Spawns workers per cycle
    (same subprocess isolation as `driverMain`) so a CDT2d abort on
    one bad cycle doesn't take down the run. -/
def inputMain (inputPath : String) (outputPath : String)
    (proxQ : Nat) : IO Unit := do
  let prox : Float := Q64.toFloat proxQ
  let exe := (← IO.appPath).toString
  IO.eprintln s!"[input] loading polylines from {inputPath}"
  let (strokes, _ids, cubics, splits) ← CassiePolylinesJson.loadFile inputPath
  IO.eprintln s!"[input]   strokes: {strokes.size}"
  let anySplits := Id.run do
    for sps in splits do
      if sps.size > 0 then return true
    return false
  if anySplits then
    IO.eprintln "[input]   using temporal splits from appliedPositionConstraints"
  let g :=
    if anySplits then
      buildArrangementAugmentedFromSplits strokes splits prox
    else
      buildArrangementAugmented strokes cubics prox prox 8
  IO.eprintln s!"[input]   nodes={g.nodes.size}  edges={g.edges.size}"
  let cs := cyclesUnion g
  IO.eprintln s!"[input]   cycles: {cs.size}"
  let tmpDir := outputPath ++ ".parts"
  IO.FS.createDirAll tmpDir
  let mut bodies : Array String := #[]
  for i in [:cs.size] do
    let part := s!"{tmpDir}/p{i}.json"
    let res ← IO.Process.output {
      cmd := exe,
      args := #[s!"--input={inputPath}", s!"--cycle={i}",
                s!"--prox-q={proxQ}", s!"--out={part}"], stdin := .null }
    if res.exitCode = 0 ∧ (← System.FilePath.pathExists part) then
      bodies := bodies.push (← IO.FS.readFile part)
  let label := s!"cycle_patch --input {inputPath} prox={prox}"
  let body := ",".intercalate bodies.toList
  IO.FS.writeFile outputPath
    s!"\{\"label\":\"{label}\",\"patches\":[{body}]}"
  IO.eprintln s!"[input] wrote {bodies.size} patches → {outputPath}"

/-- Dispatch: `lake exe cycle_patch` → driver; `lake exe cycle_patch
    --cycle=N` → worker on cycle N; `lake exe cycle_patch --input
    X.json --out Y.json` → runtime polylines loader (Phase A.2). -/
def main (args : List String) : IO Unit := do
  let mut cycleIdx : Option Nat := none
  let mut mode : String := "8"
  let mut prox : Float := 0.0017
  let mut inputPath : Option String := none
  let mut outputPath : String := "patches.json"
  let mut proxQ : Nat := Q64.ofRat 17 10000
  for a in args do
    if a.startsWith "--input=" then
      inputPath := some (a.drop 8).toString
    else if a.startsWith "--out=" then
      outputPath := (a.drop 6).toString
    else if a.startsWith "--cycle=" then
      cycleIdx := some ((a.drop 8).toString).toNat!
    else if a.startsWith "--mode=" then
      mode := (a.drop 7).toString
    else if a.startsWith "--prox-q=" then
      -- Q64.64 fixed-point: argument is the raw Nat storage (= value
      -- × 2⁶⁴), so 0.0017 → 0.0017 × 2⁶⁴ ≈ 31_359_464_925_306_237.
      -- Single arbitrary-precision Nat through the CLI; ulp ≈ 2⁻⁶⁴
      -- (~5×10⁻²⁰), well below Float64 round-trip noise.
      let q := (a.drop 9).toString.toNat!
      prox := q.toFloat / Q64.scale.toFloat
      proxQ := q
  -- Worker mode (--cycle=N) is the most specific; honor --input there.
  match cycleIdx with
  | some i =>
    let mut workerOutPath : Option String := none
    for a in args do
      if a.startsWith "--out=" then workerOutPath := some (a.drop 6).toString
    runOneCycle i mode prox workerOutPath inputPath
  | none =>
    match inputPath with
    | some ip => inputMain ip outputPath proxQ
    | none => driverMain

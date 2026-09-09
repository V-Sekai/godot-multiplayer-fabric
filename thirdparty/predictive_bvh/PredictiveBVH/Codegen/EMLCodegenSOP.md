# SOP: Hooking EML Adversarial Heuristics into Godot without Python

This Standard Operating Procedure (SOP) defines the strictly offline, strictly Lean-to-C pipeline for evaluating and embedding ZK heuristic constants (like the C4 lifecycle latency gap) into Godot via `bvh-codegen`. No Python ML libraries are used; E-Graph Equality Saturation (`AmoLean.EGraph`) acts as the optimization engine.

## Step 1: Telemetry Data Generation (Godot)
You must generate raw latency and scale contradiction datasets out of Godot to parameterize the algebraic inputs.
1. Run local / headless V-Sekai MMO instances (`vsk-multiplayer-fabric-4.7`).
2. Induce the *C4 / G131 Entity lifecycle gap*: have players rapidly connect/disconnect.
3. Record their internal `vMaxPhysical` and `spawnGapDistance` directly in `predictive_bvh.cpp` using `FileAccess` or Godot's print log (`packet_latency`, `vMax`, `actual_distance_traveled`).
4. Output this data to a JSON or CSV file under `thirdparty/predictive_bvh/godot_telemetry.csv`.

## Step 2: C-Code EML Emission via `lake exe`
Since `PredictiveBVH/Spatial/EMLAdversarialHeuristic.lean` already formally bounds these scenarios to `Expr Int` representations:
1. In `PredictiveBVH/Codegen/CodeGen.lean`, import `PredictiveBVH.Spatial.EMLAdversarialHeuristic`.
2. Use the local `genC` helper (defined in `CodeGen.lean`), which threads `opt` (e-graph saturation) → `toLowLevel` (CSE) → `generateCFn` (R128 pretty-printer). `AmoLean.CodeGen.generateCFunction` takes a `LowLevelProgram`, not an `Expr Int`, so do not call it directly on a gap formula.
3. Define an `emlC` string after `constantsC` that emits one R128 helper per scenario, and splice it into `cFile` between `constantsC` and the R128 kernels block:
```lean
open PredictiveBVH.EML in
private def emlC : String :=
  genC "pbvh_eml_c1_velocity_injection_gap"       ["v_true", "v_max", "delta"]       c1GapFormula ++ "\n\n" ++
  genC "pbvh_eml_c2_acceleration_underreport_gap" ["accel_floor", "delta"]           c2GapFormula ++ "\n\n" ++
  genC "pbvh_eml_c3_portal_discontinuity_gap"     ["jump_um", "ghost_bound_um"]      c3GapFormula ++ "\n\n" ++
  genC "pbvh_eml_c4_lifecycle_gap_bound"          ["v", "latency_ticks"]             c4GapFormula ++ "\n\n" ++
  genC "pbvh_eml_c5_satellite_rtt_gap"            ["v", "sat_delta", "local_delta"]  c5GapFormula ++ "\n\n" ++
  genC "pbvh_eml_c6_coord_frame_offset_gap"       ["chunk_origin_offset_um"]         c6GapFormula ++ "\n\n" ++
  genC "pbvh_eml_c7_segment_boundary_gap"         ["v_funnel", "v_max", "delta"]     c7GapFormula

private def cFile : String :=
  ...
  constantsC ++ "\n\n" ++
  emlC ++ "\n\n" ++
  scalarFnC ++ "\n\n"
```

Note: the emitted helpers take every physical constant (`v_max`, `accel_floor`, `latency_ticks`, `sat_delta`, `v_funnel`, `chunk_origin_offset_um`) as a runtime argument, not a baked literal. Callers pass the parametric `pbvh_*` helpers from `constantsC` (e.g. `pbvh_v_max_physical_um_per_tick(hz)`, `pbvh_latency_ticks(hz)`, `pbvh_accel_floor_um_per_tick2(hz)`) so the bound tracks the engine's actual physics tick rate. The Lean module still defines each constant at default-hz for internal proofs, but the emission path no longer freezes them into the header.

## Step 3: Compile Lean -> C Headers
Run the ZK Z-bound compiler native to your system:
```bash
cd thirdparty/predictive_bvh
lake exe bvh-codegen
```
*Result:* `predictive_bvh.h` now contains e-graph-optimized `static inline R128 pbvh_eml_cN_*(...)` helpers — one per adversarial scenario — whose bodies are decomposed into CSE'd `r128_add`/`r128_mul` chains.

## Step 4: Godot Runtime Hook (C++)
In `fabric_zone.cpp` or your physics processing tree, call the R128 helper at the constraint-check site and bridge to `real_t` / `int64_t` at the boundary, the same way the existing R128 spatial primitives are consumed. Example for the C4 lifecycle-race bound:
```cpp
#include <thirdparty/predictive_bvh/predictive_bvh.h>

// During physics step or remote sync:
const uint32_t hz = Engine::get_singleton()->get_physics_ticks_per_second();
const R128 v = r128_from_int(pbvh_v_max_physical_um_per_tick(hz));
const R128 latency = r128_from_int((int64_t)pbvh_latency_ticks(hz));
const R128 c4_bound_um = pbvh_eml_c4_lifecycle_gap_bound(v, latency);

if (r128_le(c4_bound_um, r128_from_int(latency_distance_um)) == 0) {
    // latency_distance > c4_bound — rebuild BVH ghost leaf, invalid constraint!
}
```
For the remaining scenarios, consume C1/C5/C7 at authority/interest boundaries (pass v_max via `pbvh_v_max_physical_um_per_tick(hz)`), C3 at portal traversal, C6 at zone-handoff coordinate reframing (pass the configured chunk offset), and C2 in acceleration-integrity checks (pass `pbvh_accel_floor_um_per_tick2(hz)`). All helpers return R128 μm.

## Maintenance & Updates
If new physics systems are added to Godot (e.g., higher rip-current peak velocities for C7), simply bump the constants in `PredictiveBVH/Primitives/Types.lean` and rerun `lake exe bvh-codegen`. Lean 4 will mathematically reprove the E-graph equivalencies and overwrite `predictive_bvh.h` with zero structural overhead.

# Contributing to `predictive_bvh`

This module is a Lean-proved BVH with a codegen pipeline that emits
`predictive_bvh.h` / `predictive_bvh.rs` from Lean sources. The invariant
is that every algorithmic claim about the emitted C must trace back to a
proof under [PredictiveBVH/](PredictiveBVH/). No hand-written C appears
in the header except through the paths documented below.

## Mental model

- **Lean 4 + Batteries, no Mathlib.** Tool choice is locked. Proofs use
  `omega`, `grind`, `decide`, and hand induction.
- **AmoLean EGraph** ([../AmoLean/](../AmoLean/)) is the arithmetic
  optimiser. It operates on `Expr Int` — integer-like ring polynomials
  only. Comparisons, ternaries, and booleans are not ring ops.
- **`bvh-codegen`** is the IO entry. `lake exe bvh-codegen` writes
  `predictive_bvh.h` (consumed by
  [`core/math/predictive_bvh_adapter.h`](../../core/math/predictive_bvh_adapter.h))
  and `predictive_bvh.rs` (consumed by downstream Rust clients).
- **Proof layout.** `PredictiveBVH/Spatial/` — tree ops + query proofs.
  `PredictiveBVH/Formulas/` — algebraic formulas (ghost bound, surface
  area, EML gaps). `PredictiveBVH/Protocol/` — fabric, interest,
  capacity. `PredictiveBVH/Codegen/` — C + Rust emission.

## The codegen pipeline

Polynomial scalar formulas flow through three steps, all in
[Codegen/CodeGen.lean](PredictiveBVH/Codegen/CodeGen.lean):

```
Expr Int  ──opt──▶  Expr Int (EGraph-saturated)
          ──toLowLevel──▶  LowLevelProgram
          ──generateCFn──▶  "static inline R128 fn(R128 ...) { ... }"
```

The public entry is `genC name params e`. Every polynomial emitted helper
(`ghost_bound`, `surface_area`, `pbvh_plane_corner_val`, the per-δ cost
functions, the quintic Hermite basis) goes through this pipeline.

Control-flow C (tree traversal, bucket directories, refit marking, query
descent) lives in [Codegen/TreeC.lean](PredictiveBVH/Codegen/TreeC.lean)
as raw string literals. That's where hand-written C is permitted — but
**no ring arithmetic** should appear inline in those strings.

### Codegen discipline

- **Ring arithmetic in TreeC.lean ⇒ bug.** If you find yourself writing
  `r128_add` / `r128_mul` / `r128_sub` inline in a string literal, stop:
  extract the polynomial as an `Expr Int`, emit via `genC`, and call
  the helper.
- **Comparisons and branches — ring-lift paths, current state.**
  `r128_le`, ternaries, and `if` branches are not ring ops over plain
  `Expr Int`, but AmoLean provides the machinery to ring-lift them:
  - **Z ↔ GF(2) bridge** (shipped). Sign-bit extraction turns a
    comparison into a ring operation over GF(2)ⁿ; min / max / contains /
    overlap all become branchless ring polynomials with a witness
    sign bit. Already used by the Rust emission for `aabb_union_ring`,
    `aabb_contains_ring`, `aabb_overlaps_ring` — see
    [Codegen/CodeGen.lean:280](PredictiveBVH/Codegen/CodeGen.lean#L280)
    onward. The C-side `aabb_union` / `aabb_contains` / `aabb_overlaps`
    in [`aabbC`](PredictiveBVH/Codegen/CodeGen.lean) still use the
    hand-written `r128_le ? a : b` ternary form; migrating them to the
    bridge is on the roadmap.
  - **Blend pattern** (shipped). `cond ? new : old = old + flag * (new - old)`
    once `flag ∈ {0, 1}`. See `bvh_blend` at
    [Codegen/CodeGen.lean:134](PredictiveBVH/Codegen/CodeGen.lean#L134).
    Composes cleanly with the bridge's sign-bit flag.
- **Boilerplate C functions** that aren't templated (one-off glue,
  struct constructors, etc.) go in
  [`core/math/predictive_bvh_adapter.h`](../../core/math/predictive_bvh_adapter.h),
  not in the emitted header.

## Build + verification

```bash
# Lean proofs — must be green, zero sorry, zero axiom
cd thirdparty/predictive_bvh
lake build                   # expect: 313 jobs green

# Regenerate emitted files
lake exe bvh-codegen         # writes predictive_bvh.h + predictive_bvh.rs

# Back to repo root — regression gate
cd ../..
bin/godot.macos.editor.dev.arm64 --headless --test \
  --test-case="*FabricZone*,*PredictiveBVH*"
# expect: 32 cases green, 4347+ assertions

# Stress / soundness (truth comparison at N ∈ {4k, 16k, 65k})
bin/godot.macos.editor.dev.arm64 --headless --test \
  --test-case="*stress*"
# expect: truth=pbvh at every N

# Per-frame perf vs DynamicBVH
bin/godot.macos.editor.dev.arm64 --headless --test \
  --test-case="*per-frame*"
```

## Current invariants

- **Zero `sorry`** under `PredictiveBVH/`.
- **Zero `axiom`** under `PredictiveBVH/`.
- **313 Lean jobs green** from `lake build`.
- **Regression gate** — 32 cases / 4347+ assertions green.
- **Bucket bound** — `bmax ≤ 2 · PBVH_BUCKET_K_TARGET` at every built N,
  enforced by the bench via `CHECK_MESSAGE`. The average case is proved
  in [Spatial/BucketBound.lean](PredictiveBVH/Spatial/BucketBound.lean):
  `N ≤ K_target · 2^(bucketBitsFor N)`.

## Deferred items

Tracked in [todo.md](todo.md) and the root plan. Safe to defer because
production consumers re-test results via callback predicates, so
over-emission inside a proof gap is tolerated.

- **Phase 1c** — functionalise `insertionSortByHilbert` off `Id.run do`,
  then prove `sorted_is_ascending_after_build` and
  `aabbQueryB_agrees_with_aabbQueryN`.
- **Phase 2b'** — soundness / completeness for `rayQueryN` /
  `convexQueryN`.
- **Incremental-branch `tick_agrees_with_build`** — currently proved
  only for the `bucketBits = 0` fallback path. Mechanising the
  incremental branch needs a window-preservation companion to
  `resortBucket_preserves_structural`.

## Where invariants are proved — one-line index

| File | Invariant |
|---|---|
| [Primitives/Types.lean](PredictiveBVH/Primitives/Types.lean) | `PbvhLeaf` / `PbvhInternal` / `BoundingBox` + basic ops |
| [Spatial/Tree.lean](PredictiveBVH/Spatial/Tree.lean) | `build` / `aabbQueryN` soundness + completeness |
| [Spatial/RefitIncremental.lean](PredictiveBVH/Spatial/RefitIncremental.lean) | Incremental refit soundness (`refitFull_preserves_size`, `refitIncrementalSpec_allMarked_eq_refitFull`, `refitIncrementalSpec_establishes_cover`) |
| [Spatial/BucketBound.lean](PredictiveBVH/Spatial/BucketBound.lean) | `N ≤ K_target · 2^(bucketBitsFor N)` (average bucket bound) |
| [Spatial/HilbertBroadphase.lean](PredictiveBVH/Spatial/HilbertBroadphase.lean) | Hilbert-prefix broadphase correctness |
| [Spatial/HilbertRoundtrip.lean](PredictiveBVH/Spatial/HilbertRoundtrip.lean) | Forward / inverse Hilbert bijection |
| [Spatial/Partition.lean](PredictiveBVH/Spatial/Partition.lean) | AABB partition / union / containment |
| [Spatial/ScaleContradictions.lean](PredictiveBVH/Spatial/ScaleContradictions.lean) | Scale-proof contradictions |
| [Spatial/EMLAdversarialHeuristic.lean](PredictiveBVH/Spatial/EMLAdversarialHeuristic.lean) | EML adversarial gap bounds |
| [Formulas/Formula.lean](PredictiveBVH/Formulas/Formula.lean) | `ghostBoundExpr`, `surfaceAreaExpr`, `predictiveCostFormula` |
| [Formulas/LowerBound.lean](PredictiveBVH/Formulas/LowerBound.lean) | SAH lower bound |
| [Formulas/ScaleProofs.lean](PredictiveBVH/Formulas/ScaleProofs.lean) | Scale-proof capacity bounds |
| [Protocol/Build.lean](PredictiveBVH/Protocol/Build.lean) | Tree build protocol + Hilbert sort |
| [Protocol/Saturate.lean](PredictiveBVH/Protocol/Saturate.lean) | EGraph saturation on BVH formulas |
| [Protocol/Fabric.lean](PredictiveBVH/Protocol/Fabric.lean) | Multi-zone migration state |
| [Protocol/WaypointBound.lean](PredictiveBVH/Protocol/WaypointBound.lean) | Waypoint distance bound |
| [Protocol/AbyssalSLA.lean](PredictiveBVH/Protocol/AbyssalSLA.lean) | Abyssal-tier SLA bounds |
| [Interest/AuthorityInterest.lean](PredictiveBVH/Interest/AuthorityInterest.lean) | `InterestReplica`, authority zone |
| [Codegen/CodeGen.lean](PredictiveBVH/Codegen/CodeGen.lean) | `genC` / `genRs` pipeline + header assembly |
| [Codegen/TreeC.lean](PredictiveBVH/Codegen/TreeC.lean) | Tree-op C (control-flow only) |
| [Codegen/QuinticHermite.lean](PredictiveBVH/Codegen/QuinticHermite.lean) | C³ quintic Hermite basis |

## How to add a new formula

1. State it as `Expr Int` in the relevant `Formulas/*.lean`.
2. Prove any required lemmas (non-negativity, monotonicity, whatever the
   caller relies on).
3. Add `private def myFormulaC : String := genC "my_formula" ["arg0", ...] myFormulaExpr`
   in `Codegen/CodeGen.lean` and wire it into `cFile`.
4. `lake build && lake exe bvh-codegen` — inspect the emitted C to
   confirm EGraph gave you the CSE you expected.
5. Call it from the adapter or from `Codegen/TreeC.lean` as needed.

## How to add a new tree op

1. State it in `Spatial/Tree.lean` as a pure function on `PbvhTree`.
2. Prove the invariants it preserves (structural, cover, skip-pointer,
   etc.) — reuse lemmas from the index above.
3. Emit the C body as a string in `Codegen/TreeC.lean`, calling existing
   EGraph-emitted helpers for any ring arithmetic.
4. Regenerate, add a doctest under
   [`tests/scene/test_predictive_bvh_*.cpp`](../../tests/scene/), run
   the regression gate.

## Design philosophy

We sequence risk here so the production module stays light. Verification
mass lives in this directory; production logic is exported as a single
verified C header.

## Documentation style: Hz, seconds, metres

All human-facing prose in `README.md`, `OptimalPartitionBook.md`,
`CONCEPT*.md`, and any other reader-facing doc must use Hz, seconds,
and metres as public units. Internal integer encodings (μm, μm/tick) are
an implementation detail of the exact-arithmetic core — they belong in
code, and at most in a single Units note that explains why the encoding
exists.

The word "tick" is forbidden in human-facing prose. It survives only in:

1. Wire-format field names literally named `server_tick` / `player_tick`
   in the protocol.
2. Code identifiers and symbols (`pbvh_latency_ticks`, `simTickHz`,
   `currentFunnelPeakVUmTick`) when the text is referring to the symbol
   itself.
3. One sentence in the Units section of the README that explains the
   μm/tick internal encoding.

Everywhere else:

- Durations in seconds / ms (e.g. "4 s migration hysteresis", "100 ms
  latency floor"), not ticks.
- Rates in Hz (e.g. "20 Hz default simulation rate"), not "per tick".
- Distances in metres (e.g. "5 m interest radius"), not μm or mm.
- Velocities in m/s (e.g. "10 m/s velocity cap"), not μm/tick or
  mm/tick.
- Accelerations in m/s².

If you must reference a tick-rate-dependent quantity, express it
parametrically (`pbvh_latency_ticks(hz)`) and give the physical meaning
(100 ms) alongside. The `_DEFAULT` convenience constants exist only so
wire-format scales can be compile-time values; at runtime everything
reads the live rate from `Engine::get_physics_ticks_per_second()`.

Rationale: the simulation tick rate is a retargetable implementation
choice. Public docs should read the same whether we run at 20 Hz, 64 Hz,
or 120 Hz. Writing "192 ticks" or "156 250 μm/tick" freezes the doc to a
specific rate and silently rots when the rate changes — which is exactly
what happened during the 64 Hz → 20 Hz refactor.

# AGENTS.md — multiplayer-fabric-predictive-bvh

Guidance for AI coding agents working in this submodule.

## What this is

Holds the Lean proofs that physics, geometry, and algorithm code in the
rest of the multiplayer-fabric stack must agree with. Contains formal
Lean 4 proofs and a code-generation pipeline that writes
`predictive_bvh.h` (C header consumed by the mmog module).

Do not implement algorithms elsewhere that contradict a proof here. If
an implementation differs from the Lean proof, trust the proof.

## Build

```sh
lake build            # checks all proofs, runs the bvh-codegen executable
```

The `bvh-codegen` executable writes `predictive_bvh.h` to
`thirdparty/predictive_bvh/predictive_bvh.h`. Commit the generated header
after running codegen.

Lean toolchain version is pinned in `lean-toolchain`.

## Key files

| Path | Purpose |
|------|---------|
| `lakefile.lean` | Lake build config; `bvh-codegen` default target |
| `lean-toolchain` | Pinned Lean 4 version |
| `PredictiveBVH.lean` | Root import aggregating all proof modules |
| `PredictiveBVH/` | Proof modules (Spatial, Formulas, Protocol, Relativistic, Codegen, Interest) |
| `PredictiveBVH/Codegen/CodeGen.lean` | Code-generation entrypoint → `predictive_bvh.h` |
| `predictive_bvh.h` | Generated C header (committed, not hand-edited) |

## What is proved

- O(1) per-entity interest management via Hilbert curve broadphase
- BVH construction and refit correctness
- ReBAC authorization bounds
- Adversarial physics geometric stability
- Rate-distortion / bandwidth tradeoffs for MMO scaling
- Relativistic zone theory (no global authority)

## Conventions

- New algorithms go here first as Lean proofs, then get ported to Elixir/C++.
- Cross-reference scan must include `.h`, `.c`, `.cpp` — the generated C header
  is the bridge to production code.
- Commit message style: sentence case, no `type(scope):` prefix.
  Example: `Prove O(1) refit bound for incremental BVH updates`

# AGENTS.md — multiplayer-fabric-predictive-bvh-research

Guidance for AI coding agents working in this repo.

## What this is

Aspirational / research-tier Lean proofs split out of
[multiplayer-fabric-predictive-bvh](https://github.com/V-Sekai-fire/multiplayer-fabric-predictive-bvh).
None of these modules are in the production codegen import closure. The
production repo's `bvh-codegen` exe writes `predictive_bvh.h` without
touching anything here.

The modules cover query soundness on the abstract BVH, zone migration
protocol, and ReBAC authorization. They are currently broken under Lean
4.26 and are tracked for incremental repair.

## Build

```sh
lake update
lake build
```

`lake build` is expected to fail. Repair work targets one module at a
time; use `lake build PredictiveBVH.<Module>` to iterate on a specific
research-tier module (they now share the `PredictiveBVH.*` namespace and
file tree), or `lake build PredictiveBVHResearch` to build the full
research-tier aggregator.

## Lake dependencies

- `optimal-partition` — the upstream production repo. Provides
  `PredictiveBVH.Primitives.Types`, `PredictiveBVH.Formulas.*`,
  `PredictiveBVH.Spatial.HilbertBroadphase`,
  `PredictiveBVH.Relativistic.NoGod`, etc. These are imported under
  their original `PredictiveBVH.*` paths.

## Conventions

- Research-tier modules live under `PredictiveBVH/` alongside production
  files. The `PredictiveBVHResearch.lean` aggregator at repo root pins
  the research-tier import closure for `lake build PredictiveBVHResearch`.
- Internal Lean `namespace PredictiveBVH …` declarations are unchanged.
- Cross-references between research-tier files use `PredictiveBVH.*`
  imports (same as production).
- Commit message style: sentence case, no `type(scope):` prefix.

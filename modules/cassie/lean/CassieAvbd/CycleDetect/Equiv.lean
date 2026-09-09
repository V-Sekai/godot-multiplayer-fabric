import CassieAvbd.CycleDetect.Vec
import CassieAvbd.CycleDetect.Graph
import CassieAvbd.CycleDetect.Arrangement
import CassieAvbd.CycleDetect.Walk
import CassieAvbd.CycleDetect.Fixtures

/-!
# `CassieAvbd.CycleDetect.Equiv` — equivalence pins

Lean-side ground-truth assertions for the cycle-detection kernels,
pinned by `native_decide`. Any drift in the Lean spec OR in the C++
runtime (as soon as the Slang-emit step lands) trips at lake-build
time. Mirrors the pattern from `CassieAvbd.PolarDecomp`.

Each `example` pins one concrete (input → output) for a kernel; they
collectively are the "expected" table the runtime is compared against.
-/

namespace CassieAvbd.CycleDetect.Equiv

open CassieAvbd.CycleDetect
open CassieAvbd.CycleDetect.Fixtures

-- ── Vec3 ops ───────────────────────────────────────────────────────────────
-- Float `=` isn't Decidable, so pin via `==` (which returns Bool) wrapped
-- in a `= true` so we can still drive it through `native_decide`.

example : (dot (1.0, 0.0, 0.0) (1.0, 0.0, 0.0) == 1.0) = true := by native_decide
example : ((cross (1.0, 0.0, 0.0) (0.0, 1.0, 0.0)).x == 0.0 ∧
            (cross (1.0, 0.0, 0.0) (0.0, 1.0, 0.0)).y == 0.0 ∧
            (cross (1.0, 0.0, 0.0) (0.0, 1.0, 0.0)).z == 1.0) := by
  refine ⟨?_, ?_, ?_⟩ <;> native_decide
example : (vdist (0.0, 0.0, 0.0) (3.0, 4.0, 0.0) == 5.0) = true := by native_decide

-- ── Graph helpers on the triangle ──────────────────────────────────────────

example : (nodeEdges triangleGraph 0).size = 2                := by native_decide
example : (nodeEdges triangleGraph 1).size = 2                := by native_decide
example : (nodeEdges triangleGraph 2).size = 2                := by native_decide

example : opposite triangleGraph.edges[0]! 0 = 1              := by native_decide
example : opposite triangleGraph.edges[1]! 1 = 2              := by native_decide
example : opposite triangleGraph.edges[2]! 2 = 0              := by native_decide

-- ── Cycle walk on the triangle ─────────────────────────────────────────────
-- Walking from edge 0 starting at node 0, CCW, in the Y-up plane,
-- yields edge 1 at the next step (B → C). Pinned so any future
-- algorithm refactor that breaks this trips immediately.

example : nextEdgeAt triangleGraph 1 0 (0.0, 1.0, 0.0) true = some 1 := by native_decide
example : nextEdgeAt triangleGraph 2 1 (0.0, 1.0, 0.0) true = some 2 := by native_decide
example : nextEdgeAt triangleGraph 0 2 (0.0, 1.0, 0.0) true = some 0 := by native_decide

-- Triangle has exactly one minimal-face cycle.
example : (findCycles triangleGraph).size = 1                 := by native_decide

-- ── Cycle walk on the 2×2 grid ─────────────────────────────────────────────
-- Euler V−E+F = 2 with V=9, E=12 gives F=5 (4 inner quads + outer face).

example : gridGraph2x2.nodes.size = 9                         := by native_decide
example : gridGraph2x2.edges.size = 12                        := by native_decide
example : (findCycles gridGraph2x2).size = 5                  := by native_decide

-- ── Arrangement build round-trip ───────────────────────────────────────────
-- Three line segments that share endpoints exactly — buildArrangement
-- recovers a 3-node, 3-edge graph with one cycle (the triangle face).

private def triPolylines : Array (Array Vec3) := #[
  #[(0.0, 0.0, 0.0), (1.0, 0.0, 0.0)],
  #[(1.0, 0.0, 0.0), (0.5, 0.0, 1.0)],
  #[(0.5, 0.0, 1.0), (0.0, 0.0, 0.0)] ]

example : (buildArrangement triPolylines #[] 0.001 0.001 1).nodes.size = 3 := by native_decide
example : (buildArrangement triPolylines #[] 0.001 0.001 1).edges.size = 3 := by native_decide
example : (findCycles (buildArrangement triPolylines #[] 0.001 0.001 1)).size = 1 := by
  native_decide

-- ── Segment-segment closest-pair (numerical) ───────────────────────────────
-- Two perpendicular line segments crossing at origin: closest pair is
-- (origin, origin), dist² = 0.

example : ((segSegClosest (-1.0, 0.0, 0.0) (1.0, 0.0, 0.0)
                          (0.0, -1.0, 0.0) (0.0, 1.0, 0.0)).dist2 == 0.0) = true := by
  native_decide

end CassieAvbd.CycleDetect.Equiv

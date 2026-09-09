import CassieAvbd.CycleDetect.Vec
import CassieAvbd.CycleDetect.Graph
import CassieAvbd.CycleDetect.Arrangement
import CassieAvbd.CycleDetect.Walk
import CassieAvbd.CycleDetect.Fixtures

/-!
# `CassieAvbd.CycleDetect` — planar arrangement face traversal

Re-export module + integration smoke tests. The actual kernels live in
the submodules:

  - `CycleDetect.Vec`         — 3D vector ops
  - `CycleDetect.Graph`       — Edge / Graph data types + tangent / nodeEdges
  - `CycleDetect.Arrangement` — buildArrangement (planar arrangement build
                                from polyline list)
  - `CycleDetect.Walk`        — nextEdgeAt (paper §4.4 angular pick) +
                                findCycles (face traversal)
  - `CycleDetect.Fixtures`    — small test graphs (triangle, 2×2 grid)

Iteration loop is `lake env lean CassieAvbd/CycleDetect.lean` — ~2s
type-check + #eval round-trip. Once the algorithm stabilises here on
the in-tree fixtures, the kernels port to a `SlangShaderModule` for
the runtime path (see CurveRdp.lean for the pattern). The
parallel-transport step (future work) will reuse this project's polar
decomposition (`CassieAvbd.PolarDecomp` / `cassie_polar.cpp`) for the
3x3 → rotation solve — NOT QCP, see the memory entry on
many_bone_ik 7-point star + polar at the top of MEMORY.md.
-/

open CassieAvbd.CycleDetect
open CassieAvbd.CycleDetect.Fixtures

-- Triangle smoke
#eval triangleGraph.nodes.size                                    -- expect 3
#eval triangleGraph.edges.size                                    -- expect 3
#eval nextEdgeAt triangleGraph 1 0 (0.0, 1.0, 0.0) true            -- expect some 1
#eval (findCycles triangleGraph).size                             -- expect 1

-- 2×2 grid: hand-built. Euler V-E+F = 2 with V=9, E=12 gives F=5
-- (4 inner quads + 1 outer face) — we find all 5.
#eval gridGraph2x2.nodes.size                                     -- expect 9
#eval gridGraph2x2.edges.size                                     -- expect 12
#eval (findCycles gridGraph2x2).size                              -- expect 5

-- buildArrangement: feed three lines that form a triangle by sharing
-- endpoints exactly — no actual crossings to detect — and confirm we
-- recover a 3-node, 3-edge graph
def triPolylines : Array (Array Vec3) := #[
  #[(0.0, 0.0, 0.0), (1.0, 0.0, 0.0)],
  #[(1.0, 0.0, 0.0), (0.5, 0.0, 1.0)],
  #[(0.5, 0.0, 1.0), (0.0, 0.0, 0.0)] ]

#eval (buildArrangement triPolylines #[] 0.001 0.001 1).nodes.size       -- expect 3
#eval (buildArrangement triPolylines #[] 0.001 0.001 1).edges.size       -- expect 3
#eval (findCycles (buildArrangement triPolylines #[] 0.001 0.001 1)).size -- expect 1

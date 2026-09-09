import CassieAvbd.CycleDetect.Vec
import CassieAvbd.CycleDetect.Graph

/-!
# `CassieAvbd.CycleDetect.Fixtures` — test inputs

Small graphs we can iterate against without dragging in the full
hat.json. The hat fixture will land as a separate `hat_strokes.lean`
generated from the dataset's `allSketchedStrokes[*].ctrlPts` once the
algorithm stabilises here on the triangles.
-/
namespace CassieAvbd.CycleDetect.Fixtures

open CassieAvbd.CycleDetect

/-- Triangle in the XZ plane. 3 nodes, 3 edges. -/
def triangleGraph : Graph :=
  { nodes := #[(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.5, 0.0, 1.0)]
  , edges := #[
      { pts := #[(0.0, 0.0, 0.0), (1.0, 0.0, 0.0)], na := 0, nb := 1, src := 0 },
      { pts := #[(1.0, 0.0, 0.0), (0.5, 0.0, 1.0)], na := 1, nb := 2, src := 1 },
      { pts := #[(0.5, 0.0, 1.0), (0.0, 0.0, 0.0)], na := 2, nb := 0, src := 2 } ] }

/-- 2×2 grid of quad faces, 4 cycles expected (4 inner squares).
    Nodes are a 3×3 lattice; each grid line is one stroke split into
    sub-edges by the crossings. We hand-pre-arrange this to skip the
    arrangement build for the cycle-walker unit test. -/
def gridGraph2x2 : Graph :=
  { nodes := #[
      -- y0 row
      (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (2.0, 0.0, 0.0),
      -- y1 row
      (0.0, 0.0, 1.0), (1.0, 0.0, 1.0), (2.0, 0.0, 1.0),
      -- y2 row
      (0.0, 0.0, 2.0), (1.0, 0.0, 2.0), (2.0, 0.0, 2.0) ]
  , edges := #[
      -- Horizontal (x-direction) sub-edges
      { pts := #[(0.0,0.0,0.0), (1.0,0.0,0.0)], na := 0, nb := 1, src := 0 },
      { pts := #[(1.0,0.0,0.0), (2.0,0.0,0.0)], na := 1, nb := 2, src := 0 },
      { pts := #[(0.0,0.0,1.0), (1.0,0.0,1.0)], na := 3, nb := 4, src := 1 },
      { pts := #[(1.0,0.0,1.0), (2.0,0.0,1.0)], na := 4, nb := 5, src := 1 },
      { pts := #[(0.0,0.0,2.0), (1.0,0.0,2.0)], na := 6, nb := 7, src := 2 },
      { pts := #[(1.0,0.0,2.0), (2.0,0.0,2.0)], na := 7, nb := 8, src := 2 },
      -- Vertical (z-direction) sub-edges
      { pts := #[(0.0,0.0,0.0), (0.0,0.0,1.0)], na := 0, nb := 3, src := 3 },
      { pts := #[(0.0,0.0,1.0), (0.0,0.0,2.0)], na := 3, nb := 6, src := 3 },
      { pts := #[(1.0,0.0,0.0), (1.0,0.0,1.0)], na := 1, nb := 4, src := 4 },
      { pts := #[(1.0,0.0,1.0), (1.0,0.0,2.0)], na := 4, nb := 7, src := 4 },
      { pts := #[(2.0,0.0,0.0), (2.0,0.0,1.0)], na := 2, nb := 5, src := 5 },
      { pts := #[(2.0,0.0,1.0), (2.0,0.0,2.0)], na := 5, nb := 8, src := 5 } ] }

end CassieAvbd.CycleDetect.Fixtures

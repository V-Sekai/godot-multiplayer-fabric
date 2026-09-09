import CassieAvbd.CycleDetect.Transport
import CassieAvbd.CycleDetect.NodeAugment
import CassieAvbd.CycleDetect.Walk

/-!
# `CassieAvbd.CycleDetect.WalkFixtures` — pinned regression checks

Hand-derived inputs + expected outputs for the leaf kernels Phase B.0
ported from Unity (`Transport.lean`, `NodeAugment.lean`, `Walk.lean`).
Pinned by `native_decide` where the result type is `Bool`; the
`Vec3`-returning kernels use a fixed-tolerance `vecAlmostEq` helper so
the equality check is still decidable.

Each fixture's `expected` value is documented with the derivation
(closed-form math or Unity-source semantics). When we later pull real
Unity dumps via the MCP — `Graph.GetGraphData()` on the hat session
plus targeted `Debug.Log` traces of `CycleDetection` — those numerical
fixtures land here too, replacing the synthetic seeds where the Unity
data is more representative of the actual algorithm path.
-/
namespace CassieAvbd.CycleDetect

/-- Approximate Vec3 equality at a fixed tolerance. `1e-6` matches the
    plan's positional tolerance band; for unit-normal comparisons it's
    well inside the f32→f64 round-trip error. -/
@[inline] def vecAlmostEq (a b : Vec3) : Bool :=
  let dx := a.1 - b.1
  let dy := a.2.1 - b.2.1
  let dz := a.2.2 - b.2.2
  dx * dx + dy * dy + dz * dz < 1.0e-12

/-- `shouldReverse(prev, next) = dot prev next < 0.5`. Mirror of
    `Walk.shouldReverse` — Unity's threshold is the literal 0.5
    (= cos 60°) from `CycleDetection.ShouldReverse`. -/
def shouldReverseFx : Array (Vec3 × Vec3 × Bool) := #[
  -- Parallel normals: dot = 1 > 0.5 → do NOT reverse.
  ((1.0, 0.0, 0.0), (1.0, 0.0, 0.0), false),
  -- Anti-parallel: dot = -1 < 0.5 → reverse.
  ((1.0, 0.0, 0.0), (-1.0, 0.0, 0.0), true),
  -- 60° apart: dot = 0.5 — boundary case, NOT < 0.5 → do not reverse.
  ((1.0, 0.0, 0.0), (0.5, 0.86602540378, 0.0), false),
  -- 61° apart: dot ≈ 0.4848 < 0.5 → reverse.
  ((1.0, 0.0, 0.0), (0.484809620, 0.874619707, 0.0), true),
  -- 89° apart: dot ≈ 0.0175 < 0.5 → reverse.
  ((1.0, 0.0, 0.0), (0.017452406, 0.999847695, 0.0), true)
]

/-- Every `shouldReverseFx` entry matches `Walk.shouldReverse` on the
    same input. Pinned with `native_decide` (Bool result is
    decidable). -/
theorem shouldReverse_matches : ∀ i : Fin shouldReverseFx.size,
    Walk.shouldReverse shouldReverseFx[i].1 shouldReverseFx[i].2.1 =
    shouldReverseFx[i].2.2 := by
  intro i
  native_decide

/-- Identity transport: `tPrev == tNext` ⇒ the normal is unchanged.
    Closed-form: Rodrigues rotation with zero axis-angle. -/
def transportAcrossNode_identityFx : Array (Vec3 × Vec3 × Vec3 × Vec3) := #[
  -- (tPrev, tNext, n_in, n_out)
  ((1.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 1.0, 0.0)),
  ((0.0, 1.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0, 1.0)),
  ((0.0, 0.0, 1.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0), (1.0, 0.0, 0.0))
]

/-- `transportAcrossNode` is the identity when `tPrev == tNext`. -/
theorem transportAcrossNode_identity :
    ∀ i : Fin transportAcrossNode_identityFx.size,
    vecAlmostEq
      (transportAcrossNode
        transportAcrossNode_identityFx[i].1
        transportAcrossNode_identityFx[i].2.1
        transportAcrossNode_identityFx[i].2.2.1)
      transportAcrossNode_identityFx[i].2.2.2 := by
  intro i
  native_decide

/-- Transport whose axis IS the normal: normal stays on the rotation
    axis, so it's preserved. Specifically `tPrev = +X`, `tNext = +Y`
    rotates around `+Z`; applying that to `n = (0, 0, 1)` keeps it. -/
def transportAcrossNode_axisFx : Array (Vec3 × Vec3 × Vec3 × Vec3) := #[
  ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0, 1.0)),
  ((0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0), (1.0, 0.0, 0.0)),
  ((0.0, 0.0, 1.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 1.0, 0.0))
]

theorem transportAcrossNode_axisInvariant :
    ∀ i : Fin transportAcrossNode_axisFx.size,
    vecAlmostEq
      (transportAcrossNode
        transportAcrossNode_axisFx[i].1
        transportAcrossNode_axisFx[i].2.1
        transportAcrossNode_axisFx[i].2.2.1)
      transportAcrossNode_axisFx[i].2.2.2 := by
  intro i
  native_decide

/-- Parallel transport along a straight Bezier preserves the input
    vector (Bishop frame on a straight line has constant tangent). -/
def parallelTransport_straightFx : Array (Vec3 × Vec3 × Vec3 × Vec3 × Vec3) := #[
  -- (p0, p1, p2, p3, v) — control polygon collinear along +X.
  ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (2.0, 0.0, 0.0), (3.0, 0.0, 0.0),
   (0.0, 1.0, 0.0)),
  ((0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 2.0, 0.0), (0.0, 3.0, 0.0),
   (1.0, 0.0, 0.0)),
  ((0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0, 2.0), (0.0, 0.0, 3.0),
   (0.5, 0.5, 0.0))
]

theorem parallelTransport_straightPreservesInput :
    ∀ i : Fin parallelTransport_straightFx.size,
    vecAlmostEq
      (parallelTransport
        parallelTransport_straightFx[i].1
        parallelTransport_straightFx[i].2.1
        parallelTransport_straightFx[i].2.2.1
        parallelTransport_straightFx[i].2.2.2.1
        parallelTransport_straightFx[i].2.2.2.2 0.0 1.0)
      parallelTransport_straightFx[i].2.2.2.2 := by
  intro i
  native_decide

end CassieAvbd.CycleDetect

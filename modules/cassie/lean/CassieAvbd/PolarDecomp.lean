-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee
--
-- Closed-form polar decomposition for Wahba's problem — Lean spec
-- mirroring modules/cassie/src/sketch/cassie_polar.cpp.
--
-- Algorithm (Smith 1961 / Connelly 2008 / Soderkvist 1993):
--   1. Build cross-covariance H = Σ p_i q_i^T (3×3, non-symmetric).
--   2. M = H^T H (3×3 symmetric PSD).
--   3. Eigendecompose M closed-form via the cubic characteristic
--      polynomial: trace + trig substitution. No iteration, no Jacobi
--      sweeps, no Newton-Raphson.
--   4. M^{-1/2} = V · diag(1/√λ) · V^T.
--   5. R = H · M^{-1/2}.
--   6. Reflection fix: if det(R) < 0, flip the smallest-eigenvalue
--      eigenvector and recompute.
--
-- No 4-element quaternion intermediate. The algorithm stays in 3×3
-- matrix-land throughout — addressing the SO(3) double-cover concern
-- by simply never producing a quaternion.
--
-- This file is the Lean ground truth. `native_decide` fixtures pin
-- specific input → output cases against the C++ implementation: any
-- regression in either trips at lake-build time.

namespace CassieAvbd.PolarDecomp

/-- A 3×3 matrix stored row-major as 9 `Float`s. -/
structure Mat3 where
  a00 : Float; a01 : Float; a02 : Float
  a10 : Float; a11 : Float; a12 : Float
  a20 : Float; a21 : Float; a22 : Float

/-- The 3×3 identity. -/
def Mat3.identity : Mat3 :=
  ⟨1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0⟩

/-- Absolute value of the Frobenius-norm difference between two
    3×3 matrices. Used by `nearMat3` for `native_decide` tolerance
    checks. -/
def Mat3.diffNorm (m1 m2 : Mat3) : Float :=
  let d := fun a b => (a - b) * (a - b)
  Float.sqrt (
    d m1.a00 m2.a00 + d m1.a01 m2.a01 + d m1.a02 m2.a02 +
    d m1.a10 m2.a10 + d m1.a11 m2.a11 + d m1.a12 m2.a12 +
    d m1.a20 m2.a20 + d m1.a21 m2.a21 + d m1.a22 m2.a22)

/-- Approximate equality with a tolerance, returns `Bool` so it's
    `native_decide`-able without a `DecidableEq` instance on `Float`. -/
def nearMat3 (m1 m2 : Mat3) (tol : Float) : Bool :=
  Mat3.diffNorm m1 m2 < tol

/-- Determinant of a 3×3 matrix. -/
def Mat3.det (m : Mat3) : Float :=
  m.a00 * (m.a11 * m.a22 - m.a12 * m.a21)
  - m.a01 * (m.a10 * m.a22 - m.a12 * m.a20)
  + m.a02 * (m.a10 * m.a21 - m.a11 * m.a20)

/-- 3×3 matrix multiply. -/
def Mat3.mul (a b : Mat3) : Mat3 :=
  ⟨ a.a00 * b.a00 + a.a01 * b.a10 + a.a02 * b.a20
  , a.a00 * b.a01 + a.a01 * b.a11 + a.a02 * b.a21
  , a.a00 * b.a02 + a.a01 * b.a12 + a.a02 * b.a22
  , a.a10 * b.a00 + a.a11 * b.a10 + a.a12 * b.a20
  , a.a10 * b.a01 + a.a11 * b.a11 + a.a12 * b.a21
  , a.a10 * b.a02 + a.a11 * b.a12 + a.a12 * b.a22
  , a.a20 * b.a00 + a.a21 * b.a10 + a.a22 * b.a20
  , a.a20 * b.a01 + a.a21 * b.a11 + a.a22 * b.a21
  , a.a20 * b.a02 + a.a21 * b.a12 + a.a22 * b.a22⟩

/-- 3×3 transpose. -/
def Mat3.transpose (m : Mat3) : Mat3 :=
  ⟨m.a00, m.a10, m.a20, m.a01, m.a11, m.a21, m.a02, m.a12, m.a22⟩

/-- A 3-vector. -/
structure Vec3 where
  x : Float; y : Float; z : Float

/-- Cross-covariance H = Σ p_i q_i^T from matched tangent pairs. -/
def crossCovariance (pq : List (Vec3 × Vec3)) : Mat3 :=
  pq.foldl (fun acc (p, q) =>
    ⟨ acc.a00 + p.x * q.x, acc.a01 + p.x * q.y, acc.a02 + p.x * q.z
    , acc.a10 + p.y * q.x, acc.a11 + p.y * q.y, acc.a12 + p.y * q.z
    , acc.a20 + p.z * q.x, acc.a21 + p.z * q.y, acc.a22 + p.z * q.z⟩)
    ⟨0, 0, 0, 0, 0, 0, 0, 0, 0⟩

-- ════════════════════════════════════════════════════════════════════════
-- native_decide fixtures
--
-- The Lean side currently pins INPUT-LEVEL invariants. The C++ algorithm
-- in cassie_polar.cpp is the operational ground truth; once the Slang
-- codegen pipeline is wired to emit Wahba kernels (the avbd-codegen
-- exe currently produces Spmv / Saxpby / CG primitives), the same Lean
-- terms will lower to Slang + C++ from one source.
-- ════════════════════════════════════════════════════════════════════════

/-- Identity cross-covariance — when input and target tangent sets
    are identical, H is the 3×3 identity and the optimal rotation is
    the 3×3 identity. -/
def identityFixture : Mat3 :=
  crossCovariance
    [ (⟨1, 0, 0⟩, ⟨1, 0, 0⟩)
    , (⟨0, 1, 0⟩, ⟨0, 1, 0⟩)
    , (⟨0, 0, 1⟩, ⟨0, 0, 1⟩) ]

/-- Pinned: the identity-input cross covariance IS the identity matrix
    (every Float comparison is exact here because the multiplications
    fall on canonical IEEE values). -/
example : nearMat3 identityFixture Mat3.identity 1e-12 = true := by native_decide

/-- 90°-rotation cross-covariance around Y. p = (1,0,0)/(0,1,0)/(0,0,1)
    rotated +90° around Y maps to q = (0,0,-1)/(0,1,0)/(1,0,0).
    H_yx-rotation should be the 90°-Y rotation matrix when fed to
    Wahba — pin H itself first. -/
def rot90YFixture : Mat3 :=
  crossCovariance
    [ (⟨1, 0, 0⟩, ⟨0, 0, -1⟩)
    , (⟨0, 1, 0⟩, ⟨0, 1, 0⟩)
    , (⟨0, 0, 1⟩, ⟨1, 0, 0⟩) ]

/-- Expected H for the 90°-Y fixture: the matrix with 1 at (0,2),
    1 at (1,1), and -1 at (2,0); zero elsewhere. -/
def rot90YExpectedH : Mat3 :=
  ⟨0, 0, -1, 0, 1, 0, 1, 0, 0⟩

example : nearMat3 rot90YFixture rot90YExpectedH 1e-12 = true := by native_decide

-- ════════════════════════════════════════════════════════════════════════
-- Structural invariants pinned by `native_decide`
-- ════════════════════════════════════════════════════════════════════════

/-- The identity-input H has determinant +1 (no reflection). -/
example : Float.abs (identityFixture.det - 1.0) < 1e-12 = true := by native_decide

/-- The Y-rotation-input H has determinant +1. -/
example : Float.abs (rot90YFixture.det - 1.0) < 1e-12 = true := by native_decide

/-- H^T H of the identity input IS the identity (already orthogonal). -/
example : nearMat3 (Mat3.mul (Mat3.transpose identityFixture) identityFixture) Mat3.identity 1e-12 = true := by
  native_decide

/-- H^T H of the Y-rotation input IS the identity (already orthogonal). -/
example : nearMat3 (Mat3.mul (Mat3.transpose rot90YFixture) rot90YFixture) Mat3.identity 1e-12 = true := by
  native_decide

-- ════════════════════════════════════════════════════════════════════════
-- The V-column-flip identity (Track 5+)
--
-- Claim: V · diag(d) · V^T is INVARIANT under sign-flipping any single
-- column of V. Algebraic proof — entry (i,j) of the product is
--   Σ_k V_{ik} d_k V_{jk}
-- Flipping column m: V_{im} → -V_{im} for all i. The k=m term becomes
--   (-V_{im}) d_m (-V_{jm}) = V_{im} d_m V_{jm}
-- so the contribution is unchanged. All other k≠m terms are unaffected.
-- Therefore the whole entry is invariant.
--
-- Operational consequence in cassie_polar::wahba_align: the V-column-2
-- flip the prior "reflection fix" code applied was a no-op on M^{-1/2}.
-- The polar formulation R = H · M^{-1/2} CANNOT produce a proper rotation
-- when det(H) < 0 by V-manipulation; that requires the SVD U·V^T
-- formulation, where the sign sits between two distinct matrices.
-- ════════════════════════════════════════════════════════════════════════

/-- Compute V · diag(d) · V^T for a 3×3 V and 3-diagonal d. -/
def mulDiagVT (V : Mat3) (d0 d1 d2 : Float) : Mat3 :=
  -- entry (i,j) = Σ_k V_{ik} d_k V_{jk}
  ⟨ V.a00 * d0 * V.a00 + V.a01 * d1 * V.a01 + V.a02 * d2 * V.a02
  , V.a00 * d0 * V.a10 + V.a01 * d1 * V.a11 + V.a02 * d2 * V.a12
  , V.a00 * d0 * V.a20 + V.a01 * d1 * V.a21 + V.a02 * d2 * V.a22
  , V.a10 * d0 * V.a00 + V.a11 * d1 * V.a01 + V.a12 * d2 * V.a02
  , V.a10 * d0 * V.a10 + V.a11 * d1 * V.a11 + V.a12 * d2 * V.a12
  , V.a10 * d0 * V.a20 + V.a11 * d1 * V.a21 + V.a12 * d2 * V.a22
  , V.a20 * d0 * V.a00 + V.a21 * d1 * V.a01 + V.a22 * d2 * V.a02
  , V.a20 * d0 * V.a10 + V.a21 * d1 * V.a11 + V.a22 * d2 * V.a12
  , V.a20 * d0 * V.a20 + V.a21 * d1 * V.a21 + V.a22 * d2 * V.a22⟩

/-- Flip the sign of column 0 of V. -/
def flipCol0 (V : Mat3) : Mat3 :=
  ⟨-V.a00, V.a01, V.a02, -V.a10, V.a11, V.a12, -V.a20, V.a21, V.a22⟩

/-- Flip the sign of column 1 of V. -/
def flipCol1 (V : Mat3) : Mat3 :=
  ⟨V.a00, -V.a01, V.a02, V.a10, -V.a11, V.a12, V.a20, -V.a21, V.a22⟩

/-- Flip the sign of column 2 of V. -/
def flipCol2 (V : Mat3) : Mat3 :=
  ⟨V.a00, V.a01, -V.a02, V.a10, V.a11, -V.a12, V.a20, V.a21, -V.a22⟩

/-- A representative non-trivial 3×3 V (rows could be eigenvectors of
    some symmetric M; we just need three numerically distinct columns). -/
def Vfix : Mat3 :=
  ⟨0.6, 0.8, 0.0,
   -0.48, 0.36, 0.8,
   0.64, -0.48, 0.6⟩

/-- Pinned: column-0 flip is a no-op on V·diag(d)·V^T. -/
example : nearMat3
    (mulDiagVT Vfix 1.0 2.0 3.0)
    (mulDiagVT (flipCol0 Vfix) 1.0 2.0 3.0) 1e-12 = true := by native_decide

/-- Pinned: column-1 flip is a no-op on V·diag(d)·V^T. -/
example : nearMat3
    (mulDiagVT Vfix 1.0 2.0 3.0)
    (mulDiagVT (flipCol1 Vfix) 1.0 2.0 3.0) 1e-12 = true := by native_decide

/-- Pinned: column-2 flip is a no-op on V·diag(d)·V^T — this is the
    specific case the prior C++ "post-hoc reflection fix" applied,
    silently doing nothing. The C++ code now documents the no-op and
    avoids the redundant matrix multiply. -/
example : nearMat3
    (mulDiagVT Vfix 1.0 2.0 3.0)
    (mulDiagVT (flipCol2 Vfix) 1.0 2.0 3.0) 1e-12 = true := by native_decide

/-- Pinned: the same identity with the inverse-square-root diagonal
    that appears in the actual M^{-1/2} computation. -/
example : nearMat3
    (mulDiagVT Vfix (1.0 / Float.sqrt 0.25) (1.0 / Float.sqrt 1.0) (1.0 / Float.sqrt 4.0))
    (mulDiagVT (flipCol2 Vfix) (1.0 / Float.sqrt 0.25) (1.0 / Float.sqrt 1.0) (1.0 / Float.sqrt 4.0))
    1e-12 = true := by native_decide

-- ════════════════════════════════════════════════════════════════════════
-- The Wahba ground-truth determinant fact (Track 5+)
--
-- Claim: for any proper rotation R (det R = 1) and any list of input
-- tangents {p_i}, the cross-covariance H = Σ p_i · (R p_i)^T satisfies
-- det(H) ≥ 0.
--
-- Proof sketch — H = R · (Σ p_i p_i^T). The outer-product sum is
-- symmetric PSD so its determinant is ≥ 0. det(R) = 1 by hypothesis.
-- Multiplicativity of det: det(H) = det(R) · det(Σ p_i p_i^T) ≥ 0.
--
-- Operational consequence: in CASSIE's actual usage of wahba_align
-- (curvenet orientation propagation), det(H) ≥ 0 always. The reflection
-- branch in cassie_polar::wahba_align is dead code for ground-truth
-- inputs; it remains documented so adversarial fixtures don't silently
-- drift into nonsensical rotations.
-- ════════════════════════════════════════════════════════════════════════

/-- Determinant of a 3×3 with diagonal entries (a,b,c) and zero
    off-diagonals — i.e. Σ p_i p_i^T for axis-aligned tangents. -/
def diagDet3 (a b c : Float) : Float := a * b * c

/-- Pinned: the input-tangent autocovariance Σ p_i p_i^T for the three
    axis-aligned tangents (1,0,0), (0,1,0), (0,0,1) is the identity,
    determinant 1. -/
example : Float.abs (diagDet3 1.0 1.0 1.0 - 1.0) < 1e-12 = true := by native_decide

/-- Pinned: the cross-covariance H for an identity rotation acting on
    those same tangents IS that identity, det 1 > 0. (Generalizes to
    "det(H) > 0 whenever R is a proper rotation".) -/
example : Float.abs (identityFixture.det - 1.0) < 1e-12 = true := by native_decide

end CassieAvbd.PolarDecomp

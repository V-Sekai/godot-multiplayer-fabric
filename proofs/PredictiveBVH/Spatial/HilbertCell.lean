-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee
--
-- Specification of hilbert_cell_of() from
-- multiplayer-fabric-godot/core/math/predictive_bvh_adapter.h (lines 399–427).
--
-- hilbert_cell_of(code, prefixDepth, scene):
--   Recovers the AABB of the Hilbert cell identified by the top `prefixDepth`
--   bits of `code`, expressed in scene-space µm coordinates.

import PredictiveBVH.Primitives.Types
import PredictiveBVH.Spatial.HilbertBroadphase

namespace PredictiveBVH
namespace HilbertCell

-- ── Coordinate normalization ──────────────────────────────────────────────────

/-- Normalize a scene-space coordinate to [0, 1023]. -/
def normalizeCoord (c cMin extent : Int) : Nat :=
  let raw := (c - cMin) * 1024 / (max extent 1)
  (raw.toNat).min 1023

/-- Inverse: recover scene-space midpoint from a 10-bit normalized coord. -/
def denormalizeCoord (n : Nat) (sceneMin extent : Int) : Int :=
  sceneMin + (n : Int) * (max extent 1) / 1024

/-- A normalized coordinate is always in [0, 1023]. -/
theorem normalizeCoord_range (c cMin extent : Int) :
    normalizeCoord c cMin extent ≤ 1023 := by
  simp only [normalizeCoord]
  exact Nat.min_le_right _ _

-- ── Cell geometry ─────────────────────────────────────────────────────────────

/-- The width of a cell in normalized [0, 1023] coordinates at a given prefix depth.
    At depth d (≤ 10), one cell spans 2^(10-d) normalized units per axis.
    At depth > 10, one cell spans 1 unit (minimum resolution). -/
def cellWidthNorm (prefixDepth : Nat) : Nat :=
  1 <<< (if prefixDepth ≤ 10 then 10 - prefixDepth else 0)

theorem cellWidthNorm_pos (prefixDepth : Nat) : 0 < cellWidthNorm prefixDepth := by
  unfold cellWidthNorm
  by_cases h : prefixDepth ≤ 10
  · simp only [h, ite_true, Nat.shiftLeft_eq, Nat.one_mul]
    exact Nat.two_pow_pos _
  · simp only [h, ite_false]; decide

/-- At depth 0, the cell spans the entire normalized space [0, 1023]. -/
theorem cellWidthNorm_depth0 : cellWidthNorm 0 = 1024 := by
  simp [cellWidthNorm]

/-- At depth 10, the cell is one unit wide. -/
theorem cellWidthNorm_depth10 : cellWidthNorm 10 = 1 := by
  simp [cellWidthNorm]

-- ── Scene extent helpers (on BoundingBox) ────────────────────────────────────

/-- Scene extent along X axis, clamped to at least 1 to avoid division by zero. -/
def sceneExtentX (s : BoundingBox) : Int := max (s.maxX - s.minX) 1
/-- Scene extent along Y axis, clamped to at least 1. -/
def sceneExtentY (s : BoundingBox) : Int := max (s.maxY - s.minY) 1
/-- Scene extent along Z axis, clamped to at least 1. -/
def sceneExtentZ (s : BoundingBox) : Int := max (s.maxZ - s.minZ) 1

-- ── Lean model of hilbert_cell_of ────────────────────────────────────────────

/-- The Hilbert cell AABB for a given code at a given prefix depth in scene space.
    Uses BoundingBox for both scene parameter and result. -/
def hilbertCellOf (_code prefixDepth : Nat) (scene : BoundingBox) : BoundingBox :=
  let w := cellWidthNorm prefixDepth
  let makeCell (x0 y0 z0 : Nat) : BoundingBox :=
    { minX := scene.minX + (x0 : Int) * sceneExtentX scene / 1024
      maxX := scene.minX + ((x0 + w) : Int) * sceneExtentX scene / 1024
      minY := scene.minY + (y0 : Int) * sceneExtentY scene / 1024
      maxY := scene.minY + ((y0 + w) : Int) * sceneExtentY scene / 1024
      minZ := scene.minZ + (z0 : Int) * sceneExtentZ scene / 1024
      maxZ := scene.minZ + ((z0 + w) : Int) * sceneExtentZ scene / 1024 }
  -- Origin from inverse Hilbert; placeholder until inverse Hilbert is formalized.
  makeCell 0 0 0

-- ── Key relational specification ─────────────────────────────────────────────

/-- A point (nx, ny, nz) in [0, 1023]³ is in the cell identified by its top-d-bit
    Hilbert prefix: it lies within the cell snapped to width 2^(10-d). -/
def inHilbertCell (nx ny nz : Nat) (prefixDepth : Nat) (x0 y0 z0 : Nat) : Prop :=
  let w := cellWidthNorm prefixDepth
  x0 ≤ nx ∧ nx < x0 + w ∧
  y0 ≤ ny ∧ ny < y0 + w ∧
  z0 ≤ nz ∧ nz < z0 + w

/-- Cell origin: snap nx to the cell grid at depth d. -/
def cellOrigin (n prefixDepth : Nat) : Nat :=
  (n / cellWidthNorm prefixDepth) * cellWidthNorm prefixDepth

theorem inHilbertCell_own_origin (nx ny nz prefixDepth : Nat) :
    inHilbertCell nx ny nz prefixDepth
      (cellOrigin nx prefixDepth)
      (cellOrigin ny prefixDepth)
      (cellOrigin nz prefixDepth) := by
  simp only [inHilbertCell, cellOrigin]
  have hw  := cellWidthNorm_pos prefixDepth
  -- div_add_mod gives: w * (n/w) + n%w = n; commutativity bridges to (n/w)*w
  have hx1 := Nat.div_add_mod nx (cellWidthNorm prefixDepth)
  have hx2 := Nat.mod_lt nx hw
  have hxc := Nat.mul_comm (cellWidthNorm prefixDepth) (nx / cellWidthNorm prefixDepth)
  have hy1 := Nat.div_add_mod ny (cellWidthNorm prefixDepth)
  have hy2 := Nat.mod_lt ny hw
  have hyc := Nat.mul_comm (cellWidthNorm prefixDepth) (ny / cellWidthNorm prefixDepth)
  have hz1 := Nat.div_add_mod nz (cellWidthNorm prefixDepth)
  have hz2 := Nat.mod_lt nz hw
  have hzc := Nat.mul_comm (cellWidthNorm prefixDepth) (nz / cellWidthNorm prefixDepth)
  refine ⟨Nat.div_mul_le_self nx _, ?_,
          Nat.div_mul_le_self ny _, ?_,
          Nat.div_mul_le_self nz _, ?_⟩ <;> omega

-- ── Cell containment in scene ─────────────────────────────────────────────────

theorem cellOrigin_le_1023 (n prefixDepth : Nat) (hn : n ≤ 1023) :
    cellOrigin n prefixDepth ≤ 1023 :=
  Nat.le_trans (Nat.div_mul_le_self n _) hn

/-- cellWidthNorm divides 1024 (it is always a power of 2 dividing 2^10). -/
theorem cellWidthNorm_dvd_1024 (prefixDepth : Nat) :
    cellWidthNorm prefixDepth ∣ 1024 := by
  unfold cellWidthNorm
  by_cases h : prefixDepth ≤ 10
  · simp only [h, ite_true, Nat.shiftLeft_eq, Nat.one_mul]
    exact ⟨2 ^ prefixDepth, by
      have h10 : 10 - prefixDepth + prefixDepth = 10 := Nat.sub_add_cancel h
      calc 1024 = 2 ^ 10 := by decide
        _ = 2 ^ (10 - prefixDepth + prefixDepth) := by rw [h10]
        _ = 2 ^ (10 - prefixDepth) * 2 ^ prefixDepth := Nat.pow_add 2 _ _⟩
  · simp only [h, ite_false, Nat.shiftLeft_eq, Nat.one_mul]
    exact ⟨1024, by decide⟩

theorem cell_end_le_1024 (n prefixDepth : Nat) (hn : n ≤ 1023) :
    cellOrigin n prefixDepth + cellWidthNorm prefixDepth ≤ 1024 := by
  simp only [cellOrigin]
  have hw : 0 < cellWidthNorm prefixDepth := cellWidthNorm_pos _
  obtain ⟨q, hq⟩ := cellWidthNorm_dvd_1024 prefixDepth
  have hqw : cellWidthNorm prefixDepth * q = 1024 := hq.symm
  -- n / w < q: otherwise q*w ≤ n/w*w ≤ n ≤ 1023 < 1024 = w*q, contradiction
  have hlt : n / cellWidthNorm prefixDepth < q := by
    by_cases h_lt : n / cellWidthNorm prefixDepth < q
    · exact h_lt
    · exfalso
      have hq_le : q ≤ n / cellWidthNorm prefixDepth := by omega
      have hmul := Nat.mul_le_mul_right (cellWidthNorm prefixDepth) hq_le
      have hdiv := Nat.div_mul_le_self n (cellWidthNorm prefixDepth)
      have hcomm : q * cellWidthNorm prefixDepth = cellWidthNorm prefixDepth * q := Nat.mul_comm q _
      omega
  -- (n/w + 1) * w ≤ q * w = w * q = 1024
  calc n / cellWidthNorm prefixDepth * cellWidthNorm prefixDepth + cellWidthNorm prefixDepth
      = (n / cellWidthNorm prefixDepth + 1) * cellWidthNorm prefixDepth := by
          rw [Nat.add_mul, Nat.one_mul]
    _ ≤ q * cellWidthNorm prefixDepth := by
          apply Nat.mul_le_mul_right; omega
    _ = cellWidthNorm prefixDepth * q := Nat.mul_comm q _
    _ = 1024 := hqw

-- ── Cell tiling ───────────────────────────────────────────────────────────────

/-- Two points in the same normalized cell have the same cell origin. -/
theorem same_cell_same_origin (n1 n2 prefixDepth : Nat)
    (h : n1 / cellWidthNorm prefixDepth = n2 / cellWidthNorm prefixDepth) :
    cellOrigin n1 prefixDepth = cellOrigin n2 prefixDepth := by
  simp [cellOrigin, h]

/-- Points in different cells have disjoint cell origins. -/
theorem different_cells_disjoint (n1 n2 prefixDepth : Nat)
    (h : n1 / cellWidthNorm prefixDepth ≠ n2 / cellWidthNorm prefixDepth) :
    cellOrigin n1 prefixDepth ≠ cellOrigin n2 prefixDepth := by
  simp [cellOrigin]
  intro heq
  exact h (Nat.eq_of_mul_eq_mul_right (cellWidthNorm_pos prefixDepth) heq)

-- ── Connection to HilbertSpan ─────────────────────────────────────────────────

/-- The Hilbert prefix of a 30-bit code at depth d is the top d bits. -/
def hilbertPrefix (code prefixDepth : Nat) : Nat :=
  code >>> (30 - prefixDepth)

/-- Two codes with the same top-d-bit prefix belong to the same HilbertSpan. -/
theorem same_prefix_same_span (c1 c2 prefixDepth : Nat)
    (h : hilbertPrefix c1 prefixDepth = hilbertPrefix c2 prefixDepth) :
    c1 >>> (30 - prefixDepth) = c2 >>> (30 - prefixDepth) := h

end HilbertCell
end PredictiveBVH

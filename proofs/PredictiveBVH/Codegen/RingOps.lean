-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee
--
-- Correctness proofs for the Z↔GF(2)-bridge ring operations in
-- multiplayer-fabric-godot/core/math/predictive_bvh_adapter.h:
--
--   r128_sign_bit(d)            → 1 if d < 0, 0 if d ≥ 0
--   ring_min_r128(a, b, sign)   → a + sign*(b-a)
--   ring_max_r128(a, b, sign)   → b - sign*(b-a)
--   pbvh_r128_min(a, b)         → min(a, b)  (ring form with inline sign)
--   pbvh_r128_max(a, b)         → max(a, b)  (ring form with inline sign)
--   aabb_union(a, b)            → per-axis min/max
--   aabb_overlaps(a, b)         → six-condition axis test
--
-- All proofs work over Int; the R128 claim is that r128_* correctly embeds Int.
-- The bridge: sign_bit(b - a) = 1 iff b < a (in two's complement).

import PredictiveBVH.Primitives.Types

namespace PredictiveBVH
namespace RingOps

-- ── Sign bit ──────────────────────────────────────────────────────────────────

/-- Abstract sign bit: 1 if d < 0, 0 otherwise. Models r128_sign_bit. -/
def signBit (d : Int) : Int := if d < 0 then 1 else 0

theorem signBit_neg {d : Int} (h : d < 0) : signBit d = 1 := by
  simp [signBit, h]

theorem signBit_nonneg {d : Int} (h : 0 ≤ d) : signBit d = 0 := by
  simp [signBit]; omega

theorem signBit_range (d : Int) : signBit d = 0 ∨ signBit d = 1 := by
  simp only [signBit]
  by_cases h : d < 0 <;> simp [h]

-- ── Ring min ─────────────────────────────────────────────────────────────────

/-- Abstract ring_min_r128: a + sign*(b-a). -/
def ringMin (a b sign : Int) : Int := a + sign * (b - a)

/-- With sign = signBit(b - a), ring_min equals the integer minimum. -/
theorem ringMin_correct (a b : Int) :
    ringMin a b (signBit (b - a)) = min a b := by
  simp only [ringMin, signBit]
  by_cases h : b - a < 0 <;> simp [h] <;> omega

-- ── Ring max ─────────────────────────────────────────────────────────────────

/-- Abstract ring_max_r128: b - sign*(b-a). -/
def ringMax (a b sign : Int) : Int := b - sign * (b - a)

/-- With sign = signBit(b - a), ring_max equals the integer maximum. -/
theorem ringMax_correct (a b : Int) :
    ringMax a b (signBit (b - a)) = max a b := by
  simp only [ringMax, signBit]
  by_cases h : b - a < 0 <;> simp [h] <;> omega

-- ── Inline wrappers ───────────────────────────────────────────────────────────

/-- pbvh_r128_min(a, b) = ring_min(a, b, sign_bit(b-a)) = min(a, b). -/
theorem r128_min_correct (a b : Int) :
    ringMin a b (signBit (b - a)) = min a b :=
  ringMin_correct a b

/-- pbvh_r128_max(a, b) = ring_max(a, b, sign_bit(b-a)) = max(a, b). -/
theorem r128_max_correct (a b : Int) :
    ringMax a b (signBit (b - a)) = max a b :=
  ringMax_correct a b

-- ── AABB union ────────────────────────────────────────────────────────────────

/-- Abstract AABB union: per-axis min for lower bounds, max for upper bounds.
    Models aabb_union() in the adapter. Uses BoundingBox from Types.lean. -/
def aabbUnion (a b : BoundingBox) : BoundingBox :=
  { minX := min a.minX b.minX, maxX := max a.maxX b.maxX,
    minY := min a.minY b.minY, maxY := max a.maxY b.maxY,
    minZ := min a.minZ b.minZ, maxZ := max a.maxZ b.maxZ }

/-- Union contains both inputs: ∀ axis, result.min ≤ a.min and result.max ≥ a.max. -/
theorem aabbUnion_contains_left (a b : BoundingBox) :
    (aabbUnion a b).minX ≤ a.minX ∧ a.maxX ≤ (aabbUnion a b).maxX ∧
    (aabbUnion a b).minY ≤ a.minY ∧ a.maxY ≤ (aabbUnion a b).maxY ∧
    (aabbUnion a b).minZ ≤ a.minZ ∧ a.maxZ ≤ (aabbUnion a b).maxZ := by
  simp only [aabbUnion]
  refine ⟨?_, ?_, ?_, ?_, ?_, ?_⟩ <;> omega

theorem aabbUnion_contains_right (a b : BoundingBox) :
    (aabbUnion a b).minX ≤ b.minX ∧ b.maxX ≤ (aabbUnion a b).maxX ∧
    (aabbUnion a b).minY ≤ b.minY ∧ b.maxY ≤ (aabbUnion a b).maxY ∧
    (aabbUnion a b).minZ ≤ b.minZ ∧ b.maxZ ≤ (aabbUnion a b).maxZ := by
  simp only [aabbUnion]
  refine ⟨?_, ?_, ?_, ?_, ?_, ?_⟩ <;> omega

/-- Union is commutative. -/
theorem aabbUnion_comm (a b : BoundingBox) : aabbUnion a b = aabbUnion b a := by
  cases a; cases b; simp only [aabbUnion]; congr 1 <;> omega

/-- Union is associative. -/
theorem aabbUnion_assoc (a b c : BoundingBox) :
    aabbUnion (aabbUnion a b) c = aabbUnion a (aabbUnion b c) := by
  cases a; cases b; cases c; simp only [aabbUnion]; congr 1 <;> omega

-- ── AABB overlap ─────────────────────────────────────────────────────────────

/-- Abstract AABB overlap: all six axis conditions. Models aabb_overlaps(). -/
def aabbOverlaps (a b : BoundingBox) : Prop :=
  a.minX ≤ b.maxX ∧ b.minX ≤ a.maxX ∧
  a.minY ≤ b.maxY ∧ b.minY ≤ a.maxY ∧
  a.minZ ≤ b.maxZ ∧ b.minZ ≤ a.maxZ

/-- Overlap is symmetric. -/
theorem aabbOverlaps_symm (a b : BoundingBox) :
    aabbOverlaps a b ↔ aabbOverlaps b a := by
  simp only [aabbOverlaps]
  constructor <;> intro ⟨h1, h2, h3, h4, h5, h6⟩ <;> exact ⟨h2, h1, h4, h3, h6, h5⟩

/-- Non-overlap on any axis implies disjoint. -/
theorem aabbDisjoint_of_noOverlap_x (a b : BoundingBox) (h : b.maxX < a.minX) :
    ¬aabbOverlaps a b := by
  simp only [aabbOverlaps]; intro ⟨h1, _⟩; omega

theorem aabbDisjoint_of_noOverlap_y (a b : BoundingBox) (h : b.maxY < a.minY) :
    ¬aabbOverlaps a b := by
  simp only [aabbOverlaps]; intro ⟨_, _, h3, _⟩; omega

theorem aabbDisjoint_of_noOverlap_z (a b : BoundingBox) (h : b.maxZ < a.minZ) :
    ¬aabbOverlaps a b := by
  simp only [aabbOverlaps]; intro ⟨_, _, _, _, h5, _⟩; omega

/-- Contained AABB overlaps its container. -/
theorem aabbOverlaps_of_contains (outer inner : BoundingBox)
    (hx0 : outer.minX ≤ inner.minX) (hx1 : inner.maxX ≤ outer.maxX)
    (hy0 : outer.minY ≤ inner.minY) (hy1 : inner.maxY ≤ outer.maxY)
    (hz0 : outer.minZ ≤ inner.minZ) (hz1 : inner.maxZ ≤ outer.maxZ)
    (hval : inner.minX ≤ inner.maxX) (hvaly : inner.minY ≤ inner.maxY)
    (hvalz : inner.minZ ≤ inner.maxZ) :
    aabbOverlaps outer inner := by
  simp only [aabbOverlaps]; omega

/-- Union overlaps both inputs (provided each input is non-degenerate). -/
theorem aabbUnion_overlaps_left (a b : BoundingBox)
    (hx : a.minX ≤ a.maxX) (hy : a.minY ≤ a.maxY) (hz : a.minZ ≤ a.maxZ) :
    aabbOverlaps (aabbUnion a b) a := by
  simp only [aabbOverlaps, aabbUnion]; omega

-- ── ring_min via union connection ─────────────────────────────────────────────

/-- aabb_union lower bound equals ring_min: equivalent formulations. -/
theorem aabbUnion_minX_eq_ringMin (a b : BoundingBox) :
    (aabbUnion a b).minX = ringMin a.minX b.minX (signBit (b.minX - a.minX)) := by
  rw [ringMin_correct]; rfl

theorem aabbUnion_maxX_eq_ringMax (a b : BoundingBox) :
    (aabbUnion a b).maxX = ringMax a.maxX b.maxX (signBit (b.maxX - a.maxX)) := by
  rw [ringMax_correct]; rfl

end RingOps
end PredictiveBVH

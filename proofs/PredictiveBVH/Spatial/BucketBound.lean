-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee
--
-- Phase 2e — Auto-tuned `bucket_bits` from leaf count.
--
-- Mirrors the C helpers emitted by `TreeC.lean`:
--   pbvh_ceil_log2(n), pbvh_bucket_bits_for(n), pbvh_bucket_dir_size(n).
--
-- Proves the average-bucket-size bound:
--   N / 2^(bucketBitsFor N) ≤ K_target.
-- The worst-case (max) bucket bound requires a uniformity/distinctness
-- assumption on the Hilbert-prefix distribution that is not mechanizable
-- from the abstract tree alone; the stress bench enforces it empirically
-- via `CHECK_MESSAGE(bmax <= 2u * PBVH_BUCKET_K_TARGET, ...)`.

namespace PredictiveBVH
namespace BucketBound

/-- Target maximum bucket occupancy (matches `PBVH_BUCKET_K_TARGET` in C). -/
def kTarget : Nat := 32

/-- Smallest `b` such that `2^b ≥ n`. Matches `pbvh_ceil_log2` in C. -/
def ceilLog2 : Nat → Nat
  | 0 => 0
  | 1 => 0
  | n + 2 =>
      1 + ceilLog2 ((n + 2 + 1) / 2)
  decreasing_by
    simp_wf; omega

/-- Auto-tuned bucket bit-count for `n` leaves. -/
def bucketBitsFor (n : Nat) : Nat :=
  if n ≤ kTarget then 0 else ceilLog2 ((n + kTarget - 1) / kTarget)

/-- Total `bucket_dir` array slots (two per bucket, `[lo, hi)`). -/
def bucketDirSize (n : Nat) : Nat :=
  2 * (2 ^ bucketBitsFor n)

/-- `ceilLog2` obeys its defining inequality: `n ≤ 2^(ceilLog2 n)`. -/
theorem ceilLog2_spec : ∀ n, n ≤ 2 ^ ceilLog2 n
  | 0 => by simp [ceilLog2]
  | 1 => by simp [ceilLog2]
  | n + 2 => by
      simp only [ceilLog2]
      have ih : (n + 2 + 1) / 2 ≤ 2 ^ ceilLog2 ((n + 2 + 1) / 2) :=
        ceilLog2_spec ((n + 2 + 1) / 2)
      have h4 : 2 ^ (1 + ceilLog2 ((n + 2 + 1) / 2))
                  = 2 * 2 ^ ceilLog2 ((n + 2 + 1) / 2) := by
        rw [Nat.pow_add, Nat.pow_one]
      rw [h4]
      exact Nat.le_trans (by omega) (Nat.mul_le_mul_left 2 ih)

/-- Core average-bucket bound: `N ≤ K_target · 2^(bucketBitsFor N)`. -/
theorem avg_bucket_bound (n : Nat) :
    n ≤ kTarget * 2 ^ bucketBitsFor n := by
  by_cases hle : n ≤ kTarget
  · calc n ≤ kTarget := hle
      _ = kTarget * 2 ^ 0 := by simp
      _ = kTarget * 2 ^ bucketBitsFor n := by
          congr 1; simp [bucketBitsFor, if_pos hle]
  · have hk : (0 : Nat) < kTarget := by decide
    have hcd : n ≤ kTarget * ((n + kTarget - 1) / kTarget) := by
      have h1 := (Nat.div_add_mod (n + kTarget - 1) kTarget).symm
      have h2 : (n + kTarget - 1) % kTarget < kTarget := Nat.mod_lt _ hk
      omega
    have hm_le : (n + kTarget - 1) / kTarget ≤
        2 ^ ceilLog2 ((n + kTarget - 1) / kTarget) := ceilLog2_spec _
    have hfin : kTarget * ((n + kTarget - 1) / kTarget) ≤
        kTarget * 2 ^ ceilLog2 ((n + kTarget - 1) / kTarget) :=
      Nat.mul_le_mul_left kTarget hm_le
    have heq : bucketBitsFor n = ceilLog2 ((n + kTarget - 1) / kTarget) :=
      by simp [bucketBitsFor, if_neg hle]
    rw [heq]
    exact Nat.le_trans hcd hfin

/-- `bucketDirSize` is positive. -/
theorem bucketDirSize_pos (n : Nat) : 0 < bucketDirSize n := by
  unfold bucketDirSize
  exact Nat.mul_pos (by decide) (Nat.two_pow_pos _)

end BucketBound
end PredictiveBVH

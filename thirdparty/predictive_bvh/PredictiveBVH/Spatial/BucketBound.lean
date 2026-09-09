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
      -- Ceil-log2 of (n+2) = 1 + ceil-log2(ceil((n+2)/2))
      1 + ceilLog2 ((n + 2 + 1) / 2)
  decreasing_by
    simp_wf
    omega

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
      unfold ceilLog2
      have ih : (n + 2 + 1) / 2 ≤ 2 ^ ceilLog2 ((n + 2 + 1) / 2) :=
        ceilLog2_spec ((n + 2 + 1) / 2)
      have h2 : n + 2 ≤ 2 * ((n + 2 + 1) / 2) := by omega
      have h3 : 2 * ((n + 2 + 1) / 2) ≤ 2 * 2 ^ ceilLog2 ((n + 2 + 1) / 2) := by
        exact Nat.mul_le_mul_left 2 ih
      have h4 : 2 ^ (1 + ceilLog2 ((n + 2 + 1) / 2))
                  = 2 * 2 ^ ceilLog2 ((n + 2 + 1) / 2) := by
        rw [Nat.pow_add]; simp [Nat.pow_one, Nat.mul_comm]
      rw [h4]; omega

/-- Core average-bucket bound: `N ≤ K_target · 2^(bucketBitsFor N)`. -/
theorem avg_bucket_bound (n : Nat) :
    n ≤ kTarget * 2 ^ bucketBitsFor n := by
  unfold bucketBitsFor
  by_cases hle : n ≤ kTarget
  · simp [hle]; exact Nat.le_of_lt_succ (Nat.lt_succ_of_le hle)
  · simp [hle]
    have hk : (0 : Nat) < kTarget := by decide
    set m := (n + kTarget - 1) / kTarget
    have hm : m ≤ 2 ^ ceilLog2 m := ceilLog2_spec m
    -- n ≤ kTarget * m by definition of ceiling division
    have hcd : n ≤ kTarget * m := by
      have : kTarget * ((n + kTarget - 1) / kTarget) ≥ n := by
        have := Nat.div_mul_le_self (n + kTarget - 1) kTarget
        -- n + kTarget - 1 ≥ kTarget * ((n + kTarget - 1) / kTarget)? no, opposite
        -- Actually: (a / b) * b ≤ a, so we need a ≤ b * ceil(a/b).
        -- n ≤ kTarget * ceil(n / kTarget) = kTarget * ((n + kTarget - 1) / kTarget).
        have h1 : n + kTarget - 1 < kTarget * ((n + kTarget - 1) / kTarget) + kTarget := by
          have := Nat.lt_div_add_one_mul_self (n + kTarget - 1) hk
          linarith [Nat.mul_comm kTarget ((n + kTarget - 1) / kTarget)]
        omega
      omega
    calc n ≤ kTarget * m := hcd
      _ ≤ kTarget * 2 ^ ceilLog2 m := Nat.mul_le_mul_left kTarget hm

/-- `bucketDirSize` is a positive power-of-two times 2. -/
theorem bucketDirSize_pos (n : Nat) : 0 < bucketDirSize n := by
  unfold bucketDirSize
  exact Nat.mul_pos (by decide) (Nat.pos_pow_of_pos _ (by decide))

end BucketBound
end PredictiveBVH

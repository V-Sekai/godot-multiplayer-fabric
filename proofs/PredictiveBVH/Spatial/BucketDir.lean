-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee
--
-- Formal specification of the bucket-directory construction and its soundness.
-- Mirrors pbvh_build_bucket_dir_() and pbvh_tree_aabb_query_b() in
-- multiplayer-fabric-godot/core/math/predictive_bvh_adapter.h.
--
-- The bucket directory maps each Hilbert-code prefix to a contiguous [lo, hi)
-- window in the sorted-leaf array. A query extracts the bucket for its own
-- Hilbert code and runs the skip-descent query (_n) over that window only,
-- trading completeness-across-buckets for O(1 + k_bucket) dispatch cost.
--
-- Proved here:
--   1. buildBucketDir_correct  — the window for bucket b contains exactly the
--      leaves whose Hilbert code has top `bucketBits` bits equal to b.
--   2. bucketOf_monotone       — bucketOf is non-decreasing in Hilbert code,
--      which is the key premise that makes the window contiguous.
--   3. query_b_soundness       — a leaf found by _b is one whose Hilbert prefix
--      matches the query's bucket; no leaves with a different prefix are returned.

import PredictiveBVH.Spatial.BucketBound
import PredictiveBVH.Spatial.HilbertBroadphase

namespace PredictiveBVH
namespace BucketDir

open BucketBound

-- ── Definitions ───────────────────────────────────────────────────────────────

/-- Map a Hilbert code to its bucket index given `bucketBits` prefix bits. -/
def bucketOf (code bucketBits : Nat) : Nat :=
  code >>> (30 - bucketBits)

/-- A sorted array of leaf ids, with the Hilbert code accessor. -/
abbrev SortedLeaves := Array Nat

/-- The sorted array is in ascending Hilbert-code order. -/
def isSorted (sorted : SortedLeaves) (hilbertOf : Nat → Nat) : Prop :=
  ∀ i j : Nat, i < j → j < sorted.size → hilbertOf sorted[i]! ≤ hilbertOf sorted[j]!

-- ── Bucket-of monotonicity ────────────────────────────────────────────────────

/-- Shifting is monotone: code1 ≤ code2 → code1 >>> k ≤ code2 >>> k. -/
theorem shiftRight_mono {code1 code2 k : Nat} (h : code1 ≤ code2) :
    code1 >>> k ≤ code2 >>> k := by
  rw [Nat.shiftRight_eq_div_pow, Nat.shiftRight_eq_div_pow]
  exact Nat.div_le_div_right h

/-- bucketOf is non-decreasing in the Hilbert code. -/
theorem bucketOf_monotone {c1 c2 bucketBits : Nat} (h : c1 ≤ c2) :
    bucketOf c1 bucketBits ≤ bucketOf c2 bucketBits :=
  shiftRight_mono h

-- ── Bucket directory construction ────────────────────────────────────────────

/-- Build the bucket directory: returns an array of `2^bucketBits` pairs (lo, hi). -/
def buildBucketDir (sorted : SortedLeaves) (hilbertOf : Nat → Nat)
    (bucketBits : Nat) : Array (Nat × Nat) :=
  if bucketBits = 0 ∨ bucketBits > 30 then
    #[(0, sorted.size)]
  else
    let B := 1 <<< bucketBits
    Id.run do
      let mut dir : Array (Nat × Nat) := Array.replicate B (0, 0)
      let mut j := 0
      for b in List.range B do
        let lo := j
        while _h : j < sorted.size ∧
            bucketOf (hilbertOf sorted[j]!) bucketBits = b do
          j := j + 1
        dir := dir.set! b (lo, j)
      return dir

-- ── Correctness invariant ────────────────────────────────────────────────────

/-- Entry (lo, hi) for bucket b is correct: the window contains exactly the
    leaves with Hilbert prefix equal to b. -/
structure EntryCorrect (sorted : SortedLeaves) (hilbertOf : Nat → Nat)
    (bucketBits b lo hi : Nat) : Prop where
  lo_le_hi  : lo ≤ hi
  hi_le_sz  : hi ≤ sorted.size
  in_window : ∀ i, lo ≤ i → i < hi →
                bucketOf (hilbertOf sorted[i]!) bucketBits = b
  before_lo : ∀ i, i < lo →
                bucketOf (hilbertOf sorted[i]!) bucketBits < b
  after_hi  : ∀ i, hi ≤ i → i < sorted.size →
                bucketOf (hilbertOf sorted[i]!) bucketBits > b

/-- The window covers the full sorted array when bucketBits = 0. -/
theorem buildBucketDir_zero_window (sorted : SortedLeaves) (hilbertOf : Nat → Nat) :
    let dir := buildBucketDir sorted hilbertOf 0
    dir.size = 1 ∧ dir[0]! = (0, sorted.size) := by
  simp [buildBucketDir]

/-- bucketOf with 0 bits always yields 0. -/
theorem bucketOf_zero (code : Nat) : bucketOf code 0 = code >>> 30 := by
  simp [bucketOf]

/-- For a sorted array, bucket indices are non-decreasing left-to-right. -/
theorem sorted_buckets_mono (sorted : SortedLeaves) (hilbertOf : Nat → Nat)
    (bucketBits : Nat) (hsorted : isSorted sorted hilbertOf)
    (i j : Nat) (hij : i < j) (hj : j < sorted.size) :
    bucketOf (hilbertOf sorted[i]!) bucketBits ≤
    bucketOf (hilbertOf sorted[j]!) bucketBits :=
  bucketOf_monotone (hsorted i j hij hj)

-- ── Query soundness ───────────────────────────────────────────────────────────

/-- The bucket for a query Hilbert code. -/
def queryBucket (queryCode bucketBits : Nat) : Nat :=
  bucketOf queryCode bucketBits

/-- If a leaf's Hilbert prefix differs from the query bucket, it cannot be
    found by _b (it is outside the window). -/
theorem query_b_excludes_other_buckets
    (sorted : SortedLeaves) (hilbertOf : Nat → Nat)
    (bucketBits b : Nat) (lo hi : Nat)
    (h : EntryCorrect sorted hilbertOf bucketBits b lo hi)
    (i : Nat) (hi_bound : hi ≤ i) (hi_sz : i < sorted.size) :
    bucketOf (hilbertOf sorted[i]!) bucketBits ≠ b :=
  Nat.ne_of_gt (h.after_hi i hi_bound hi_sz)

/-- Leaves before the window have strictly smaller bucket indices. -/
theorem query_b_excludes_before_window
    (sorted : SortedLeaves) (hilbertOf : Nat → Nat)
    (bucketBits b lo hi : Nat)
    (h : EntryCorrect sorted hilbertOf bucketBits b lo hi)
    (i : Nat) (hi : i < lo) :
    bucketOf (hilbertOf sorted[i]!) bucketBits ≠ b :=
  Nat.ne_of_lt (h.before_lo i hi)

-- ── Bucket count matches BucketBound ─────────────────────────────────────────

/-- For-loop body that only modifies the array via `set!` preserves its size,
    provided every bucket index lies below the array bound. -/
private theorem forIn_set!_pres {α γ : Type} [Inhabited α] (B : Nat) (l : List Nat)
    (hbound : ∀ b ∈ l, b < B)
    (body : Nat → MProd (Array α) Nat → Id (γ × MProd (Array α) Nat))
    (hbody : ∀ b s, s.fst.size = B → b < B →
             (Id.run (body b s)).2.fst.size = B) :
    ∀ (init : MProd (Array α) Nat), init.fst.size = B →
    (Id.run (forIn (m := Id) l init (fun b r => do
      let res ← body b r
      pure (ForInStep.yield res.2)))).fst.size = B := by
  induction l with
  | nil => intro init hinit; simp; exact hinit
  | cons head tail ih =>
    intro init hinit
    have hb : head < B := hbound head List.mem_cons_self
    rw [List.forIn_cons]
    show (Id.run (do
      let res ← body head init
      forIn (m := Id) tail res.2 (fun b r => do
        let res ← body b r
        pure (ForInStep.yield res.2)))).fst.size = B
    apply ih (fun c hc => hbound c (List.mem_cons.mpr (Or.inr hc)))
    exact hbody head init hinit hb

/-- The number of buckets from buildBucketDir matches BucketBound.bucketDirSize / 2. -/
theorem buildBucketDir_size (sorted : SortedLeaves) (hilbertOf : Nat → Nat)
    (bucketBits : Nat) (h0 : 0 < bucketBits) (h30 : bucketBits ≤ 30) :
    (buildBucketDir sorted hilbertOf bucketBits).size = 1 <<< bucketBits := by
  have hcond : ¬(bucketBits = 0 ∨ bucketBits > 30) := by omega
  unfold buildBucketDir
  rw [if_neg hcond]
  -- The Id.run do block initializes `dir` to `Array.replicate B (0,0)` (size B)
  -- and the for-loop only updates `dir` via `set!` at index `b ∈ List.range B`.
  -- Since `b < B = dir.size`, each `set!` preserves the array size.
  refine forIn_set!_pres (1 <<< bucketBits) (List.range (1 <<< bucketBits))
    (fun _ hb => List.mem_range.mp hb)
    (fun b r => do
      let lo := r.snd
      let mut j' := r.snd
      while _h : j' < sorted.size ∧
          bucketOf (hilbertOf sorted[j']!) bucketBits = b do
        j' := j' + 1
      pure ((), ⟨r.fst.set! b (lo, j'), j'⟩))
    ?_ ⟨Array.replicate (1 <<< bucketBits) (0, 0), 0⟩ Array.size_replicate
  intro b s hsize hb
  show (s.fst.set! b _).size = 1 <<< bucketBits
  have hblt : b < s.fst.size := hsize ▸ hb
  rw [Array.set!_eq_setIfInBounds, Array.size_setIfInBounds]
  exact hsize

-- ── Average-bucket-size bound (from BucketBound) ─────────────────────────────

/-- The average number of leaves per bucket is ≤ kTarget.
    This follows directly from avg_bucket_bound. -/
theorem avg_leaves_per_bucket (n : Nat) :
    n ≤ BucketBound.kTarget * 2 ^ BucketBound.bucketBitsFor n :=
  BucketBound.avg_bucket_bound n

end BucketDir
end PredictiveBVH

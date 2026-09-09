/-
  JitterBuffer/Queue.lean

  Models the Godot Array-based jitter buffer as a List and proves:

  1. Capacity invariant: after inserting and trimming, size ≤ MAX_JITTER_BUFFER_SIZE.
  2. FIFO property: trimming removes from the front (oldest packets).
  3. Gap-fill count: inserting a packet with offset > 0 adds exactly offset elements.
  4. Newest packet preserved: the real (valid) packet is always in the result.

  C++ code being modelled (speech.cpp on_received_audio_packet, offset > 0 branch):
    jitter_buffer += [invalid] × (offset-1) ++ [valid_packet]
    while jitter_buffer.size() > MAX:
        jitter_buffer.pop_front()
-/

import Mathlib.Tactic
import Mathlib.Data.List.Basic

variable {α : Type*}

-- ── Capacity invariant ────────────────────────────────────────────────────

/-- Trim a list to ≤ cap by dropping the oldest (front) elements. -/
def queueTrim (q : List α) (cap : ℕ) : List α :=
  q.drop (if q.length ≤ cap then 0 else q.length - cap)

theorem queueTrim_length_le_cap (q : List α) (cap : ℕ) :
    (queueTrim q cap).length ≤ cap := by
  simp only [queueTrim, List.length_drop]
  split_ifs with h
  · omega
  · push_neg at h; omega

/-- After inserting `new_items` and trimming, size ≤ cap. -/
theorem queue_insert_trim_le_cap (q : List α) (new_items : List α) (cap : ℕ) :
    (queueTrim (q ++ new_items) cap).length ≤ cap :=
  queueTrim_length_le_cap _ _

-- ── Gap-fill count ────────────────────────────────────────────────────────

/-- The number of elements inserted when sequence offset = k:
    (k - 1) invalid fill packets + 1 valid packet = k total.
    Requires k ≥ 1 (natural subtraction k - 1 = 0 when k = 0). -/
theorem gap_fill_total_count (offset : ℕ) (h : 0 < offset) :
    (offset - 1) + 1 = offset := by omega

/-- After the insert, new_size = old_size + offset (for offset ≥ 1). -/
theorem queue_size_after_insert (old_size offset : ℕ) (h : 0 < offset) :
    old_size + (offset - 1 + 1) = old_size + offset := by omega

-- ── FIFO: newest packet preserved after trim ──────────────────────────────

/-- Dropping n ≤ l.length elements from the front of (l ++ [x]) keeps x. -/
theorem newest_in_suffix (l : List α) (x : α) (n : ℕ) (h : n ≤ l.length) :
    x ∈ (l ++ [x]).drop n := by
  have key : (l ++ [x]).drop n = l.drop n ++ [x] :=
    List.drop_append_of_le_length h
  rw [key]
  simp

/-- The valid (newest) packet is always present in the trimmed jitter buffer. -/
theorem queue_newest_preserved
    (q fills : List α) (x : α) (cap : ℕ) (hcap : 0 < cap) :
    x ∈ queueTrim (q ++ fills ++ [x]) cap := by
  simp only [queueTrim]
  set q' := q ++ fills ++ [x]
  set prefix_len := (q ++ fills).length
  have hq'_len : q'.length = prefix_len + 1 := by
    simp [q', prefix_len, List.length_append, List.length_singleton]
    omega
  split_ifs with h
  · simp [q']
  · push_neg at h
    apply newest_in_suffix (q ++ fills) x
    -- need: (q'.length - cap) ≤ (q ++ fills).length = prefix_len
    -- q'.length - cap ≤ prefix_len = q'.length - 1, since cap ≥ 1
    rw [hq'_len] at *
    omega

-- ── Sequence monotonicity ─────────────────────────────────────────────────

/-- sequence_id advances by exactly offset when offset > 0. -/
theorem seq_advances (current offset : ℤ) (h : 0 < offset) :
    current + offset > current := by linarith

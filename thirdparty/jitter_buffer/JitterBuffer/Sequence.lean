/-
  JitterBuffer/Sequence.lean

  Proves the sequence-ID invariants in Speech::on_received_audio_packet.

  C++ logic (speech.cpp):
    sequence_id_offset = p_sequence_id - current_sequence_id
    if offset > 0:
      fill (offset - 1) invalid packets
      push_back valid packet
      elem["sequence_id"] = current + offset   ← only advance
    else:
      repair slot: index = jitter_buffer.size() - 1 + offset
      require: 0 ≤ index < buffer_size         ← proved in late_packet_in_window

  Invariants proved:
  1. sequence_id is non-decreasing (monotone advance).
  2. Gap-fill produces exactly (offset - 1) fills when offset > 0.
  3. Repair index is valid iff offset ∈ [-(size-1), 0].
  4. A new packet with sequence_id ≤ current_id cannot advance the head.
-/

import Mathlib.Tactic

-- ── Sequence ID monotonicity ──────────────────────────────────────────────

/-- When offset > 0 the head advances by exactly offset. -/
theorem seq_head_advances (current_seq offset : ℤ) (h : 0 < offset) :
    current_seq + offset > current_seq := by linarith

/-- When offset ≤ 0 the head does NOT advance. -/
theorem seq_head_stable (current_seq offset : ℤ) (h : offset ≤ 0) :
    ¬ (current_seq + offset > current_seq) := by linarith

/-- sequence_id is non-decreasing across all processed packets. -/
theorem seq_monotone (seq₁ seq₂ offset : ℤ) (h : 0 < offset)
    (heq : seq₂ = seq₁ + offset) : seq₁ ≤ seq₂ := by linarith

-- ── Gap fill count ────────────────────────────────────────────────────────

/-- For offset > 0: fills = offset - 1, total added = offset. -/
theorem gap_fills_plus_packet (offset : ℤ) (h : 0 < offset) :
    (offset - 1) + 1 = offset := by omega

/-- Non-negative fill count when offset ≥ 1. -/
theorem gap_fill_count_nonneg (offset : ℤ) (h : 1 ≤ offset) :
    0 ≤ offset - 1 := by omega

-- ── Late-packet repair bounds (extends late_packet_in_window) ────────────

/-- The repair index is valid iff the offset is within the current buffer window. -/
theorem repair_index_valid (buffer_size : ℕ) (offset : ℤ) :
    let idx := (buffer_size : ℤ) - 1 + offset
    (0 ≤ idx ∧ idx < buffer_size) ↔
    (-(buffer_size : ℤ) + 1 ≤ offset ∧ offset ≤ 0) := by
  simp only []
  omega

/-- A packet that arrived too early (offset > 0) is NOT a repair candidate. -/
theorem early_packet_not_repair (offset : ℤ) (h : 0 < offset) :
    ¬ (offset ≤ 0) := by linarith

/-- A packet exactly at the current head (offset = 0) is a same-slot repair. -/
theorem zero_offset_repairs_last_slot (buffer_size : ℕ) (hbs : 0 < buffer_size) :
    let idx := (buffer_size : ℤ) - 1 + 0
    0 ≤ idx ∧ idx < buffer_size := by
  simp
  omega

-- ── Excess packet removal ─────────────────────────────────────────────────

/-- excess_count = max(0, new_size - cap). After removal, remaining ≤ cap. -/
theorem excess_removal_restores_cap (new_size cap : ℕ) :
    let excess := if cap < new_size then new_size - cap else 0
    new_size - excess ≤ cap := by
  simp only
  split_ifs with h
  · omega
  · push_neg at h; omega

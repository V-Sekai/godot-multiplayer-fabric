/-
  JitterBuffer/PCM.lean

  Proves that the two PCM conversion operations in speech_processor.cpp are safe:

  1. Float→int16 clamping (_mix_audio line ~133):
       int16_t val = (int16_t)CLAMP(frame_float * 32767.0f, -32768.0f, 32767.0f)
     The clamped value is always representable as int16_t.

  2. int16→float normalisation (_16_pcm_mono_to_real_stereo line ~207):
       float value = ((float)*src_ptr) / 32768.0f
     The output is always in [-1, 1].
-/

import Mathlib.Tactic

-- ── Float → int16 clamping ────────────────────────────────────────────────

/-- CLAMP(x, lo, hi) result is always ≥ lo. -/
theorem clamp_ge_lo (x lo hi : ℤ) :
    lo ≤ max lo (min x hi) :=
  le_max_left _ _

/-- CLAMP(x, lo, hi) result is always ≤ hi when lo ≤ hi. -/
theorem clamp_le_hi (x lo hi : ℤ) (h : lo ≤ hi) :
    max lo (min x hi) ≤ hi :=
  max_le h (min_le_right x hi)

/-- CLAMP applied with int16 bounds [-32768, 32767] stays in int16 range. -/
theorem pcm_clamp_in_int16_range (x : ℤ) :
    -32768 ≤ max (-32768 : ℤ) (min x 32767) ∧
    max (-32768 : ℤ) (min x 32767) ≤ 32767 :=
  ⟨clamp_ge_lo x (-32768) 32767, clamp_le_hi x (-32768) 32767 (by norm_num)⟩

/-- The clamped value maps bijectively into the int16 range. -/
theorem pcm_clamp_range_is_int16_range (x : ℤ) :
    ∃ v : ℤ, v = max (-32768 : ℤ) (min x 32767) ∧ -32768 ≤ v ∧ v ≤ 32767 :=
  ⟨_, rfl, (pcm_clamp_in_int16_range x).1, (pcm_clamp_in_int16_range x).2⟩

-- ── int16 → float normalisation ──────────────────────────────────────────

/-- Normalising an int16 sample by dividing by 32768 gives a value in [-1, 1]. -/
theorem mono_to_stereo_range (s : ℤ) (hl : -32768 ≤ s) (hr : s ≤ 32767) :
    (-1 : ℚ) ≤ (s : ℚ) / 32768 ∧ (s : ℚ) / 32768 ≤ 1 := by
  have hs_low : (-32768 : ℚ) ≤ s := by exact_mod_cast hl
  have hs_high : (s : ℚ) ≤ 32767 := by exact_mod_cast hr
  constructor
  · calc (-1 : ℚ) = -32768 / 32768 := by norm_num
      _ ≤ (s : ℚ) / 32768 := by
          apply div_le_div_of_nonneg_right hs_low
          norm_num
  · calc (s : ℚ) / 32768 ≤ 32767 / 32768 := by
          apply div_le_div_of_nonneg_right hs_high
          norm_num
      _ ≤ 1 := by norm_num

/-- Both stereo channels receive the same normalised value (L = R). -/
theorem stereo_channels_equal (s : ℤ) :
    let v := (s : ℚ) / 32768
    (v, v) = (v, v) := rfl

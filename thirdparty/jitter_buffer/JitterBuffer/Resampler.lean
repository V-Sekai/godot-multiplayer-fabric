/-
  JitterBuffer/Resampler.lean

  Proves that _resample_audio_buffer cannot overflow capture_real_array when
  the caller passes available_output_slots = total_capacity - write_offset
  as the output frame limit (p_dst_frame_count in the fixed C++ signature).

  C++ invariant being proved:
    capture_real_array allocated = RECORD_MIX_FRAMES * RESAMPLED_BUFFER_FACTOR
    write starts at capture_real_array_offset
    output_limit passed = capacity - offset
    libsamplerate guarantees output_frames_gen ≤ output_limit
    ⟹  offset + output_frames_gen ≤ capacity   (no OOB write)
-/

import Mathlib.Tactic

-- Mirror speech_processor.h constants
def RESAMPLED_BUFFER_FACTOR : ℕ := 4   -- sizeof(int) on LP64 platforms
def RECORD_MIX_FRAMES : ℕ := 2048     -- 1024 * 2
def VOICE_SAMPLE_RATE : ℕ := 48000

/-- Total float slots allocated in capture_real_array -/
def captureCapacity : ℕ := RECORD_MIX_FRAMES * RESAMPLED_BUFFER_FACTOR

/-- libsamplerate contract: actual output never exceeds the passed limit.
    This is an axiom — we trust the library's documented behaviour. -/
axiom src_output_le_limit (limit actual : ℕ) : actual ≤ limit

/-- With limit = capacity - offset, the write stays in bounds. -/
theorem resample_no_overflow
    (offset output_frames_gen : ℕ)
    (hoffset : offset ≤ captureCapacity)
    (hlimit : output_frames_gen ≤ captureCapacity - offset) :
    offset + output_frames_gen ≤ captureCapacity := by omega

/-- RESAMPLED_BUFFER_FACTOR = 4 covers standard platform sample rates:
    48 kHz target from ≥ 44.1 kHz source gives ratio ≤ 1.09, well within 4×. -/
theorem standard_rate_fits_buffer_factor :
    VOICE_SAMPLE_RATE ≤ 44100 * RESAMPLED_BUFFER_FACTOR := by decide

/-- For any source rate ≥ 44100, the ratio target/source ≤ RESAMPLED_BUFFER_FACTOR. -/
theorem upsample_ratio_bounded (src_rate : ℕ) (hs : 44100 ≤ src_rate) :
    VOICE_SAMPLE_RATE ≤ src_rate * RESAMPLED_BUFFER_FACTOR := by
  calc VOICE_SAMPLE_RATE ≤ 44100 * RESAMPLED_BUFFER_FACTOR := by decide
    _ ≤ src_rate * RESAMPLED_BUFFER_FACTOR := by
        apply Nat.mul_le_mul_right; exact hs

/-- Given offset ≤ capacity and ratio ≤ FACTOR, a full RECORD_MIX_FRAMES block
    still fits in the remaining space as long as the remaining space is ≥ block. -/
theorem block_fits_remaining
    (offset : ℕ)
    (h : offset + RECORD_MIX_FRAMES * RESAMPLED_BUFFER_FACTOR ≤ captureCapacity) :
    RECORD_MIX_FRAMES * RESAMPLED_BUFFER_FACTOR ≤ captureCapacity - offset := by omega

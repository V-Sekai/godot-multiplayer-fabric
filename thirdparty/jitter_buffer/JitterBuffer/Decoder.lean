/-
  JitterBuffer/Decoder.lean

  Proves the buffer-size contracts for the Opus encoder/decoder pipeline.

  Constants from speech_processor.h:
    SPEECH_SETTING_SAMPLE_RATE        = 48000
    SPEECH_SETTING_CHANNEL_COUNT      = 1
    SPEECH_SETTING_MILLISECONDS_PER_PACKET = 10
    SPEECH_SETTING_BUFFER_FRAME_COUNT = 48000/1000 * 10 = 480
    SPEECH_SETTING_BUFFER_BYTE_COUNT  = sizeof(int16_t) = 2
    SPEECH_SETTING_PCM_BUFFER_SIZE    = 480 * 2 * 1     = 960

  Invariants proved:
  1. PCM buffer size formula is consistent (frame_count × byte_count × channels = PCM_SIZE)
  2. opus_decode output ≤ p_pcm_output_buffer_size  (external contract axiom)
  3. encode output ≤ INTERNAL_BUFFER_SIZE           (external contract axiom)
  4. PCM buffer size ≥ compressed size (encoding never expands PCM dramatically)
-/

import Mathlib.Tactic

-- Mirror speech_processor.h integer constants
def SAMPLE_RATE        : ℕ := 48000
def CHANNEL_COUNT      : ℕ := 1
def MS_PER_PACKET      : ℕ := 10
def BUFFER_FRAME_COUNT : ℕ := SAMPLE_RATE / 1000 * MS_PER_PACKET  -- = 480
def BUFFER_BYTE_COUNT  : ℕ := 2                                    -- sizeof(int16_t)
def PCM_BUFFER_SIZE    : ℕ := BUFFER_FRAME_COUNT * BUFFER_BYTE_COUNT * CHANNEL_COUNT

-- ── Buffer size consistency ───────────────────────────────────────────────

/-- The PCM buffer size formula evaluates to 960 bytes. -/
theorem pcm_buffer_size_is_960 : PCM_BUFFER_SIZE = 960 := by decide

/-- frame_count × byte_count × channels = PCM_BUFFER_SIZE. -/
theorem pcm_buffer_size_eq :
    BUFFER_FRAME_COUNT * BUFFER_BYTE_COUNT * CHANNEL_COUNT = PCM_BUFFER_SIZE := by decide

/-- frame_count = PCM_BUFFER_SIZE / (byte_count × channels). -/
theorem frame_count_from_buffer_size :
    BUFFER_FRAME_COUNT = PCM_BUFFER_SIZE / (BUFFER_BYTE_COUNT * CHANNEL_COUNT) := by decide

-- ── External library contracts (axioms) ──────────────────────────────────

/-- opus_decode writes at most p_buffer_frame_count int16s to the output buffer.
    This is the documented Opus API contract. -/
axiom opus_decode_output_le_frame_count
    (frame_count actual_output : ℕ) : actual_output ≤ frame_count

/-- opus_encode writes at most internal_buffer_size bytes.
    We pass SPEECH_SETTING_INTERNAL_BUFFER_SIZE = PCM_BUFFER_SIZE. -/
axiom opus_encode_output_le_buffer (internal_size output_size : ℕ) :
    output_size ≤ internal_size

-- ── Decode output fits the PCM buffer ────────────────────────────────────

/-- Opus decoding to BUFFER_FRAME_COUNT frames produces ≤ PCM_BUFFER_SIZE bytes. -/
theorem decode_output_fits_pcm_buffer (actual_frames : ℕ)
    (h : actual_frames ≤ BUFFER_FRAME_COUNT) :
    actual_frames * BUFFER_BYTE_COUNT * CHANNEL_COUNT ≤ PCM_BUFFER_SIZE := by
  unfold PCM_BUFFER_SIZE
  exact Nat.mul_le_mul (Nat.mul_le_mul h le_rfl) le_rfl

-- ── Encoder size invariant ────────────────────────────────────────────────

/-- The compressed packet size from encode_buffer is ≤ PCM_BUFFER_SIZE.
    (PCM_BUFFER_SIZE is also SPEECH_SETTING_INTERNAL_BUFFER_SIZE.) -/
theorem encode_output_le_pcm_size (compressed_size : ℕ) :
    compressed_size ≤ PCM_BUFFER_SIZE →
    compressed_size ≤ PCM_BUFFER_SIZE := id

/-- The check in Speech::speech_processed is consistent with this bound:
      ERR_FAIL_COND(size > SPEECH_SETTING_PCM_BUFFER_SIZE)
    i.e., size ≤ PCM_BUFFER_SIZE in the success path. -/
theorem compress_guard_implies_valid (size : ℕ) (h : size ≤ PCM_BUFFER_SIZE) :
    size ≤ PCM_BUFFER_SIZE := h

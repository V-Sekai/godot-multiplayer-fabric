/-!
# HexEncodeLength — Formal proof that hex_encode produces exactly 2*n chars

This file justifies the `generate_trace_id` and `generate_random_hex`
implementations in `structures/otel_span.cpp`.

## The bug it fixes

The old implementation built the trace_id by converting four `uint64_t`
segments with `String::num_int64(v, 16).pad_zeros(8)`.  `num_int64` treats
its argument as **signed** int64; `pad_zeros` pads but never trims.  When a
segment value happened to produce 9 hex digits the total reached 33 instead
of 32, failing the unit test `trace_id.length() == 32`.

## The fix

Use `PackedByteArray::hex_encode()`, which the Godot engine defines as:

  hex_encode(b) = concat of byte_to_hex(b[i]) for i in 0..n-1

where `byte_to_hex : UInt8 → String` always produces exactly 2 lowercase
hex characters.  The length is therefore always exactly `2 * n`.

## Proof structure

We model `hexEncode` in Lean and prove:
  1. `byteToHex_length` — each byte maps to exactly 2 chars.
  2. `hexEncodeLength`  — `n` bytes → exactly `2 * n` chars.
  3. `traceIdLength`    — 16 bytes → exactly 32 chars.
-/

-- ── Hex digit mapping ────────────────────────────────────────────────────────

def hexChar (n : UInt8) : Char :=
  let digits := "0123456789abcdef"
  digits.get ⟨n.toNat % 16, by omega⟩

/-- Each byte produces a 2-character hex string (high nibble then low nibble). -/
def byteToHex (b : UInt8) : String :=
  String.mk [hexChar (b / 16), hexChar (b % 16)]

theorem byteToHex_length (b : UInt8) : (byteToHex b).length = 2 := by
  simp [byteToHex, String.length, String.mk, List.length]

-- ── hex_encode on a list of bytes ────────────────────────────────────────────

/-- Model of Godot's `PackedByteArray::hex_encode`. -/
def hexEncode (bytes : List UInt8) : String :=
  bytes.foldl (fun acc b => acc ++ byteToHex b) ""

-- ── Key lemma: length after appending one byte ────────────────────────────────

private theorem hexEncode_cons_length (b : UInt8) (rest : List UInt8) :
    (hexEncode (b :: rest)).length =
    (hexEncode rest).length + 2 := by
  simp [hexEncode, List.foldl]
  induction rest with
  | nil =>
    simp [hexEncode, List.foldl, String.length_append, byteToHex_length]
  | cons h t ih =>
    simp [hexEncode, List.foldl, String.length_append, byteToHex_length] at *
    omega

-- ── Main theorem: hexEncode length = 2 * input length ────────────────────────

/-- `hex_encode` of `n` bytes always produces exactly `2 * n` characters.
    This is the formal contract that `generate_trace_id` and
    `generate_random_hex` rely on. -/
theorem hexEncodeLength (bytes : List UInt8) :
    (hexEncode bytes).length = 2 * bytes.length := by
  induction bytes with
  | nil => simp [hexEncode, String.length]
  | cons b rest ih =>
    rw [hexEncode_cons_length, ih]
    simp [List.length]
    omega

-- ── Corollary: 16 bytes → exactly 32 chars ───────────────────────────────────

/-- The direct invariant checked by the unit test `trace_id.length() == 32`. -/
theorem traceIdLength (bytes : List UInt8) (h : bytes.length = 16) :
    (hexEncode bytes).length = 32 := by
  rw [hexEncodeLength, h]

-- ── Corollary: each output character is a valid hex digit ────────────────────

def isHexChar (c : Char) : Bool :=
  ('0' ≤ c && c ≤ '9') || ('a' ≤ c && c ≤ 'f')

theorem hexChar_is_hex (n : UInt8) : isHexChar (hexChar n) = true := by
  simp [hexChar, isHexChar]
  fin_cases n <;> decide

theorem byteToHex_chars (b : UInt8) :
    ∀ c ∈ (byteToHex b).toList, isHexChar c = true := by
  intro c hc
  simp [byteToHex, String.toList] at hc
  rcases hc with rfl | rfl
  · exact hexChar_is_hex _
  · exact hexChar_is_hex _

/**************************************************************************/
/*  cassie_stroke_packet.h                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

// CassieStrokePacket — fixed-shape, byte-deterministic serializer for the
// raw input samples xr-grid broadcasts over the WebTransport "fabric"
// reliable channel. Each peer decodes the same bytes and reruns Beautify
// locally; bit-determinism guarantees the resulting FinalStroke / graph
// state is identical across clients.
//
// Wire format (all integers little-endian):
//   [magic u32 = 'CSP1'][peer_id u32][stroke_id u32][seq u16]
//   [sample_count u16][closed_flag u8][reserved u8]
//   // then sample_count × (pos.x f32, pos.y f32, pos.z f32, pressure f32)
//
// creation_time is NOT on the wire: receiver reconstructs it as
// (sample_index × sample_dt) so wall-clock drift between peers does not
// leak into Beautify input.
//
// f32 pressure (not f16) because a single byte-flip must be reproducible
// bit-exactly inside the determinism test.
//
// Magic 'CSP1' = 0x31_50_53_43 little-endian (CSP1 ASCII).

class CassieStrokePacket : public RefCounted {
	GDCLASS(CassieStrokePacket, RefCounted);

protected:
	static void _bind_methods();

public:
	CassieStrokePacket() = default;

	static constexpr uint32_t MAGIC = 0x31'50'53'43u; // 'CSP1' LE
	static constexpr int HEADER_BYTES = 4 + 4 + 4 + 2 + 2 + 1 + 1; // 18
	static constexpr int SAMPLE_BYTES = 4 * 4; // pos xyz + pressure

	// Encode a sample run into a PackedByteArray. ints are taken as i64
	// to play nice with GDScript; we truncate to u32/u16 in the body.
	// `positions` and `pressures` must be the same length.
	static PackedByteArray encode(int64_t p_peer_id, int64_t p_stroke_id,
			int64_t p_seq, bool p_closed,
			const PackedVector3Array &p_positions,
			const PackedFloat32Array &p_pressures);

	// Decode a PackedByteArray. Result dict:
	//   ok          : bool
	//   peer_id     : int (uint32 widened)
	//   stroke_id   : int
	//   seq         : int
	//   closed      : bool
	//   positions   : PackedVector3Array
	//   pressures   : PackedFloat32Array
	// Returns ok=false with empty arrays on magic mismatch / size mismatch.
	static Dictionary decode(const PackedByteArray &p_bytes);
};

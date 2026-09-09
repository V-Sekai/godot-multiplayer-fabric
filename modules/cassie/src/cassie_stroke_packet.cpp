/**************************************************************************/
/*  cassie_stroke_packet.cpp                                              */
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

#include "cassie_stroke_packet.h"

#include "core/object/class_db.h"

namespace {

inline void write_u8(PackedByteArray &p_buf, int p_off, uint8_t p_v) {
	p_buf.write[p_off] = p_v;
}
inline void write_u16(PackedByteArray &p_buf, int p_off, uint16_t p_v) {
	p_buf.write[p_off + 0] = uint8_t(p_v & 0xFFu);
	p_buf.write[p_off + 1] = uint8_t((p_v >> 8) & 0xFFu);
}
inline void write_u32(PackedByteArray &p_buf, int p_off, uint32_t p_v) {
	p_buf.write[p_off + 0] = uint8_t(p_v & 0xFFu);
	p_buf.write[p_off + 1] = uint8_t((p_v >> 8) & 0xFFu);
	p_buf.write[p_off + 2] = uint8_t((p_v >> 16) & 0xFFu);
	p_buf.write[p_off + 3] = uint8_t((p_v >> 24) & 0xFFu);
}
inline void write_f32(PackedByteArray &p_buf, int p_off, float p_v) {
	uint32_t bits;
	memcpy(&bits, &p_v, sizeof(uint32_t));
	write_u32(p_buf, p_off, bits);
}

inline uint8_t read_u8(const PackedByteArray &p_buf, int p_off) {
	return uint8_t(p_buf[p_off]);
}
inline uint16_t read_u16(const PackedByteArray &p_buf, int p_off) {
	return uint16_t(uint8_t(p_buf[p_off])) |
			(uint16_t(uint8_t(p_buf[p_off + 1])) << 8);
}
inline uint32_t read_u32(const PackedByteArray &p_buf, int p_off) {
	return uint32_t(uint8_t(p_buf[p_off])) |
			(uint32_t(uint8_t(p_buf[p_off + 1])) << 8) |
			(uint32_t(uint8_t(p_buf[p_off + 2])) << 16) |
			(uint32_t(uint8_t(p_buf[p_off + 3])) << 24);
}
inline float read_f32(const PackedByteArray &p_buf, int p_off) {
	uint32_t bits = read_u32(p_buf, p_off);
	float v;
	memcpy(&v, &bits, sizeof(float));
	return v;
}

} // namespace

PackedByteArray CassieStrokePacket::encode(int64_t p_peer_id, int64_t p_stroke_id,
		int64_t p_seq, bool p_closed,
		const PackedVector3Array &p_positions,
		const PackedFloat32Array &p_pressures) {
	PackedByteArray out;
	const int n = MIN(p_positions.size(), p_pressures.size());
	const int total = HEADER_BYTES + n * SAMPLE_BYTES;
	out.resize(total);

	int off = 0;
	write_u32(out, off, MAGIC);
	off += 4;
	write_u32(out, off, uint32_t(p_peer_id));
	off += 4;
	write_u32(out, off, uint32_t(p_stroke_id));
	off += 4;
	write_u16(out, off, uint16_t(p_seq));
	off += 2;
	write_u16(out, off, uint16_t(n));
	off += 2;
	write_u8(out, off, p_closed ? uint8_t(1) : uint8_t(0));
	off += 1;
	write_u8(out, off, uint8_t(0));
	off += 1; // reserved

	for (int i = 0; i < n; ++i) {
		const Vector3 &p = p_positions[i];
		write_f32(out, off, float(p.x));
		off += 4;
		write_f32(out, off, float(p.y));
		off += 4;
		write_f32(out, off, float(p.z));
		off += 4;
		write_f32(out, off, p_pressures[i]);
		off += 4;
	}
	return out;
}

Dictionary CassieStrokePacket::decode(const PackedByteArray &p_bytes) {
	Dictionary r;
	r["ok"] = false;
	r["peer_id"] = 0;
	r["stroke_id"] = 0;
	r["seq"] = 0;
	r["closed"] = false;
	r["positions"] = PackedVector3Array();
	r["pressures"] = PackedFloat32Array();

	if (p_bytes.size() < HEADER_BYTES) {
		return r;
	}
	if (read_u32(p_bytes, 0) != MAGIC) {
		return r;
	}

	int off = 4;
	const uint32_t peer_id = read_u32(p_bytes, off);
	off += 4;
	const uint32_t stroke_id = read_u32(p_bytes, off);
	off += 4;
	const uint16_t seq = read_u16(p_bytes, off);
	off += 2;
	const uint16_t sample_count = read_u16(p_bytes, off);
	off += 2;
	const uint8_t closed_flag = read_u8(p_bytes, off);
	off += 1;
	off += 1; // reserved

	const int expected = HEADER_BYTES + int(sample_count) * SAMPLE_BYTES;
	if (p_bytes.size() < expected) {
		return r;
	}

	PackedVector3Array positions;
	PackedFloat32Array pressures;
	positions.resize(sample_count);
	pressures.resize(sample_count);
	for (int i = 0; i < int(sample_count); ++i) {
		const float x = read_f32(p_bytes, off);
		off += 4;
		const float y = read_f32(p_bytes, off);
		off += 4;
		const float z = read_f32(p_bytes, off);
		off += 4;
		const float pr = read_f32(p_bytes, off);
		off += 4;
		positions.write[i] = Vector3(x, y, z);
		pressures.write[i] = pr;
	}

	r["ok"] = true;
	r["peer_id"] = int64_t(peer_id);
	r["stroke_id"] = int64_t(stroke_id);
	r["seq"] = int64_t(seq);
	r["closed"] = closed_flag != 0;
	r["positions"] = positions;
	r["pressures"] = pressures;
	return r;
}

void CassieStrokePacket::_bind_methods() {
	ClassDB::bind_static_method("CassieStrokePacket",
			D_METHOD("encode", "peer_id", "stroke_id", "seq",
					"closed", "positions", "pressures"),
			&CassieStrokePacket::encode);
	ClassDB::bind_static_method("CassieStrokePacket",
			D_METHOD("decode", "bytes"),
			&CassieStrokePacket::decode);
}

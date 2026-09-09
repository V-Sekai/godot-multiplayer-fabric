/**************************************************************************/
/*  test_xr_grid_wire.h                                                   */
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

#include "../src/xr_grid_bool_timer.h"
#include "../src/xr_grid_entity_packet.h"
#include "../src/xr_grid_swing_twist_codec.h"
#include "../src/xr_grid_zone_scene_tree.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "tests/test_macros.h"

namespace TestXRGridWire {

TEST_CASE("[XRGrid][EntityPacket] round-trip preserves position + envelope") {
	const Vector3 pos(0.125, 1.5625, -2.21875);
	const Vector3 vel(0.5, -0.25, 0.125);
	const Quaternion rot(0.1f, 0.2f, 0.3f, 0.9272486f); // normalized
	const PackedByteArray enc = XRGridEntityPacket::encode(
			0xCAFEBABE, pos, vel, rot.normalized(),
			/*entity_class=*/3, /*owner_id=*/0xBEEF,
			/*hlc_frame=*/0x123456, /*hlc_counter=*/0x42,
			/*sub_index=*/2);
	REQUIRE_EQ(enc.size(), XRGridEntityPacket::PACKET_SIZE);

	const Dictionary dec = XRGridEntityPacket::decode(enc);
	REQUIRE_FALSE(dec.is_empty());
	CHECK_EQ(int64_t(dec.get("global_id", -1)), int64_t(0xCAFEBABE));
	CHECK_EQ(int(dec.get("entity_class", -1)), 3);
	CHECK_EQ(int(dec.get("owner_id", -1)), 0xBEEF);
	CHECK_EQ(int(dec.get("sub_index", -1)), 2);
	CHECK_EQ(int(dec.get("hlc_frame", -1)), 0x123456);
	CHECK_EQ(int(dec.get("hlc_counter", -1)), 0x42);

	const Vector3 dec_pos = dec.get("position", Vector3());
	CHECK(Math::is_equal_approx(dec_pos.x, pos.x));
	CHECK(Math::is_equal_approx(dec_pos.y, pos.y));
	CHECK(Math::is_equal_approx(dec_pos.z, pos.z));

	// Velocity is quantized × 1000 to i16 → ±0.001 round-trip error.
	const Vector3 dec_vel = dec.get("velocity", Vector3());
	CHECK(Math::abs(dec_vel.x - vel.x) < 0.0011);
	CHECK(Math::abs(dec_vel.y - vel.y) < 0.0011);
	CHECK(Math::abs(dec_vel.z - vel.z) < 0.0011);
}

TEST_CASE("[XRGrid][EntityPacket] decode rejects undersized buffers") {
	PackedByteArray short_buf;
	short_buf.resize(50);
	const Dictionary dec = XRGridEntityPacket::decode(short_buf);
	CHECK(dec.is_empty());
}

TEST_CASE("[XRGrid][EntityPacket] encode is bit-stable across two runs") {
	const Vector3 pos(7.0, 8.0, 9.0);
	const Vector3 vel(0.0, 0.0, 0.0);
	const Quaternion rot(0, 0, 0, 1);
	const PackedByteArray a = XRGridEntityPacket::encode(
			42, pos, vel, rot, 1, 7, 0, 0, 1);
	const PackedByteArray b = XRGridEntityPacket::encode(
			42, pos, vel, rot, 1, 7, 0, 0, 1);
	REQUIRE_EQ(a.size(), b.size());
	CHECK(memcmp(a.ptr(), b.ptr(), a.size()) == 0);
}

TEST_CASE("[XRGrid][SwingTwistCodec] quaternion round-trip under quantization") {
	// Several axis-and-angle rotations; quantization error stays under
	// QUANTIZE_SCALE-driven bound (~π/32767 ≈ 1e-4 rad).
	const Quaternion cases[5] = {
		Quaternion(Vector3(1, 0, 0).normalized(), 0.5f),
		Quaternion(Vector3(0, 1, 0).normalized(), -1.0f),
		Quaternion(Vector3(0, 0, 1).normalized(), 1.5f),
		Quaternion(Vector3(0.5f, 0.5f, 0.5f).normalized(), 0.7f),
		Quaternion(0, 0, 0, 1),
	};
	for (int i = 0; i < 5; ++i) {
		const Quaternion q = cases[i].normalized();
		const PackedInt32Array enc =
				XRGridSwingTwistCodec::encode_rotation_i16(q);
		REQUIRE_EQ(enc.size(), 3);
		const Quaternion dec = XRGridSwingTwistCodec::decode_rotation_i16(
				enc[0], enc[1], enc[2]);
		// Compare via dot — sign-flipped quaternions encode the same
		// rotation, so |dot| close to 1 is the right invariant.
		const double dot_abs = Math::abs(double(q.dot(dec)));
		CHECK(dot_abs > 0.9995);
	}
}

TEST_CASE("[XRGrid][BoolTimer] set_true holds true; reset releases") {
	Ref<XRGridBoolTimer> t;
	t.instantiate();
	CHECK_FALSE(t->get_value());
	t->set_true(2.0);
	CHECK(t->get_value());
	t->reset();
	CHECK_FALSE(t->get_value());
}

TEST_CASE("[XRGrid][SceneTree] zone scene tree class is registered and can be instantiated") {
	// Boot path: `godot --main-loop XRGridZoneSceneTree`. The engine
	// looks up the class via ClassDB::instantiate, so being absent
	// from ClassDB silently breaks the boot. Confirm the class is
	// known and instantiable here.
	CHECK(ClassDB::class_exists("XRGridZoneSceneTree"));
	CHECK(ClassDB::is_parent_class("XRGridZoneSceneTree", "SceneTree"));
	// Instantiate via ClassDB to exercise the same path the engine
	// uses on --main-loop boot, then free directly (this skips the
	// initialize() server-spinup path, which needs WebTransportPeer
	// + Crypto and isn't safe to run inside the unit-test runner).
	Object *obj = ClassDB::instantiate("XRGridZoneSceneTree");
	REQUIRE(obj != nullptr);
	XRGridZoneSceneTree *st = Object::cast_to<XRGridZoneSceneTree>(obj);
	REQUIRE(st != nullptr);
	CHECK_EQ(st->get_zone_port(), 9000);
	CHECK_EQ(st->get_relay_count(), int64_t(0));
	CHECK_FALSE(st->is_running());
	memdelete(obj);
}

} // namespace TestXRGridWire

/**************************************************************************/
/*  test_cassie_beautify_determinism.h                                    */
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

#include "../src/cassie_beautifier.h"
#include "../src/cassie_beautifier_params.h"
#include "../src/cassie_sketcher.h"
#include "../src/cassie_stroke_packet.h"
#include "../src/sketch/cassie_input_stroke.h"
#include "../src/sketch/cassie_sketch_graph.h"

#include "tests/test_macros.h"

namespace TestCassieBeautifyDeterminism {

// Serialize a Curve3D's control points + handles into a stable byte
// blob suitable for hash + memcmp comparisons.
static PackedByteArray _serialize_curve(const Ref<Curve3D> &p_curve) {
	PackedByteArray out;
	if (p_curve.is_null()) {
		return out;
	}
	const int n = p_curve->get_point_count();
	out.resize(n * 9 * 4); // 9 floats per anchor (pos + in + out), 4 bytes each
	int off = 0;
	for (int i = 0; i < n; ++i) {
		const Vector3 p = p_curve->get_point_position(i);
		const Vector3 in = p_curve->get_point_in(i);
		const Vector3 out_h = p_curve->get_point_out(i);
		const float v[9] = {
			float(p.x), float(p.y), float(p.z),
			float(in.x), float(in.y), float(in.z),
			float(out_h.x), float(out_h.y), float(out_h.z)
		};
		for (int k = 0; k < 9; ++k) {
			uint32_t bits;
			memcpy(&bits, &v[k], sizeof(uint32_t));
			out.write[off + 0] = uint8_t(bits & 0xFFu);
			out.write[off + 1] = uint8_t((bits >> 8) & 0xFFu);
			out.write[off + 2] = uint8_t((bits >> 16) & 0xFFu);
			out.write[off + 3] = uint8_t((bits >> 24) & 0xFFu);
			off += 4;
		}
	}
	return out;
}

// Build a hand-coded InputStroke. creation_time is sample_index * dt
// (the determinism contract), NOT wall-clock.
static Ref<CassieInputStroke> _make_stroke(const PackedVector3Array &p_pts,
		float p_dt = 1.0f / 60.0f, float p_pressure = 0.5f) {
	Ref<CassieInputStroke> s;
	s.instantiate();
	for (int i = 0; i < p_pts.size(); ++i) {
		s->add_sample(p_pts[i], float(i) * p_dt, p_pressure);
	}
	return s;
}

TEST_CASE("[Cassie][BeautifyDeterminism] single stroke beautified twice produces identical bytes") {
	Ref<CassieBeautifierParams> params;
	params.instantiate();
	// Lower the validity threshold so a short hand-coded stroke passes.
	params->set_min_sketching_time(0.0f);
	params->set_min_stroke_size(0.0f);

	PackedVector3Array pts;
	pts.push_back(Vector3(0, 0, 0));
	pts.push_back(Vector3(0.1f, 0.02f, 0));
	pts.push_back(Vector3(0.2f, 0.03f, 0));
	pts.push_back(Vector3(0.3f, 0.02f, 0));
	pts.push_back(Vector3(0.4f, 0.0f, 0));

	Ref<CassieInputStroke> s_a = _make_stroke(pts);
	Ref<CassieInputStroke> s_b = _make_stroke(pts);

	Ref<CassieBeautifier> b;
	b.instantiate();
	Ref<CassieSketchContext> ctx;
	ctx.instantiate();

	const Dictionary r_a = b->beautify(s_a, ctx, params, false, false);
	const Dictionary r_b = b->beautify(s_b, ctx, params, false, false);

	CHECK(bool(r_a.get("is_valid", false)));
	CHECK(bool(r_b.get("is_valid", false)));

	Ref<Curve3D> c_a = r_a.get("curve", Variant());
	Ref<Curve3D> c_b = r_b.get("curve", Variant());
	REQUIRE(c_a.is_valid());
	REQUIRE(c_b.is_valid());

	const PackedByteArray blob_a = _serialize_curve(c_a);
	const PackedByteArray blob_b = _serialize_curve(c_b);
	CHECK_EQ(blob_a.size(), blob_b.size());
	if (blob_a.size() == blob_b.size()) {
		CHECK(memcmp(blob_a.ptr(), blob_b.ptr(), blob_a.size()) == 0);
	}
}

TEST_CASE("[Cassie][BeautifyDeterminism] CassieStrokePacket round-trips bit-exactly") {
	PackedVector3Array positions;
	positions.push_back(Vector3(0.0f, 1.5f, -2.25f));
	positions.push_back(Vector3(0.125f, 1.5625f, -2.21875f));
	positions.push_back(Vector3(0.25f, 1.625f, -2.1875f));
	PackedFloat32Array pressures;
	pressures.push_back(0.0f);
	pressures.push_back(0.5f);
	pressures.push_back(1.0f);

	const PackedByteArray enc = CassieStrokePacket::encode(
			0xDEADBEEFu, 7u, 42u, true, positions, pressures);
	const Dictionary dec = CassieStrokePacket::decode(enc);
	CHECK(bool(dec.get("ok", false)));
	CHECK_EQ(int64_t(dec.get("peer_id", -1)), int64_t(0xDEADBEEFu));
	CHECK_EQ(int64_t(dec.get("stroke_id", -1)), int64_t(7));
	CHECK_EQ(int64_t(dec.get("seq", -1)), int64_t(42));
	CHECK(bool(dec.get("closed", false)));
	const PackedVector3Array out_p = dec.get("positions", PackedVector3Array());
	const PackedFloat32Array out_pr = dec.get("pressures", PackedFloat32Array());
	REQUIRE_EQ(out_p.size(), positions.size());
	REQUIRE_EQ(out_pr.size(), pressures.size());
	for (int i = 0; i < positions.size(); ++i) {
		CHECK_EQ(out_p[i], positions[i]);
		CHECK_EQ(out_pr[i], pressures[i]);
	}

	// Second encode of the decoded values must equal the first byte-for-byte.
	const PackedByteArray enc2 = CassieStrokePacket::encode(
			uint32_t(int64_t(dec.get("peer_id", 0))),
			uint32_t(int64_t(dec.get("stroke_id", 0))),
			uint16_t(int64_t(dec.get("seq", 0))),
			bool(dec.get("closed", false)),
			out_p, out_pr);
	REQUIRE_EQ(enc.size(), enc2.size());
	CHECK(memcmp(enc.ptr(), enc2.ptr(), enc.size()) == 0);
}

TEST_CASE("[Cassie][BeautifyDeterminism] packet malformed input rejects safely") {
	PackedByteArray bad;
	bad.push_back(uint8_t(0));
	bad.push_back(uint8_t(0));
	bad.push_back(uint8_t(0));
	bad.push_back(uint8_t(0));
	const Dictionary dec = CassieStrokePacket::decode(bad);
	CHECK_FALSE(bool(dec.get("ok", true)));
}

TEST_CASE("[Cassie][BeautifyDeterminism] CassieSketcher local vs remote replay agree") {
	Ref<CassieBeautifierParams> params;
	params.instantiate();
	params->set_min_sketching_time(0.0f);
	params->set_min_stroke_size(0.0f);

	CassieSketcher local;
	CassieSketcher remote;
	local.set_beautifier_params(params);
	remote.set_beautifier_params(params);

	const int sid = local.begin_stroke(Vector3(0, 0, 0), 0.5f);
	local.add_sample(sid, Vector3(0.1f, 0.02f, 0), 0.5f);
	local.add_sample(sid, Vector3(0.2f, 0.03f, 0), 0.5f);
	local.add_sample(sid, Vector3(0.3f, 0.02f, 0), 0.5f);
	local.add_sample(sid, Vector3(0.4f, 0.0f, 0), 0.5f);
	const Dictionary r_local = local.commit_stroke(sid);
	REQUIRE(bool(r_local.get("ok", false)));

	const PackedByteArray packet = local.encode_stroke_packet(sid);
	REQUIRE(packet.size() > 0);

	const Dictionary r_remote = remote.apply_remote_samples(packet);
	REQUIRE(bool(r_remote.get("ok", false)));

	Ref<CassieFinalStroke> fs_local = r_local.get("final_stroke", Variant());
	Ref<CassieFinalStroke> fs_remote = r_remote.get("final_stroke", Variant());
	REQUIRE(fs_local.is_valid());
	REQUIRE(fs_remote.is_valid());

	const PackedByteArray blob_a = _serialize_curve(fs_local->get_curve());
	const PackedByteArray blob_b = _serialize_curve(fs_remote->get_curve());
	REQUIRE_EQ(blob_a.size(), blob_b.size());
	CHECK(memcmp(blob_a.ptr(), blob_b.ptr(), blob_a.size()) == 0);
}

TEST_CASE("[Cassie][BeautifyDeterminism] find_cycles emits insertion-order-invariant output") {
	// Triangle from three polylines: order A→B→C vs B→C→A must produce
	// the same canonical cycle list.
	Ref<CassieSketchGraph> graph_a;
	graph_a.instantiate();
	Ref<CassieSketchGraph> graph_b;
	graph_b.instantiate();

	PackedVector3Array e_ab;
	e_ab.push_back(Vector3(0, 0, 0));
	e_ab.push_back(Vector3(1, 0, 0));
	PackedVector3Array e_bc;
	e_bc.push_back(Vector3(1, 0, 0));
	e_bc.push_back(Vector3(0.5f, 1, 0));
	PackedVector3Array e_ca;
	e_ca.push_back(Vector3(0.5f, 1, 0));
	e_ca.push_back(Vector3(0, 0, 0));
	PackedVector3Array empty_n;

	graph_a->add_stroke(e_ab, empty_n);
	graph_a->add_stroke(e_bc, empty_n);
	graph_a->add_stroke(e_ca, empty_n);

	graph_b->add_stroke(e_bc, empty_n);
	graph_b->add_stroke(e_ca, empty_n);
	graph_b->add_stroke(e_ab, empty_n);

	const Array cycles_a = graph_a->find_cycles();
	const Array cycles_b = graph_b->find_cycles();
	REQUIRE_EQ(cycles_a.size(), cycles_b.size());

	// After canonical sort, the sorted-signature of each entry should match.
	for (int i = 0; i < cycles_a.size(); ++i) {
		PackedInt32Array ca = cycles_a[i];
		PackedInt32Array cb = cycles_b[i];
		PackedInt32Array sa = ca;
		PackedInt32Array sb = cb;
		sa.sort();
		sb.sort();
		REQUIRE_EQ(sa.size(), sb.size());
		for (int j = 0; j < sa.size(); ++j) {
			CHECK_EQ(sa[j], sb[j]);
		}
	}
}

} // namespace TestCassieBeautifyDeterminism

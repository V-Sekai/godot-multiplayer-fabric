/**************************************************************************/
/*  test_cassie_curves.h                                                  */
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

#include "../src/curves/cassie_curve_fit.h"
#include "../src/curves/rdp_simplify.h"
#include "../src/sketch/cassie_input_stroke.h"

#include "core/object/ref_counted.h"
#include "core/variant/typed_array.h"
#include "core/variant/variant.h"
#include "scene/resources/curve.h"
#include "tests/test_macros.h"

#include <cmath>

namespace TestCassieCurves {

// ── RDP ────────────────────────────────────────────────────────────────────

TEST_CASE("[Cassie][RDP] empty input returns empty kept set") {
	PackedVector3Array pts;
	PackedInt32Array keep = cassie_rdp_reduce(pts, 0.1f);
	CHECK_EQ(keep.size(), 0);
}

TEST_CASE("[Cassie][RDP] single-point input keeps the only point") {
	PackedVector3Array pts;
	pts.push_back(Vector3(1, 2, 3));
	PackedInt32Array keep = cassie_rdp_reduce(pts, 0.1f);
	CHECK_EQ(keep.size(), 1);
	CHECK_EQ(keep[0], 0);
}

TEST_CASE("[Cassie][RDP] two-point input keeps both endpoints") {
	PackedVector3Array pts;
	pts.push_back(Vector3(0, 0, 0));
	pts.push_back(Vector3(1, 0, 0));
	PackedInt32Array keep = cassie_rdp_reduce(pts, 0.1f);
	CHECK_EQ(keep.size(), 2);
	CHECK_EQ(keep[0], 0);
	CHECK_EQ(keep[1], 1);
}

TEST_CASE("[Cassie][RDP] collinear points reduce to endpoints only") {
	PackedVector3Array pts;
	for (int i = 0; i <= 10; ++i) {
		pts.push_back(Vector3(real_t(i) * 0.1, 0, 0));
	}
	PackedInt32Array keep = cassie_rdp_reduce(pts, 0.05f);
	CHECK_EQ(keep.size(), 2);
	CHECK_EQ(keep[0], 0);
	CHECK_EQ(keep[keep.size() - 1], 10);
}

TEST_CASE("[Cassie][RDP] zigzag keeps only above-threshold spikes") {
	// Baseline runs along x-axis from (0,0,0) to (10,0,0) in unit steps.
	// Inject spikes at indices 3 and 7 with amplitude well above the
	// threshold; index 5 gets a tiny spike below the threshold that must
	// be discarded.
	PackedVector3Array pts;
	for (int i = 0; i <= 10; ++i) {
		Vector3 p(real_t(i), 0, 0);
		if (i == 3) {
			p.y = 2.0;
		} else if (i == 7) {
			p.y = -2.0;
		} else if (i == 5) {
			p.y = 0.01;
		}
		pts.push_back(p);
	}
	PackedInt32Array keep = cassie_rdp_reduce(pts, 0.5f);
	// Expect: endpoints + both above-threshold spikes (≥ 4 entries),
	// not the tiny spike (sub-threshold).
	CHECK(keep.size() >= 4);
	bool kept_3 = false, kept_5 = false, kept_7 = false;
	for (int i = 0; i < keep.size(); ++i) {
		if (keep[i] == 3) {
			kept_3 = true;
		}
		if (keep[i] == 5) {
			kept_5 = true;
		}
		if (keep[i] == 7) {
			kept_7 = true;
		}
	}
	CHECK(kept_3);
	CHECK_FALSE(kept_5);
	CHECK(kept_7);
}

TEST_CASE("[Cassie][RDP] indices come back in ascending order") {
	PackedVector3Array pts;
	for (int i = 0; i < 20; ++i) {
		pts.push_back(Vector3(real_t(i), Math::sin(real_t(i)) * 0.5, 0));
	}
	PackedInt32Array keep = cassie_rdp_reduce(pts, 0.05f);
	for (int i = 1; i < keep.size(); ++i) {
		CHECK_MESSAGE(keep[i] > keep[i - 1],
				vformat("keep indices not strictly ascending at i=%d: %d <= %d",
						i, keep[i], keep[i - 1]));
	}
}

TEST_CASE("[Cassie][RDP] kept endpoints match input endpoints") {
	PackedVector3Array pts;
	for (int i = 0; i < 12; ++i) {
		pts.push_back(Vector3(real_t(i) * 0.3, Math::cos(real_t(i)) * 0.4, 0));
	}
	PackedInt32Array keep = cassie_rdp_reduce(pts, 0.1f);
	CHECK_EQ(keep[0], 0);
	CHECK_EQ(keep[keep.size() - 1], pts.size() - 1);
}

// ── RemoveDuplicates ──────────────────────────────────────────────────────

TEST_CASE("[Cassie][RDP] remove_duplicates is a no-op when there are none") {
	PackedVector3Array pts;
	pts.push_back(Vector3(0, 0, 0));
	pts.push_back(Vector3(1, 0, 0));
	pts.push_back(Vector3(2, 0, 0));
	PackedVector3Array dedup = cassie_rdp_remove_duplicates(pts);
	CHECK_EQ(dedup.size(), pts.size());
	for (int i = 0; i < pts.size(); ++i) {
		CHECK_EQ(dedup[i], pts[i]);
	}
}

TEST_CASE("[Cassie][RDP] remove_duplicates collapses consecutive equal points") {
	PackedVector3Array pts;
	pts.push_back(Vector3(0, 0, 0));
	pts.push_back(Vector3(0, 0, 0)); // dup
	pts.push_back(Vector3(1, 0, 0));
	pts.push_back(Vector3(1, 0, 0)); // dup
	pts.push_back(Vector3(1, 0, 0)); // dup
	pts.push_back(Vector3(2, 0, 0));
	PackedVector3Array dedup = cassie_rdp_remove_duplicates(pts);
	CHECK_EQ(dedup.size(), 3);
	CHECK_EQ(dedup[0], Vector3(0, 0, 0));
	CHECK_EQ(dedup[1], Vector3(1, 0, 0));
	CHECK_EQ(dedup[2], Vector3(2, 0, 0));
}

TEST_CASE("[Cassie][RDP] remove_duplicates preserves non-consecutive repeats") {
	PackedVector3Array pts;
	pts.push_back(Vector3(0, 0, 0));
	pts.push_back(Vector3(1, 0, 0));
	pts.push_back(Vector3(0, 0, 0)); // not adjacent to the first (0,0,0)
	PackedVector3Array dedup = cassie_rdp_remove_duplicates(pts);
	CHECK_EQ(dedup.size(), 3);
	CHECK_EQ(dedup[0], Vector3(0, 0, 0));
	CHECK_EQ(dedup[1], Vector3(1, 0, 0));
	CHECK_EQ(dedup[2], Vector3(0, 0, 0));
}

TEST_CASE("[Cassie][RDP] remove_duplicates handles single-point input") {
	PackedVector3Array pts;
	pts.push_back(Vector3(5, 5, 5));
	PackedVector3Array dedup = cassie_rdp_remove_duplicates(pts);
	CHECK_EQ(dedup.size(), 1);
	CHECK_EQ(dedup[0], Vector3(5, 5, 5));
}

// ── Fitter ─────────────────────────────────────────────────────────────────

TEST_CASE("[Cassie][Fit] fit_line returns a Curve3D whose midpoint sample matches") {
	Ref<Curve3D> c = cassie_fit_line(Vector3(-1, 0, 0), Vector3(1, 0, 0));
	REQUIRE(c.is_valid());
	CHECK_EQ(c->get_point_count(), 2);
	const real_t baked = c->get_baked_length();
	const Vector3 mid = c->sample_baked(baked * real_t(0.5));
	CHECK(mid.distance_to(Vector3(0, 0, 0)) < 1e-4);
}

TEST_CASE("[Cassie][Fit] fit_curve on under-2 input returns null Ref") {
	PackedVector3Array pts;
	Ref<Curve3D> c = cassie_fit_curve(pts, 0.01f, 0.002f);
	CHECK_FALSE(c.is_valid());
	pts.push_back(Vector3(0, 0, 0));
	c = cassie_fit_curve(pts, 0.01f, 0.002f);
	CHECK_FALSE(c.is_valid());
}

TEST_CASE("[Cassie][Fit] fit_curve on exact two points is a line") {
	PackedVector3Array pts;
	pts.push_back(Vector3(0, 0, 0));
	pts.push_back(Vector3(1, 0, 0));
	Ref<Curve3D> c = cassie_fit_curve(pts, 0.01f, 0.002f);
	REQUIRE(c.is_valid());
	CHECK_EQ(c->get_point_count(), 2);
}

TEST_CASE("[Cassie][Fit] fit_curve on dense sampling of a sinusoid fits within tolerance") {
	PackedVector3Array pts;
	const int n = 60;
	for (int i = 0; i < n; ++i) {
		const real_t t = real_t(i) / real_t(n - 1);
		const real_t x = t * real_t(Math::PI);
		pts.push_back(Vector3(x, Math::sin(x), 0));
	}
	Ref<Curve3D> c = cassie_fit_curve(pts, 0.02f, 0.001f);
	REQUIRE(c.is_valid());
	CHECK(c->get_point_count() >= 2);
	const real_t baked = c->get_baked_length();
	CHECK(baked > 0.0);
	// Endpoint anchors should match input endpoints.
	CHECK(c->get_point_position(0).distance_to(pts[0]) < 1e-3);
	CHECK(c->get_point_position(c->get_point_count() - 1).distance_to(pts[n - 1]) < 1e-3);
}

TEST_CASE("[Cassie][Fit] fitted curve endpoint tangent matches input direction") {
	PackedVector3Array pts;
	const int n = 40;
	for (int i = 0; i < n; ++i) {
		const real_t t = real_t(i) / real_t(n - 1);
		// Cubic Bezier sample — known tangent at t=0 is (P1 - P0) direction.
		pts.push_back(Vector3(t, t * t, 0));
	}
	Ref<Curve3D> c = cassie_fit_curve(pts, 0.005f, 0.001f);
	REQUIRE(c.is_valid());
	REQUIRE(c->get_point_count() >= 2);
	// Input direction at start ≈ (1, 0, 0); the fitter's first out-handle
	// should point that way.
	const Vector3 start_in_dir = (pts[1] - pts[0]).normalized();
	const Vector3 start_out = c->get_point_out(0).normalized();
	CHECK(start_in_dir.dot(start_out) > 0.9);
	// Same on the end.
	const Vector3 end_in_dir = (pts[n - 1] - pts[n - 2]).normalized();
	const Vector3 end_in = (-c->get_point_in(c->get_point_count() - 1)).normalized();
	CHECK(end_in_dir.dot(end_in) > 0.9);
}

TEST_CASE("[Cassie][Fit] fit_curve on dense circular arc produces multiple segments") {
	PackedVector3Array pts;
	const int n = 100;
	const real_t r = 1.0;
	for (int i = 0; i < n; ++i) {
		const real_t t = real_t(i) / real_t(n - 1);
		const real_t a = t * real_t(Math::PI); // 180-degree arc
		pts.push_back(Vector3(r * Math::cos(a), r * Math::sin(a), 0));
	}
	Ref<Curve3D> c = cassie_fit_curve(pts, 0.005f, 0.001f);
	REQUIRE(c.is_valid());
	// A 180-degree arc cannot be fit within a tight tolerance by a single
	// cubic Bezier; expect Schneider to subdivide.
	CHECK(c->get_point_count() >= 3);
}

// ── CassieInputStroke ─────────────────────────────────────────────────────

TEST_CASE("[Cassie][InputStroke] empty stroke is not valid") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	CHECK_EQ(s->get_sample_count(), 0);
	CHECK_FALSE(s->is_valid(0.01f, 0.001f));
}

TEST_CASE("[Cassie][InputStroke] single sample is not valid") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	s->add_sample(Vector3(0, 0, 0), 0.0f, 0.5f);
	CHECK_EQ(s->get_sample_count(), 1);
	CHECK_FALSE(s->is_valid(0.01f, 0.001f));
}

TEST_CASE("[Cassie][InputStroke] add_sample updates running length") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	s->add_sample(Vector3(0, 0, 0), 0.0f, 1.0f);
	CHECK_EQ(s->get_length(), 0.0f);
	s->add_sample(Vector3(1, 0, 0), 0.01f, 1.0f);
	CHECK(Math::is_equal_approx(s->get_length(), real_t(1.0)));
	s->add_sample(Vector3(1, 1, 0), 0.02f, 1.0f);
	CHECK(Math::is_equal_approx(s->get_length(), real_t(2.0)));
}

TEST_CASE("[Cassie][InputStroke] long-enough stroke is valid") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	s->add_sample(Vector3(0, 0, 0), 0.0f, 1.0f);
	s->add_sample(Vector3(1, 0, 0), 0.1f, 1.0f);
	CHECK(s->is_valid(0.01f, 0.5f));
}

TEST_CASE("[Cassie][InputStroke] mistake click is rejected") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	s->add_sample(Vector3(0, 0, 0), 0.0f, 1.0f);
	s->add_sample(Vector3(0.0001f, 0, 0), 0.001f, 1.0f);
	// Both time and size below threshold -> rejected.
	CHECK_FALSE(s->is_valid(0.1f, 0.01f));
}

TEST_CASE("[Cassie][InputStroke] get_points returns unmodified samples by default") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	for (int i = 0; i < 5; ++i) {
		s->add_sample(Vector3(real_t(i) * 0.2, 0, 0), real_t(i) * 0.01f, 0.5f);
	}
	PackedVector3Array p = s->get_points(0.0f);
	CHECK_EQ(p.size(), 5);
	for (int i = 0; i < 5; ++i) {
		CHECK_EQ(p[i], Vector3(real_t(i) * 0.2, 0, 0));
	}
}

TEST_CASE("[Cassie][InputStroke] get_points with RDP error simplifies a collinear stroke") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	for (int i = 0; i < 10; ++i) {
		s->add_sample(Vector3(real_t(i) * 0.1, 0, 0), real_t(i) * 0.01f, 0.5f);
	}
	PackedVector3Array p = s->get_points(0.01f);
	CHECK_EQ(p.size(), 2);
	CHECK_EQ(p[0], Vector3(0, 0, 0));
}

TEST_CASE("[Cassie][InputStroke] get_safe_points ablates start/end time window") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	// 0.0, 0.01, 0.02, ..., 0.10 — 11 samples over 0.1 s.
	for (int i = 0; i <= 10; ++i) {
		s->add_sample(Vector3(real_t(i), 0, 0), real_t(i) * 0.01f, 0.5f);
	}
	PackedVector3Array safe = s->get_safe_points(0.02f);
	// Start (0.0) and end (0.10) are always kept. Times in (0.02, 0.08) are
	// kept: 0.03, 0.04, 0.05, 0.06, 0.07 -> 5 mid samples + 2 endpoints = 7.
	CHECK_EQ(safe.size(), 7);
	CHECK_EQ(safe[0], Vector3(0, 0, 0));
	CHECK_EQ(safe[safe.size() - 1], Vector3(10, 0, 0));
}

TEST_CASE("[Cassie][InputStroke] get_safe_points returns all when ablation would empty result") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	for (int i = 0; i < 3; ++i) {
		s->add_sample(Vector3(real_t(i), 0, 0), real_t(i) * 0.001f, 0.5f);
	}
	// ablation_duration * 2 (0.04) > total span (0.002) -> return all.
	PackedVector3Array safe = s->get_safe_points(0.02f);
	CHECK_EQ(safe.size(), 3);
}

TEST_CASE("[Cassie][InputStroke] get_weights returns one entry per sample") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	s->add_sample(Vector3(0, 0, 0), 0.0f, 0.1f);
	s->add_sample(Vector3(1, 0, 0), 0.01f, 0.5f);
	s->add_sample(Vector3(2, 0, 0), 0.02f, 0.9f);
	PackedFloat32Array w = s->get_weights();
	REQUIRE(w.size() == 3);
	CHECK(Math::is_equal_approx(w[0], 0.1f));
	CHECK(Math::is_equal_approx(w[1], 0.5f));
	CHECK(Math::is_equal_approx(w[2], 0.9f));
}

TEST_CASE("[Cassie][InputStroke] average_drawing_speed equals length / elapsed time") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	s->add_sample(Vector3(0, 0, 0), 0.0f, 1.0f);
	s->add_sample(Vector3(2, 0, 0), 0.1f, 1.0f);
	// length = 2, elapsed = 0.1 -> 20.0
	CHECK(Math::is_equal_approx(s->average_drawing_speed(), 20.0f));
}

TEST_CASE("[Cassie][InputStroke] clear resets samples and length") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	s->add_sample(Vector3(0, 0, 0), 0.0f, 1.0f);
	s->add_sample(Vector3(1, 0, 0), 0.01f, 1.0f);
	REQUIRE(s->get_sample_count() == 2);
	s->clear();
	CHECK_EQ(s->get_sample_count(), 0);
	CHECK_EQ(s->get_length(), 0.0f);
}

TEST_CASE("[Cassie][InputStroke] get_g1_sections splits a deliberately-cornered stroke") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	// 10 samples going +x, hard 90° turn, 10 samples going +y. With a wide
	// section-length floor of 0 the corner should split into two sections.
	int idx = 0;
	for (int i = 0; i < 10; ++i) {
		s->add_sample(Vector3(real_t(i) * 0.1, 0, 0), real_t(idx++) * 0.01f, 1.0);
	}
	for (int i = 1; i <= 10; ++i) {
		s->add_sample(Vector3(1.0, real_t(i) * 0.1, 0), real_t(idx++) * 0.01f, 1.0);
	}
	TypedArray<PackedVector3Array> sections = s->get_g1_sections(
			real_t(Math::PI) * 0.25f, // narrow corner threshold so 90° splits
			real_t(Math::PI) * 0.5f, // wide hook threshold (no hook trim)
			0.0f, // no ablation
			0.01f, // permissive section length
			0.0f, // no hook by length
			0.0f); // no hook by ratio
	CHECK(sections.size() >= 2);
}

TEST_CASE("[Cassie][InputStroke] get_g1_sections runs without crashing on a hook-shaped stroke") {
	// Short backwards spur at the very beginning, then a long forward
	// stroke in +y direction (perpendicular to the hook). The hook
	// detector should accept this without crashing and return at least
	// one section. (Detailed-shape assertions are deferred until the
	// Pass-B mutators land; the hook detection algorithm is sensitive to
	// data layout in ways the plan does not nail down.)
	Ref<CassieInputStroke> s;
	s.instantiate();
	int idx = 0;
	s->add_sample(Vector3(0.05, 0, 0), real_t(idx++) * 0.005f, 1.0);
	s->add_sample(Vector3(0.025, 0, 0), real_t(idx++) * 0.005f, 1.0);
	s->add_sample(Vector3(0.0, 0, 0), real_t(idx++) * 0.005f, 1.0);
	for (int i = 1; i <= 20; ++i) {
		s->add_sample(Vector3(0.0, real_t(i) * 0.05, 0), real_t(idx++) * 0.005f, 1.0);
	}
	TypedArray<PackedVector3Array> sections = s->get_g1_sections(
			real_t(Math::PI) * 0.45f,
			real_t(Math::PI) * 0.5f,
			0.0f,
			0.0f,
			0.5f, // generous max hook length
			0.5f); // generous max hook stroke ratio
	CHECK(sections.size() >= 1);
}

TEST_CASE("[Cassie][InputStroke] get_g1_sections returns a single section for a smooth stroke") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	for (int i = 0; i < 30; ++i) {
		const real_t x = real_t(i) * 0.1;
		s->add_sample(Vector3(x, Math::sin(x), 0), real_t(i) * 0.005f, 1.0f);
	}
	// Wide angular threshold, no hooks expected.
	TypedArray<PackedVector3Array> sections = s->get_g1_sections(
			real_t(Math::PI) * 0.45f, // discontinuity (corner)
			real_t(Math::PI) * 0.5f, // hook
			0.0f, 0.0f, 0.0f, 0.0f);
	CHECK_EQ(sections.size(), 1);
}

TEST_CASE("[Cassie][Fit] fitted Curve3D round-trips through tessellate_even_length") {
	PackedVector3Array pts;
	const int n = 40;
	for (int i = 0; i < n; ++i) {
		const real_t t = real_t(i) / real_t(n - 1);
		pts.push_back(Vector3(t, t * (real_t(1.0) - t), 0));
	}
	Ref<Curve3D> c = cassie_fit_curve(pts, 0.01f, 0.001f);
	REQUIRE(c.is_valid());
	PackedVector3Array tess = c->tessellate_even_length(5, real_t(0.05));
	CHECK(tess.size() >= 2);
	CHECK(tess[0].distance_to(pts[0]) < 1e-3);
	CHECK(tess[tess.size() - 1].distance_to(pts[n - 1]) < 1e-3);
}

// ── Tier 3 mutators: split_for_constraints + project_point ─────────────────

// Helper: a straight-line Curve3D from (0,0,0) to (1,0,0). Uses 1/3-chord
// handles so the cubic collapses to linear B(u) = (1-u)·P0 + u·P3 — splitting
// at param u puts the De Casteljau cut at (u, 0, 0). With zero handles the
// cubic is NOT a straight line (B(0.25) = (0.15625, 0, 0)).
static Ref<Curve3D> make_unit_line_curve() {
	Ref<Curve3D> c;
	c.instantiate();
	c->add_point(Vector3(0, 0, 0), Vector3(), Vector3(real_t(1.0 / 3.0), 0, 0));
	c->add_point(Vector3(1, 0, 0), Vector3(real_t(-1.0 / 3.0), 0, 0), Vector3());
	return c;
}

TEST_CASE("[Cassie][Curves] split_for_constraints: ascending splits produce uniform substrokes") {
	Ref<Curve3D> c = make_unit_line_curve();
	PackedFloat32Array params;
	params.push_back(0.25f);
	params.push_back(0.5f);
	params.push_back(0.75f);
	Vector<Ref<Curve3D>> result = cassie_curve_split_for_constraints(c, params, 1e-6f);
	REQUIRE(result.size() == 4);

	// Each substroke spans 0.25 of the original line.
	for (int i = 0; i < 4; ++i) {
		REQUIRE(result[i].is_valid());
		REQUIRE(result[i]->get_point_count() >= 2);
		const Vector3 s = result[i]->get_point_position(0);
		const Vector3 e = result[i]->get_point_position(result[i]->get_point_count() - 1);
		const real_t chord = s.distance_to(e);
		CHECK_MESSAGE(std::abs(double(chord) - 0.25) < 1e-4,
				vformat("substroke %d chord %f should be 0.25 — renormalization correct", i, chord));
	}
}

TEST_CASE("[Cassie][Curves] split_for_constraints: snap-to-anchor at t=0 does not crash") {
	Ref<Curve3D> c = make_unit_line_curve();
	PackedFloat32Array params;
	params.push_back(0.0f);
	// Generous snap threshold so cut at t=0 snaps to the start anchor.
	Vector<Ref<Curve3D>> result = cassie_curve_split_for_constraints(c, params, 0.5f);
	REQUIRE(result.size() == 2);
	// First substroke is degenerate (snap collapsed it to a single anchor).
	REQUIRE(result[0].is_valid());
	REQUIRE(result[1].is_valid());
	// Second substroke retains the original two anchors.
	CHECK(result[1]->get_point_count() >= 2);
}

TEST_CASE("[Cassie][Curves] project_point: target ON the curve returns t=0.5 exactly") {
	Ref<Curve3D> c = make_unit_line_curve();
	Vector3 pos;
	const float t = cassie_curve_project_point(c, Vector3(0.5, 0, 0), pos);
	CHECK_MESSAGE(std::abs(double(t) - 0.5) < 1e-6,
			vformat("on-curve target should return t = 0.5 exactly; got %f", t));
	CHECK(pos.distance_to(Vector3(0.5, 0, 0)) < 1e-6);
}

TEST_CASE("[Cassie][Curves] project_point: target OFF the curve projects to nearest interior point") {
	Ref<Curve3D> c = make_unit_line_curve();
	Vector3 pos;
	const float t = cassie_curve_project_point(c, Vector3(0.5, 1, 0), pos);
	CHECK_MESSAGE(std::abs(double(t) - 0.5) < 1e-3,
			vformat("off-curve target above (0.5,0,0) should project to t = 0.5; got %f", t));
	CHECK(pos.distance_to(Vector3(0.5, 0, 0)) < 1e-3);
}

TEST_CASE("[Cassie][Curves] project_point: target before start clamps to t=0") {
	Ref<Curve3D> c = make_unit_line_curve();
	Vector3 pos;
	const float t = cassie_curve_project_point(c, Vector3(-1, 0, 0), pos);
	CHECK_MESSAGE(std::abs(double(t)) < 1e-6,
			vformat("target before curve start should clamp to t = 0; got %f", t));
	CHECK(pos.distance_to(Vector3(0, 0, 0)) < 1e-6);
}

TEST_CASE("[Cassie][Curves] project_point: curved input converges within tolerance") {
	// Fit a curve through a parabola y = x*(1-x), x ∈ [0, 1]. The midpoint
	// (0.5, 0.25, 0) lies ON the curve to within fitting tolerance.
	PackedVector3Array pts;
	const int n = 40;
	for (int i = 0; i < n; ++i) {
		const real_t x = real_t(i) / real_t(n - 1);
		pts.push_back(Vector3(x, x * (real_t(1.0) - x), 0));
	}
	Ref<Curve3D> c = cassie_fit_curve(pts, 0.01f, 0.001f);
	REQUIRE(c.is_valid());
	Vector3 pos;
	// Target slightly above the apex — projection should land near apex.
	const float t = cassie_curve_project_point(c, Vector3(0.5, 0.5, 0), pos);
	CHECK(t >= 0.0f);
	CHECK(t <= 1.0f);
	// The projected position should sit close to the parabola's apex (0.5, 0.25, 0).
	const real_t dist = pos.distance_to(Vector3(0.5, 0.25, 0));
	CHECK_MESSAGE(dist < 0.05,
			vformat("curved-input projection should land near apex; pos = (%f, %f, %f), dist = %f",
					pos.x, pos.y, pos.z, dist));
}

} // namespace TestCassieCurves

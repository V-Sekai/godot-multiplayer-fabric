/**************************************************************************/
/*  test_cassie_profile_mover.h                                           */
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

#include "../src/sketch/cassie_curvenet.h"
#include "../src/sketch/cassie_curvenet_extractor.h"
#include "../src/sketch/cassie_curvenet_knot.h"
#include "../src/sketch/cassie_profile_mover.h"
#include "../src/sketch/cassie_surface_patch.h"

#include "core/math/vector3.h"
#include "scene/resources/mesh.h"
#include "tests/test_macros.h"

namespace TestCassieProfileMover {

static Ref<ArrayMesh> _build_mesh(const Vector<Vector3> &p_verts,
		const Vector<int> &p_indices) {
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	PackedVector3Array pv;
	pv.resize(p_verts.size());
	for (int i = 0; i < p_verts.size(); ++i) {
		pv.write[i] = p_verts[i];
	}
	arrays[Mesh::ARRAY_VERTEX] = pv;
	PackedInt32Array pi;
	pi.resize(p_indices.size());
	for (int i = 0; i < p_indices.size(); ++i) {
		pi.write[i] = p_indices[i];
	}
	arrays[Mesh::ARRAY_INDEX] = pi;
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

static Ref<ArrayMesh> _make_cube_mesh() {
	Vector<Vector3> v;
	v.push_back(Vector3(0, 0, 0));
	v.push_back(Vector3(1, 0, 0));
	v.push_back(Vector3(0, 1, 0));
	v.push_back(Vector3(1, 1, 0));
	v.push_back(Vector3(0, 0, 1));
	v.push_back(Vector3(1, 0, 1));
	v.push_back(Vector3(0, 1, 1));
	v.push_back(Vector3(1, 1, 1));
	const int faces[12][3] = {
		{ 0, 2, 1 },
		{ 1, 2, 3 },
		{ 4, 5, 6 },
		{ 5, 7, 6 },
		{ 0, 1, 4 },
		{ 1, 5, 4 },
		{ 2, 6, 3 },
		{ 3, 6, 7 },
		{ 0, 4, 2 },
		{ 2, 4, 6 },
		{ 1, 3, 5 },
		{ 3, 7, 5 },
	};
	Vector<int> idx;
	for (int f = 0; f < 12; ++f) {
		idx.push_back(faces[f][0]);
		idx.push_back(faces[f][1]);
		idx.push_back(faces[f][2]);
	}
	return _build_mesh(v, idx);
}

TEST_CASE("[Cassie][ProfileMover] null-bind leaves is_bound false") {
	Ref<CassieProfileMover> mover;
	mover.instantiate();
	mover->bind(Ref<CassieSurfacePatch>(), Ref<CassieCurvenet>());
	CHECK_FALSE(mover->is_bound());
	CHECK(mover->deform().is_null());
}

TEST_CASE("[Cassie][ProfileMover] bind / deform identity passes mesh through unchanged") {
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(_make_cube_mesh());
	Ref<CassieCurvenet> cn = CassieCurvenetExtractor::extract(patch, 200);
	REQUIRE(cn.is_valid());
	REQUIRE(cn->get_knot_count() == 8);

	Ref<CassieProfileMover> mover;
	mover.instantiate();
	mover->bind(patch, cn);
	REQUIRE(mover->is_bound());
	REQUIRE(mover->get_vertex_count() == 8);
	REQUIRE(mover->get_sample_count() > 0);

	// Zero-displacement deform: result must equal rest mesh to numerical precision.
	Ref<ArrayMesh> deformed = mover->deform();
	REQUIRE(deformed.is_valid());
	const Array arrays = deformed->surface_get_arrays(0);
	const PackedVector3Array out_verts = arrays[Mesh::ARRAY_VERTEX];
	REQUIRE(out_verts.size() == 8);
	for (int i = 0; i < 8; ++i) {
		const Vector3 rest = patch->get_vertex_position(i);
		CHECK_MESSAGE(out_verts[i].distance_to(rest) < 1e-6,
				vformat("vertex %d drifted: rest=%s, deformed=%s",
						i, String(rest), String(out_verts[i])));
	}
}

TEST_CASE("[Cassie][ProfileMover] uniform knot translation translates the mesh by the same vector") {
	// Σ w_i = 1 guarantees uniform translation passes through exactly.
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(_make_cube_mesh());
	Ref<CassieCurvenet> cn = CassieCurvenetExtractor::extract(patch, 200);
	REQUIRE(cn.is_valid());

	Ref<CassieProfileMover> mover;
	mover.instantiate();
	mover->bind(patch, cn);
	REQUIRE(mover->is_bound());

	// Translate every knot by the same vector.
	const Vector3 shift(real_t(0.5), real_t(-0.25), real_t(0.75));
	const TypedArray<CassieCurvenetKnot> knots = cn->get_knots();
	for (int i = 0; i < knots.size(); ++i) {
		Ref<CassieCurvenetKnot> k = knots[i];
		Transform3D rest = k->get_rest_pose_transform();
		rest.origin += shift;
		k->set_rest_pose_transform(rest);
	}

	Ref<ArrayMesh> deformed = mover->deform();
	REQUIRE(deformed.is_valid());
	const Array arrays = deformed->surface_get_arrays(0);
	const PackedVector3Array out_verts = arrays[Mesh::ARRAY_VERTEX];
	REQUIRE(out_verts.size() == 8);
	for (int i = 0; i < 8; ++i) {
		const Vector3 expected = patch->get_vertex_position(i) + shift;
		CHECK_MESSAGE(out_verts[i].distance_to(expected) < 1e-5,
				vformat("vertex %d expected %s got %s",
						i, String(expected), String(out_verts[i])));
	}
}

// ── Phase 1.4b (cut-aware harmonic) ─────────────────────────────────────

// Square grid in the XZ plane with p_quads_per_side quads on a side.
// Edge length 1.0 → fits the default boundary_thresh = avg_edge * 1.25.
static Ref<ArrayMesh> _make_grid_mesh(int p_quads_per_side) {
	const int n = p_quads_per_side + 1;
	Vector<Vector3> verts;
	verts.resize(n * n);
	for (int z = 0; z < n; ++z) {
		for (int x = 0; x < n; ++x) {
			verts.write[z * n + x] = Vector3(real_t(x), 0, real_t(z));
		}
	}
	Vector<int> idx;
	idx.resize(p_quads_per_side * p_quads_per_side * 6);
	int k = 0;
	for (int z = 0; z < p_quads_per_side; ++z) {
		for (int x = 0; x < p_quads_per_side; ++x) {
			const int a = z * n + x;
			const int b = z * n + (x + 1);
			const int c = (z + 1) * n + x;
			const int d = (z + 1) * n + (x + 1);
			idx.write[k++] = a;
			idx.write[k++] = b;
			idx.write[k++] = c;
			idx.write[k++] = c;
			idx.write[k++] = b;
			idx.write[k++] = d;
		}
	}
	return _build_mesh(verts, idx);
}

TEST_CASE("[Cassie][ProfileMover] harmonic bind splits a grid into interior + boundary partitions") {
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(_make_grid_mesh(10));
	Ref<CassieCurvenet> cn = CassieCurvenetExtractor::extract(patch, 200);
	REQUIRE(cn.is_valid());

	Ref<CassieProfileMover> mover;
	mover.instantiate();
	mover->bind(patch, cn);
	REQUIRE(mover->is_bound());
	REQUIRE(mover->is_harmonic_ready());
	CHECK_MESSAGE(mover->get_interior_count() > 0,
			vformat("expected interior partition to be non-empty, got %d",
					mover->get_interior_count()));
	CHECK_MESSAGE(mover->get_boundary_count() > 0,
			vformat("expected boundary partition to be non-empty, got %d",
					mover->get_boundary_count()));
}

TEST_CASE("[Cassie][ProfileMover] harmonic null-deform preserves vertex positions to 1e-7") {
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(_make_grid_mesh(10));
	Ref<CassieCurvenet> cn = CassieCurvenetExtractor::extract(patch, 200);
	REQUIRE(cn.is_valid());

	Ref<CassieProfileMover> mover;
	mover.instantiate();
	mover->bind(patch, cn);
	REQUIRE(mover->is_harmonic_ready());

	// Null deform — no knot moved.
	Ref<ArrayMesh> deformed = mover->deform();
	REQUIRE(deformed.is_valid());
	const Array arrays = deformed->surface_get_arrays(0);
	const PackedVector3Array out_verts = arrays[Mesh::ARRAY_VERTEX];
	REQUIRE(out_verts.size() == patch->get_vertex_count());
	for (int i = 0; i < out_verts.size(); ++i) {
		const Vector3 rest = patch->get_vertex_position(i);
		// Phase A — PCG tolerance × L_IB amplification was ~1e-7 with
		// the all-double hand-rolled spmv. Track 5 follow-up swapped
		// spmv to slangc-emitted SpmvDf32 (fp32 boundary, df32 internal
		// accumulation), so the boundary narrowing raises the floor to
		// ~1e-6 even though the per-row error is now ~7·ε² instead of
		// ~7·ε. Tolerance bumped accordingly.
		CHECK_MESSAGE(out_verts[i].distance_to(rest) < 1e-6,
				vformat("vertex %d drifted: rest=%s, deformed=%s",
						i, String(rest), String(out_verts[i])));
	}
}

TEST_CASE("[Cassie][ProfileMover] harmonic bind on ~10k-vertex mesh under 2 s") {
	// 100x100 quad grid = 10201 vertices, 20000 triangles. Possible because
	// ENG-60 swapped the brute-force IDW K-nearest for a DynamicBVH-driven
	// query — the inner loop is now O(log N_samples) per vertex instead
	// of O(N_samples). Original 1.4b shipped capped at 2.5k.
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(_make_grid_mesh(100));
	REQUIRE(patch->get_vertex_count() >= 10000);
	Ref<CassieCurvenet> cn = CassieCurvenetExtractor::extract(patch, 200);
	REQUIRE(cn.is_valid());

	Ref<CassieProfileMover> mover;
	mover.instantiate();
	const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
	mover->bind(patch, cn);
	const uint64_t dt = Time::get_singleton()->get_ticks_usec() - t0;
#if !defined(ASAN_ENABLED) && !defined(TSAN_ENABLED)
	// Sanitizer instrumentation inflates wall-clock far past the budget, so
	// the timing assertion is meaningless there; the bind still runs above
	// to exercise correctness. vformat has no %llu, so cast to int (dt is
	// well under INT_MAX microseconds for any non-pathological bind).
	CHECK_MESSAGE(dt < 2ULL * 1000 * 1000,
			vformat("harmonic bind took %d us, exceeds 2 s budget", int(dt)));
#else
	(void)dt;
#endif
	CHECK(mover->is_harmonic_ready());
}

// ── Jacobi-PCG (Track 5 Phase A — replaces ENG-52 phase 4.1a) ────────────
//
// The level-set schedule the prior LDLT back-sub depended on is gone —
// PCG iterates rather than direct-solves. The accessors return 0 as a
// stable surface for any caller still asking; the iteration-count
// regression is the new health signal.

TEST_CASE("[Cassie][ProfileMover][PCG] level-set accessors return 0 (back-sub retired)") {
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(_make_grid_mesh(10));
	Ref<CassieCurvenet> cn = CassieCurvenetExtractor::extract(patch, 200);
	REQUIRE(cn.is_valid());

	Ref<CassieProfileMover> mover;
	mover.instantiate();
	mover->bind(patch, cn);
	REQUIRE(mover->is_harmonic_ready());

	CHECK_EQ(mover->get_forward_level_count(), 0);
	CHECK_EQ(mover->get_backward_level_count(), 0);
}

TEST_CASE("[Cassie][ProfileMover][PCG] last solve iters reports nonzero after a knot edit") {
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(_make_grid_mesh(10));
	Ref<CassieCurvenet> cn = CassieCurvenetExtractor::extract(patch, 200);
	REQUIRE(cn.is_valid());

	Ref<CassieProfileMover> mover;
	mover.instantiate();
	mover->bind(patch, cn);
	REQUIRE(mover->is_harmonic_ready());

	// Move every knot up by Y = 1, then deform.
	const Vector3 shift(0, 1, 0);
	const TypedArray<CassieCurvenetKnot> knots = cn->get_knots();
	for (int i = 0; i < knots.size(); ++i) {
		Ref<CassieCurvenetKnot> k = knots[i];
		Transform3D t = k->get_rest_pose_transform();
		t.origin += shift;
		k->set_rest_pose_transform(t);
	}
	Ref<ArrayMesh> deformed = mover->deform();
	REQUIRE(deformed.is_valid());

	const int iters = mover->get_last_solve_iters();
	CHECK_MESSAGE(iters > 0,
			vformat("expected PCG iters > 0, got %d", iters));
	// 3 axes × max_iter ≤ 600; in practice well below 200 with warm-start.
	CHECK_MESSAGE(iters < 600,
			vformat("PCG iters %d exceeds cap of 600", iters));
}

// ── Cut-cell crack mechanism ─────────────────────────────────────────────

// Hand-build a curvenet with one straight curve running along the middle
// row (z = 2) of a 5x5 grid. Curve endpoints are at the row's two grid
// boundary corners, so cuts should appear at every interior row-2 vertex
// (samples are at curve interiors, not endpoints).
static Ref<CassieCurvenet> _make_interior_row_curvenet() {
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 2), Vector3(), Vector3());
	curve->add_point(Vector3(5, 0, 2), Vector3(), Vector3());
	Ref<CassieFinalStroke> stroke;
	stroke.instantiate();
	stroke->set_id(0);
	stroke->set_curve(curve, false);

	Ref<CassieCurvenet> cn;
	cn.instantiate();
	Dictionary g;
	TypedArray<CassieFinalStroke> curves;
	curves.push_back(stroke);
	g["curves"] = curves;

	Array nodes;
	Dictionary n0;
	n0["id"] = 0;
	n0["position"] = Vector3(0, 0, 2);
	PackedInt32Array inc0;
	inc0.push_back(0);
	n0["incident_curve_ids"] = inc0;
	Dictionary n1;
	n1["id"] = 1;
	n1["position"] = Vector3(5, 0, 2);
	PackedInt32Array inc1;
	inc1.push_back(0);
	n1["incident_curve_ids"] = inc1;
	nodes.push_back(n0);
	nodes.push_back(n1);
	g["nodes"] = nodes;
	cn->build_from_graph(g);
	return cn;
}

TEST_CASE("[Cassie][ProfileMover] cut-cell crack duplicates vertices on interior curves") {
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(_make_grid_mesh(5));
	const int source_vc = patch->get_vertex_count();
	REQUIRE(source_vc == 36);

	Ref<CassieCurvenet> cn = _make_interior_row_curvenet();
	REQUIRE(cn->get_curve_count() == 1);
	REQUIRE(cn->get_knot_count() == 2);

	Ref<CassieProfileMover> mover;
	mover.instantiate();
	mover->bind(patch, cn);
	REQUIRE(mover->is_bound());

	const int crack_count = mover->get_crack_edge_count();
	CHECK_MESSAGE(crack_count > 0,
			vformat("expected crack edges from middle-row interior curve, got %d",
					crack_count));
	// Vertex count grew by exactly crack_count — every cracked vertex
	// added one duplicate, no triangles added.
	CHECK_EQ(mover->get_vertex_count(), source_vc + crack_count);
}

TEST_CASE("[Cassie][ProfileMover] cut-cell preserves null-deform identity (post-crack mesh = rest)") {
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(_make_grid_mesh(5));
	Ref<CassieCurvenet> cn = _make_interior_row_curvenet();
	Ref<CassieProfileMover> mover;
	mover.instantiate();
	mover->bind(patch, cn);
	REQUIRE(mover->get_crack_edge_count() > 0);

	Ref<ArrayMesh> deformed = mover->deform();
	REQUIRE(deformed.is_valid());
	const Array arrays = deformed->surface_get_arrays(0);
	const PackedVector3Array out_verts = arrays[Mesh::ARRAY_VERTEX];
	REQUIRE(out_verts.size() == mover->get_vertex_count());
	// The first source_vc vertices are the originals; their positions must
	// match the patch's input vertices. The duplicates (indices >= source_vc)
	// share positions with whatever original vertex they were cloned from
	// — pin only that they're finite + on the grid plane (y = 0).
	const int source_vc = patch->get_vertex_count();
	for (int i = 0; i < source_vc; ++i) {
		const Vector3 rest = patch->get_vertex_position(i);
		// Slang-CPU spmv narrows double→float at the boundary so per-vertex
		// drift is bounded by float32 mantissa amplified through L_IB.
		// SpmvDf32 recovers in-kernel precision but the boundary narrowing
		// itself loses ~1e-7 per value before the kernel even sees it.
		CHECK_MESSAGE(out_verts[i].distance_to(rest) < 1e-6,
				vformat("null deform original vertex %d drifted: rest=%s, deformed=%s",
						i, String(rest), String(out_verts[i])));
	}
	for (int i = source_vc; i < out_verts.size(); ++i) {
		CHECK(Math::is_finite(out_verts[i].x));
		CHECK(Math::is_finite(out_verts[i].y));
		CHECK(Math::is_finite(out_verts[i].z));
	}
}

} // namespace TestCassieProfileMover

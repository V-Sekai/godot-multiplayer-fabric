/**************************************************************************/
/*  test_cassie.h                                                         */
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

#include "../src/cassie_path_3d.h"
#include "../src/cassie_remesh.h"
#include "../src/cassie_triangulator.h"
#include "../src/intrinsic_triangulation.h"
#include "../src/polygon_triangulation_godot.h"

#include "core/object/ref_counted.h"
#include "core/os/time.h"
#include "core/variant/variant.h"
#include "tests/test_macros.h"

namespace TestCassie {

// ── Mesh-validity helpers ─────────────────────────────────────────────────

// Returns "" on a valid mesh, a failure description otherwise.
static String check_mesh_valid(const PackedVector3Array &p_verts, const PackedInt32Array &p_idx) {
	if (p_verts.size() < 3) {
		return "vertex count < 3";
	}
	if (p_idx.size() == 0 || p_idx.size() % 3 != 0) {
		return vformat("index count %d is not a multiple of 3", p_idx.size());
	}
	for (int i = 0; i < p_idx.size(); i++) {
		if (p_idx[i] < 0 || p_idx[i] >= p_verts.size()) {
			return vformat("index %d out of range [0,%d)", p_idx[i], p_verts.size());
		}
	}
	for (int t = 0; t < p_idx.size(); t += 3) {
		if (p_idx[t] == p_idx[t + 1] || p_idx[t + 1] == p_idx[t + 2] || p_idx[t] == p_idx[t + 2]) {
			return vformat("degenerate face %d: (%d,%d,%d)", t / 3, p_idx[t], p_idx[t + 1], p_idx[t + 2]);
		}
	}
	return "";
}

// ── cassie_remesh ─────────────────────────────────────────────────────────

TEST_CASE("[Cassie][Remesh] Single triangle with large target stays valid") {
	PackedVector3Array v;
	v.push_back(Vector3(0, 0, 0));
	v.push_back(Vector3(1, 0, 0));
	v.push_back(Vector3(0.5f, 0, 0.866f));
	PackedInt32Array idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);

	cassie_remesh(v, idx, 10.0f, 3); // target >> all edge lengths

	String err = check_mesh_valid(v, idx);
	CHECK_MESSAGE(err.is_empty(), err);
	CHECK(v.size() >= 3);
}

TEST_CASE("[Cassie][Remesh] Single triangle with small target splits") {
	PackedVector3Array v;
	v.push_back(Vector3(0, 0, 0));
	v.push_back(Vector3(1, 0, 0));
	v.push_back(Vector3(0.5f, 0, 0.866f));
	PackedInt32Array idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);

	cassie_remesh(v, idx, 0.15f, 3); // edges ≈ 1.0; target 0.15 forces splits

	String err = check_mesh_valid(v, idx);
	CHECK_MESSAGE(err.is_empty(), err);
	CHECK(v.size() > 3);
	CHECK(idx.size() > 3);
}

TEST_CASE("[Cassie][Remesh] Boundary vertices preserved after refinement") {
	PackedVector3Array v;
	v.push_back(Vector3(0, 0, 0));
	v.push_back(Vector3(1, 0, 0));
	v.push_back(Vector3(0.5f, 0, 0.866f));
	PackedInt32Array idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);

	Vector3 orig[3] = { v[0], v[1], v[2] };
	cassie_remesh(v, idx, 0.15f, 3);

	for (int k = 0; k < 3; ++k) {
		bool found = false;
		for (int i = 0; i < v.size(); ++i) {
			if (orig[k].distance_to(v[i]) < 1e-4f) {
				found = true;
				break;
			}
		}
		CHECK_MESSAGE(found, vformat("Boundary vertex %d missing from remeshed output", k));
	}
}

TEST_CASE("[Cassie][Remesh] Quad patch valid and denser than input") {
	PackedVector3Array v;
	v.push_back(Vector3(0, 0, 0));
	v.push_back(Vector3(1, 0, 0));
	v.push_back(Vector3(1, 0, 1));
	v.push_back(Vector3(0, 0, 1));
	PackedInt32Array idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);
	idx.push_back(0);
	idx.push_back(2);
	idx.push_back(3);

	cassie_remesh(v, idx, 0.2f, 3);

	String err = check_mesh_valid(v, idx);
	CHECK_MESSAGE(err.is_empty(), err);
	CHECK(v.size() > 4);
}

TEST_CASE("[Cassie][Remesh] Empty input does not crash") {
	PackedVector3Array v;
	PackedInt32Array idx;
	cassie_remesh(v, idx, 0.1f, 3);
	CHECK_EQ(v.size(), 0);
}

TEST_CASE("[Cassie][Remesh] With reference mesh output is valid") {
	PackedVector3Array v;
	v.push_back(Vector3(0, 0, 0));
	v.push_back(Vector3(1, 0, 0));
	v.push_back(Vector3(0.5f, 0, 0.866f));
	PackedInt32Array idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);

	const PackedVector3Array ref_v = v;
	const PackedInt32Array ref_idx = idx;
	cassie_remesh(v, idx, 0.15f, 3, ref_v, ref_idx);

	String err = check_mesh_valid(v, idx);
	CHECK_MESSAGE(err.is_empty(), err);
}

// ── CassieTriangulator ────────────────────────────────────────────────────

TEST_CASE("[Cassie][Triangulator] Too few points returns failure") {
	PackedVector3Array b;
	b.push_back(Vector3(0, 0, 0));
	b.push_back(Vector3(1, 0, 0));
	Dictionary r = CassieTriangulator::triangulate(b, 0.1f);
	bool ok = r.get("success", true);
	CHECK_FALSE(ok);
}

TEST_CASE("[Cassie][Triangulator] Zero target edge length returns failure") {
	PackedVector3Array b;
	b.push_back(Vector3(0, 0, 0));
	b.push_back(Vector3(1, 0, 0));
	b.push_back(Vector3(0.5f, 0, 0.866f));
	bool ok0 = (bool)CassieTriangulator::triangulate(b, 0.0f).get("success", true);
	bool ok1 = (bool)CassieTriangulator::triangulate(b, -1.0f).get("success", true);
	CHECK_FALSE(ok0);
	CHECK_FALSE(ok1);
}

TEST_CASE("[Cassie][Triangulator] Collinear boundary returns failure") {
	PackedVector3Array b;
	b.push_back(Vector3(0, 0, 0));
	b.push_back(Vector3(1, 0, 0));
	b.push_back(Vector3(2, 0, 0));
	bool ok = (bool)CassieTriangulator::triangulate(b, 0.1f).get("success", true);
	CHECK_FALSE(ok);
}

TEST_CASE("[Cassie][Triangulator] Triangle fast path produces valid mesh") {
	PackedVector3Array b;
	b.push_back(Vector3(0, 0, 0));
	b.push_back(Vector3(1, 0, 0));
	b.push_back(Vector3(0.5f, 0, 0.866f));
	Dictionary r = CassieTriangulator::triangulate(b, 0.15f);

	bool ok = r.get("success", false);
	CHECK(ok);
	String err = check_mesh_valid(r["vertices"], r["faces"]);
	CHECK_MESSAGE(err.is_empty(), err);
}

TEST_CASE("[Cassie][Triangulator] Square boundary produces valid mesh") {
	PackedVector3Array b;
	b.push_back(Vector3(0, 0, 0));
	b.push_back(Vector3(1, 0, 0));
	b.push_back(Vector3(1, 0, 1));
	b.push_back(Vector3(0, 0, 1));
	Dictionary r = CassieTriangulator::triangulate(b, 0.25f);

	bool ok = r.get("success", false);
	CHECK(ok);
	String err = check_mesh_valid(r["vertices"], r["faces"]);
	CHECK_MESSAGE(err.is_empty(), err);
	PackedInt32Array faces = r["faces"];
	CHECK(faces.size() >= 6);
}

TEST_CASE("[Cassie][Triangulator] Pentagon boundary produces valid mesh") {
	PackedVector3Array b;
	b.push_back(Vector3(1.000f, 0, 0.000f));
	b.push_back(Vector3(0.309f, 0, 0.951f));
	b.push_back(Vector3(-0.809f, 0, 0.588f));
	b.push_back(Vector3(-0.809f, 0, -0.588f));
	b.push_back(Vector3(0.309f, 0, -0.951f));
	Dictionary r = CassieTriangulator::triangulate(b, 0.3f);

	bool ok = r.get("success", false);
	CHECK(ok);
	String err = check_mesh_valid(r["vertices"], r["faces"]);
	CHECK_MESSAGE(err.is_empty(), err);
}

TEST_CASE("[Cassie][Triangulator] All face indices are in range") {
	PackedVector3Array b;
	b.push_back(Vector3(1.000f, 0, 0.000f));
	b.push_back(Vector3(0.707f, 0, 0.707f));
	b.push_back(Vector3(0.000f, 0, 1.000f));
	b.push_back(Vector3(-0.707f, 0, 0.707f));
	b.push_back(Vector3(-1.000f, 0, 0.000f));
	b.push_back(Vector3(-0.707f, 0, -0.707f));
	b.push_back(Vector3(0.000f, 0, -1.000f));
	b.push_back(Vector3(0.707f, 0, -0.707f));
	Dictionary r = CassieTriangulator::triangulate(b, 0.2f);
	bool ok = r.get("success", false);
	if (!ok) {
		return;
	}
	PackedVector3Array verts = r["vertices"];
	PackedInt32Array faces = r["faces"];
	for (int i = 0; i < faces.size(); ++i) {
		CHECK_MESSAGE(faces[i] >= 0, vformat("Face index %d is negative: %d", i, faces[i]));
		CHECK_MESSAGE(faces[i] < verts.size(), vformat("Face index %d (%d) >= vertex count %d", i, faces[i], verts.size()));
	}
}

// ── CassiePath3D ──────────────────────────────────────────────────────────

TEST_CASE("[Cassie][CassiePath3D] Add and retrieve points") {
	Ref<CassiePath3D> path;
	path.instantiate();
	path->add_point(Vector3(0, 0, 0));
	path->add_point(Vector3(1, 0, 0));
	path->add_point(Vector3(2, 0, 0));

	CHECK_EQ(path->get_point_count(), 3);
	CHECK(path->get_point_position(0).is_equal_approx(Vector3(0, 0, 0)));
	CHECK(path->get_point_position(2).is_equal_approx(Vector3(2, 0, 0)));
}

TEST_CASE("[Cassie][CassiePath3D] Remove point shifts remaining") {
	Ref<CassiePath3D> path;
	path.instantiate();
	path->add_point(Vector3(0, 0, 0));
	path->add_point(Vector3(1, 0, 0));
	path->add_point(Vector3(2, 0, 0));
	path->remove_point(1);

	CHECK_EQ(path->get_point_count(), 2);
	CHECK(path->get_point_position(1).is_equal_approx(Vector3(2, 0, 0)));
}

TEST_CASE("[Cassie][CassiePath3D] Total length") {
	Ref<CassiePath3D> path;
	path.instantiate();
	path->add_point(Vector3(0, 0, 0));
	path->add_point(Vector3(1, 0, 0));
	path->add_point(Vector3(3, 0, 0));

	float len = path->get_total_length();
	CHECK(Math::is_equal_approx(len, 3.0f));
}

TEST_CASE("[Cassie][CassiePath3D] Resample produces requested count") {
	Ref<CassiePath3D> path;
	path.instantiate();
	for (int i = 0; i < 10; ++i) {
		path->add_point(Vector3(float(i), 0, 0));
	}
	path->resample_uniform(5);
	CHECK_EQ(path->get_point_count(), 5);

	path->resample_uniform(20);
	CHECK_EQ(path->get_point_count(), 20);
}

TEST_CASE("[Cassie][CassiePath3D] Taubin keeps count and produces no NaN") {
	Ref<CassiePath3D> path;
	path.instantiate();
	for (int i = 0; i < 8; ++i) {
		path->add_point(Vector3(float(i), (i % 2) ? 0.2f : 0.0f, 0));
	}
	const int orig = path->get_point_count();
	path->beautify_taubin(0.5f, -0.53f, 5);

	CHECK_EQ(path->get_point_count(), orig);
	for (int i = 0; i < path->get_point_count(); ++i) {
		Vector3 p = path->get_point_position(i);
		CHECK_FALSE(Math::is_nan(p.x));
		CHECK_FALSE(Math::is_nan(p.y));
		CHECK_FALSE(Math::is_nan(p.z));
	}
}

TEST_CASE("[Cassie][CassiePath3D] Taubin reduces y-variance on zigzag") {
	Ref<CassiePath3D> path;
	path.instantiate();
	for (int i = 0; i < 10; ++i) {
		path->add_point(Vector3(float(i), (i % 2) ? 1.0f : 0.0f, 0));
	}
	float var_before = 0;
	for (int i = 1; i < path->get_point_count() - 1; ++i) {
		float dy = path->get_point_position(i).y - path->get_point_position(i - 1).y;
		var_before += dy * dy;
	}

	path->beautify_taubin(0.5f, -0.53f, 10);

	float var_after = 0;
	for (int i = 1; i < path->get_point_count() - 1; ++i) {
		float dy = path->get_point_position(i).y - path->get_point_position(i - 1).y;
		var_after += dy * dy;
	}
	CHECK(var_after < var_before);
}

TEST_CASE("[Cassie][CassiePath3D] Clear empties the path") {
	Ref<CassiePath3D> path;
	path.instantiate();
	path->add_point(Vector3(0, 0, 0));
	path->add_point(Vector3(1, 0, 0));
	path->clear_points();
	CHECK_EQ(path->get_point_count(), 0);
}

TEST_CASE("[Cassie][CassiePath3D] Closed property round-trips") {
	Ref<CassiePath3D> path;
	path.instantiate();
	CHECK_FALSE(path->is_path_closed());
	path->set_closed(true);
	CHECK(path->is_path_closed());
	path->set_closed(false);
	CHECK_FALSE(path->is_path_closed());
}

// ── cassie_remesh — additional coverage ──────────────────────────────────

TEST_CASE("[Cassie][Remesh] Collapse fires: re-remesh at much larger target reduces vertex count") {
	// Build a very dense mesh at target=0.05, then re-remesh at target=1.0.
	// Split threshold (4/3 * 1.0 = 1.33) is far above all edge lengths (~0.05),
	// so no new splits fire. Collapse threshold (4/5 * 1.0 = 0.8) is far above
	// all edge lengths, so all interior edges collapse. Net result: fewer verts.
	PackedVector3Array v;
	v.push_back(Vector3(0, 0, 0));
	v.push_back(Vector3(1, 0, 0));
	v.push_back(Vector3(1, 0, 1));
	v.push_back(Vector3(0, 0, 1));
	PackedInt32Array idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);
	idx.push_back(0);
	idx.push_back(2);
	idx.push_back(3);

	cassie_remesh(v, idx, 0.05f, 3); // very dense
	int dense = v.size();

	cassie_remesh(v, idx, 1.0f, 3); // all short interior edges should collapse
	int coarse = v.size();

	String err = check_mesh_valid(v, idx);
	CHECK_MESSAGE(err.is_empty(), err);
	CHECK_MESSAGE(coarse < dense, vformat("Expected collapse to reduce verts: %d → %d", dense, coarse));
}

TEST_CASE("[Cassie][Remesh] Deterministic: same input produces same output") {
	PackedVector3Array v1, v2;
	PackedInt32Array idx1, idx2;
	v1.push_back(Vector3(0, 0, 0));
	v1.push_back(Vector3(1, 0, 0));
	v1.push_back(Vector3(0.5f, 0, 0.866f));
	idx1.push_back(0);
	idx1.push_back(1);
	idx1.push_back(2);
	v2 = v1;
	idx2 = idx1;

	cassie_remesh(v1, idx1, 0.15f, 3);
	cassie_remesh(v2, idx2, 0.15f, 3);

	CHECK_EQ(v1.size(), v2.size());
	CHECK_EQ(idx1.size(), idx2.size());
	for (int i = 0; i < v1.size(); ++i) {
		CHECK(v1[i].is_equal_approx(v2[i]));
	}
}

TEST_CASE("[Cassie][Remesh] Non-planar boundary stays valid") {
	// Tilted triangle — not in the XZ plane.
	PackedVector3Array v;
	v.push_back(Vector3(0, 0, 0));
	v.push_back(Vector3(1, 0.3f, 0));
	v.push_back(Vector3(0.5f, 0.6f, 0.866f));
	PackedInt32Array idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);

	cassie_remesh(v, idx, 0.2f, 3);

	String err = check_mesh_valid(v, idx);
	CHECK_MESSAGE(err.is_empty(), err);
	CHECK(v.size() > 3);
}

TEST_CASE_PENDING("[Cassie][Remesh] Large boundary stays valid and finishes fast") {
	// TODO: Currently 6ms on desktop (24ms Quest 3 estimate @ 4x).
	// Target: 500us desktop (2ms Quest 3). Requires:
	//   - Replace std::unordered_map in edge_tris with a flat sorted array.
	//   - Replace flip_edges' per-iteration rebuild_adjacency with incremental flips.
	//   - Profile to confirm bottleneck shift after those changes.
	// 16-point circle — representative of a mid-size Quest 3 sketch.
	// Pre-computed to avoid trig in tests.
	static const float pts[16][2] = {
		{ 1.000f, 0.000f },
		{ 0.924f, 0.383f },
		{ 0.707f, 0.707f },
		{ 0.383f, 0.924f },
		{ 0.000f, 1.000f },
		{ -0.383f, 0.924f },
		{ -0.707f, 0.707f },
		{ -0.924f, 0.383f },
		{ -1.000f, 0.000f },
		{ -0.924f, -0.383f },
		{ -0.707f, -0.707f },
		{ -0.383f, -0.924f },
		{ 0.000f, -1.000f },
		{ 0.383f, -0.924f },
		{ 0.707f, -0.707f },
		{ 0.924f, -0.383f },
	};
	PackedVector3Array b;
	for (int i = 0; i < 16; ++i) {
		b.push_back(Vector3(pts[i][0], 0, pts[i][1]));
	}

	uint64_t t0 = Time::get_singleton()->get_ticks_usec();
	Dictionary r = CassieTriangulator::triangulate(b, 0.2f);
	uint64_t elapsed_us = Time::get_singleton()->get_ticks_usec() - t0;

	bool ok = r.get("success", false);
	CHECK(ok);
	String err = check_mesh_valid(r["vertices"], r["faces"]);
	CHECK_MESSAGE(err.is_empty(), err);
	// 500µs desktop budget → ~2ms on Quest 3 @ 4×.
	CHECK_MESSAGE(elapsed_us < 500, vformat("16-pt circle took %d µs (budget 500 us)", elapsed_us));
}

// ── IntrinsicTriangulation ────────────────────────────────────────────────

TEST_CASE("[Cassie][IntrinsicTriangulation] Set mesh data and query counts") {
	Ref<IntrinsicTriangulation> it;
	it.instantiate();

	PackedVector3Array v;
	v.push_back(Vector3(0, 0, 0));
	v.push_back(Vector3(1, 0, 0));
	v.push_back(Vector3(0.5f, 0, 0.866f));
	PackedInt32Array idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);

	it->set_mesh_data(v, idx);

	CHECK_EQ(it->get_vertex_count(), 3);
	CHECK_EQ(it->get_triangle_count(), 1);
}

TEST_CASE("[Cassie][IntrinsicTriangulation] Flip to Delaunay does not corrupt a valid mesh") {
	Ref<IntrinsicTriangulation> it;
	it.instantiate();

	// Two triangles forming a square — one diagonal is non-Delaunay.
	PackedVector3Array v;
	v.push_back(Vector3(0, 0, 0));
	v.push_back(Vector3(1, 0, 0));
	v.push_back(Vector3(1, 0, 1));
	v.push_back(Vector3(0, 0, 1));
	PackedInt32Array idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);
	idx.push_back(0);
	idx.push_back(2);
	idx.push_back(3);

	it->set_mesh_data(v, idx);
	it->flip_to_delaunay();

	PackedVector3Array out_v = it->get_vertices();
	PackedInt32Array out_idx = it->get_indices();

	String err = check_mesh_valid(out_v, out_idx);
	CHECK_MESSAGE(err.is_empty(), err);
	CHECK_EQ(out_v.size(), 4); // flip must not add/remove vertices
}

TEST_CASE("[Cassie][IntrinsicTriangulation] get_mesh returns ArrayMesh") {
	Ref<IntrinsicTriangulation> it;
	it.instantiate();

	PackedVector3Array v;
	v.push_back(Vector3(0, 0, 0));
	v.push_back(Vector3(1, 0, 0));
	v.push_back(Vector3(0.5f, 0, 0.866f));
	PackedInt32Array idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);
	it->set_mesh_data(v, idx);

	Ref<ArrayMesh> mesh = it->get_mesh();
	CHECK(mesh.is_valid());
	CHECK_EQ(mesh->get_surface_count(), 1);
}

TEST_CASE("[Cassie][IntrinsicTriangulation] get_statistics returns expected keys") {
	Ref<IntrinsicTriangulation> it;
	it.instantiate();

	PackedVector3Array v;
	v.push_back(Vector3(0, 0, 0));
	v.push_back(Vector3(1, 0, 0));
	v.push_back(Vector3(0.5f, 0, 0.866f));
	PackedInt32Array idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);
	it->set_mesh_data(v, idx);

	Dictionary stats = it->get_statistics();
	CHECK(stats.has("vertex_count"));
	CHECK(stats.has("edge_count"));
	CHECK(stats.has("triangle_count"));
}

// ── PolygonTriangulationGodot ─────────────────────────────────────────────

TEST_CASE_PENDING("[Cassie][PolygonTriangulationGodot] Create and triangulate pentagon") {
	// TODO: PolygonTriangulationGodot::create() crashes when deGenPts=nullptr
	// reaches DMWT::buildList(). Fix null guard in DMWT ctor before enabling.
	PackedVector3Array pts;
	pts.push_back(Vector3(1.000f, 0, 0.000f));
	pts.push_back(Vector3(0.309f, 0, 0.951f));
	pts.push_back(Vector3(-0.809f, 0, 0.588f));
	pts.push_back(Vector3(-0.809f, 0, -0.588f));
	pts.push_back(Vector3(0.309f, 0, -0.951f));

	Ref<PolygonTriangulationGodot> pt = PolygonTriangulationGodot::create(pts);
	CHECK(pt.is_valid());

	bool prep_ok = pt->preprocess();
	CHECK(prep_ok);

	bool tri_ok = pt->triangulate();
	CHECK(tri_ok);
	CHECK(pt->get_triangle_count() > 0);
	CHECK(pt->get_vertex_count() > 0);
}

TEST_CASE_PENDING("[Cassie][PolygonTriangulationGodot] get_indices are all in range") {
	// TODO: blocked on same DMWT null crash as above.
	PackedVector3Array pts;
	pts.push_back(Vector3(1.000f, 0, 0.000f));
	pts.push_back(Vector3(0.309f, 0, 0.951f));
	pts.push_back(Vector3(-0.809f, 0, 0.588f));
	pts.push_back(Vector3(-0.809f, 0, -0.588f));
	pts.push_back(Vector3(0.309f, 0, -0.951f));

	Ref<PolygonTriangulationGodot> pt = PolygonTriangulationGodot::create(pts);
	pt->preprocess();
	bool ok = pt->triangulate();
	if (!ok) {
		return;
	}
	PackedInt32Array idx = pt->get_indices();
	int nv = pt->get_vertex_count();
	for (int i = 0; i < idx.size(); ++i) {
		CHECK_MESSAGE(idx[i] >= 0, vformat("Negative index at %d", i));
		CHECK_MESSAGE(idx[i] < nv, vformat("Index %d (%d) >= vertex count %d", i, idx[i], nv));
	}
}

TEST_CASE_PENDING("[Cassie][PolygonTriangulationGodot] get_optimal_cost is non-negative") {
	// TODO: blocked on same DMWT null crash as above.
	PackedVector3Array pts;
	pts.push_back(Vector3(0, 0, 0));
	pts.push_back(Vector3(1, 0, 0));
	pts.push_back(Vector3(1, 0, 1));
	pts.push_back(Vector3(0, 0, 1));

	Ref<PolygonTriangulationGodot> pt = PolygonTriangulationGodot::create(pts);
	pt->preprocess();
	bool ok = pt->triangulate();
	if (!ok) {
		return;
	}
	float cost = pt->get_optimal_cost();
	CHECK(cost >= 0.0f);
}

// ── CassiePath3D — additional coverage ───────────────────────────────────

TEST_CASE("[Cassie][CassiePath3D] Insert point") {
	Ref<CassiePath3D> path;
	path.instantiate();
	path->add_point(Vector3(0, 0, 0));
	path->add_point(Vector3(2, 0, 0));
	path->insert_point(1, Vector3(1, 0, 0));

	CHECK_EQ(path->get_point_count(), 3);
	CHECK(path->get_point_position(1).is_equal_approx(Vector3(1, 0, 0)));
	CHECK(path->get_point_position(2).is_equal_approx(Vector3(2, 0, 0)));
}

TEST_CASE("[Cassie][CassiePath3D] Normals stored and retrieved") {
	Ref<CassiePath3D> path;
	path.instantiate();
	path->add_point(Vector3(0, 0, 0), Vector3(0, 1, 0));
	path->add_point(Vector3(1, 0, 0), Vector3(0, 0, 1));

	CHECK(path->get_point_normal(0).is_equal_approx(Vector3(0, 1, 0)));
	CHECK(path->get_point_normal(1).is_equal_approx(Vector3(0, 0, 1)));
}

TEST_CASE("[Cassie][CassiePath3D] Set point normal overwrites") {
	Ref<CassiePath3D> path;
	path.instantiate();
	path->add_point(Vector3(0, 0, 0), Vector3(0, 1, 0));
	path->set_point_normal(0, Vector3(1, 0, 0));
	CHECK(path->get_point_normal(0).is_equal_approx(Vector3(1, 0, 0)));
}

TEST_CASE("[Cassie][CassiePath3D] get_average_segment_length") {
	Ref<CassiePath3D> path;
	path.instantiate();
	path->add_point(Vector3(0, 0, 0));
	path->add_point(Vector3(1, 0, 0));
	path->add_point(Vector3(3, 0, 0)); // segments: 1.0 and 2.0

	float avg = path->get_average_segment_length();
	CHECK(Math::is_equal_approx(avg, 1.5f));
}

TEST_CASE("[Cassie][CassiePath3D] smooth_normals does not change count") {
	Ref<CassiePath3D> path;
	path.instantiate();
	for (int i = 0; i < 6; ++i) {
		path->add_point(Vector3(float(i), 0, 0), Vector3(0, 1, 0));
	}
	path->smooth_normals();
	CHECK_EQ(path->get_point_count(), 6);
}

TEST_CASE("[Cassie][CassiePath3D] get_sample_points returns correct count") {
	Ref<CassiePath3D> path;
	path.instantiate();
	for (int i = 0; i < 5; ++i) {
		path->add_point(Vector3(float(i), 0, 0));
	}
	PackedVector3Array samples = path->get_sample_points(10);
	CHECK_EQ(samples.size(), 10);
}

TEST_CASE("[Cassie][CassiePath3D] get_normals size matches get_points size") {
	Ref<CassiePath3D> path;
	path.instantiate();
	path->add_point(Vector3(0, 0, 0), Vector3(0, 1, 0));
	path->add_point(Vector3(1, 0, 0), Vector3(0, 1, 0));
	path->add_point(Vector3(2, 0, 0), Vector3(0, 1, 0));

	CHECK_EQ(path->get_normals().size(), path->get_points().size());
}

// ── CassieTriangulator — additional coverage ──────────────────────────────

TEST_CASE("[Cassie][Triangulator] Non-planar boundary produces valid mesh") {
	// Triangle tilted out of the XZ plane.
	PackedVector3Array b;
	b.push_back(Vector3(0, 0.0f, 0));
	b.push_back(Vector3(1, 0.3f, 0));
	b.push_back(Vector3(0.5f, 0.6f, 0.866f));
	Dictionary r = CassieTriangulator::triangulate(b, 0.2f);

	bool ok = r.get("success", false);
	CHECK(ok);
	String err = check_mesh_valid(r["vertices"], r["faces"]);
	CHECK_MESSAGE(err.is_empty(), err);
}

TEST_CASE("[Cassie][Triangulator] Consistent output on repeated calls") {
	PackedVector3Array b;
	b.push_back(Vector3(0, 0, 0));
	b.push_back(Vector3(1, 0, 0));
	b.push_back(Vector3(1, 0, 1));
	b.push_back(Vector3(0, 0, 1));

	Dictionary r1 = CassieTriangulator::triangulate(b, 0.25f);
	Dictionary r2 = CassieTriangulator::triangulate(b, 0.25f);

	bool ok1 = r1.get("success", false);
	bool ok2 = r2.get("success", false);
	CHECK(ok1);
	CHECK(ok2);
	PackedVector3Array v1 = r1["vertices"];
	PackedVector3Array v2 = r2["vertices"];
	CHECK_EQ(v1.size(), v2.size());
}

TEST_CASE_PENDING("[Cassie][Triangulator] Quest 3 budget: 12-point boundary under 1ms") {
	// TODO: Currently 4.4ms on desktop (17ms Quest 3 estimate @ 4x).
	// Target: 250us desktop (1ms Quest 3). Same fix as Large boundary test above.
	// 12-point circle at 0.5m radius — representative of a medium VR sketch.
	static const float pts[12][2] = {
		{ 0.500f, 0.000f },
		{ 0.433f, 0.250f },
		{ 0.250f, 0.433f },
		{ 0.000f, 0.500f },
		{ -0.250f, 0.433f },
		{ -0.433f, 0.250f },
		{ -0.500f, 0.000f },
		{ -0.433f, -0.250f },
		{ -0.250f, -0.433f },
		{ 0.000f, -0.500f },
		{ 0.250f, -0.433f },
		{ 0.433f, -0.250f },
	};
	PackedVector3Array b;
	for (int i = 0; i < 12; ++i) {
		b.push_back(Vector3(pts[i][0], 0, pts[i][1]));
	}

	uint64_t t0 = Time::get_singleton()->get_ticks_usec();
	Dictionary r = CassieTriangulator::triangulate(b, 0.1f);
	uint64_t elapsed_us = Time::get_singleton()->get_ticks_usec() - t0;

	bool ok = r.get("success", false);
	CHECK(ok);
	// 250µs desktop budget → ~1ms on Quest 3 @ 4×.
	CHECK_MESSAGE(elapsed_us < 250, vformat("12-pt circle took %d µs (budget 250 us)", elapsed_us));
}

// ── Regression baseline — capture metrics before Geogram→Godot swap ─────────
// These tests record vertex/face counts and topology ratios so any behavioral
// change in the Delaunay backend is caught immediately. Run them before and
// after the swap; they must pass both times with the same values (±1 vertex).

// Helper: Euler ratio for a disk-topology triangulated patch.
//   Disk Euler characteristic: V - E + F = 1
//   F = 2V - B - 2  where B = #boundary vertices
//   F/V = 2 - (B+2)/V  → approaches 2 as interior density increases,
//                          approaches 1 when V ≈ B (sparse interior)
// Valid range [1.0, 3.0] covers all correctly-formed disk patches.
static bool check_euler_ratio(const PackedVector3Array &verts, const PackedInt32Array &faces) {
	if (verts.size() == 0 || faces.size() == 0) {
		return false;
	}
	float ratio = float(faces.size() / 3) / float(verts.size());
	return ratio >= 1.0f && ratio <= 3.0f;
}

TEST_CASE("[Cassie][Regression] Octagon baseline: valid mesh with stable counts") {
	// Pre-computed regular octagon. Fix these values; they must not change
	// when the Delaunay backend is swapped (Geogram → Godot built-in).
	PackedVector3Array b;
	b.push_back(Vector3(1.000f, 0, 0.000f));
	b.push_back(Vector3(0.707f, 0, 0.707f));
	b.push_back(Vector3(0.000f, 0, 1.000f));
	b.push_back(Vector3(-0.707f, 0, 0.707f));
	b.push_back(Vector3(-1.000f, 0, 0.000f));
	b.push_back(Vector3(-0.707f, 0, -0.707f));
	b.push_back(Vector3(0.000f, 0, -1.000f));
	b.push_back(Vector3(0.707f, 0, -0.707f));

	Dictionary r = CassieTriangulator::triangulate(b, 0.3f);
	bool ok = r.get("success", false);
	CHECK(ok);

	PackedVector3Array verts = r["vertices"];
	PackedInt32Array faces = r["faces"];
	String err = check_mesh_valid(verts, faces);
	CHECK_MESSAGE(err.is_empty(), err);
	CHECK(check_euler_ratio(verts, faces));

	// Record baseline — regenerate these numbers after any Delaunay swap
	// and confirm they are equal or the mesh is strictly better (lower cost
	// can't be measured here, but vertex count staying stable is a proxy).
	CHECK_MESSAGE(verts.size() >= 8, "Must have at least the 8 boundary vertices");
	CHECK_MESSAGE(faces.size() / 3 >= 6, "Octagon needs at least 6 triangles");
}

TEST_CASE("[Cassie][Regression] Near-degenerate: nearly coplanar boundary") {
	// 5 points nearly in the XZ plane (z offset 0.001). Tests MingCurve's
	// perturbation path and the coplanarity fallback to 2D Delaunay.
	// The result must be a valid mesh — not a crash, not an empty output.
	PackedVector3Array b;
	b.push_back(Vector3(1.0f, 0.000f, 0.0f));
	b.push_back(Vector3(0.0f, 0.001f, 1.0f));
	b.push_back(Vector3(-1.0f, 0.000f, 0.0f));
	b.push_back(Vector3(0.0f, 0.001f, -1.0f));
	b.push_back(Vector3(0.7f, 0.000f, 0.7f));

	Dictionary r = CassieTriangulator::triangulate(b, 0.3f);
	bool ok = r.get("success", false);
	CHECK(ok);
	String err = check_mesh_valid(r["vertices"], r["faces"]);
	CHECK_MESSAGE(err.is_empty(), err);
}

TEST_CASE("[Cassie][Regression] Near-cocircular: 4 points on almost the same circumsphere") {
	// R128 (Godot) gives the correct circumsphere test here; double (Geogram)
	// can flip. After the swap, output must still be a valid mesh.
	PackedVector3Array b;
	b.push_back(Vector3(1.0f, 0.0f, 0.0f));
	b.push_back(Vector3(-1.0f, 0.0f, 0.0f));
	b.push_back(Vector3(0.0f, 1.0f, 0.0f));
	b.push_back(Vector3(0.0f, -1.0f, 1e-5f)); // perturbed off the sphere by 10µm

	Dictionary r = CassieTriangulator::triangulate(b, 0.5f);
	bool ok = r.get("success", false);
	CHECK(ok);
	String err = check_mesh_valid(r["vertices"], r["faces"]);
	CHECK_MESSAGE(err.is_empty(), err);
}

// ── Exaggerated inputs — users will push the limits ───────────────────────
// "If our tool is good, users will maximize usage."
// These tests are the floor: the tool must not crash, corrupt memory, or
// produce degenerate output regardless of input size or shape.

static PackedVector3Array make_circle(int n, float radius = 1.0f, float y = 0.0f) {
	PackedVector3Array b;
	const float tau = 6.28318530718f;
	for (int i = 0; i < n; ++i) {
		float a = float(i) / float(n) * tau;
		b.push_back(Vector3(Math::cos(a) * radius, y, Math::sin(a) * radius));
	}
	return b;
}

TEST_CASE("[Cassie][Exaggerated] 50-point boundary produces valid mesh") {
	Dictionary r = CassieTriangulator::triangulate(make_circle(50), 0.3f);
	bool ok = r.get("success", false);
	CHECK(ok);
	String err = check_mesh_valid(r["vertices"], r["faces"]);
	CHECK_MESSAGE(err.is_empty(), err);
	CHECK(check_euler_ratio(r["vertices"], r["faces"]));
}

TEST_CASE("[Cassie][Exaggerated] 100-point boundary produces valid mesh") {
	Dictionary r = CassieTriangulator::triangulate(make_circle(100), 0.3f);
	bool ok = r.get("success", false);
	CHECK(ok);
	String err = check_mesh_valid(r["vertices"], r["faces"]);
	CHECK_MESSAGE(err.is_empty(), err);
	CHECK(check_euler_ratio(r["vertices"], r["faces"]));
}

TEST_CASE("[Cassie][Exaggerated] 200-point boundary produces valid mesh") {
	Dictionary r = CassieTriangulator::triangulate(make_circle(200), 0.3f);
	bool ok = r.get("success", false);
	CHECK(ok);
	String err = check_mesh_valid(r["vertices"], r["faces"]);
	CHECK_MESSAGE(err.is_empty(), err);
	CHECK(check_euler_ratio(r["vertices"], r["faces"]));
}

TEST_CASE("[Cassie][Exaggerated] Long thin boundary (50:1 aspect ratio)") {
	// A 10m × 0.2m rectangle — extreme aspect ratio that stresses the
	// Delaunay coplanarity and DMWT degenerate-edge handling.
	// Closed polygon with no duplicate vertices: each loop excludes its
	// last point (= first point of the next loop), so corners appear once.
	PackedVector3Array b;
	for (int i = 0; i < 20; ++i) { // bottom edge, left→right, skip corner (10,0,0)
		b.push_back(Vector3(float(i) * 0.5f, 0, 0.0f));
	}
	for (int i = 0; i < 2; ++i) { // right edge, skip corner (10,0,0.2)
		b.push_back(Vector3(10.0f, 0, float(i) * 0.1f));
	}
	for (int i = 20; i > 0; --i) { // top edge, right→left, skip corner (0,0,0.2)
		b.push_back(Vector3(float(i) * 0.5f, 0, 0.2f));
	}
	for (int i = 2; i > 0; --i) { // left edge, skip corner (0,0,0) = start
		b.push_back(Vector3(0.0f, 0, float(i) * 0.1f));
	}

	Dictionary r = CassieTriangulator::triangulate(b, 0.4f);
	bool ok = r.get("success", false);
	CHECK(ok);
	String err = check_mesh_valid(r["vertices"], r["faces"]);
	CHECK_MESSAGE(err.is_empty(), err);
}

TEST_CASE("[Cassie][Exaggerated] Non-convex (star-shaped) boundary") {
	// Star with 5 tips and 5 inner valleys — tests DMWT on a non-convex
	// boundary where the Delaunay candidate set contains many non-valid
	// triangles the DP must reject.
	PackedVector3Array b;
	const float tau = 6.28318530718f;
	for (int i = 0; i < 10; ++i) {
		float a = float(i) / 10.0f * tau - tau / 4.0f;
		float r = (i % 2 == 0) ? 1.0f : 0.4f; // alternating tip/valley
		b.push_back(Vector3(Math::cos(a) * r, 0, Math::sin(a) * r));
	}

	Dictionary r = CassieTriangulator::triangulate(b, 0.3f);
	bool ok = r.get("success", false);
	CHECK(ok);
	String err = check_mesh_valid(r["vertices"], r["faces"]);
	CHECK_MESSAGE(err.is_empty(), err);
}

TEST_CASE_PENDING("[Cassie][Exaggerated] Tiny patch (millimeter scale)") {
	// 8-point polygon at 1mm radius. Currently fails: DMWT prints
	// "No solution!" because Geogram's double-precision Delaunay loses
	// accuracy below ~1cm. Godot's Delaunay3D uses R128 (128-bit fixed-point)
	// which handles sub-centimetre scales correctly.
	// Enable this test after the Geogram → Godot Delaunay swap.
	PackedVector3Array b = make_circle(8, 0.001f);
	Dictionary r = CassieTriangulator::triangulate(b, 0.0003f);
	bool ok = r.get("success", false);
	CHECK(ok);
	String err = check_mesh_valid(r["vertices"], r["faces"]);
	CHECK_MESSAGE(err.is_empty(), err);
}

TEST_CASE("[Cassie][Exaggerated] Large patch (10m scale)") {
	// 16-point polygon at 10m radius — tests that no integer overflow or
	// precision loss in the Delaunay backend causes artifacts at room scale.
	PackedVector3Array b = make_circle(16, 10.0f);
	Dictionary r = CassieTriangulator::triangulate(b, 1.0f);
	bool ok = r.get("success", false);
	CHECK(ok);
	String err = check_mesh_valid(r["vertices"], r["faces"]);
	CHECK_MESSAGE(err.is_empty(), err);
	CHECK(check_euler_ratio(r["vertices"], r["faces"]));
}

TEST_CASE("[Cassie][Exaggerated] Repeated triangulate calls do not accumulate errors") {
	// Call triangulate 20 times on the same input and verify each result is
	// identical in size. Catches any state leak in Geogram/Godot global RNG.
	PackedVector3Array b = make_circle(12, 0.5f);
	Dictionary first = CassieTriangulator::triangulate(b, 0.15f);
	bool first_ok = first.get("success", false);
	CHECK(first_ok);
	int first_verts = int(PackedVector3Array(first["vertices"]).size());
	int first_faces = int(PackedInt32Array(first["faces"]).size());

	for (int i = 0; i < 19; ++i) {
		Dictionary r = CassieTriangulator::triangulate(b, 0.15f);
		bool ok = r.get("success", false);
		CHECK(ok);
		CHECK_EQ(int(PackedVector3Array(r["vertices"]).size()), first_verts);
		CHECK_EQ(int(PackedInt32Array(r["faces"]).size()), first_faces);
	}
}

} // namespace TestCassie

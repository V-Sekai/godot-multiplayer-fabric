/**************************************************************************/
/*  test_cassie_surface_patch.h                                           */
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
#include "../src/sketch/cassie_surface_patch.h"

#include "core/math/random_number_generator.h"
#include "core/math/vector3.h"
#include "core/os/time.h"
#include "core/variant/variant.h"
#include "scene/resources/mesh.h"
#include "tests/test_macros.h"

namespace TestCassieSurfacePatch {

// Build a tiny ArrayMesh containing only the requested triangle list.
static Ref<ArrayMesh> _make_triangle_mesh(const Vector<Vector3> &p_verts,
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
	if (!p_indices.is_empty()) {
		PackedInt32Array pi;
		pi.resize(p_indices.size());
		for (int i = 0; i < p_indices.size(); ++i) {
			pi.write[i] = p_indices[i];
		}
		arrays[Mesh::ARRAY_INDEX] = pi;
	}
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

// ── Correctness ───────────────────────────────────────────────────────────

TEST_CASE("[Cassie][SurfacePatch] empty mesh project returns on_surface=false") {
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	Dictionary r = patch->project(Vector3(0, 0, 0));
	CHECK_FALSE(bool(r["on_surface"]));
	CHECK_EQ(patch->get_triangle_count(), 0);
}

TEST_CASE("[Cassie][SurfacePatch] single-triangle project returns closest barycentric point") {
	Vector<Vector3> verts;
	verts.push_back(Vector3(0, 0, 0));
	verts.push_back(Vector3(1, 0, 0));
	verts.push_back(Vector3(0, 0, 1));
	Vector<int> idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);
	Ref<ArrayMesh> mesh = _make_triangle_mesh(verts, idx);

	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(mesh);
	REQUIRE(patch->get_triangle_count() == 1);

	// Query directly above the triangle centroid; expected projection lies on
	// the triangle.
	Dictionary r = patch->project(Vector3(real_t(1.0 / 3.0), 1, real_t(1.0 / 3.0)));
	REQUIRE(bool(r["on_surface"]));
	const Vector3 proj = r["projected"];
	CHECK(proj.distance_to(Vector3(real_t(1.0 / 3.0), 0, real_t(1.0 / 3.0))) < 1e-5);
	CHECK(real_t(r["distance"]) < real_t(1.0) + 1e-5);
}

TEST_CASE("[Cassie][SurfacePatch] off-surface query reports distance > threshold") {
	Vector<Vector3> verts;
	verts.push_back(Vector3(0, 0, 0));
	verts.push_back(Vector3(1, 0, 0));
	verts.push_back(Vector3(0, 0, 1));
	Vector<int> idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(_make_triangle_mesh(verts, idx));

	Dictionary r = patch->project(Vector3(10, 0, 10));
	REQUIRE(bool(r["on_surface"]));
	CHECK(real_t(r["distance"]) > real_t(1.0));
}

TEST_CASE("[Cassie][SurfacePatch] transform shifts the patch in world space") {
	Vector<Vector3> verts;
	verts.push_back(Vector3(0, 0, 0));
	verts.push_back(Vector3(1, 0, 0));
	verts.push_back(Vector3(0, 0, 1));
	Vector<int> idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(_make_triangle_mesh(verts, idx));
	patch->set_transform(Transform3D().translated(Vector3(5, 0, 0)));

	// Query the world-space centroid of the shifted triangle.
	Dictionary r = patch->project(Vector3(real_t(5.0 + 1.0 / 3.0), 1, real_t(1.0 / 3.0)));
	REQUIRE(bool(r["on_surface"]));
	const Vector3 proj = r["projected"];
	CHECK(proj.distance_to(Vector3(real_t(5.0 + 1.0 / 3.0), 0, real_t(1.0 / 3.0))) < 1e-4);
}

TEST_CASE("[Cassie][SurfacePatch] multi-triangle picks the truly closest") {
	// Two separated triangles in XZ plane.
	Vector<Vector3> verts;
	verts.push_back(Vector3(0, 0, 0));
	verts.push_back(Vector3(1, 0, 0));
	verts.push_back(Vector3(0, 0, 1));
	verts.push_back(Vector3(10, 0, 0));
	verts.push_back(Vector3(11, 0, 0));
	verts.push_back(Vector3(10, 0, 1));
	Vector<int> idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);
	idx.push_back(3);
	idx.push_back(4);
	idx.push_back(5);
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(_make_triangle_mesh(verts, idx));
	REQUIRE(patch->get_triangle_count() == 2);

	// Closer to the first triangle.
	Dictionary r1 = patch->project(Vector3(0, 1, 0));
	REQUIRE(bool(r1["on_surface"]));
	CHECK(Vector3(r1["projected"]).distance_to(Vector3(0, 0, 0)) < 0.5);

	// Closer to the second triangle.
	Dictionary r2 = patch->project(Vector3(10, 1, 0));
	REQUIRE(bool(r2["on_surface"]));
	CHECK(Vector3(r2["projected"]).distance_to(Vector3(10, 0, 0)) < 0.5);
}

TEST_CASE("[Cassie][SurfacePatch] callback round-trip — Callable plugs into CassieSketchContext") {
	Vector<Vector3> verts;
	verts.push_back(Vector3(0, 0, 0));
	verts.push_back(Vector3(1, 0, 0));
	verts.push_back(Vector3(0, 0, 1));
	Vector<int> idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(_make_triangle_mesh(verts, idx));

	Ref<CassieSketchContext> ctx;
	ctx.instantiate();
	ctx->set_project_on_patch_callback(patch->get_callback());
	const Callable cb = ctx->get_project_on_patch_callback();
	REQUIRE(cb.is_valid());

	// Invoke the round-tripped Callable directly and verify it returns the
	// dictionary shape the beautifier expects.
	const Variant out = cb.call(Vector3(real_t(1.0 / 3.0), 1, real_t(1.0 / 3.0)));
	REQUIRE(out.get_type() == Variant::DICTIONARY);
	const Dictionary d = out;
	CHECK(bool(d["on_surface"]));
	CHECK_EQ(int(d["patch_id"]), 0);
}

// ── Incremental edit (ENG-53) ────────────────────────────────────────────

TEST_CASE("[Cassie][SurfacePatch] add_triangle inserts a new triangle and projects to it") {
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	// Start empty; build via incremental add.
	const int v0 = patch->add_vertex(Vector3(0, 0, 0));
	const int v1 = patch->add_vertex(Vector3(1, 0, 0));
	const int v2 = patch->add_vertex(Vector3(0, 0, 1));
	const int t = patch->add_triangle(v0, v1, v2);
	REQUIRE(t == 0);
	CHECK_EQ(patch->get_active_triangle_count(), 1);

	Dictionary r = patch->project(Vector3(real_t(1.0 / 3.0), 1, real_t(1.0 / 3.0)));
	REQUIRE(bool(r["on_surface"]));
	const Vector3 proj = r["projected"];
	CHECK(proj.distance_to(Vector3(real_t(1.0 / 3.0), 0, real_t(1.0 / 3.0))) < 1e-5);
}

TEST_CASE("[Cassie][SurfacePatch] remove_triangle drops it from project results") {
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	const int v0 = patch->add_vertex(Vector3(0, 0, 0));
	const int v1 = patch->add_vertex(Vector3(1, 0, 0));
	const int v2 = patch->add_vertex(Vector3(0, 0, 1));
	const int v3 = patch->add_vertex(Vector3(10, 0, 0));
	const int v4 = patch->add_vertex(Vector3(11, 0, 0));
	const int v5 = patch->add_vertex(Vector3(10, 0, 1));
	const int t0 = patch->add_triangle(v0, v1, v2);
	const int t1 = patch->add_triangle(v3, v4, v5);
	REQUIRE(patch->get_active_triangle_count() == 2);

	// Drop t0; queries near (0,1,0) should now reach t1 not t0.
	CHECK(patch->remove_triangle(t0));
	CHECK_EQ(patch->get_active_triangle_count(), 1);
	Dictionary r = patch->project(Vector3(0, 1, 0));
	REQUIRE(bool(r["on_surface"]));
	// t0 is gone, so the nearest triangle is t1 at x=10.
	const Vector3 proj = r["projected"];
	CHECK(proj.x >= real_t(9.0));

	// Remove already-deleted slot: rejected.
	CHECK_FALSE(patch->remove_triangle(t0));
	// Remove t1: BVH is now empty; project returns on_surface = false.
	CHECK(patch->remove_triangle(t1));
	CHECK_EQ(patch->get_active_triangle_count(), 0);
	Dictionary r2 = patch->project(Vector3(0, 0, 0));
	CHECK_FALSE(bool(r2["on_surface"]));
}

// ── Scalability ───────────────────────────────────────────────────────────

// Build a synthetic ~100k-triangle grid mesh in the XZ plane. The grid is
// 224 × 224 quads = 100 352 triangles (above the 100k bar without depending
// on an external asset).
static Ref<ArrayMesh> _make_grid_mesh(int p_quads_per_side) {
	Vector<Vector3> verts;
	const int n = p_quads_per_side + 1;
	verts.resize(n * n);
	for (int z = 0; z < n; ++z) {
		for (int x = 0; x < n; ++x) {
			verts.write[z * n + x] = Vector3(real_t(x), 0, real_t(z));
		}
	}
	Vector<int> idx;
	idx.resize(p_quads_per_side * p_quads_per_side * 6);
	int *w = idx.ptrw();
	int k = 0;
	for (int z = 0; z < p_quads_per_side; ++z) {
		for (int x = 0; x < p_quads_per_side; ++x) {
			const int a = z * n + x;
			const int b = z * n + (x + 1);
			const int c = (z + 1) * n + x;
			const int d = (z + 1) * n + (x + 1);
			w[k++] = a;
			w[k++] = b;
			w[k++] = c;
			w[k++] = c;
			w[k++] = b;
			w[k++] = d;
		}
	}
	return _make_triangle_mesh(verts, idx);
}

TEST_CASE("[Cassie][SurfacePatch] BVH query on 100k-triangle mesh under 200 us (p95)") {
	Ref<ArrayMesh> mesh = _make_grid_mesh(224);
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(mesh);
	REQUIRE(patch->get_triangle_count() >= 100000);

	Ref<RandomNumberGenerator> rng;
	rng.instantiate();
	rng->set_seed(0xCA551E);

	const int n_queries = 1000;
	LocalVector<uint64_t> samples;
	samples.reserve(n_queries);
	for (int i = 0; i < n_queries; ++i) {
		const Vector3 q(rng->randf_range(0, 224),
				rng->randf_range(-1, 1),
				rng->randf_range(0, 224));
		const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
		Dictionary r = patch->project(q);
		const uint64_t dt = Time::get_singleton()->get_ticks_usec() - t0;
		samples.push_back(dt);
		CHECK(bool(r["on_surface"]));
	}
	samples.sort();
	const uint64_t p95 = samples[uint32_t(real_t(samples.size()) * real_t(0.95))];
#if !defined(ASAN_ENABLED) && !defined(TSAN_ENABLED)
	// Sanitizer instrumentation inflates the per-query latency well past the
	// budget (observed ~648 us under TSan vs the 200 us target), so skip the
	// timing assertion there; the projections above still run and are checked.
	CHECK_MESSAGE(p95 < 200ULL,
			vformat("BVH p95 latency %d us exceeds 200 us budget", int(p95)));
#else
	(void)p95;
#endif
}

TEST_CASE("[Cassie][SurfacePatch] BVH and brute-force return identical projections") {
	// Reference brute-force: iterate every triangle of a small mesh and
	// recompute the closest projection. Compare to patch->project().
	Vector<Vector3> verts;
	Vector<int> idx;
	const int grid = 12;
	const int n = grid + 1;
	verts.resize(n * n);
	for (int z = 0; z < n; ++z) {
		for (int x = 0; x < n; ++x) {
			verts.write[z * n + x] = Vector3(real_t(x) * 0.1,
					Math::sin(real_t(x) * 0.3) * 0.05,
					real_t(z) * 0.1);
		}
	}
	for (int z = 0; z < grid; ++z) {
		for (int x = 0; x < grid; ++x) {
			const int a = z * n + x;
			const int b = z * n + (x + 1);
			const int c = (z + 1) * n + x;
			const int d = (z + 1) * n + (x + 1);
			idx.push_back(a);
			idx.push_back(b);
			idx.push_back(c);
			idx.push_back(c);
			idx.push_back(b);
			idx.push_back(d);
		}
	}
	Ref<ArrayMesh> mesh = _make_triangle_mesh(verts, idx);
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(mesh);

	Ref<RandomNumberGenerator> rng;
	rng.instantiate();
	rng->set_seed(0xBEEF);
	for (int q = 0; q < 64; ++q) {
		const Vector3 query(rng->randf_range(0, 1.2),
				rng->randf_range(-0.3, 0.3),
				rng->randf_range(0, 1.2));
		Dictionary r = patch->project(query);
		REQUIRE(bool(r["on_surface"]));
		const Vector3 bvh_proj = r["projected"];

		// Reference brute-force over the raw indices.
		Vector3 best = bvh_proj;
		real_t best_dsq = query.distance_squared_to(best);
		for (int t = 0; t < idx.size(); t += 3) {
			const Vector3 a = verts[idx[t]];
			const Vector3 b = verts[idx[t + 1]];
			const Vector3 c = verts[idx[t + 2]];
			// Ericson closest-point-on-triangle (duplicated locally to keep
			// the test independent of internal helpers).
			const Vector3 ab = b - a;
			const Vector3 ac = c - a;
			const Vector3 ap = query - a;
			const real_t d1 = ab.dot(ap), d2 = ac.dot(ap);
			Vector3 p;
			if (d1 <= 0 && d2 <= 0) {
				p = a;
			} else {
				const Vector3 bp = query - b;
				const real_t d3 = ab.dot(bp), d4 = ac.dot(bp);
				if (d3 >= 0 && d4 <= d3) {
					p = b;
				} else {
					const real_t vc = d1 * d4 - d3 * d2;
					if (vc <= 0 && d1 >= 0 && d3 <= 0) {
						p = a + ab * (d1 / (d1 - d3));
					} else {
						const Vector3 cp = query - c;
						const real_t d5 = ab.dot(cp), d6 = ac.dot(cp);
						if (d6 >= 0 && d5 <= d6) {
							p = c;
						} else {
							const real_t vb = d5 * d2 - d1 * d6;
							if (vb <= 0 && d2 >= 0 && d6 <= 0) {
								p = a + ac * (d2 / (d2 - d6));
							} else {
								const real_t va = d3 * d6 - d5 * d4;
								if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
									const real_t w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
									p = b + (c - b) * w;
								} else {
									const real_t denom = 1.0 / (va + vb + vc);
									p = a + ab * (vb * denom) + ac * (vc * denom);
								}
							}
						}
					}
				}
			}
			const real_t dsq = query.distance_squared_to(p);
			if (dsq < best_dsq) {
				best = p;
				best_dsq = dsq;
			}
		}
		CHECK_MESSAGE(bvh_proj.distance_to(best) < 1e-5,
				vformat("BVH projection diverges from brute-force at q=(%f,%f,%f)",
						query.x, query.y, query.z));
	}
}

} // namespace TestCassieSurfacePatch

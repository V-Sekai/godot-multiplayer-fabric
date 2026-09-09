/**************************************************************************/
/*  test_cassie_surface_manager.h                                         */
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

#include "../src/sketch/cassie_sketch_graph.h"
#include "../src/sketch/cassie_surface_manager.h"
#include "../src/sketch/cassie_surface_patch.h"

#include "core/math/vector3.h"
#include "tests/test_macros.h"

namespace TestCassieSurfaceManager {

static PackedVector3Array _segment(const Vector3 &a, const Vector3 &b,
		int p_samples = 16) {
	PackedVector3Array pts;
	pts.resize(p_samples);
	for (int i = 0; i < p_samples; ++i) {
		const real_t t = real_t(i) / real_t(p_samples - 1);
		pts.write[i] = a.lerp(b, t);
	}
	return pts;
}

static PackedVector3Array _up_normals(int p_count) {
	PackedVector3Array out;
	out.resize(p_count);
	for (int i = 0; i < p_count; ++i) {
		out.write[i] = Vector3(0, 0, 1);
	}
	return out;
}

TEST_CASE("[Cassie][SurfaceManager] no graph → update returns zero everything") {
	Ref<CassieSurfaceManager> mgr;
	mgr.instantiate();
	Dictionary r = mgr->update();
	const TypedArray<CassieSurfacePatch> new_p = r["new_patches"];
	const TypedArray<CassieSurfacePatch> rem_p = r["removed_patches"];
	CHECK_EQ(new_p.size(), 0);
	CHECK_EQ(rem_p.size(), 0);
	CHECK_EQ(int(r["active_count"]), 0);
	CHECK_EQ(mgr->get_patch_count(), 0);
}

TEST_CASE("[Cassie][SurfaceManager] empty graph → update yields no patches") {
	Ref<CassieSketchGraph> g;
	g.instantiate();
	Ref<CassieSurfaceManager> mgr;
	mgr.instantiate();
	mgr->set_graph(g);
	Dictionary r = mgr->update();
	const TypedArray<CassieSurfacePatch> new_p = r["new_patches"];
	CHECK_EQ(new_p.size(), 0);
	CHECK_EQ(int(r["active_count"]), 0);
}

TEST_CASE("[Cassie][SurfaceManager] three-stroke triangle yields exactly one new patch") {
	Ref<CassieSketchGraph> g;
	g.instantiate();
	const Vector3 v0(0, 0, 0);
	const Vector3 v1(1, 0, 0);
	const Vector3 v2(real_t(0.5), real_t(0.866), 0);
	const PackedVector3Array e0 = _segment(v0, v1);
	const PackedVector3Array e1 = _segment(v1, v2);
	const PackedVector3Array e2 = _segment(v2, v0);
	g->add_stroke(e0, _up_normals(e0.size()));
	g->add_stroke(e1, _up_normals(e1.size()));
	g->add_stroke(e2, _up_normals(e2.size()));

	Ref<CassieSurfaceManager> mgr;
	mgr.instantiate();
	mgr->set_graph(g);
	mgr->set_target_edge_length(real_t(0.15));
	mgr->set_async_triangulation(false); // keep tests deterministic/synchronous

	Dictionary r = mgr->update();
	const TypedArray<CassieSurfacePatch> new_p = r["new_patches"];
	const TypedArray<CassieSurfacePatch> rem_p = r["removed_patches"];
	REQUIRE_MESSAGE(new_p.size() == 1,
			vformat("triangle should produce 1 new patch on first update; got %d",
					new_p.size()));
	CHECK_EQ(rem_p.size(), 0);
	CHECK_EQ(int(r["active_count"]), 1);
	CHECK_EQ(mgr->get_patch_count(), 1);

	Ref<CassieSurfacePatch> patch = new_p[0];
	REQUIRE(patch.is_valid());
	Ref<Mesh> mesh = patch->get_mesh();
	REQUIRE(mesh.is_valid());
	CHECK(patch->get_vertex_count() >= 3);
	CHECK(patch->get_triangle_count() >= 1);
}

TEST_CASE("[Cassie][SurfaceManager] update is idempotent — second call yields zero new, zero removed") {
	Ref<CassieSketchGraph> g;
	g.instantiate();
	const Vector3 v0(0, 0, 0);
	const Vector3 v1(1, 0, 0);
	const Vector3 v2(real_t(0.5), real_t(0.866), 0);
	g->add_stroke(_segment(v0, v1), _up_normals(16));
	g->add_stroke(_segment(v1, v2), _up_normals(16));
	g->add_stroke(_segment(v2, v0), _up_normals(16));

	Ref<CassieSurfaceManager> mgr;
	mgr.instantiate();
	mgr->set_graph(g);
	mgr->set_target_edge_length(real_t(0.15));
	mgr->set_async_triangulation(false);

	Dictionary first = mgr->update();
	CHECK_EQ(int(first["active_count"]), 1);

	Dictionary second = mgr->update();
	const TypedArray<CassieSurfacePatch> new_p = second["new_patches"];
	const TypedArray<CassieSurfacePatch> rem_p = second["removed_patches"];
	CHECK_MESSAGE(new_p.size() == 0,
			vformat("repeated update should not re-materialize patches; got %d new",
					new_p.size()));
	CHECK_EQ(rem_p.size(), 0);
	CHECK_EQ(int(second["active_count"]), 1);
}

TEST_CASE("[Cassie][SurfaceManager] two-stroke open chain → no patches") {
	Ref<CassieSketchGraph> g;
	g.instantiate();
	const Vector3 shared(0.5, 0.5, 0);
	g->add_stroke(_segment(Vector3(0, 0, 0), shared), _up_normals(16));
	g->add_stroke(_segment(shared, Vector3(1, 1, 0)), _up_normals(16));

	Ref<CassieSurfaceManager> mgr;
	mgr.instantiate();
	mgr->set_graph(g);
	mgr->set_target_edge_length(real_t(0.15));
	mgr->set_async_triangulation(false); // keep tests deterministic/synchronous

	Dictionary r = mgr->update();
	const TypedArray<CassieSurfacePatch> new_p = r["new_patches"];
	CHECK_EQ(new_p.size(), 0);
	CHECK_EQ(int(r["active_count"]), 0);
}

TEST_CASE("[Cassie][SurfaceManager] clear drops all active patches") {
	Ref<CassieSketchGraph> g;
	g.instantiate();
	const Vector3 v0(0, 0, 0);
	const Vector3 v1(1, 0, 0);
	const Vector3 v2(real_t(0.5), real_t(0.866), 0);
	g->add_stroke(_segment(v0, v1), _up_normals(16));
	g->add_stroke(_segment(v1, v2), _up_normals(16));
	g->add_stroke(_segment(v2, v0), _up_normals(16));

	Ref<CassieSurfaceManager> mgr;
	mgr.instantiate();
	mgr->set_graph(g);
	mgr->set_target_edge_length(real_t(0.15));
	mgr->set_async_triangulation(false);
	mgr->update();
	CHECK_EQ(mgr->get_patch_count(), 1);

	mgr->clear();
	CHECK_EQ(mgr->get_patch_count(), 0);
	// After clear, update repopulates from the still-existing graph.
	Dictionary r = mgr->update();
	const TypedArray<CassieSurfacePatch> new_p = r["new_patches"];
	CHECK_EQ(new_p.size(), 1);
}

} // namespace TestCassieSurfaceManager

/**************************************************************************/
/*  refine.cpp                                                            */
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

#include "refine.h"

// PMP path (ENG-88 follow-up). Mirrors upstream
// cassie-triangulation/src/refine.cpp: pmp::uniform_remeshing on a
// SurfaceMesh built from p_verts + p_indices, with the boundary marked
// as a feature edge so pmp's split_long_edges keeps the polyline geometry
// intact. use_projection=true keeps refined vertices on the input
// (DMWT-output) surface, so no separate reference mesh is needed.
#include <pmp/algorithms/remeshing.h>
#include <pmp/surface_mesh.h>

void refine_patch(PackedVector3Array &p_verts, PackedInt32Array &p_indices,
		float p_target_edge_length) {
	if (p_verts.size() < 3 || p_indices.size() < 3 ||
			p_indices.size() % 3 != 0) {
		return;
	}

	pmp::SurfaceMesh mesh;
	std::vector<pmp::Vertex> vmap;
	vmap.reserve(p_verts.size());
	for (int i = 0; i < p_verts.size(); ++i) {
		const Vector3 v = p_verts[i];
		vmap.push_back(mesh.add_vertex(
				pmp::Point(double(v.x), double(v.y), double(v.z))));
	}
	for (int i = 0; i < p_indices.size(); i += 3) {
		const int a = p_indices[i + 0];
		const int b = p_indices[i + 1];
		const int c = p_indices[i + 2];
		if (a < 0 || b < 0 || c < 0 ||
				a >= int(vmap.size()) || b >= int(vmap.size()) || c >= int(vmap.size())) {
			continue;
		}
		// add_face returns invalid Face on non-manifold input (no-exception
		// patch in surface_mesh.cpp). Just skip the bad triangle.
		mesh.add_face({ vmap[a], vmap[b], vmap[c] });
	}
	if (mesh.n_faces() == 0) {
		return;
	}

	// Mark boundary as feature so split_long_edges places midpoints
	// exactly on the polyline (preserves the curve geometry the user drew).
	auto efeature = mesh.add_edge_property<bool>("e:feature", false);
	auto vfeature = mesh.add_vertex_property<bool>("v:feature", false);
	for (auto e : mesh.edges()) {
		if (mesh.is_boundary(e)) {
			efeature[e] = true;
			vfeature[mesh.vertex(e, 0)] = true;
			vfeature[mesh.vertex(e, 1)] = true;
		}
	}

	pmp::uniform_remeshing(mesh,
			static_cast<pmp::Scalar>(p_target_edge_length),
			/*iterations=*/3,
			/*use_projection=*/true);

	// Write back to p_verts / p_indices.
	PackedVector3Array out_verts;
	out_verts.resize(int(mesh.n_vertices()));
	int vi = 0;
	std::unordered_map<pmp::IndexType, int> vmap_out;
	vmap_out.reserve(mesh.n_vertices());
	for (auto v : mesh.vertices()) {
		const pmp::Point &p = mesh.position(v);
		out_verts.set(vi, Vector3(float(p[0]), float(p[1]), float(p[2])));
		vmap_out[v.idx()] = vi;
		++vi;
	}
	PackedInt32Array out_idx;
	out_idx.reserve(int(mesh.n_faces()) * 3);
	for (auto f : mesh.faces()) {
		int k = 0;
		int verts[3] = { 0, 0, 0 };
		for (auto v : mesh.vertices(f)) {
			if (k < 3) {
				verts[k] = vmap_out[v.idx()];
			}
			++k;
		}
		if (k == 3) {
			out_idx.push_back(verts[0]);
			out_idx.push_back(verts[1]);
			out_idx.push_back(verts[2]);
		}
	}
	p_verts = out_verts;
	p_indices = out_idx;
}

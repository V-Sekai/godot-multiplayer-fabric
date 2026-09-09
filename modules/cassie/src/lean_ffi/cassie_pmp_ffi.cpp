/**************************************************************************/
/*  cassie_pmp_ffi.cpp                                                    */
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

#include <lean/lean.h>
#include <pmp/algorithms/remeshing.h>
#include <pmp/algorithms/smoothing.h>
#include <pmp/surface_mesh.h>

#include <cstring>
#include <vector>

namespace {

// Cast a USize handle to the underlying mesh pointer.
inline pmp::SurfaceMesh *as_mesh(size_t handle) {
	return reinterpret_cast<pmp::SurfaceMesh *>(handle);
}

} // namespace

extern "C" {

// -- meshNew (n_verts, positions[FloatArray], n_tris, tris[ByteArray]) ---
LEAN_EXPORT lean_obj_res cassie_pmp_mesh_new(
		size_t n_verts, lean_obj_arg positions,
		size_t n_tris, lean_obj_arg tris, lean_obj_arg /*world*/) {
	auto *mesh = new pmp::SurfaceMesh();
	std::vector<pmp::Vertex> vmap;
	vmap.reserve(n_verts);
	const double *pos = lean_float_array_cptr(positions);
	for (size_t i = 0; i < n_verts; ++i) {
		vmap.push_back(mesh->add_vertex(
				pmp::Point(pos[3 * i + 0], pos[3 * i + 1], pos[3 * i + 2])));
	}
	const uint8_t *tris_bytes = lean_sarray_cptr(tris);
	const auto *idx = reinterpret_cast<const uint32_t *>(tris_bytes);
	for (size_t i = 0; i < n_tris; ++i) {
		const uint32_t a = idx[3 * i + 0];
		const uint32_t b = idx[3 * i + 1];
		const uint32_t c = idx[3 * i + 2];
		if (a >= n_verts || b >= n_verts || c >= n_verts) {
			continue;
		}
		if (a == b || b == c || a == c) {
			continue;
		}
		// Caller (Lean side) is expected to hand only manifold input.
		// PMP's add_face throws on non-manifold; the FFI lets that abort
		// the process so the offending cycle gets flagged at its source
		// rather than silently producing a half-built mesh.
		mesh->add_face({ vmap[a], vmap[b], vmap[c] });
	}
	lean_dec_ref(positions);
	lean_dec_ref(tris);
	return lean_io_result_mk_ok(lean_box_usize(reinterpret_cast<size_t>(mesh)));
}

LEAN_EXPORT lean_obj_res cassie_pmp_mesh_free(size_t handle, lean_obj_arg /*world*/) {
	delete as_mesh(handle);
	return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res cassie_pmp_mark_boundary_feature(
		size_t handle, lean_obj_arg /*world*/) {
	auto *mesh = as_mesh(handle);
	auto efeature = mesh->edge_property<bool>("e:feature", false);
	auto vfeature = mesh->vertex_property<bool>("v:feature", false);
	for (auto e : mesh->edges()) {
		if (mesh->is_boundary(e)) {
			efeature[e] = true;
			vfeature[mesh->vertex(e, 0)] = true;
			vfeature[mesh->vertex(e, 1)] = true;
		}
	}
	return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res cassie_pmp_uniform_remeshing(
		size_t handle, double target_edge_length, size_t iters,
		uint8_t use_projection, lean_obj_arg /*world*/) {
	auto *mesh = as_mesh(handle);
	pmp::uniform_remeshing(*mesh,
			static_cast<pmp::Scalar>(target_edge_length),
			static_cast<unsigned int>(iters),
			use_projection != 0);
	return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res cassie_pmp_implicit_smoothing(
		size_t handle, double timestep, uint8_t hold_boundary,
		lean_obj_arg /*world*/) {
	auto *mesh = as_mesh(handle);
	pmp::implicit_smoothing(*mesh,
			static_cast<pmp::Scalar>(timestep),
			/*use_uniform_laplace=*/false,
			/*rescale=*/false,
			hold_boundary != 0);
	return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res cassie_pmp_n_vertices(size_t handle, lean_obj_arg /*world*/) {
	return lean_io_result_mk_ok(lean_box_usize(as_mesh(handle)->n_vertices()));
}

LEAN_EXPORT lean_obj_res cassie_pmp_n_faces(size_t handle, lean_obj_arg /*world*/) {
	return lean_io_result_mk_ok(lean_box_usize(as_mesh(handle)->n_faces()));
}

LEAN_EXPORT lean_obj_res cassie_pmp_get_positions(
		size_t handle, lean_obj_arg out, lean_obj_arg /*world*/) {
	auto *mesh = as_mesh(handle);
	// Caller owns refcount; Lean's FloatArray bytecode emits a copy
	// when needed before invoking us, so we can write in-place.
	double *dst = lean_float_array_cptr(out);
	size_t i = 0;
	for (auto v : mesh->vertices()) {
		const pmp::Point &p = mesh->position(v);
		dst[3 * i + 0] = static_cast<double>(p[0]);
		dst[3 * i + 1] = static_cast<double>(p[1]);
		dst[3 * i + 2] = static_cast<double>(p[2]);
		++i;
	}
	return lean_io_result_mk_ok(out);
}

LEAN_EXPORT lean_obj_res cassie_pmp_get_triangles(
		size_t handle, lean_obj_arg out, lean_obj_arg /*world*/) {
	auto *mesh = as_mesh(handle);
	auto *dst = reinterpret_cast<uint32_t *>(lean_sarray_cptr(out));
	size_t i = 0;
	for (auto f : mesh->faces()) {
		int k = 0;
		uint32_t verts[3] = { 0, 0, 0 };
		for (auto v : mesh->vertices(f)) {
			if (k < 3) {
				verts[k] = static_cast<uint32_t>(v.idx());
			}
			++k;
		}
		if (k == 3) {
			dst[3 * i + 0] = verts[0];
			dst[3 * i + 1] = verts[1];
			dst[3 * i + 2] = verts[2];
			++i;
		}
	}
	return lean_io_result_mk_ok(out);
}

} // extern "C"

/**************************************************************************/
/*  cassie_geogram_ffi.cpp                                                */
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

#include <geogram/basic/common.h>
#include <geogram/delaunay/CDT_2d.h>
#include <lean/lean.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

std::atomic_bool geogram_initialized{ false };

void ensure_init() {
	bool expected = false;
	if (geogram_initialized.compare_exchange_strong(expected, true)) {
		GEO::initialize();
	}
}

struct DelaunayResult {
	std::vector<double> verts; // flat x,y,z (z = 0 for our 2D path)
	std::vector<uint32_t> tris; // flat a,b,c
};

inline DelaunayResult *as_result(size_t handle) {
	return reinterpret_cast<DelaunayResult *>(handle);
}

} // namespace

// Tears down geogram if ensure_init() ever ran. GEO::initialize() allocates a
// process-global Logger (CERRStream) that LeakSanitizer otherwise reports as a
// leak at engine shutdown; the cassie module calls this from its uninitialize
// hook. No-op when geogram was never initialized.
void cassie_geogram_shutdown() {
	bool expected = true;
	if (geogram_initialized.compare_exchange_strong(expected, false)) {
		GEO::terminate();
	}
}

extern "C" {

LEAN_EXPORT lean_obj_res cassie_geogram_delaunay_from_boundary(
		size_t n_pts, lean_obj_arg positions, double target_edge_length,
		lean_obj_arg /*world*/) {
	(void)target_edge_length;
	ensure_init();
	auto *res = new DelaunayResult();
	if (n_pts >= 3) {
		const double *src = lean_float_array_cptr(positions);
		// Geogram 2D Delaunay takes XY pairs; strip Y (CASSIE's "up").
		// Dedup: drop consecutive identical samples *and* drop a
		// trailing sample that equals the first (closed-loop redundancy).
		// CDT2d aborts (not throws) on duplicate constraint endpoints —
		// only the Lean side ever sees the post-dedup count.
		std::vector<double> xy;
		std::vector<size_t> kept;
		xy.reserve(2 * n_pts);
		kept.reserve(n_pts);
		const double eps2 = 1e-20;
		auto same = [&](size_t i, size_t j) -> bool {
			const double dx = src[3 * i + 0] - src[3 * j + 0];
			const double dz = src[3 * i + 2] - src[3 * j + 2];
			return dx * dx + dz * dz < eps2;
		};
		for (size_t i = 0; i < n_pts; ++i) {
			if (!kept.empty() && same(i, kept.back())) {
				continue;
			}
			kept.push_back(i);
			xy.push_back(src[3 * i + 0]);
			xy.push_back(src[3 * i + 2]);
		}
		while (kept.size() >= 2 && same(kept.front(), kept.back())) {
			kept.pop_back();
			xy.pop_back();
			xy.pop_back();
		}
		n_pts = kept.size();
		if (n_pts < 3) {
			lean_dec_ref(positions);
			return lean_io_result_mk_ok(lean_box_usize(reinterpret_cast<size_t>(res)));
		}
		// CDT2d is *not* a Delaunay factory creator — it's a direct class
		// (CDTBase2d subclass), so GEO::Delaunay::create(2, "BDEL2d")
		// returns null. Drive it directly: enclosing rect → insert points
		// as constrained boundary edges (closed polyline) → remove the
		// external triangles so only the interior triangulation remains.
		double xmin = xy[0], xmax = xy[0], ymin = xy[1], ymax = xy[1];
		for (size_t i = 1; i < n_pts; ++i) {
			xmin = (xy[2 * i + 0] < xmin) ? xy[2 * i + 0] : xmin;
			xmax = (xy[2 * i + 0] > xmax) ? xy[2 * i + 0] : xmax;
			ymin = (xy[2 * i + 1] < ymin) ? xy[2 * i + 1] : ymin;
			ymax = (xy[2 * i + 1] > ymax) ? xy[2 * i + 1] : ymax;
		}
		const double pad = ((xmax - xmin) + (ymax - ymin)) * 0.5 + 1.0;
		GEO::CDT2d cdt;
		cdt.create_enclosing_rectangle(
				xmin - pad, ymin - pad, xmax + pad, ymax + pad);
		// Rectangle inserted 4 corner verts at indices 0..3 — user points
		// start at 4. Collect their cdt indices so we can remap and emit
		// boundary constraints.
		std::vector<GEO::index_t> user_idx(n_pts);
		for (size_t i = 0; i < n_pts; ++i) {
			user_idx[i] = cdt.insert(GEO::vec2(xy[2 * i + 0], xy[2 * i + 1]));
		}
		// Treat the input as a closed boundary polyline so we get a
		// constrained Delaunay of the polygon interior (matches what
		// CASSIE wants for surface fairing inside a cycle).
		for (size_t i = 0; i < n_pts; ++i) {
			GEO::index_t a = user_idx[i];
			GEO::index_t b = user_idx[(i + 1) % n_pts];
			if (a != b) {
				cdt.insert_constraint(a, b);
			}
		}
		cdt.remove_external_triangles(false);
		const GEO::index_t nb_v = cdt.nv();
		const GEO::index_t nb_t = cdt.nT();
		// Only emit verts that surviving triangles reference — drops the
		// 4 enclosing-rect corners (and any unused Steiners) so PMP gets
		// a clean connected mesh.
		std::vector<int32_t> remap(nb_v, -1);
		std::vector<int32_t> inv;
		inv.reserve(n_pts);
		for (GEO::index_t t = 0; t < nb_t; ++t) {
			for (int lv = 0; lv < 3; ++lv) {
				const GEO::index_t v = cdt.Tv(t, lv);
				if (remap[v] < 0) {
					remap[v] = int32_t(inv.size());
					int32_t input_idx = -1;
					for (size_t i = 0; i < n_pts; ++i) {
						if (user_idx[i] == v) {
							input_idx = int32_t(kept[i]);
							break;
						}
					}
					inv.push_back(input_idx);
				}
			}
		}
		const size_t out_v = inv.size();
		res->verts.resize(3 * out_v);
		for (GEO::index_t v = 0; v < nb_v; ++v) {
			if (remap[v] < 0) {
				continue;
			}
			const GEO::vec2 p = cdt.point(v);
			const size_t o = size_t(remap[v]);
			res->verts[3 * o + 0] = p.x;
			res->verts[3 * o + 1] = (inv[o] >= 0) ? src[3 * inv[o] + 1] : 0.0;
			res->verts[3 * o + 2] = p.y;
		}
		res->tris.reserve(3 * nb_t);
		for (GEO::index_t t = 0; t < nb_t; ++t) {
			res->tris.push_back(uint32_t(remap[cdt.Tv(t, 0)]));
			res->tris.push_back(uint32_t(remap[cdt.Tv(t, 1)]));
			res->tris.push_back(uint32_t(remap[cdt.Tv(t, 2)]));
		}
	}
	lean_dec_ref(positions);
	return lean_io_result_mk_ok(lean_box_usize(reinterpret_cast<size_t>(res)));
}

LEAN_EXPORT lean_obj_res cassie_geogram_delaunay_free(
		size_t handle, lean_obj_arg /*world*/) {
	delete as_result(handle);
	return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res cassie_geogram_delaunay_n_vertices(
		size_t handle, lean_obj_arg /*world*/) {
	return lean_io_result_mk_ok(lean_box_usize(as_result(handle)->verts.size() / 3));
}

LEAN_EXPORT lean_obj_res cassie_geogram_delaunay_n_triangles(
		size_t handle, lean_obj_arg /*world*/) {
	return lean_io_result_mk_ok(lean_box_usize(as_result(handle)->tris.size() / 3));
}

LEAN_EXPORT lean_obj_res cassie_geogram_delaunay_get_positions(
		size_t handle, lean_obj_arg out, lean_obj_arg /*world*/) {
	const auto *res = as_result(handle);
	double *dst = lean_float_array_cptr(out);
	std::memcpy(dst, res->verts.data(), res->verts.size() * sizeof(double));
	return lean_io_result_mk_ok(out);
}

LEAN_EXPORT lean_obj_res cassie_geogram_delaunay_get_triangles(
		size_t handle, lean_obj_arg out, lean_obj_arg /*world*/) {
	const auto *res = as_result(handle);
	auto *dst = reinterpret_cast<uint32_t *>(lean_sarray_cptr(out));
	std::memcpy(dst, res->tris.data(), res->tris.size() * sizeof(uint32_t));
	return lean_io_result_mk_ok(out);
}

} // extern "C"

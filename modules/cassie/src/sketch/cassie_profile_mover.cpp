/**************************************************************************/
/*  cassie_profile_mover.cpp                                              */
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

#include "cassie_profile_mover.h"

#include "../solver/cassie_pcg.h"
#include "../solver/slang_dispatch/cassie_mas_gpu.h"
#include "../solver/slang_dispatch/cassie_slang_gpu.h"
#include "cassie_curvenet_knot.h"
#include "cassie_final_stroke.h"

#include "core/math/disjoint_set.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/os/time.h"
#include "core/templates/hash_map.h"
#include "core/templates/pair.h"
#include "scene/resources/curve.h"

#include <algorithm>

// PIMPL state for the Phase A Jacobi-PCG harmonic solve (Track 5 —
// Eigen-removal). CSR sparse layout for L_II + L_IB, Jacobi
// preconditioner cached as 1/diag(L_II), and the previous frame's
// solution per axis for warm-starting the next deform call.
struct CassieProfileMoverHarmonic {
	cassie_pcg::CSRMatrix L_II;
	cassie_pcg::CSRMatrix L_IB;
	LocalVector<double> diag_inv; // size Ni; 1 / max(diag(L_II), eps)
	// Warm-start cache — one buffer per axis (x / y / z). bind() must
	// invalidate these on rebind to avoid topology mismatch.
	LocalVector<double> warm_x;
	LocalVector<double> warm_y;
	LocalVector<double> warm_z;
	int last_solve_iters = 0;

	// GPU float3 PCG state — built lazily at end of _build_harmonic_state
	// when a RenderingDevice is available. Per-frame deform() updates the
	// RHS via cg.update_cg3_rhs and calls solve_sparse_gpu_jacobi3,
	// reusing the cached matrix + diag_inv + uniform sets. ~22 ms / solve
	// vs ~18 s for the 3 sequential CPU PCG axes.
	std::unique_ptr<cassie_slang_gpu::CassieSlangGpu> gpu_cg;
	cassie_slang_gpu::CassieSlangGpu::CgPcg3Handle gpu_handle;
	LocalVector<float> rhs_f3_unpacked; // 3 floats per row (xyz)
	LocalVector<float> x_init_f3_unpacked; // warm-start packed xyz
	LocalVector<float> sol_f3_unpacked; // solution xyz
};

CassieProfileMover::CassieProfileMover() = default;
CassieProfileMover::~CassieProfileMover() {
	if (harmonic_impl && harmonic_impl->gpu_cg) {
		harmonic_impl->gpu_cg->free_cg3_state(harmonic_impl->gpu_handle);
	}
}

namespace {

// DynamicBVH::aabb_query is _FORCE_INLINE_ and allocates its traversal
// stack with alloca. When GCC inlines it into a caller, the alloca lands in
// the caller's frame, and memory alloca'd by an inlined callee is only
// reclaimed when the caller returns — so the expanding-radius loop in
// bind() (up to 24 queries per vertex across a 10k-vertex mesh) accumulates
// ~1 KB of stack per query and overflows the thread stack: SIGSEGV on the
// stack[0] write at dynamic_bvh.h:332 on every GCC build. Clang wraps
// inlined allocas in stack save/restore and was unaffected. This noinline
// wrapper gives each query its own frame, so the alloca dies on return.
template <typename Collector>
_NO_INLINE_ void query_bvh_frame_scoped(DynamicBVH &p_bvh, const AABB &p_box, Collector &r_collector) {
	p_bvh.aabb_query(p_box, r_collector);
}

inline void insert_into_topk(int p_K, int *p_idx, real_t *p_dsq, int p_new_idx, real_t p_new_dsq) {
	for (int k = 0; k < p_K; ++k) {
		if (p_new_dsq < p_dsq[k]) {
			for (int j = p_K - 1; j > k; --j) {
				p_idx[j] = p_idx[j - 1];
				p_dsq[j] = p_dsq[j - 1];
			}
			p_idx[k] = p_new_idx;
			p_dsq[k] = p_new_dsq;
			return;
		}
	}
}

} // namespace

void CassieProfileMover::bind(const Ref<CassieSurfacePatch> &p_patch,
		const Ref<CassieCurvenet> &p_curvenet) {
	bound_patch = p_patch;
	bound_curvenet = p_curvenet;
	rest_vertex_positions.clear();
	rest_vertex_normals.clear();
	triangles.clear();
	rest_face_frame_inv.clear();
	curve_sample_offset.clear();
	bind_sample_positions.clear();
	sample_knot_a.clear();
	sample_knot_b.clear();
	sample_t.clear();
	bind_knot_positions.clear();
	bind_knot_transforms.clear();
	vertex_sample_indices.clear();
	vertex_sample_weights.clear();

	if (p_patch.is_null() || p_curvenet.is_null()) {
		return;
	}
	const int vc = p_patch->get_vertex_count();
	const int tc = p_patch->get_triangle_count();
	if (vc == 0 || tc == 0) {
		return;
	}

	// 1. Snapshot mesh.
	rest_vertex_positions.resize(vc);
	rest_vertex_normals.resize(vc);
	for (int i = 0; i < vc; ++i) {
		rest_vertex_positions[i] = p_patch->get_vertex_position(i);
		rest_vertex_normals[i] = p_patch->get_vertex_normal(i);
	}
	triangles.resize(tc);
	for (int i = 0; i < tc; ++i) {
		triangles[i] = p_patch->get_triangle_indices(i);
	}

	// 2. Snapshot knot rest positions + full transforms. Position-only
	// stays for the existing translation-delta path; the full Transform
	// powers the rotation/scale wiring in deform() (ENG-65 phase 1).
	const TypedArray<CassieCurvenetKnot> knots = p_curvenet->get_knots();
	const int kc = knots.size();
	bind_knot_positions.resize(kc);
	bind_knot_transforms.resize(kc);
	for (int i = 0; i < kc; ++i) {
		Ref<CassieCurvenetKnot> k = knots[i];
		const Transform3D t = k.is_valid()
				? k->get_rest_pose_transform()
				: Transform3D();
		bind_knot_positions[i] = t.origin;
		bind_knot_transforms[i] = t;
	}

	// 2b. Reset crack accounting; the cut pass runs after sampling.
	crack_edge_count_value = 0;

	// 3. Sample every curve.
	const TypedArray<CassieFinalStroke> curves = p_curvenet->get_curves();
	const int cc = curves.size();
	curve_sample_offset.reserve(cc + 1);
	curve_sample_offset.push_back(0);
	for (int c = 0; c < cc; ++c) {
		Ref<CassieFinalStroke> stroke = curves[c];
		if (stroke.is_null()) {
			curve_sample_offset.push_back(int(bind_sample_positions.size()));
			continue;
		}
		Ref<Curve3D> curve = stroke->get_curve();
		if (curve.is_null() || curve->get_baked_length() <= real_t(0.0)) {
			curve_sample_offset.push_back(int(bind_sample_positions.size()));
			continue;
		}
		const Vector2i endpoints = p_curvenet->get_curve_endpoint_knots(c);
		const real_t L = curve->get_baked_length();
		for (int s = 0; s < kSamplesPerCurve; ++s) {
			const real_t t = real_t(s) / real_t(kSamplesPerCurve - 1);
			const Vector3 pos = curve->sample_baked(t * L);
			bind_sample_positions.push_back(pos);
			sample_knot_a.push_back(endpoints.x);
			sample_knot_b.push_back(endpoints.y);
			sample_t.push_back(t);
		}
		curve_sample_offset.push_back(int(bind_sample_positions.size()));
	}

	// 3b. Cut-cell crack pass — duplicates mesh vertices that lie on a
	// curvenet curve when their incident triangles straddle the curve. All
	// downstream bookkeeping (IDW K-nearest, harmonic Laplacian, deform
	// output) sees the cut topology, so re-fetch vc after the cut.
	_apply_cut_topology();
	const int vc_post_cut = int(rest_vertex_positions.size());

	// 4. Per-vertex K-nearest IDW. Brute-force fell over at 100k+ vertices,
	// so build a DynamicBVH over the sample positions and query with an
	// expanding box until the K-th nearest distance lies inside the box.
	const int N_samples = int(bind_sample_positions.size());
	vertex_sample_indices.resize(vc_post_cut * K);
	vertex_sample_weights.resize(vc_post_cut * K);
	for (int i = 0; i < int(vertex_sample_indices.size()); ++i) {
		vertex_sample_indices[i] = -1;
		vertex_sample_weights[i] = real_t(0.0);
	}
	sample_bvh.clear();
	if (N_samples == 0) {
		return; // no curvenet to drive deform — bind() leaves identity behavior
	}

	const real_t kPointEps = real_t(1e-4);
	for (int s = 0; s < N_samples; ++s) {
		const Vector3 &p = bind_sample_positions[s];
		AABB box(p - Vector3(kPointEps, kPointEps, kPointEps),
				Vector3(kPointEps, kPointEps, kPointEps) * real_t(2.0));
		sample_bvh.insert(box, reinterpret_cast<void *>(uintptr_t(s)));
	}
	// Initial search radius = 4× average consecutive-sample spacing.
	double total_spacing = 0.0;
	int spacing_count = 0;
	for (uint32_t c = 0; c + 1 < curve_sample_offset.size(); ++c) {
		const int s0 = curve_sample_offset[c];
		const int s1 = curve_sample_offset[c + 1];
		for (int s = s0; s + 1 < s1; ++s) {
			total_spacing += double(
					bind_sample_positions[s].distance_to(bind_sample_positions[s + 1]));
			++spacing_count;
		}
	}
	initial_sample_search_radius = spacing_count > 0
			? real_t(total_spacing / double(spacing_count)) * real_t(4.0)
			: real_t(1.0);
	if (initial_sample_search_radius <= real_t(0.0)) {
		initial_sample_search_radius = real_t(1.0);
	}

	struct NearestKCollector {
		const CassieProfileMover *self;
		Vector3 query;
		int *near_idx;
		real_t *near_dsq;
		int top_k;
		bool operator()(void *p_data) {
			const int s = int(uintptr_t(p_data));
			const real_t dsq = query.distance_squared_to(self->bind_sample_positions[s]);
			if (dsq < near_dsq[top_k - 1]) {
				insert_into_topk(top_k, near_idx, near_dsq, s, dsq);
			}
			return false; // keep visiting
		}
	};

	for (int v = 0; v < vc_post_cut; ++v) {
		const Vector3 &p = rest_vertex_positions[v];
		int near_idx[K];
		real_t near_dsq[K];
		for (int k = 0; k < K; ++k) {
			near_idx[k] = -1;
			near_dsq[k] = real_t(INFINITY);
		}
		NearestKCollector c{ this, p, near_idx, near_dsq, K };
		real_t r = initial_sample_search_radius;
		for (int attempt = 0; attempt < 24; ++attempt) {
			AABB box(p - Vector3(r, r, r), Vector3(r, r, r) * real_t(2.0));
			query_bvh_frame_scoped(sample_bvh, box, c);
			// Converged once K slots are filled AND the farthest of the K
			// lies within the search radius (any unseen sample would be
			// strictly farther).
			if (near_idx[K - 1] >= 0 &&
					Math::sqrt(near_dsq[K - 1]) <= r) {
				break;
			}
			r *= real_t(2.0);
		}
		// Inverse-distance weights with epsilon to avoid division by zero.
		real_t w[K];
		real_t total_w = real_t(0.0);
		for (int k = 0; k < K; ++k) {
			if (near_idx[k] < 0) {
				w[k] = real_t(0.0);
				continue;
			}
			const real_t d = Math::sqrt(near_dsq[k]);
			w[k] = real_t(1.0) / (d + real_t(1e-6));
			total_w += w[k];
		}
		const real_t inv_total = total_w > real_t(1e-12) ? real_t(1.0) / total_w : real_t(0.0);
		for (int k = 0; k < K; ++k) {
			vertex_sample_indices[v * K + k] = near_idx[k];
			vertex_sample_weights[v * K + k] = w[k] * inv_total;
		}
	}

	// Phase 1.4b — build the harmonic-solve state on top of the IDW state.
	// On failure (degenerate geometry, no boundary identified, sparse-LDLT
	// rejected, etc.), harmonic_ready stays false and deform() falls back
	// to IDW.
	_build_harmonic_state();
}

int CassieProfileMover::get_last_solve_iters() const {
	return harmonic_impl ? harmonic_impl->last_solve_iters : 0;
}

int CassieProfileMover::get_forward_level_size(int p_level) const {
	if (p_level < 0 || p_level + 1 >= int(forward_level_offset.size())) {
		return 0;
	}
	return forward_level_offset[p_level + 1] - forward_level_offset[p_level];
}

int CassieProfileMover::get_backward_level_size(int p_level) const {
	if (p_level < 0 || p_level + 1 >= int(backward_level_offset.size())) {
		return 0;
	}
	return backward_level_offset[p_level + 1] - backward_level_offset[p_level];
}

void CassieProfileMover::_apply_cut_topology() {
	// Vertex-doubling cut. For each mesh vertex whose nearest curvenet
	// sample is within ~half an edge, classify each incident triangle by
	// the sign of (centroid - vertex) · side_normal where side_normal =
	// curve_tangent × vertex_surface_normal. When both signs are present,
	// duplicate the vertex: the original keeps the negative-side
	// triangles, the duplicate (appended to rest_vertex_positions) takes
	// the positive-side triangles. The cotangent Laplacian then has no
	// cross-curve cot contributions because no triangle uses both copies.

	const int vc = int(rest_vertex_positions.size());
	const int tc = int(triangles.size());
	const int N_samples = int(bind_sample_positions.size());
	if (vc == 0 || tc == 0 || N_samples == 0) {
		return;
	}

	// Average edge length sets the on-curve threshold.
	double total_edge = 0.0;
	int edge_count = 0;
	for (int t = 0; t < tc; ++t) {
		const Vector3i tri = triangles[t];
		const Vector3 &a = rest_vertex_positions[tri.x];
		const Vector3 &b = rest_vertex_positions[tri.y];
		const Vector3 &c = rest_vertex_positions[tri.z];
		total_edge += double(a.distance_to(b)) + double(b.distance_to(c)) + double(c.distance_to(a));
		edge_count += 3;
	}
	const double avg_edge = edge_count > 0 ? total_edge / double(edge_count) : 1.0;
	const real_t on_curve_thresh = real_t(avg_edge * 0.5);
	const real_t on_curve_thresh_sq = on_curve_thresh * on_curve_thresh;

	// Per-vertex incident-triangle list.
	LocalVector<LocalVector<int>> vert_to_tris;
	vert_to_tris.resize(vc);
	for (int t = 0; t < tc; ++t) {
		vert_to_tris[triangles[t].x].push_back(t);
		vert_to_tris[triangles[t].y].push_back(t);
		vert_to_tris[triangles[t].z].push_back(t);
	}

	// Per-vertex average surface normal (sum of incident face normals).
	LocalVector<Vector3> vert_normals;
	vert_normals.resize(vc);
	for (int v = 0; v < vc; ++v) {
		vert_normals[v] = Vector3();
	}
	for (int t = 0; t < tc; ++t) {
		const Vector3i tri = triangles[t];
		const Vector3 &a = rest_vertex_positions[tri.x];
		const Vector3 &b = rest_vertex_positions[tri.y];
		const Vector3 &c = rest_vertex_positions[tri.z];
		const Vector3 fn = (b - a).cross(c - a);
		vert_normals[tri.x] += fn;
		vert_normals[tri.y] += fn;
		vert_normals[tri.z] += fn;
	}
	for (int v = 0; v < vc; ++v) {
		const Vector3 n = vert_normals[v];
		if (n.length_squared() > real_t(1e-12)) {
			vert_normals[v] = n.normalized();
		}
	}

	// Find the curve a given sample belongs to.
	auto curve_of_sample = [&](int p_sample) -> int {
		for (uint32_t c = 0; c + 1 < curve_sample_offset.size(); ++c) {
			if (p_sample >= curve_sample_offset[c] &&
					p_sample < curve_sample_offset[c + 1]) {
				return int(c);
			}
		}
		return -1;
	};

	int doubled_count = 0;
	for (int v = 0; v < vc; ++v) {
		const Vector3 &p = rest_vertex_positions[v];
		// Nearest sample (brute force — N_samples is small compared to vc).
		int near_s = -1;
		real_t near_dsq = real_t(INFINITY);
		for (int s = 0; s < N_samples; ++s) {
			const real_t dsq = p.distance_squared_to(bind_sample_positions[s]);
			if (dsq < near_dsq) {
				near_dsq = dsq;
				near_s = s;
			}
		}
		if (near_s < 0 || near_dsq > on_curve_thresh_sq) {
			continue;
		}
		const int c_idx = curve_of_sample(near_s);
		if (c_idx < 0) {
			continue;
		}
		const int s0 = curve_sample_offset[c_idx];
		const int s1 = curve_sample_offset[c_idx + 1];
		// Only cut at curve interiors. Endpoint samples mark knot
		// positions where multiple curves can converge; cutting there
		// would slit the mesh open at every knot (a cube corner sits at
		// a 3-curve intersection — cutting it produces 16 vertices for
		// what should stay as 8).
		if (near_s == s0 || near_s == s1 - 1) {
			continue;
		}
		const int prev_s = near_s - 1;
		const int next_s = near_s + 1;
		if (prev_s == next_s) {
			continue;
		}
		const Vector3 tangent = (bind_sample_positions[next_s] - bind_sample_positions[prev_s]).normalized();
		const Vector3 side_n_raw = tangent.cross(vert_normals[v]);
		if (side_n_raw.length_squared() < real_t(1e-12)) {
			continue;
		}
		const Vector3 side_n = side_n_raw.normalized();

		// Classify incident triangles by side.
		const LocalVector<int> &incident = vert_to_tris[v];
		LocalVector<int> tris_pos;
		LocalVector<int> tris_neg;
		for (uint32_t i = 0; i < incident.size(); ++i) {
			const int t = incident[i];
			const Vector3i tri = triangles[t];
			const Vector3 centroid = (rest_vertex_positions[tri.x] +
											 rest_vertex_positions[tri.y] +
											 rest_vertex_positions[tri.z]) /
					real_t(3.0);
			const real_t side_val = (centroid - p).dot(side_n);
			if (side_val > real_t(0.0)) {
				tris_pos.push_back(t);
			} else {
				tris_neg.push_back(t);
			}
		}
		if (tris_pos.is_empty() || tris_neg.is_empty()) {
			continue; // all triangles on one side — no cut here
		}

		// Duplicate the vertex; route positive-side triangles to the copy.
		const int v_dup = int(rest_vertex_positions.size());
		rest_vertex_positions.push_back(p);
		// Mirror the normal so the cut-cell duplicate shades the same
		// as its source. Authored shading carries through both sides
		// of the crack.
		if (uint32_t(v) < rest_vertex_normals.size()) {
			rest_vertex_normals.push_back(rest_vertex_normals[v]);
		} else {
			rest_vertex_normals.push_back(Vector3(0, 1, 0));
		}
		for (uint32_t i = 0; i < tris_pos.size(); ++i) {
			const int t = tris_pos[i];
			Vector3i &cut_tri = triangles[t];
			if (cut_tri.x == v) {
				cut_tri.x = v_dup;
			}
			if (cut_tri.y == v) {
				cut_tri.y = v_dup;
			}
			if (cut_tri.z == v) {
				cut_tri.z = v_dup;
			}
		}
		++doubled_count;
	}
	crack_edge_count_value = doubled_count;
}

void CassieProfileMover::_build_harmonic_state() {
	harmonic_ready = false;
	mesh_to_partition.clear();
	is_boundary.clear();
	interior_to_mesh.clear();
	boundary_to_mesh.clear();
	boundary_driver_sample.clear();
	boundary_rest_offset.clear();
	interior_detail.clear();
	harmonic_impl.reset();

	const int vc = int(rest_vertex_positions.size());
	const int tc = int(triangles.size());
	const int N_samples = int(bind_sample_positions.size());
	if (vc < 3 || tc == 0 || N_samples == 0) {
		return;
	}

	// Average mesh edge length — sets the boundary "near a curve" threshold.
	double total_edge = 0.0;
	int edge_count = 0;
	for (int t = 0; t < tc; ++t) {
		const Vector3i tri = triangles[t];
		const Vector3 &a = rest_vertex_positions[tri.x];
		const Vector3 &b = rest_vertex_positions[tri.y];
		const Vector3 &c = rest_vertex_positions[tri.z];
		total_edge += double(a.distance_to(b)) + double(b.distance_to(c)) + double(c.distance_to(a));
		edge_count += 3;
	}
	const double avg_edge = edge_count > 0 ? total_edge / double(edge_count) : 1.0;
	const double boundary_thresh = avg_edge * 1.25; // < ~one edge from a curve sample
	const double boundary_thresh_sq = boundary_thresh * boundary_thresh;

	// Classify each vertex by distance to its nearest sample (already
	// available in vertex_sample_indices[v * K + 0] from the IDW pass).
	mesh_to_partition.resize(vc);
	is_boundary.resize(vc);
	for (int v = 0; v < vc; ++v) {
		const int nearest_s = vertex_sample_indices[v * K + 0];
		if (nearest_s < 0) {
			is_boundary[v] = false;
			continue;
		}
		const double dsq = double(rest_vertex_positions[v].distance_squared_to(
				bind_sample_positions[nearest_s]));
		is_boundary[v] = dsq <= boundary_thresh_sq;
	}
	// Build I-list and B-list.
	for (int v = 0; v < vc; ++v) {
		if (is_boundary[v]) {
			mesh_to_partition[v] = int(boundary_to_mesh.size());
			boundary_to_mesh.push_back(v);
		} else {
			mesh_to_partition[v] = int(interior_to_mesh.size());
			interior_to_mesh.push_back(v);
		}
	}
	const int Ni = int(interior_to_mesh.size());
	const int Nb = int(boundary_to_mesh.size());
	if (Ni == 0 || Nb == 0) {
		return; // need both partitions
	}

	// Boundary driver assignments — each boundary vertex tracks its nearest
	// sample and the rest-pose offset from sample to vertex.
	boundary_driver_sample.resize(Nb);
	boundary_rest_offset.resize(Nb);
	for (int b = 0; b < Nb; ++b) {
		const int v = boundary_to_mesh[b];
		const int s = vertex_sample_indices[v * K + 0];
		boundary_driver_sample[b] = s;
		boundary_rest_offset[b] = rest_vertex_positions[v] - bind_sample_positions[s];
	}

	// Track 5 Phase A — assemble the cotangent Laplacian directly into
	// CSR L_II + L_IB. We bypass the dense `L` intermediate that the
	// Eigen-era path used: triplets land in per-row HashMaps that we
	// then lower to CSR in a single sort-by-row pass. Sliver triangles
	// produce huge cot values; clamp at assembly so Jacobi-PCG's
	// diagonal dominance holds.
	const double kCotClamp = 1e6;
	auto cot_at = [&](const Vector3 &p, const Vector3 &q, const Vector3 &r) -> double {
		const Vector3 u = q - p;
		const Vector3 v = r - p;
		const double dot = double(u.dot(v));
		const double cross = double(u.cross(v).length());
		if (cross <= 1e-12) {
			return 0.0;
		}
		double c = dot / cross;
		if (c > kCotClamp) {
			c = kCotClamp;
		} else if (c < -kCotClamp) {
			c = -kCotClamp;
		}
		return c;
	};

	// Per-row coefficient accumulators keyed by column. L_II rows are
	// keyed by interior index (li); L_IB rows are also keyed by li (col
	// is the boundary index). Duplicate triplets merge by summation —
	// the cotangent assembly emits many per (i,j) pair.
	LocalVector<HashMap<int, double>> rows_II;
	LocalVector<HashMap<int, double>> rows_IB;
	rows_II.resize(Ni);
	rows_IB.resize(Ni);

	auto deposit = [&](int p_i, int p_j, double p_w) {
		if (is_boundary[p_i]) {
			return; // boundary rows don't participate in the harmonic solve
		}
		const int li = mesh_to_partition[p_i];
		if (is_boundary[p_j]) {
			const int bj = mesh_to_partition[p_j];
			HashMap<int, double>::Iterator it = rows_IB[li].find(bj);
			if (it == rows_IB[li].end()) {
				rows_IB[li].insert(bj, p_w);
			} else {
				it->value += p_w;
			}
		} else {
			const int lj = mesh_to_partition[p_j];
			HashMap<int, double>::Iterator it = rows_II[li].find(lj);
			if (it == rows_II[li].end()) {
				rows_II[li].insert(lj, p_w);
			} else {
				it->value += p_w;
			}
		}
	};

	for (int t = 0; t < tc; ++t) {
		const Vector3i tri = triangles[t];
		const Vector3 &a = rest_vertex_positions[tri.x];
		const Vector3 &b = rest_vertex_positions[tri.y];
		const Vector3 &c = rest_vertex_positions[tri.z];
		const double half_cot_a = 0.5 * cot_at(a, b, c);
		const double half_cot_b = 0.5 * cot_at(b, c, a);
		const double half_cot_c = 0.5 * cot_at(c, a, b);
		auto add_edge = [&](int i, int j, double w) {
			deposit(i, j, -w);
			deposit(j, i, -w);
			deposit(i, i, w);
			deposit(j, j, w);
		};
		add_edge(tri.y, tri.z, half_cot_a);
		add_edge(tri.z, tri.x, half_cot_b);
		add_edge(tri.x, tri.y, half_cot_c);
	}

	// Regularize the diagonal so Jacobi-PCG's preconditioner is
	// well-defined on numerically-zero rows (degenerate triangles,
	// isolated interior vertices). Same eps as the prior LDLT path.
	const double kDiagReg = 1e-9;
	for (int li = 0; li < Ni; ++li) {
		HashMap<int, double>::Iterator it = rows_II[li].find(li);
		if (it == rows_II[li].end()) {
			rows_II[li].insert(li, kDiagReg);
		} else {
			it->value += kDiagReg;
		}
	}

	harmonic_impl.reset(new CassieProfileMoverHarmonic());

	auto lower_to_csr = [](const LocalVector<HashMap<int, double>> &p_rows,
								int p_cols, cassie_pcg::CSRMatrix &r_csr) {
		const int n_rows = int(p_rows.size());
		r_csr.rows = n_rows;
		r_csr.cols = p_cols;
		r_csr.row_ptr.resize(n_rows + 1);
		r_csr.row_ptr[0] = 0;
		int nnz = 0;
		for (int i = 0; i < n_rows; ++i) {
			nnz += int(p_rows[i].size());
			r_csr.row_ptr[i + 1] = nnz;
		}
		r_csr.col_idx.resize(nnz);
		r_csr.val.resize(nnz);
		for (int i = 0; i < n_rows; ++i) {
			int cursor = r_csr.row_ptr[i];
			// Snapshot keys for in-row ordering by column.
			LocalVector<Pair<int, double>> entries;
			entries.reserve(p_rows[i].size());
			for (const KeyValue<int, double> &kv : p_rows[i]) {
				entries.push_back({ kv.key, kv.value });
			}
			struct ByCol {
				bool operator()(const Pair<int, double> &a, const Pair<int, double> &b) const {
					return a.first < b.first;
				}
			};
			entries.sort_custom<ByCol>();
			for (uint32_t e = 0; e < entries.size(); ++e) {
				r_csr.col_idx[cursor] = entries[e].first;
				r_csr.val[cursor] = entries[e].second;
				++cursor;
			}
		}
	};

	lower_to_csr(rows_II, Ni, harmonic_impl->L_II);
	lower_to_csr(rows_IB, Nb, harmonic_impl->L_IB);

	// Jacobi preconditioner = 1 / max(diag(L_II), eps_reg).
	harmonic_impl->diag_inv.resize(Ni);
	LocalVector<double> diag_raw;
	diag_raw.resize(Ni);
	cassie_pcg::extract_diagonal(harmonic_impl->L_II, diag_raw.ptr());
	for (int i = 0; i < Ni; ++i) {
		const double d = diag_raw[i] > kDiagReg ? diag_raw[i] : kDiagReg;
		harmonic_impl->diag_inv[i] = 1.0 / d;
	}
	harmonic_impl->warm_x.resize(Ni);
	harmonic_impl->warm_y.resize(Ni);
	harmonic_impl->warm_z.resize(Ni);
	for (int i = 0; i < Ni; ++i) {
		harmonic_impl->warm_x[i] = 0.0;
		harmonic_impl->warm_y[i] = 0.0;
		harmonic_impl->warm_z[i] = 0.0;
	}

	// Detail offsets: solve L_II xi_harm = -L_IB x_B_rest via PCG, then
	// d_i = x_I_rest_i - xi_harm_i. Tolerance is generous on the rest
	// solve — these vectors get added back in deform() so any residual
	// shows up at the same level there.
	LocalVector<double> xb_rest;
	LocalVector<double> yb_rest;
	LocalVector<double> zb_rest;
	xb_rest.resize(Nb);
	yb_rest.resize(Nb);
	zb_rest.resize(Nb);
	for (int b = 0; b < Nb; ++b) {
		const Vector3 &p = rest_vertex_positions[boundary_to_mesh[b]];
		xb_rest[b] = double(p.x);
		yb_rest[b] = double(p.y);
		zb_rest[b] = double(p.z);
	}
	LocalVector<double> rhs_x;
	LocalVector<double> rhs_y;
	LocalVector<double> rhs_z;
	rhs_x.resize(Ni);
	rhs_y.resize(Ni);
	rhs_z.resize(Ni);
	cassie_pcg::spmv(harmonic_impl->L_IB, xb_rest.ptr(), rhs_x.ptr());
	cassie_pcg::spmv(harmonic_impl->L_IB, yb_rest.ptr(), rhs_y.ptr());
	cassie_pcg::spmv(harmonic_impl->L_IB, zb_rest.ptr(), rhs_z.ptr());
	for (int i = 0; i < Ni; ++i) {
		rhs_x[i] = -rhs_x[i];
		rhs_y[i] = -rhs_y[i];
		rhs_z[i] = -rhs_z[i];
	}
	LocalVector<double> xi_harm;
	LocalVector<double> yi_harm;
	LocalVector<double> zi_harm;
	// resize_initialized (NOT plain resize) so the buffers are zeroed:
	// cassie_pcg::solve_sparse uses these as the warm-start initial
	// guess, and `double` is trivially constructible so plain resize
	// leaves heap garbage. Non-zero junk poisons the initial residual
	// r = b − A·x_0 and (depending on what was in memory) can push PCG
	// above the kRestTol budget within kRestMaxIter — leaving
	// interior_detail with a residual that surfaces as null-deform
	// drift in the harmonic test. Flake root cause for
	// `[Cassie][ProfileMover] harmonic null-deform`.
	xi_harm.resize_initialized(Ni);
	yi_harm.resize_initialized(Ni);
	zi_harm.resize_initialized(Ni);
	const int kRestMaxIter = std::max(400, Ni);
	const double kRestTol = 1e-10;
	double res_unused = 0.0;
	cassie_pcg::solve_sparse(harmonic_impl->L_II, rhs_x.ptr(), xi_harm.ptr(),
			harmonic_impl->diag_inv.ptr(), kRestMaxIter, kRestTol, &res_unused);
	cassie_pcg::solve_sparse(harmonic_impl->L_II, rhs_y.ptr(), yi_harm.ptr(),
			harmonic_impl->diag_inv.ptr(), kRestMaxIter, kRestTol, &res_unused);
	cassie_pcg::solve_sparse(harmonic_impl->L_II, rhs_z.ptr(), zi_harm.ptr(),
			harmonic_impl->diag_inv.ptr(), kRestMaxIter, kRestTol, &res_unused);

	interior_detail.resize(Ni);
	for (int i = 0; i < Ni; ++i) {
		const Vector3 &p = rest_vertex_positions[interior_to_mesh[i]];
		interior_detail[i] = Vector3(
				real_t(double(p.x) - xi_harm[i]),
				real_t(double(p.y) - yi_harm[i]),
				real_t(double(p.z) - zi_harm[i]));
	}

	// Phase A — the level-set scheduler that ENG-52 phase 4.1a built
	// for the Eigen-LDLT back-sub is no longer wired (PCG doesn't need
	// it). Keep the schedule arrays empty so the existing accessors
	// don't lie.
	forward_level_offset.clear();
	forward_level_rows.clear();
	backward_level_offset.clear();
	backward_level_rows.clear();

	// GPU float3 PCG handle. Probes RD availability via the constructor
	// (sets is_available()==false if no Vulkan); uploads L_II + diag_inv
	// once for the lifetime of the bind. Per-frame deform refreshes only
	// b_rhs via update_cg3_rhs. CPU fallback path stays intact for
	// headless / no-RD environments.
	if (harmonic_impl->gpu_cg) {
		harmonic_impl->gpu_cg->free_cg3_state(harmonic_impl->gpu_handle);
		harmonic_impl->gpu_cg.reset();
	}
	auto gpu = std::make_unique<cassie_slang_gpu::CassieSlangGpu>();
	if (gpu->is_available()) {
		const int Nnz = int(harmonic_impl->L_II.val.size());
		LocalVector<float> values_f;
		values_f.resize(Nnz);
		for (int k = 0; k < Nnz; ++k) {
			values_f[k] = float(harmonic_impl->L_II.val[k]);
		}
		LocalVector<float> diag_inv_f;
		diag_inv_f.resize(Ni);
		for (int i = 0; i < Ni; ++i) {
			diag_inv_f[i] = float(harmonic_impl->diag_inv[i]);
		}
		LocalVector<float> rhs_zero;
		rhs_zero.resize(Ni * 3);
		for (int k = 0; k < Ni * 3; ++k) {
			rhs_zero[k] = 0.0f;
		}
		harmonic_impl->gpu_handle = gpu->upload_cg3_state(
				Ni,
				harmonic_impl->L_II.row_ptr.ptr(),
				harmonic_impl->L_II.col_idx.ptr(),
				Nnz,
				values_f.ptr(),
				diag_inv_f.ptr(),
				rhs_zero.ptr());
		if (harmonic_impl->gpu_handle.is_valid()) {
			harmonic_impl->gpu_cg = std::move(gpu);
			harmonic_impl->rhs_f3_unpacked.resize(Ni * 3);
			harmonic_impl->x_init_f3_unpacked.resize(Ni * 3);
			harmonic_impl->sol_f3_unpacked.resize(Ni * 3);

			// Re-solve the rest decomposition through the SAME GPU solver
			// the production deform path uses, then overwrite
			// interior_detail with rest - x_rest_gpu. Reason: the CPU rest
			// solve above converges to residual ~0 (1e-10 tol, 400+ iters);
			// the GPU deform solve at 200 iters has residual ~15890. The
			// errors don't cancel and the null-deform output stretches.
			//
			// Iteratively refine via repeated 200-iter passes until the
			// solution stops changing (the GPU's fp32 fixed point). At
			// the fixed point, deform-time `solve(b_rest, x_fp, 200)`
			// returns x_fp unchanged — null deform yields exactly the
			// rest mesh. Capped at 8 passes (~1600 effective iters) which
			// the bench hits at 4-5 passes; one-shot at bind, so the
			// wall cost lives outside the per-frame budget.
			LocalVector<float> rhs_rest_f3;
			LocalVector<float> x_warm_f3;
			LocalVector<float> sol_rest_f3;
			rhs_rest_f3.resize(Ni * 3);
			x_warm_f3.resize(Ni * 3);
			sol_rest_f3.resize(Ni * 3);
			for (int i = 0; i < Ni; ++i) {
				rhs_rest_f3[i * 3 + 0] = float(rhs_x[i]);
				rhs_rest_f3[i * 3 + 1] = float(rhs_y[i]);
				rhs_rest_f3[i * 3 + 2] = float(rhs_z[i]);
			}
			for (int k = 0; k < Ni * 3; ++k) {
				x_warm_f3[k] = 0.0f;
			}
			harmonic_impl->gpu_cg->update_cg3_rhs(
					harmonic_impl->gpu_handle, rhs_rest_f3.ptr());
			for (int pass = 0; pass < 8; ++pass) {
				harmonic_impl->gpu_cg->solve_sparse_gpu_jacobi3(
						harmonic_impl->gpu_handle,
						x_warm_f3.ptr(),
						200,
						sol_rest_f3.ptr(),
						nullptr);
				float max_delta = 0.0f;
				for (int k = 0; k < Ni * 3; ++k) {
					const float d = std::abs(sol_rest_f3[k] - x_warm_f3[k]);
					if (d > max_delta) {
						max_delta = d;
					}
				}
				for (int k = 0; k < Ni * 3; ++k) {
					x_warm_f3[k] = sol_rest_f3[k];
				}
				if (max_delta < 1e-5f) {
					break;
				}
			}
			for (int i = 0; i < Ni; ++i) {
				const Vector3 &p = rest_vertex_positions[interior_to_mesh[i]];
				const double sx = double(sol_rest_f3[i * 3 + 0]);
				const double sy = double(sol_rest_f3[i * 3 + 1]);
				const double sz = double(sol_rest_f3[i * 3 + 2]);
				interior_detail[i] = Vector3(
						real_t(double(p.x) - sx),
						real_t(double(p.y) - sy),
						real_t(double(p.z) - sz));
				// Warm-start deform from the GPU fixed point so the
				// per-frame 200-iter call lands back at the fixed point
				// for null deltas — that's what makes null deform exactly
				// reproduce the rest mesh.
				harmonic_impl->warm_x[i] = sx;
				harmonic_impl->warm_y[i] = sy;
				harmonic_impl->warm_z[i] = sz;
			}
		}
	}

	// Pre-invert per-face rest frame for the Sumner & Popovic 2004
	// J^-T normal transfer. F_rest = [e1_rest | e2_rest | n_rest_unit]
	// columns. Constant across frames; per-frame deform() reads
	// rest_face_frame_inv[t] instead of computing F_rest.inverse()
	// — saves one Basis::inverse() per face per frame (~12K on the
	// character fixture, ~23 ms recovered).
	const int post_cut_tc = int(triangles.size());
	rest_face_frame_inv.resize(post_cut_tc);
	for (int t = 0; t < post_cut_tc; ++t) {
		const Vector3i &tri = triangles[t];
		if (tri.x < 0 || tri.y < 0 || tri.z < 0 || tri.x >= int(rest_vertex_positions.size()) || tri.y >= int(rest_vertex_positions.size()) || tri.z >= int(rest_vertex_positions.size())) {
			rest_face_frame_inv[t] = Basis();
			continue;
		}
		const Vector3 &a = rest_vertex_positions[tri.x];
		const Vector3 &b = rest_vertex_positions[tri.y];
		const Vector3 &c = rest_vertex_positions[tri.z];
		const Vector3 e1 = b - a;
		const Vector3 e2 = c - a;
		const Vector3 n_raw = e1.cross(e2);
		if (n_raw.length_squared() < real_t(1e-20)) {
			rest_face_frame_inv[t] = Basis();
			continue;
		}
		const Vector3 n_unit = n_raw.normalized();
		Basis F_rest;
		F_rest.set_column(0, e1);
		F_rest.set_column(1, e2);
		F_rest.set_column(2, n_unit);
		rest_face_frame_inv[t] = F_rest.inverse();
	}

	harmonic_ready = true;
}

Ref<ArrayMesh> CassieProfileMover::deform() const {
	if (bound_patch.is_null() || bound_curvenet.is_null() ||
			rest_vertex_positions.is_empty()) {
		return Ref<ArrayMesh>();
	}

	const int vc = int(rest_vertex_positions.size());
	const int tc = int(triangles.size());

	// 1. Per-knot translation delta. Translation-only as in pre-ENG-65;
	// rotation/scale wiring deferred (task #84 reopened — see notes).
	const TypedArray<CassieCurvenetKnot> knots = bound_curvenet->get_knots();
	const int kc = MIN(knots.size(), int(bind_knot_positions.size()));
	LocalVector<Vector3> knot_delta;
	knot_delta.resize(kc);
	for (int i = 0; i < kc; ++i) {
		Ref<CassieCurvenetKnot> k = knots[i];
		const Vector3 curr = k.is_valid()
				? k->get_rest_pose_transform().origin
				: bind_knot_positions[i];
		knot_delta[i] = curr - bind_knot_positions[i];
	}

	// 2. Per-sample delta interpolates along curve from endpoint knots.
	const int N_samples = int(bind_sample_positions.size());
	LocalVector<Vector3> sample_delta;
	sample_delta.resize(N_samples);
	for (int s = 0; s < N_samples; ++s) {
		const int ka = sample_knot_a[s];
		const int kb = sample_knot_b[s];
		const real_t t = sample_t[s];
		const Vector3 da = (ka >= 0 && ka < kc) ? knot_delta[ka] : Vector3();
		const Vector3 db = (kb >= 0 && kb < kc) ? knot_delta[kb] : da;
		sample_delta[s] = da.lerp(db, t);
	}

	// 3. Per-vertex deformed positions.
	PackedVector3Array out_verts;
	out_verts.resize(vc);
	if (harmonic_ready) {
		// Harmonic path: boundary positions track their driving sample;
		// interior positions = sparse-Cholesky solve + cached detail offset.
		const int Nb = int(boundary_to_mesh.size());
		const int Ni = int(interior_to_mesh.size());
		LocalVector<double> xb;
		LocalVector<double> yb;
		LocalVector<double> zb;
		xb.resize(Nb);
		yb.resize(Nb);
		zb.resize(Nb);
		for (int b = 0; b < Nb; ++b) {
			const int s = boundary_driver_sample[b];
			const Vector3 boundary_pos = (s >= 0 && s < N_samples)
					? bind_sample_positions[s] + sample_delta[s] + boundary_rest_offset[b]
					: rest_vertex_positions[boundary_to_mesh[b]];
			xb[b] = double(boundary_pos.x);
			yb[b] = double(boundary_pos.y);
			zb[b] = double(boundary_pos.z);
			out_verts.write[boundary_to_mesh[b]] = boundary_pos;
		}
		// Phase A — Jacobi-PCG. Three solves (x/y/z) per frame, each
		// warm-started from the previous frame's solution to slash
		// iterations on smooth knot edits.
		LocalVector<double> rhs_x;
		LocalVector<double> rhs_y;
		LocalVector<double> rhs_z;
		rhs_x.resize(Ni);
		rhs_y.resize(Ni);
		rhs_z.resize(Ni);
		cassie_pcg::spmv(harmonic_impl->L_IB, xb.ptr(), rhs_x.ptr());
		cassie_pcg::spmv(harmonic_impl->L_IB, yb.ptr(), rhs_y.ptr());
		cassie_pcg::spmv(harmonic_impl->L_IB, zb.ptr(), rhs_z.ptr());
		for (int i = 0; i < Ni; ++i) {
			rhs_x[i] = -rhs_x[i];
			rhs_y[i] = -rhs_y[i];
			rhs_z[i] = -rhs_z[i];
		}
		const int kMaxIter = std::max(200, Ni / 4);
		const double kTol = 1e-8;
		// GPU path runs fp32 Jacobi PCG. Per-iter wall is ~50 µs;
		// 500 iters = ~25 ms which fits the 35 ms realtime gate and
		// reaches a tighter precision floor than 200 iters did. ENG-63's
		// multi-level MAS apply will lift the convergence ceiling so
		// fewer iters are needed.
		const int kGpuMaxIter = 500;
		if (harmonic_impl->gpu_cg && harmonic_impl->gpu_handle.is_valid()) {
			float *rhs_f3 = harmonic_impl->rhs_f3_unpacked.ptr();
			float *x_init_f3 = harmonic_impl->x_init_f3_unpacked.ptr();
			for (int i = 0; i < Ni; ++i) {
				rhs_f3[i * 3 + 0] = float(rhs_x[i]);
				rhs_f3[i * 3 + 1] = float(rhs_y[i]);
				rhs_f3[i * 3 + 2] = float(rhs_z[i]);
				x_init_f3[i * 3 + 0] = float(harmonic_impl->warm_x[i]);
				x_init_f3[i * 3 + 1] = float(harmonic_impl->warm_y[i]);
				x_init_f3[i * 3 + 2] = float(harmonic_impl->warm_z[i]);
			}
			harmonic_impl->gpu_cg->update_cg3_rhs(harmonic_impl->gpu_handle, rhs_f3);
			float residual_gpu = 0.0f;
			harmonic_impl->gpu_cg->solve_sparse_gpu_jacobi3(
					harmonic_impl->gpu_handle,
					x_init_f3,
					kGpuMaxIter,
					harmonic_impl->sol_f3_unpacked.ptr(),
					&residual_gpu);
			const float *sol_f3 = harmonic_impl->sol_f3_unpacked.ptr();
			for (int i = 0; i < Ni; ++i) {
				harmonic_impl->warm_x[i] = double(sol_f3[i * 3 + 0]);
				harmonic_impl->warm_y[i] = double(sol_f3[i * 3 + 1]);
				harmonic_impl->warm_z[i] = double(sol_f3[i * 3 + 2]);
			}
			harmonic_impl->last_solve_iters = kGpuMaxIter;
		} else {
			double res_x = 0.0, res_y = 0.0, res_z = 0.0;
			const int it_x = cassie_pcg::solve_sparse(harmonic_impl->L_II, rhs_x.ptr(),
					harmonic_impl->warm_x.ptr(), harmonic_impl->diag_inv.ptr(),
					kMaxIter, kTol, &res_x);
			const int it_y = cassie_pcg::solve_sparse(harmonic_impl->L_II, rhs_y.ptr(),
					harmonic_impl->warm_y.ptr(), harmonic_impl->diag_inv.ptr(),
					kMaxIter, kTol, &res_y);
			const int it_z = cassie_pcg::solve_sparse(harmonic_impl->L_II, rhs_z.ptr(),
					harmonic_impl->warm_z.ptr(), harmonic_impl->diag_inv.ptr(),
					kMaxIter, kTol, &res_z);
			harmonic_impl->last_solve_iters = it_x + it_y + it_z;
		}
		// Restore pre-#84 behavior: out = harmonic + interior_detail.
		// For translation BC this is exact (the harmonic carries the
		// shift, the detail is the high-frequency residual). For
		// rotation BC the detail is left un-rotated -- task #84 is
		// reopened with a different approach (the per-vertex J derived
		// from harmonic vs rest geometry conflates harmonic smoothing
		// with the deformation we want, breaking even for translation).
		// Rotation correctness likely needs the ENG-66 skin-bake
		// architecture where per-vertex weights to knots give a clean
		// local-rotation field. Tracked separately.
		for (int i = 0; i < Ni; ++i) {
			const Vector3 harm(real_t(harmonic_impl->warm_x[i]),
					real_t(harmonic_impl->warm_y[i]),
					real_t(harmonic_impl->warm_z[i]));
			out_verts.write[interior_to_mesh[i]] = harm + interior_detail[i];
		}
	} else {
		// IDW fallback path.
		for (int v = 0; v < vc; ++v) {
			Vector3 disp;
			for (int k = 0; k < K; ++k) {
				const int s = vertex_sample_indices[v * K + k];
				if (s < 0) {
					continue;
				}
				disp += vertex_sample_weights[v * K + k] * sample_delta[s];
			}
			out_verts.write[v] = rest_vertex_positions[v] + disp;
		}
	}

	// 4. Reassemble as ArrayMesh. Normals via Sumner & Popovic 2004:
	// per face compute J = frame_def * frame_rest^-1; the deformed
	// per-vertex normal is J^-T applied to the captured rest vertex
	// normal, angle-weighted (Max 1999) over incident faces, then
	// renormalized. For pure translation J = I -> deformed normals
	// equal rest normals exactly; rest and deformed shade
	// identically in Blender. For rotation R, J = R and J^-T = R,
	// so the normal rotates with the surface.
	PackedInt32Array out_idx;
	out_idx.resize(tc * 3);
	for (int t = 0; t < tc; ++t) {
		out_idx.write[t * 3] = triangles[t].x;
		out_idx.write[t * 3 + 1] = triangles[t].y;
		out_idx.write[t * 3 + 2] = triangles[t].z;
	}
	PackedVector3Array out_normals;
	out_normals.resize(vc);
	for (int v = 0; v < vc; ++v) {
		out_normals.write[v] = Vector3();
	}
	// J^-T transfer requires BOTH rest vertex normals AND the bind-time
	// pre-inverted rest face frames. The frames are only populated when
	// _build_harmonic_state runs to completion (i.e., the mesh had
	// enough vertices/triangles/samples to build the harmonic state).
	// On small meshes (e.g., the 8-vertex cube fixture) the harmonic
	// build early-exits and rest_face_frame_inv stays empty; in that
	// case fall back to the angle-weighted face-normal path so we don't
	// index an empty vector. ENG-72.
	const bool have_rest_normals = int(rest_vertex_normals.size()) == vc && int(rest_face_frame_inv.size()) == tc;
	auto vertex_angle = [](const Vector3 &u, const Vector3 &v) {
		const real_t lu = u.length();
		const real_t lv = v.length();
		if (lu < real_t(1e-20) || lv < real_t(1e-20)) {
			return real_t(0);
		}
		real_t c = u.dot(v) / (lu * lv);
		if (c > real_t(1)) {
			c = real_t(1);
		} else if (c < real_t(-1)) {
			c = real_t(-1);
		}
		return Math::acos(c);
	};
	for (int t = 0; t < tc; ++t) {
		const Vector3i tri = triangles[t];
		const Vector3 &a_def = out_verts[tri.x];
		const Vector3 &b_def = out_verts[tri.y];
		const Vector3 &c_def = out_verts[tri.z];
		const Vector3 e1_def = b_def - a_def;
		const Vector3 e2_def = c_def - a_def;
		const Vector3 face_n_def = e1_def.cross(e2_def);
		if (face_n_def.length_squared() < real_t(1e-20)) {
			continue;
		}
		const Vector3 face_n_def_unit = face_n_def.normalized();
		if (!have_rest_normals) {
			// Fallback: angle-weighted face normal accumulation
			// (Max 1999) when source omitted ARRAY_NORMAL. Same
			// behavior as the pre-ENG-65 normal pass.
			const real_t a0 = vertex_angle(e1_def, e2_def);
			const real_t a1 = vertex_angle(-e1_def, c_def - b_def);
			const real_t a2 = vertex_angle(-e2_def, -(c_def - b_def));
			out_normals.write[tri.x] += face_n_def_unit * a0;
			out_normals.write[tri.y] += face_n_def_unit * a1;
			out_normals.write[tri.z] += face_n_def_unit * a2;
			continue;
		}
		// Build the per-face deformation gradient J = F_def * F_rest^-1
		// where the 3rd basis column is the face normal. F_rest^-1 is
		// pre-computed at bind time (rest_face_frame_inv) — only
		// F_def + the matrix multiply happen per frame.
		Basis F_def;
		F_def.set_column(0, e1_def);
		F_def.set_column(1, e2_def);
		F_def.set_column(2, face_n_def_unit);
		const Basis J = F_def * rest_face_frame_inv[t];
		// J^-T transforms cotangent vectors (normals). Basis::xform
		// applies J; we want J^-T so invert then transpose. Both
		// operations exist; compose.
		const Basis JinvT = J.inverse().transposed();
		// Apply J^-T to each per-vertex rest normal; accumulate
		// angle-weighted into the deformed per-vertex normal.
		const Vector3 n0_rest = rest_vertex_normals[tri.x];
		const Vector3 n1_rest = rest_vertex_normals[tri.y];
		const Vector3 n2_rest = rest_vertex_normals[tri.z];
		const Vector3 n0_def = JinvT.xform(n0_rest);
		const Vector3 n1_def = JinvT.xform(n1_rest);
		const Vector3 n2_def = JinvT.xform(n2_rest);
		const real_t a0 = vertex_angle(e1_def, e2_def);
		const real_t a1 = vertex_angle(-e1_def, c_def - b_def);
		const real_t a2 = vertex_angle(-e2_def, -(c_def - b_def));
		out_normals.write[tri.x] += n0_def * a0;
		out_normals.write[tri.y] += n1_def * a1;
		out_normals.write[tri.z] += n2_def * a2;
	}
	for (int v = 0; v < vc; ++v) {
		const Vector3 n = out_normals[v];
		out_normals.write[v] = (n.length_squared() < real_t(1e-20))
				? Vector3(0, 1, 0)
				: n.normalized();
	}

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = out_verts;
	arrays[Mesh::ARRAY_INDEX] = out_idx;
	arrays[Mesh::ARRAY_NORMAL] = out_normals;
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

Ref<ArrayMesh> CassieProfileMover::get_bound_mesh() const {
	if (rest_vertex_positions.is_empty()) {
		return Ref<ArrayMesh>();
	}
	const int vc = int(rest_vertex_positions.size());
	const int tc = int(triangles.size());
	PackedVector3Array verts;
	verts.resize(vc);
	for (int i = 0; i < vc; ++i) {
		verts.write[i] = rest_vertex_positions[i];
	}
	// Emit the captured rest normals so the bound mesh shades like
	// the source glb in Blender. Verbatim is correct here: there is
	// no deformation, so the J^-T transfer collapses to identity.
	PackedVector3Array normals;
	const bool have_rest_normals = int(rest_vertex_normals.size()) == vc;
	if (have_rest_normals) {
		normals.resize(vc);
		for (int i = 0; i < vc; ++i) {
			normals.write[i] = rest_vertex_normals[i];
		}
	}
	PackedInt32Array idx;
	idx.resize(tc * 3);
	for (int t = 0; t < tc; ++t) {
		idx.write[t * 3] = triangles[t].x;
		idx.write[t * 3 + 1] = triangles[t].y;
		idx.write[t * 3 + 2] = triangles[t].z;
	}
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	if (have_rest_normals) {
		arrays[Mesh::ARRAY_NORMAL] = normals;
	}
	arrays[Mesh::ARRAY_INDEX] = idx;
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

// ENG-66 — bake harmonic basis into linear blend skin weights.
//
// For each knot k:
//   1. Build per-boundary-sample indicator e_k from sample_knot_a/b/t
//      (each boundary sample driven by curve sample s sees value
//      (1-t) if knot a == k, t if knot b == k, 0 otherwise).
//   2. Compute per-boundary-vert e_k[b] = e_k[boundary_driver_sample[b]].
//   3. Solve L_II * phi_k_interior = -L_IB * e_k_boundary via CPU PCG.
//   4. Append to per-vertex weight array (boundary vert gets its
//      e_k_boundary value; interior vert gets phi_k_interior value).
//
// Per vertex: collect all knot weights, sort descending, keep top K,
// renormalize. Write into ARRAY_BONES + ARRAY_WEIGHTS with the
// ARRAY_FLAG_USE_8_BONE_WEIGHTS flag when K=8.
Ref<ArrayMesh> CassieProfileMover::bake_skin(int p_max_weights_per_vert) const {
	if (!harmonic_ready || harmonic_impl == nullptr) {
		return Ref<ArrayMesh>();
	}
	if (p_max_weights_per_vert != 4 && p_max_weights_per_vert != 8) {
		// Godot ArrayMesh supports 4 (default) or 8 weights per vert.
		// Anything else is rejected.
		return Ref<ArrayMesh>();
	}
	const int Ni = int(interior_to_mesh.size());
	const int Nb = int(boundary_to_mesh.size());
	const int N_samples = int(bind_sample_positions.size());
	const TypedArray<CassieCurvenetKnot> knots = bound_curvenet->get_knots();
	const int Nknots = MIN(int(bind_knot_transforms.size()), int(knots.size()));
	if (Ni == 0 || Nb == 0 || Nknots == 0) {
		return Ref<ArrayMesh>();
	}
	const int vc = int(rest_vertex_positions.size());

	// Step 1: per-knot basis. basis_per_vert[v] holds the (knot, weight)
	// list for vertex v — but to keep allocation bounded we accumulate
	// into a dense [Nknots][vc] matrix first, then per-vert top-K.
	// Memory: Nknots × vc × 4 bytes. For Nknots=100, vc=9203: ~3.6 MB.
	// Acceptable at bind/bake time.
	LocalVector<float> basis_dense;
	basis_dense.resize(Nknots * vc);
	for (int k = 0; k < Nknots * vc; ++k) {
		basis_dense[k] = 0.0f;
	}

	// Scratch arrays reused across knot solves.
	LocalVector<double> e_boundary;
	LocalVector<double> rhs;
	LocalVector<double> phi_interior;
	e_boundary.resize(Nb);
	rhs.resize(Ni);
	phi_interior.resize(Ni);

	const int kBakeMaxIter = std::max(2000, Ni);
	const double kBakeTol = 1e-12;
	for (int k = 0; k < Nknots; ++k) {
		// 1a. Build e_k_boundary using the curve sample parameter.
		for (int b = 0; b < Nb; ++b) {
			const int s = boundary_driver_sample[b];
			double w = 0.0;
			if (s >= 0 && s < N_samples) {
				const int ka = sample_knot_a[s];
				const int kb = sample_knot_b[s];
				const double t = double(sample_t[s]);
				if (ka == k) {
					w += (1.0 - t);
				}
				if (kb == k) {
					w += t;
				}
			}
			e_boundary[b] = w;
		}

		// 1b. rhs = -L_IB * e_k_boundary
		cassie_pcg::spmv(harmonic_impl->L_IB, e_boundary.ptr(), rhs.ptr());
		for (int i = 0; i < Ni; ++i) {
			rhs[i] = -rhs[i];
		}

		// 1c. Solve L_II * phi_k = rhs (CPU double-precision PCG).
		for (int i = 0; i < Ni; ++i) {
			phi_interior[i] = 0.0;
		}
		double residual_unused = 0.0;
		cassie_pcg::solve_sparse(harmonic_impl->L_II, rhs.ptr(),
				phi_interior.ptr(), harmonic_impl->diag_inv.ptr(),
				kBakeMaxIter, kBakeTol, &residual_unused);

		// 1d. Write into basis_dense column k.
		for (int b = 0; b < Nb; ++b) {
			basis_dense[k * vc + boundary_to_mesh[b]] = float(e_boundary[b]);
		}
		for (int i = 0; i < Ni; ++i) {
			basis_dense[k * vc + interior_to_mesh[i]] = float(phi_interior[i]);
		}
	}

	// Step 2: per-vertex top-K + renormalize.
	const int k_max = p_max_weights_per_vert;
	PackedInt32Array bones_out;
	PackedFloat32Array weights_out;
	bones_out.resize(vc * k_max);
	weights_out.resize(vc * k_max);
	for (int v = 0; v < vc; ++v) {
		// Collect (knot_index, weight) pairs. Harmonic basis on the
		// cottan Laplacian can produce small negative weights at
		// vertices with obtuse incident triangles; we keep top-K
		// POSITIVE weights only so LBS partition-of-unity stays
		// mathematically clean (Σ w_kept · M_k · v = M_avg · v for
		// equal bone transforms). Negative weights would cancel in
		// the renormalize and flip skin signs unpredictably.
		Vector<Pair<int, float>> pairs;
		pairs.reserve(Nknots);
		for (int k = 0; k < Nknots; ++k) {
			const float w = basis_dense[k * vc + v];
			if (w > 0.0f) {
				pairs.push_back(Pair<int, float>(k, w));
			}
		}
		struct CmpDesc {
			bool operator()(const Pair<int, float> &a, const Pair<int, float> &b) const {
				return a.second > b.second;
			}
		};
		std::sort(pairs.ptrw(), pairs.ptrw() + pairs.size(), CmpDesc());

		double sum = 0.0;
		const int n_kept = MIN(k_max, int(pairs.size()));
		for (int j = 0; j < n_kept; ++j) {
			sum += double(pairs[j].second);
		}
		// Edge case: vert has no positive harmonic weight (extreme cottan
		// pathology). Fall back to weight 1.0 on bone 0 — the deformed
		// vertex follows knot 0 rigidly. Rare, visible as an isolated
		// artifact; would be fixed by switching to a max-principle-
		// preserving Laplacian (intrinsic Delaunay) at bind time.
		if (sum < 1e-12) {
			bones_out.write[v * k_max + 0] = 0;
			weights_out.write[v * k_max + 0] = 1.0f;
			for (int j = 1; j < k_max; ++j) {
				bones_out.write[v * k_max + j] = 0;
				weights_out.write[v * k_max + j] = 0.0f;
			}
			continue;
		}
		const float norm = float(1.0 / sum);
		for (int j = 0; j < n_kept; ++j) {
			bones_out.write[v * k_max + j] = pairs[j].first;
			weights_out.write[v * k_max + j] = pairs[j].second * norm;
		}
		for (int j = n_kept; j < k_max; ++j) {
			bones_out.write[v * k_max + j] = 0;
			weights_out.write[v * k_max + j] = 0.0f;
		}
	}

	// Step 3: assemble ArrayMesh with skin weights. Pass through the
	// captured rest normals and the index buffer; the runtime skin
	// vertex shader transforms positions + normals via the bone
	// transforms each frame.
	const int tc = int(triangles.size());
	PackedVector3Array verts;
	verts.resize(vc);
	for (int i = 0; i < vc; ++i) {
		verts.write[i] = rest_vertex_positions[i];
	}
	PackedVector3Array normals;
	const bool have_rest_normals = int(rest_vertex_normals.size()) == vc;
	if (have_rest_normals) {
		normals.resize(vc);
		for (int i = 0; i < vc; ++i) {
			normals.write[i] = rest_vertex_normals[i];
		}
	}
	PackedInt32Array idx;
	idx.resize(tc * 3);
	for (int t = 0; t < tc; ++t) {
		idx.write[t * 3] = triangles[t].x;
		idx.write[t * 3 + 1] = triangles[t].y;
		idx.write[t * 3 + 2] = triangles[t].z;
	}

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	if (have_rest_normals) {
		arrays[Mesh::ARRAY_NORMAL] = normals;
	}
	arrays[Mesh::ARRAY_INDEX] = idx;
	arrays[Mesh::ARRAY_BONES] = bones_out;
	arrays[Mesh::ARRAY_WEIGHTS] = weights_out;
	const uint64_t flags = (k_max == 8)
			? uint64_t(Mesh::ARRAY_FLAG_USE_8_BONE_WEIGHTS)
			: uint64_t(0);
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES,
			arrays, TypedArray<Array>(), Dictionary(), flags);
	return mesh;
}

// ENG-66 — Skin resource describing the bake's bind pose. Each bind
// uses name "knot_<i>" so the runtime Skeleton3D needs matching bone
// names. The pose stored is the INVERSE of the knot's bind-time
// transform — LBS evaluates v_def = Sum_j w_j · M_j · bind_pose_j · v_rest,
// so bind_pose_j = bind_knot_transform[j]^-1 maps the rest mesh into
// bone j's local frame at bind time. Subsequent bone_global_pose[j]
// updates from the live knot transforms then push it back into world.
Ref<Skin> CassieProfileMover::build_skin() const {
	const int Nknots = int(bind_knot_transforms.size());
	if (Nknots == 0) {
		return Ref<Skin>();
	}
	Ref<Skin> skin;
	skin.instantiate();
	for (int i = 0; i < Nknots; ++i) {
		const String name = String("knot_") + itos(i);
		skin->add_named_bind(name, bind_knot_transforms[i].affine_inverse());
	}
	return skin;
}

Dictionary CassieProfileMover::benchmark_gpu_spmv(int p_n, int p_iterations) {
	Dictionary result;

	cassie_slang_gpu::CassieSlangGpu gpu;
	if (!gpu.is_available()) {
		result["available"] = false;
		return result;
	}

	// Build a representative tridiagonal SPD CSR. Matches the L_II
	// stencil shape; nnz ≈ 3n.
	cassie_pcg::CSRMatrix A;
	A.rows = p_n;
	A.cols = p_n;
	A.row_ptr.resize(p_n + 1);
	A.row_ptr[0] = 0;
	for (int i = 0; i < p_n; ++i) {
		if (i > 0) {
			A.col_idx.push_back(i - 1);
			A.val.push_back(-1.0);
		}
		A.col_idx.push_back(i);
		A.val.push_back(4.0);
		if (i < p_n - 1) {
			A.col_idx.push_back(i + 1);
			A.val.push_back(-1.0);
		}
		A.row_ptr[i + 1] = int(A.col_idx.size());
	}
	const int nnz = int(A.val.size());

	LocalVector<float> values_f;
	LocalVector<float> x_f;
	LocalVector<float> y_f;
	values_f.resize(nnz);
	x_f.resize(p_n);
	y_f.resize(p_n);
	for (int i = 0; i < nnz; ++i) {
		values_f[i] = float(A.val[i]);
	}
	for (int i = 0; i < p_n; ++i) {
		x_f[i] = float(i) * 0.001f;
	}

	cassie_slang_gpu::CsrHandle handle = gpu.upload_matrix(p_n, p_n,
			A.row_ptr.ptr(), A.col_idx.ptr(), nnz, values_f.ptr());
	if (!handle.is_valid()) {
		result["available"] = false;
		result["error"] = "upload_matrix failed";
		return result;
	}

	const Time *time = Time::get_singleton();

	// Warm-up.
	for (int i = 0; i < 4; ++i) {
		gpu.spmv_uploaded(handle, x_f.ptr(), y_f.ptr());
	}

	// GPU timing — one-dispatch-per-submit (the architecture we'd have
	// if we naively swapped cassie_pcg::spmv → GPU per CG iter).
	const uint64_t gpu_t0 = time->get_ticks_usec();
	for (int i = 0; i < p_iterations; ++i) {
		gpu.spmv_uploaded(handle, x_f.ptr(), y_f.ptr());
	}
	const uint64_t gpu_t1 = time->get_ticks_usec();
	const double gpu_us = double(gpu_t1 - gpu_t0) / double(p_iterations);

	// GPU timing — N-dispatches-per-submit batched into a single
	// command list. This is the upper bound on what command batching
	// alone can deliver (no ubershader). If per-dispatch incremental
	// cost is small, batching dispatches into a single submit can hide
	// the submit+sync fixed overhead.
	const uint64_t batched_t0 = time->get_ticks_usec();
	gpu.spmv_batched_benchmark(handle, x_f.ptr(), y_f.ptr(), p_iterations);
	const uint64_t batched_t1 = time->get_ticks_usec();
	const double batched_total_us = double(batched_t1 - batched_t0);
	const double batched_per_dispatch_us = batched_total_us / double(p_iterations);

	// CPU timing (same Slang CPU kernel).
	LocalVector<double> x_d;
	LocalVector<double> y_d;
	x_d.resize(p_n);
	y_d.resize(p_n);
	for (int i = 0; i < p_n; ++i) {
		x_d[i] = double(x_f[i]);
	}
	for (int i = 0; i < 4; ++i) {
		cassie_pcg::spmv(A, x_d.ptr(), y_d.ptr());
	}
	const uint64_t cpu_t0 = time->get_ticks_usec();
	for (int i = 0; i < p_iterations; ++i) {
		cassie_pcg::spmv(A, x_d.ptr(), y_d.ptr());
	}
	const uint64_t cpu_t1 = time->get_ticks_usec();
	const double cpu_us = double(cpu_t1 - cpu_t0) / double(p_iterations);

	gpu.free_matrix(handle);

	result["available"] = true;
	result["n"] = p_n;
	result["nnz"] = nnz;
	result["gpu_us"] = gpu_us;
	result["cpu_us"] = cpu_us;
	result["ratio"] = gpu_us / MAX(cpu_us, 1e-3);
	// 2700 dispatches per full GPU CG frame (9 kernels × 100 iters × 3 axes).
	result["frame_ms"] = (gpu_us * 2700.0) / 1000.0;
	result["batched_total_us"] = batched_total_us;
	result["batched_per_us"] = batched_per_dispatch_us;
	// Same 2700 dispatches if they all fit in one command list:
	// fixed submit+sync cost ≈ (one-shot gpu_us) - (batched_per * 1),
	// per-dispatch incremental ≈ batched_per_us.
	result["batched_frame_ms"] = (batched_per_dispatch_us * 2700.0 + gpu_us) / 1000.0;
	return result;
}

Dictionary CassieProfileMover::benchmark_gpu_pcg_solve(int p_n, int p_max_iter) {
	Dictionary result;

	cassie_slang_gpu::CassieSlangGpu gpu;
	if (!gpu.is_available()) {
		result["available"] = false;
		return result;
	}

	// 1D Poisson stencil (tridiag SPD): diag = 4, off-diag = -1.
	// Used identically by benchmark_gpu_spmv so the two perf numbers
	// share a fixture and the user can correlate.
	cassie_pcg::CSRMatrix A;
	A.rows = p_n;
	A.cols = p_n;
	A.row_ptr.resize(p_n + 1);
	A.row_ptr[0] = 0;
	for (int i = 0; i < p_n; ++i) {
		if (i > 0) {
			A.col_idx.push_back(i - 1);
			A.val.push_back(-1.0);
		}
		A.col_idx.push_back(i);
		A.val.push_back(4.0);
		if (i < p_n - 1) {
			A.col_idx.push_back(i + 1);
			A.val.push_back(-1.0);
		}
		A.row_ptr[i + 1] = int(A.col_idx.size());
	}
	const int nnz = int(A.val.size());

	// RHS = a non-trivial vector so CG actually iterates.
	LocalVector<double> b_d;
	LocalVector<double> x_d;
	LocalVector<double> diag_inv_d;
	b_d.resize(p_n);
	x_d.resize(p_n);
	diag_inv_d.resize(p_n);
	for (int i = 0; i < p_n; ++i) {
		b_d[i] = Math::sin(double(i) * 0.01) + 0.5;
		x_d[i] = 0.0; // cold start
		diag_inv_d[i] = 1.0 / 4.0; // diag(A) is uniformly 4
	}

	LocalVector<float> values_f;
	LocalVector<float> b_f;
	LocalVector<float> x_f;
	LocalVector<float> diag_inv_f;
	values_f.resize(nnz);
	b_f.resize(p_n);
	x_f.resize(p_n);
	diag_inv_f.resize(p_n);
	for (int i = 0; i < nnz; ++i) {
		values_f[i] = float(A.val[i]);
	}
	for (int i = 0; i < p_n; ++i) {
		b_f[i] = float(b_d[i]);
		x_f[i] = 0.0f;
		diag_inv_f[i] = float(diag_inv_d[i]);
	}

	const float tol_sq = 1e-16f; // (1e-8)² for ||r||² < tol²
	cassie_slang_gpu::CassieSlangGpu::CgPcgHandle handle = gpu.upload_cg_state(
			p_n, A.row_ptr.ptr(), A.col_idx.ptr(), nnz,
			values_f.ptr(), diag_inv_f.ptr(), b_f.ptr(), tol_sq);
	if (!handle.is_valid()) {
		result["available"] = false;
		result["error"] = "upload_cg_state failed";
		return result;
	}

	const Time *time = Time::get_singleton();

	// Warm-up — first submit pays a one-time pipeline-cache cost.
	LocalVector<float> x_out_f;
	x_out_f.resize(p_n);
	gpu.solve_sparse_gpu(handle, x_f.ptr(), 1, x_out_f.ptr());

	// GPU timing — fixed-iter constant-work design: one submit, one sync,
	// no convergence branching. End-of-solve check_residual writes
	// final ||r||² for caller observability.
	float gpu_residual = 0.0f;
	const uint64_t gpu_t0 = time->get_ticks_usec();
	const bool ok = gpu.solve_sparse_gpu(handle, x_f.ptr(), p_max_iter,
			x_out_f.ptr(), &gpu_residual);
	const uint64_t gpu_t1 = time->get_ticks_usec();
	const double gpu_solve_ms = double(gpu_t1 - gpu_t0) / 1000.0;
	if (!ok) {
		gpu.free_cg_state(handle);
		result["available"] = false;
		result["error"] = "solve_sparse_gpu dispatch failed";
		return result;
	}

	// CPU timing — same matrix, same tol target.
	LocalVector<double> x_out_d;
	x_out_d.resize(p_n);
	for (int i = 0; i < p_n; ++i) {
		x_out_d[i] = 0.0;
	}
	double residual = 0.0;
	const uint64_t cpu_t0 = time->get_ticks_usec();
	const int cpu_iters = cassie_pcg::solve_sparse(A, b_d.ptr(), x_out_d.ptr(),
			diag_inv_d.ptr(), p_max_iter, 1e-8, &residual);
	const uint64_t cpu_t1 = time->get_ticks_usec();
	const double cpu_solve_ms = double(cpu_t1 - cpu_t0) / 1000.0;

	gpu.free_cg_state(handle);

	result["available"] = true;
	result["n"] = p_n;
	result["nnz"] = nnz;
	result["max_iter"] = p_max_iter;
	result["gpu_solve_ms"] = gpu_solve_ms;
	result["gpu_residual"] = double(gpu_residual);
	result["cpu_solve_ms"] = cpu_solve_ms;
	result["cpu_iters"] = cpu_iters;
	result["cpu_residual"] = residual;
	result["ratio"] = gpu_solve_ms / MAX(cpu_solve_ms, 1e-3);
	result["per_iter_gpu_us"] = gpu_solve_ms * 1000.0 / double(p_max_iter);
	return result;
}

Dictionary CassieProfileMover::compare_cpu_gpu_solve(int p_max_iter) {
	Dictionary result;
	if (harmonic_impl == nullptr) {
		result["available"] = false;
		result["error"] = "mover not bound or harmonic state not built";
		return result;
	}

	cassie_pcg::CSRMatrix &A = harmonic_impl->L_II;
	const int Ni = A.rows;
	const int nnz = int(A.val.size());
	if (Ni == 0) {
		result["available"] = false;
		result["error"] = "interior count is zero";
		return result;
	}

	// Synthetic RHS — non-trivial sin pattern over the interior.
	LocalVector<double> b_d;
	b_d.resize(Ni);
	for (int i = 0; i < Ni; ++i) {
		b_d[i] = Math::sin(double(i) * 0.01) + 0.5;
	}

	// CPU solve via the same path deform() uses.
	LocalVector<double> x_cpu;
	x_cpu.resize(Ni);
	for (int i = 0; i < Ni; ++i) {
		x_cpu[i] = 0.0; // cold start
	}
	const double kTol = 1e-8;
	const int kCpuMaxIter = std::max(200, Ni / 4);
	double cpu_residual = 0.0;
	const Time *time = Time::get_singleton();
	const uint64_t cpu_t0 = time->get_ticks_usec();
	const int cpu_iters = cassie_pcg::solve_sparse(A, b_d.ptr(), x_cpu.ptr(),
			harmonic_impl->diag_inv.ptr(), kCpuMaxIter, kTol, &cpu_residual);
	const uint64_t cpu_t1 = time->get_ticks_usec();
	const double cpu_solve_ms = double(cpu_t1 - cpu_t0) / 1000.0;

	// GPU solve via solve_sparse_gpu on the REAL L_II.
	cassie_slang_gpu::CassieSlangGpu gpu;
	if (!gpu.is_available()) {
		result["available"] = false;
		result["ni"] = Ni;
		result["nnz"] = nnz;
		result["cpu_solve_ms"] = cpu_solve_ms;
		result["cpu_iters"] = cpu_iters;
		result["cpu_residual"] = cpu_residual;
		result["error"] = "no local RD";
		return result;
	}

	// Narrow L_II + diag_inv + b to float32 for the GPU side.
	LocalVector<float> values_f;
	LocalVector<float> diag_inv_f;
	LocalVector<float> b_f;
	LocalVector<float> x_init_f;
	values_f.resize(nnz);
	diag_inv_f.resize(Ni);
	b_f.resize(Ni);
	x_init_f.resize(Ni);
	for (int i = 0; i < nnz; ++i) {
		values_f[i] = float(A.val[i]);
	}
	for (int i = 0; i < Ni; ++i) {
		diag_inv_f[i] = float(harmonic_impl->diag_inv[i]);
		b_f[i] = float(b_d[i]);
		x_init_f[i] = 0.0f;
	}

	const float tol_sq = 1e-16f; // unused by constant-work; passed for upload API.
	cassie_slang_gpu::CassieSlangGpu::CgPcgHandle handle = gpu.upload_cg_state(
			Ni, A.row_ptr.ptr(), A.col_idx.ptr(), nnz,
			values_f.ptr(), diag_inv_f.ptr(), b_f.ptr(), tol_sq);
	if (!handle.is_valid()) {
		result["available"] = false;
		result["error"] = "upload_cg_state failed";
		return result;
	}

	// Warm-up dispatch to amortize pipeline-cache cost.
	LocalVector<float> x_gpu_f;
	x_gpu_f.resize(Ni);
	gpu.solve_sparse_gpu(handle, x_init_f.ptr(), 1, x_gpu_f.ptr());

	float gpu_residual = 0.0f;
	const uint64_t gpu_t0 = time->get_ticks_usec();
	const bool ok = gpu.solve_sparse_gpu(handle, x_init_f.ptr(), p_max_iter,
			x_gpu_f.ptr(), &gpu_residual);
	const uint64_t gpu_t1 = time->get_ticks_usec();
	const double gpu_solve_ms = double(gpu_t1 - gpu_t0) / 1000.0;

	gpu.free_cg_state(handle);

	if (!ok) {
		result["available"] = false;
		result["error"] = "solve_sparse_gpu dispatch failed";
		return result;
	}

	// Per-vertex drift between CPU (double) and GPU (float-narrowed) solutions.
	double max_drift = 0.0;
	double sum_drift = 0.0;
	for (int i = 0; i < Ni; ++i) {
		const double d = Math::abs(x_cpu[i] - double(x_gpu_f[i]));
		if (d > max_drift) {
			max_drift = d;
		}
		sum_drift += d;
	}
	const double mean_drift = sum_drift / double(Ni);

	result["available"] = true;
	result["ni"] = Ni;
	result["nnz"] = nnz;
	result["cpu_solve_ms"] = cpu_solve_ms;
	result["cpu_iters"] = cpu_iters;
	result["cpu_residual"] = cpu_residual;
	result["gpu_solve_ms"] = gpu_solve_ms;
	result["gpu_max_iter"] = p_max_iter;
	result["gpu_residual"] = double(gpu_residual);
	result["max_drift"] = max_drift;
	result["mean_drift"] = mean_drift;
	return result;
}

// Cached-uniform-set variant. Reuses a pre-built external set so we
// don't pay uniform_set_create per CG iter (~50 µs × 200 iters = 10 ms
// overhead). user_data carries the cached set RID.
struct MasApplyCachedCtx {
	cassie_mas_gpu::CassieMasGpu *mas;
	cassie_mas_gpu::MasGpuHandle *handle;
	cassie_mas_gpu::CassieMasGpu::ExternalApplySets cached_sets;
};

static bool _mas_apply_cached_thunk(void *p_user_data,
		RID p_external_r, RID p_external_z,
		RenderingDevice::ComputeListID p_cl) {
	(void)p_external_r;
	(void)p_external_z;
	MasApplyCachedCtx *ctx = static_cast<MasApplyCachedCtx *>(p_user_data);
	return ctx->mas->apply_mas_to_compute_list_cached(*ctx->handle,
			ctx->cached_sets, p_cl);
}

Dictionary CassieProfileMover::compare_jacobi_mas_solve(int p_max_iter) {
	Dictionary result;
	if (harmonic_impl == nullptr || !harmonic_ready) {
		result["available"] = false;
		result["error"] = "mover not bound or harmonic state not built";
		return result;
	}

	cassie_pcg::CSRMatrix &A = harmonic_impl->L_II;
	const int Ni = A.rows;
	const int nnz = int(A.val.size());
	if (Ni == 0) {
		result["available"] = false;
		result["error"] = "interior count is zero";
		return result;
	}

	// Synthetic float3 RHS — non-trivial sin pattern over interior. Each
	// axis gets a different phase so the three components aren't
	// trivially identical.
	LocalVector<float> b3;
	b3.resize(Ni * 3);
	for (int i = 0; i < Ni; ++i) {
		const double t = double(i) * 0.01;
		b3[i * 3 + 0] = float(Math::sin(t) + 0.5);
		b3[i * 3 + 1] = float(Math::sin(t + 1.0) + 0.5);
		b3[i * 3 + 2] = float(Math::sin(t + 2.0) + 0.5);
	}

	// Narrow L_II + diag_inv to float for the GPU side.
	LocalVector<int32_t> row_ptr_i;
	LocalVector<int32_t> col_idx_i;
	LocalVector<float> values_f;
	LocalVector<float> diag_inv_f;
	row_ptr_i.resize(Ni + 1);
	col_idx_i.resize(nnz);
	values_f.resize(nnz);
	diag_inv_f.resize(Ni);
	for (int i = 0; i <= Ni; ++i) {
		row_ptr_i[i] = int32_t(A.row_ptr[i]);
	}
	for (int k = 0; k < nnz; ++k) {
		col_idx_i[k] = int32_t(A.col_idx[k]);
		values_f[k] = float(A.val[k]);
	}
	for (int i = 0; i < Ni; ++i) {
		diag_inv_f[i] = float(harmonic_impl->diag_inv[i]);
	}

	// Interior positions in original rest-pose order (orderhash that
	// build_mas_state uses for AABB + Morton).
	LocalVector<Vector3> positions;
	positions.resize(Ni);
	for (int i = 0; i < Ni; ++i) {
		const int mesh_v = interior_to_mesh[i];
		positions[i] = rest_vertex_positions[mesh_v];
	}

	// --- GPU setup ---
	cassie_slang_gpu::CassieSlangGpu cg;
	if (!cg.is_available()) {
		result["available"] = false;
		result["error"] = "no local RD for cg_pcg3";
		return result;
	}
	// Shared-RD construction — mas uses cg's RenderingDevice so MAS
	// buffers live on the same logical device as cg's CG-loop buffers.
	// Eliminates per-iter host roundtrip in solve_sparse_gpu_mas3_shared.
	cassie_mas_gpu::CassieMasGpu mas(cg.get_rd());
	if (!mas.is_available()) {
		result["available"] = false;
		result["error"] = "no local RD for cassie_mas_gpu (shared)";
		return result;
	}

	cassie_slang_gpu::CassieSlangGpu::CgPcg3Handle cg_h =
			cg.upload_cg3_state(Ni, row_ptr_i.ptr(), col_idx_i.ptr(), nnz,
					values_f.ptr(), diag_inv_f.ptr(), b3.ptr());
	if (!cg_h.is_valid()) {
		result["available"] = false;
		result["error"] = "upload_cg3_state failed";
		return result;
	}

	cassie_mas_gpu::MasGpuHandle mas_h = mas.build_mas_state(A, positions.ptr());
	if (!mas_h.is_valid()) {
		cg.free_cg3_state(cg_h);
		result["available"] = false;
		result["error"] = "build_mas_state failed";
		return result;
	}

	// --- Solve A — Jacobi3 ---
	LocalVector<float> x_init;
	x_init.resize(Ni * 3);
	for (int i = 0; i < Ni * 3; ++i) {
		x_init[i] = 0.0f;
	}
	LocalVector<float> x_jacobi;
	x_jacobi.resize(Ni * 3);
	const Time *time = Time::get_singleton();
	// Warm up the pipeline cache (first dispatch pays JIT cost).
	cg.solve_sparse_gpu_jacobi3(cg_h, x_init.ptr(), 1, x_jacobi.ptr());
	float jacobi_residual = 0.0f;
	const uint64_t j_t0 = time->get_ticks_usec();
	const bool ok_j = cg.solve_sparse_gpu_jacobi3(cg_h, x_init.ptr(),
			p_max_iter, x_jacobi.ptr(), &jacobi_residual);
	const uint64_t j_t1 = time->get_ticks_usec();
	const double jacobi_ms = double(j_t1 - j_t0) / 1000.0;

	// --- Solve B — MAS3 (shared-RD, in-compute-list apply, cached set) ---
	LocalVector<float> x_mas;
	x_mas.resize(Ni * 3);
	// Pre-build uniform sets binding cg's b_r / b_z to MAS's slot 11 /
	// slot 12. All MAS apply dispatches in this solve reuse them — no
	// uniform_set_create per CG iter.
	cassie_mas_gpu::CassieMasGpu::ExternalApplySets cached_sets =
			mas.build_external_uniform_sets(mas_h, cg_h.b_r, cg_h.b_z);
	if (!cached_sets.is_valid()) {
		cg.free_cg3_state(cg_h);
		mas.destroy_mas_state(mas_h);
		result["available"] = false;
		result["error"] = "build_external_uniform_sets failed";
		return result;
	}
	MasApplyCachedCtx ctx_cached{ &mas, &mas_h, cached_sets };
	cg.solve_sparse_gpu_mas3_shared(cg_h, x_init.ptr(), 1,
			_mas_apply_cached_thunk, &ctx_cached, x_mas.ptr()); // warm-up
	float mas_residual = 0.0f;
	const uint64_t m_t0 = time->get_ticks_usec();
	const bool ok_m = cg.solve_sparse_gpu_mas3_shared(cg_h, x_init.ptr(),
			p_max_iter, _mas_apply_cached_thunk, &ctx_cached, x_mas.ptr(),
			&mas_residual);
	const uint64_t m_t1 = time->get_ticks_usec();
	const double mas_ms = double(m_t1 - m_t0) / 1000.0;
	mas.free_external_uniform_sets(cached_sets);

	cg.free_cg3_state(cg_h);
	mas.destroy_mas_state(mas_h);

	if (!ok_j || !ok_m) {
		result["available"] = false;
		result["error"] = ok_j ? "mas3 dispatch failed" : "jacobi3 dispatch failed";
		return result;
	}

	double max_drift = 0.0, sum_drift = 0.0;
	for (int i = 0; i < Ni * 3; ++i) {
		const double d = Math::abs(double(x_jacobi[i]) - double(x_mas[i]));
		if (d > max_drift) {
			max_drift = d;
		}
		sum_drift += d;
	}
	const double mean_drift = sum_drift / double(Ni * 3);

	result["available"] = true;
	result["ni"] = Ni;
	result["nnz"] = nnz;
	result["max_iter"] = p_max_iter;
	result["jacobi3_ms"] = jacobi_ms;
	result["jacobi3_residual"] = double(jacobi_residual);
	result["mas3_ms"] = mas_ms;
	result["mas3_residual"] = double(mas_residual);
	result["max_drift"] = max_drift;
	result["mean_drift"] = mean_drift;
	return result;
}

Dictionary CassieProfileMover::count_interior_components() const {
	Dictionary result;
	if (harmonic_impl == nullptr || !harmonic_ready) {
		result["available"] = false;
		result["error"] = "mover not bound or harmonic state not built";
		return result;
	}
	const cassie_pcg::CSRMatrix &A = harmonic_impl->L_II;
	const int n = A.rows;

	DisjointSet<int> ds;
	for (int i = 0; i < n; ++i) {
		ds.insert(i);
	}
	for (int r = 0; r < n; ++r) {
		const int row_start = A.row_ptr[r];
		const int row_end = A.row_ptr[r + 1];
		for (int k = row_start; k < row_end; ++k) {
			ds.create_union(r, A.col_idx[k]);
		}
	}

	Vector<int> roots;
	ds.get_representatives(roots);

	PackedInt32Array sizes;
	int largest = 0;
	int smallest = (n > 0) ? n : 0;
	for (int i = 0; i < roots.size(); ++i) {
		Vector<int> members;
		ds.get_members(members, roots[i]);
		const int sz = members.size();
		sizes.push_back(sz);
		if (sz > largest) {
			largest = sz;
		}
		if (sz < smallest) {
			smallest = sz;
		}
	}

	result["available"] = true;
	result["interior_count"] = n;
	result["component_count"] = int(roots.size());
	result["largest_component"] = largest;
	result["smallest_component"] = smallest;
	result["sizes"] = sizes;
	return result;
}

void CassieProfileMover::_bind_methods() {
	ClassDB::bind_static_method("CassieProfileMover",
			D_METHOD("benchmark_gpu_spmv", "n", "iterations"),
			&CassieProfileMover::benchmark_gpu_spmv,
			DEFVAL(4096), DEFVAL(100));
	ClassDB::bind_static_method("CassieProfileMover",
			D_METHOD("benchmark_gpu_pcg_solve", "n", "max_iter"),
			&CassieProfileMover::benchmark_gpu_pcg_solve,
			DEFVAL(4096), DEFVAL(100));
	ClassDB::bind_method(D_METHOD("compare_cpu_gpu_solve", "max_iter"),
			&CassieProfileMover::compare_cpu_gpu_solve,
			DEFVAL(100));
	ClassDB::bind_method(D_METHOD("compare_jacobi_mas_solve", "max_iter"),
			&CassieProfileMover::compare_jacobi_mas_solve,
			DEFVAL(100));
	ClassDB::bind_method(D_METHOD("count_interior_components"),
			&CassieProfileMover::count_interior_components);
	ClassDB::bind_method(D_METHOD("bind", "patch", "curvenet"),
			&CassieProfileMover::bind);
	ClassDB::bind_method(D_METHOD("deform"), &CassieProfileMover::deform);
	ClassDB::bind_method(D_METHOD("get_bound_mesh"),
			&CassieProfileMover::get_bound_mesh);
	ClassDB::bind_method(D_METHOD("bake_skin", "max_weights_per_vert"),
			&CassieProfileMover::bake_skin, DEFVAL(8));
	ClassDB::bind_method(D_METHOD("build_skin"),
			&CassieProfileMover::build_skin);
	ClassDB::bind_method(D_METHOD("get_crack_edge_count"),
			&CassieProfileMover::get_crack_edge_count);
	ClassDB::bind_method(D_METHOD("is_bound"), &CassieProfileMover::is_bound);
	ClassDB::bind_method(D_METHOD("get_vertex_count"),
			&CassieProfileMover::get_vertex_count);
	ClassDB::bind_method(D_METHOD("get_sample_count"),
			&CassieProfileMover::get_sample_count);
	ClassDB::bind_method(D_METHOD("is_harmonic_ready"),
			&CassieProfileMover::is_harmonic_ready);
	ClassDB::bind_method(D_METHOD("get_last_solve_iters"),
			&CassieProfileMover::get_last_solve_iters);
	ClassDB::bind_method(D_METHOD("get_interior_count"),
			&CassieProfileMover::get_interior_count);
	ClassDB::bind_method(D_METHOD("get_boundary_count"),
			&CassieProfileMover::get_boundary_count);
	ClassDB::bind_method(D_METHOD("get_forward_level_count"),
			&CassieProfileMover::get_forward_level_count);
	ClassDB::bind_method(D_METHOD("get_backward_level_count"),
			&CassieProfileMover::get_backward_level_count);
	ClassDB::bind_method(D_METHOD("get_forward_level_size", "level"),
			&CassieProfileMover::get_forward_level_size);
	ClassDB::bind_method(D_METHOD("get_backward_level_size", "level"),
			&CassieProfileMover::get_backward_level_size);
}

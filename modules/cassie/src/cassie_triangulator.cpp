/**************************************************************************/
/*  cassie_triangulator.cpp                                               */
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

#include "cassie_triangulator.h"

#include "DMWT.h"
#include "MingCurve.h"
#include "Point3.h"
#include "cassie_remesh.h" // CassieRefineProfile / cassie_refine_last_profile
#include "polygon_triangulation.h"
#include "refine.h"

#include "core/error/error_macros.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/string/print_string.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

#include <cmath>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

void CassieTriangulator::_bind_methods() {
	ClassDB::bind_static_method("CassieTriangulator",
			D_METHOD("triangulate", "boundary", "target_edge_length"),
			&CassieTriangulator::triangulate);
}

namespace {

Dictionary make_failure() {
	Dictionary out;
	out["success"] = false;
	out["vertices"] = PackedVector3Array();
	out["faces"] = PackedInt32Array();
	return out;
}

// Squared distance from point `p` to the segment `(a, b)` in 3D.
// Used as the chord-error metric for Ramer-Douglas-Peucker decimation.
// Returns the perpendicular distance when the foot lies within [a, b];
// otherwise the distance to the nearer endpoint.
double point_segment_dist2(const double *p, const double *a, const double *b) {
	const double abx = b[0] - a[0], aby = b[1] - a[1], abz = b[2] - a[2];
	const double apx = p[0] - a[0], apy = p[1] - a[1], apz = p[2] - a[2];
	const double ab_len2 = abx * abx + aby * aby + abz * abz;
	if (ab_len2 <= 0.0) {
		// Degenerate segment — treat as point distance.
		return apx * apx + apy * apy + apz * apz;
	}
	double t = (apx * abx + apy * aby + apz * abz) / ab_len2;
	if (t < 0.0) {
		t = 0.0;
	}
	if (t > 1.0) {
		t = 1.0;
	}
	const double fx = a[0] + t * abx - p[0];
	const double fy = a[1] + t * aby - p[1];
	const double fz = a[2] + t * abz - p[2];
	return fx * fx + fy * fy + fz * fz;
}

// Ramer-Douglas-Peucker decimation in place on a stride-3 double boundary.
// `eps` is the chord-error tolerance (max perpendicular deviation from the
// simplified polyline). Endpoints are always kept; intermediate points kept
// when the recursive farthest-point split exceeds eps. Returns the new nB.
// Closed-curve note: the boundary is treated as a polyline; the implicit
// closing edge is not part of the decimation. For CASSIE editing strokes
// this is fine — the boundary's first/last samples are anchor knots.
int decimate_boundary_rdp(std::vector<double> &flat, int nB, double eps) {
	if (nB <= 4 || !(eps > 0.0)) {
		return nB;
	}
	std::vector<unsigned char> keep(static_cast<size_t>(nB), 0);
	keep[0] = 1;
	keep[nB - 1] = 1;
	std::vector<std::pair<int, int>> stk;
	stk.reserve(64);
	stk.emplace_back(0, nB - 1);
	const double eps2 = eps * eps;
	while (!stk.empty()) {
		const std::pair<int, int> top = stk.back();
		stk.pop_back();
		const int lo = top.first;
		const int hi = top.second;
		if (hi - lo < 2) {
			continue;
		}
		const double *a = &flat[lo * 3];
		const double *b = &flat[hi * 3];
		double max_d2 = 0.0;
		int idx = lo;
		for (int i = lo + 1; i < hi; ++i) {
			const double d2 = point_segment_dist2(&flat[i * 3], a, b);
			if (d2 > max_d2) {
				max_d2 = d2;
				idx = i;
			}
		}
		if (max_d2 > eps2) {
			keep[idx] = 1;
			stk.emplace_back(lo, idx);
			stk.emplace_back(idx, hi);
		}
	}
	std::vector<double> out;
	out.reserve(static_cast<size_t>(nB) * 3);
	for (int i = 0; i < nB; ++i) {
		if (keep[i]) {
			out.push_back(flat[i * 3]);
			out.push_back(flat[i * 3 + 1]);
			out.push_back(flat[i * 3 + 2]);
		}
	}
	const int new_n = static_cast<int>(out.size() / 3);
	if (new_n < 4) {
		// Decimation collapsed below DMWT's minimum — bail out, keep original.
		return nB;
	}
	flat.swap(out);
	return new_n;
}

Dictionary arrays_to_dict(PackedVector3Array &p_verts, PackedInt32Array &p_idx) {
	Dictionary out;
	out["success"] = true;
	out["vertices"] = p_verts;
	out["faces"] = p_idx;
	return out;
}

// Make the triangle winding self-consistent. DMWT consumes Delaunay faces
// whose vertex indices were sorted ascending in DelaunayFaces — the
// dedupe step destroys orientation. Each triangle in the output is
// therefore independently oriented; neighbors may face opposite
// directions. BFS over edge-shared neighbors, flipping any that
// orient the shared edge in the same direction as the current
// triangle (consistent winding has the shared edge traversed in
// opposite directions on each side).
void _make_winding_consistent(PackedInt32Array &p_idx) {
	const int nt = int(p_idx.size() / 3);
	if (nt < 2) {
		return;
	}
	std::unordered_map<uint64_t, std::vector<int>> edge_to_tris;
	for (int t = 0; t < nt; ++t) {
		for (int e = 0; e < 3; ++e) {
			int a = p_idx[t * 3 + e];
			int b = p_idx[t * 3 + (e + 1) % 3];
			if (a > b) {
				std::swap(a, b);
			}
			const uint64_t key = (uint64_t(uint32_t(a)) << 32) | uint64_t(uint32_t(b));
			edge_to_tris[key].push_back(t);
		}
	}
	std::vector<bool> visited(nt, false);
	std::vector<int> queue;
	queue.reserve(nt);
	visited[0] = true;
	queue.push_back(0);
	while (!queue.empty()) {
		const int t = queue.back();
		queue.pop_back();
		const int a = p_idx[t * 3 + 0];
		const int b = p_idx[t * 3 + 1];
		const int c = p_idx[t * 3 + 2];
		const int verts[3] = { a, b, c };
		for (int e = 0; e < 3; ++e) {
			int ea = verts[e];
			int eb = verts[(e + 1) % 3];
			int key_a = ea, key_b = eb;
			if (key_a > key_b) {
				std::swap(key_a, key_b);
			}
			const uint64_t key =
					(uint64_t(uint32_t(key_a)) << 32) | uint64_t(uint32_t(key_b));
			const std::vector<int> &nbrs = edge_to_tris[key];
			for (int kk = 0; kk < int(nbrs.size()); ++kk) {
				const int nt2 = nbrs[kk];
				if (nt2 == t || visited[nt2]) {
					continue;
				}
				visited[nt2] = true;
				// In nt2 find the same edge and check its direction.
				const int na = p_idx[nt2 * 3 + 0];
				const int nb = p_idx[nt2 * 3 + 1];
				const int nc = p_idx[nt2 * 3 + 2];
				const int nverts[3] = { na, nb, nc };
				bool need_flip = false;
				for (int ne = 0; ne < 3; ++ne) {
					const int nea = nverts[ne];
					const int neb = nverts[(ne + 1) % 3];
					if (nea == ea && neb == eb) {
						// Same direction → inconsistent → flip.
						need_flip = true;
						break;
					}
					if (nea == eb && neb == ea) {
						// Opposite direction → consistent.
						break;
					}
				}
				if (need_flip) {
					const int tmp = p_idx[nt2 * 3 + 1];
					p_idx.set(nt2 * 3 + 1, p_idx[nt2 * 3 + 2]);
					p_idx.set(nt2 * 3 + 2, tmp);
				}
				queue.push_back(nt2);
			}
		}
	}
}

// Strip triangles until the mesh is 2-manifold: every interior edge
// shared by exactly 2 triangles, every boundary edge by exactly 1.
// DMWT minimizes a weight sum and does NOT guarantee manifoldness —
// it can output internal walls (edge shared by 3+ tris) and other
// artifacts. Repeatedly: find an over-incident edge, drop the
// smallest-area triangle from the offending set, rebuild adjacency.
// O((non_manifold_count) · E) — non_manifold_count is small in practice.
void _enforce_manifold(const PackedVector3Array &p_verts,
		PackedInt32Array &p_idx) {
	const int initial_nt = int(p_idx.size() / 3);
	if (initial_nt < 2) {
		return;
	}
	auto edge_key = [](int a, int b) -> uint64_t {
		if (a > b) {
			std::swap(a, b);
		}
		return (uint64_t(uint32_t(a)) << 32) | uint64_t(uint32_t(b));
	};
	std::vector<bool> alive(initial_nt, true);
	bool any_dropped = true;
	while (any_dropped) {
		any_dropped = false;
		std::unordered_map<uint64_t, std::vector<int>> edge_to_tris;
		for (int t = 0; t < initial_nt; ++t) {
			if (!alive[t]) {
				continue;
			}
			for (int e = 0; e < 3; ++e) {
				const int a = p_idx[t * 3 + e];
				const int b = p_idx[t * 3 + (e + 1) % 3];
				edge_to_tris[edge_key(a, b)].push_back(t);
			}
		}
		for (const auto &kv : edge_to_tris) {
			if (kv.second.size() <= 2) {
				continue;
			}
			// Over-incident edge — drop the smallest-area triangle.
			int worst = -1;
			double worst_area = 1e30;
			for (int tt : kv.second) {
				const Vector3 v0 = p_verts[p_idx[tt * 3 + 0]];
				const Vector3 v1 = p_verts[p_idx[tt * 3 + 1]];
				const Vector3 v2 = p_verts[p_idx[tt * 3 + 2]];
				const double area = 0.5 * double((v1 - v0).cross(v2 - v0).length());
				if (area < worst_area) {
					worst_area = area;
					worst = tt;
				}
			}
			if (worst >= 0) {
				alive[worst] = false;
				any_dropped = true;
				break;
			}
		}
	}
	// Compact survivors back into p_idx.
	PackedInt32Array out;
	out.reserve(initial_nt * 3);
	for (int t = 0; t < initial_nt; ++t) {
		if (!alive[t]) {
			continue;
		}
		out.push_back(p_idx[t * 3 + 0]);
		out.push_back(p_idx[t * 3 + 1]);
		out.push_back(p_idx[t * 3 + 2]);
	}
	p_idx = out;
}

// Build Godot packed arrays from flat double/int vectors from DMWT.
void dmwt_to_packed(const std::vector<double> &verts, const std::vector<int> &faces,
		PackedVector3Array &out_verts, PackedInt32Array &out_idx) {
	const int nv = int(verts.size()) / 3;
	out_verts.resize(nv);
	for (int i = 0; i < nv; ++i) {
		out_verts.set(i, Vector3(float(verts[3 * i]), float(verts[3 * i + 1]), float(verts[3 * i + 2])));
	}
	out_idx.resize(int(faces.size()));
	for (int i = 0; i < int(faces.size()); ++i) {
		out_idx.set(i, faces[i]);
	}
}

} // namespace

Dictionary CassieTriangulator::triangulate(const PackedVector3Array &p_boundary, float p_target_edge_length) {
	// Geogram's RNG is process-global; serialize calls.
	static std::mutex triangulate_mu;
	std::lock_guard<std::mutex> lock(triangulate_mu);

	// Reset MingCurve's perturbation RNG for deterministic results.
	mwt::reset_perturb_rng(0u);

	int nB = p_boundary.size();
	if (nB < 3) {
		return make_failure();
	}
	if (!(p_target_edge_length > 0.0f)) {
		return make_failure();
	}

	// Flatten boundary to stride-3 double array.
	std::vector<double> flat_boundary;
	flat_boundary.reserve(static_cast<size_t>(nB) * 3);
	for (int i = 0; i < nB; ++i) {
		const Vector3 p = p_boundary[i];
		flat_boundary.push_back(double(p.x));
		flat_boundary.push_back(double(p.y));
		flat_boundary.push_back(double(p.z));
	}

	// Optional Ramer-Douglas-Peucker decimation. Drops near-collinear samples
	// so the downstream Delaunay3D inside MingCurve::passTetGen scales with
	// curve complexity rather than raw sample density. Gated on env so the
	// existing correctness suite (which asserts shape against exact boundary
	// indices) stays unchanged. Chord-error tolerance is a fraction of
	// target_edge_length — points off the simplified polyline by less than
	// alpha*target_edge_length are dropped.
	if (OS::get_singleton()->has_environment("CASSIE_DECIMATE_STROKES")) {
		double alpha = 0.5;
		if (OS::get_singleton()->has_environment("CASSIE_DECIMATE_ALPHA")) {
			const String s = OS::get_singleton()->get_environment("CASSIE_DECIMATE_ALPHA");
			const double parsed = s.to_float();
			if (parsed > 0.0 && parsed < 10.0) {
				alpha = parsed;
			}
		}
		const double eps = alpha * double(p_target_edge_length);
		const int new_n = decimate_boundary_rdp(flat_boundary, nB, eps);
		if (new_n != nB) {
			nB = new_n;
		}
	}

	// nB == 3 fast path: single triangle, skip DMWT.
	if (nB == 3) {
		const double ax = flat_boundary[0], ay = flat_boundary[1], az = flat_boundary[2];
		const double bx = flat_boundary[3], by = flat_boundary[4], bz = flat_boundary[5];
		const double cx = flat_boundary[6], cy = flat_boundary[7], cz = flat_boundary[8];
		const double e1x = bx - ax, e1y = by - ay, e1z = bz - az;
		const double e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
		const double nx = e1y * e2z - e1z * e2y;
		const double ny = e1z * e2x - e1x * e2z;
		const double nz = e1x * e2y - e1y * e2x;
		if (0.5 * std::sqrt(nx * nx + ny * ny + nz * nz) < 1e-9) {
			return make_failure();
		}
		PackedVector3Array verts = { Vector3(float(ax), float(ay), float(az)),
			Vector3(float(bx), float(by), float(bz)),
			Vector3(float(cx), float(cy), float(cz)) };
		PackedInt32Array idx = { 0, 1, 2 };
		refine_patch(verts, idx, p_target_edge_length);
		return arrays_to_dict(verts, idx);
	}

	// nB >= 4: MingCurve edge-protection → DMWT → refine.
	// Per-stage timing — only emitted when run with --verbose. Lets the
	// pipeline bench (ENG-81) break down where the per-cycle 16 ms goes.
	const bool verbose = OS::get_singleton()->has_environment("CASSIE_TRIANGULATOR_PROFILE");
	const Time *time = verbose ? Time::get_singleton() : nullptr;
	uint64_t t_mc = 0, t_dmwt_prep = 0, t_dmwt_start = 0, t_pack = 0, t_refine = 0;
	// edgeProtect sub-stages (ENG-82 hot-block localization).
	uint64_t t_ep_perturb = 0, t_ep_isprot1 = 0, t_ep_corner = 0,
			 t_ep_midpts = 0, t_ep_finalize = 0;
	// dmwt_prep sub-stages (post-ENG-82 follow-up).
	uint64_t t_dp_gentri = 0, t_dp_buildlist = 0, t_dp_delaunay = 0;
	// refine sub-stages (ENG-87 probe). Populated by cassie_remesh under
	// the CASSIE_REFINE_PROFILE env gate; readable via cassie_refine_last_profile().
	uint64_t t_rf_bvh = 0, t_rf_split = 0, t_rf_collapse = 0,
			 t_rf_flip = 0, t_rf_smooth = 0, t_rf_adj = 0;
	int rf_iters = 0;
	int ep_perturb_iters = 0;

	const uint64_t t_mc0 = verbose ? time->get_ticks_usec() : 0;
	const int point_limit = 1000000;
	mwt::MingCurve curve(flat_boundary.data(), nB, point_limit, false);
	if (verbose) {
		// Mirror MingCurve::edgeProtect(true) body with per-stage timing.
		// Behaviour-preserving: identical call sequence via *Probe()
		// forwarders exposed in MingCurve.h.
		const uint64_t t_ep_perturb0 = time->get_ticks_usec();
		while (!curve.passTetGenProbe()) {
			curve.isDeGen = true;
			curve.perturbPtsProbe(0.0001); // plainPTB default in MingCurve.cpp
			ep_perturb_iters++;
			if (ep_perturb_iters > 500) {
				curve.badInput = true;
				break;
			}
		}
		t_ep_perturb = time->get_ticks_usec() - t_ep_perturb0;

		const uint64_t t_ep_isprot10 = time->get_ticks_usec();
		const bool already_protected = curve.isProtectedProbe();
		t_ep_isprot1 = time->get_ticks_usec() - t_ep_isprot10;

		if (!already_protected && !curve.badInput) {
			const uint64_t t_ep_corner0 = time->get_ticks_usec();
			curve.protectCornerProbe();
			t_ep_corner = time->get_ticks_usec() - t_ep_corner0;

			const uint64_t t_ep_midpts0 = time->get_ticks_usec();
			curve.insertMidPointsProbe();
			t_ep_midpts = time->get_ticks_usec() - t_ep_midpts0;
		}

		if (curve.badInput) {
			return make_failure();
		}
		const uint64_t t_ep_finalize0 = time->get_ticks_usec();
		curve.getCurveAfterEPProbe();
		t_ep_finalize = time->get_ticks_usec() - t_ep_finalize0;
		t_mc = time->get_ticks_usec() - t_mc0;
	} else {
		if (!curve.edgeProtect(true)) {
			return make_failure();
		}
	}

	const int ptn = curve.getNumOfPoints();
	double *pts = curve.getPoints();
	double *deGenPts = curve.getDeGenPoints();

	const uint64_t t_pp0 = verbose ? time->get_ticks_usec() : 0;
	mwt::DMWT dmwt(ptn, pts, deGenPts, curve.isDeGen);
	dmwt.setWeights(0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
	dmwt.setDot(false);
	// Cross-stage Delaunay reuse: MingCurve already computed Delaunay3D on the
	// same post-EP points. When it can hand off a remapped trifacelist, DMWT
	// skips its own delaunay.compute() call (~3 ms at nB=20, ~79 ms at nB=140
	// on the medium 8x8 bench). The vector outlives dmwt's preprocess() call.
	std::vector<int> presup_tris;
	if (curve.getRemappedDelaunayFaces(presup_tris)) {
		dmwt.setPresuppliedTris(presup_tris.data(), int(presup_tris.size() / 3));
	}
	if (verbose) {
		const uint64_t t_gt0 = time->get_ticks_usec();
		dmwt.genTriCandidatesProbe();
		t_dp_gentri = time->get_ticks_usec() - t_gt0;
		t_dp_delaunay = dmwt.last_delaunay_us;
		const uint64_t t_bl0 = time->get_ticks_usec();
		dmwt.buildListProbe();
		t_dp_buildlist = time->get_ticks_usec() - t_bl0;
		t_dmwt_prep = time->get_ticks_usec() - t_pp0;
	} else {
		dmwt.preprocess();
	}

	const uint64_t t_st0 = verbose ? time->get_ticks_usec() : 0;
	if (!dmwt.start()) {
		return make_failure();
	}
	if (verbose) {
		t_dmwt_start = time->get_ticks_usec() - t_st0;
	}

	const uint64_t t_pk0 = verbose ? time->get_ticks_usec() : 0;
	std::vector<double> verts;
	std::vector<int> faces;
	dmwt.getResultVectors(verts, faces);

	PackedVector3Array packed_verts;
	PackedInt32Array packed_idx;
	dmwt_to_packed(verts, faces, packed_verts, packed_idx);
	// DMWT's minimum-weight subset doesn't enforce 2-manifold. Upstream
	// gets manifoldness for free from pmp::SurfaceMesh (its half-edge
	// data structure rejects non-manifold add_triangle calls). We don't
	// have PMP, so enforce explicitly: drop offending triangles until
	// every interior edge has ≤2 incident faces.
	_enforce_manifold(packed_verts, packed_idx);
	// Then make winding self-consistent across the surviving triangles.
	_make_winding_consistent(packed_idx);
	if (verbose) {
		t_pack = time->get_ticks_usec() - t_pk0;
	}

	// refine_patch keeps vertices on the DMWT-output surface internally
	// (pmp use_projection=true), so no separate reference copy is needed.
	const uint64_t t_rf0 = verbose ? time->get_ticks_usec() : 0;
	refine_patch(packed_verts, packed_idx, p_target_edge_length);
	if (verbose) {
		t_refine = time->get_ticks_usec() - t_rf0;
		const CassieRefineProfile &rp = cassie_refine_last_profile();
		t_rf_bvh = rp.bvh_build_us;
		t_rf_split = rp.split_us;
		t_rf_collapse = rp.collapse_us;
		t_rf_flip = rp.flip_us;
		t_rf_smooth = rp.smooth_us;
		t_rf_adj = rp.adjacency_us;
		rf_iters = rp.iterations;
	}

	if (verbose) {
		print_line(vformat(
				"[CassieTriangulator] nB=%d  mingcurve=%d us  dmwt_prep=%d us  "
				"dmwt_start=%d us  pack=%d us  refine=%d us  total=%d us",
				nB, int(t_mc), int(t_dmwt_prep), int(t_dmwt_start),
				int(t_pack), int(t_refine),
				int(t_mc + t_dmwt_prep + t_dmwt_start + t_pack + t_refine)));
		print_line(vformat(
				"[CassieTriangulator]   dmwt_prep: genTri=%d us (delaunay3d=%d us)  "
				"buildList=%d us",
				int(t_dp_gentri), int(t_dp_delaunay), int(t_dp_buildlist)));
		print_line(vformat(
				"[CassieTriangulator]   refine: bvh=%d us  split=%d us  collapse=%d us  "
				"flip=%d us  smooth=%d us  adjacency=%d us  iters=%d",
				int(t_rf_bvh), int(t_rf_split), int(t_rf_collapse),
				int(t_rf_flip), int(t_rf_smooth), int(t_rf_adj), rf_iters));
		const CassieRefineProfile &rp = cassie_refine_last_profile();
		print_line(vformat(
				"[CassieTriangulator]   refine.flip: per_iter=[%d,%d,%d] us  "
				"calls=%d  guard_iters=%d  flips_committed=%d  circumcircle_calls=%d",
				int(rp.flip_us_iter[0]), int(rp.flip_us_iter[1]),
				int(rp.flip_us_iter[2]), rp.flip_calls,
				rp.flip_guard_iters, rp.flips_committed,
				rp.circumcircle_calls));
		print_line(vformat(
				"[CassieTriangulator]   edgeProtect: perturb=%d us (iters=%d)  "
				"isprotected=%d us  protectCorner=%d us  insertMidPoints=%d us  "
				"finalize=%d us",
				int(t_ep_perturb), ep_perturb_iters, int(t_ep_isprot1),
				int(t_ep_corner), int(t_ep_midpts), int(t_ep_finalize)));
	}

	return arrays_to_dict(packed_verts, packed_idx);
}

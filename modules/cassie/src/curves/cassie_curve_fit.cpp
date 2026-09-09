/**************************************************************************/
/*  cassie_curve_fit.cpp                                                  */
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

#include "cassie_curve_fit.h"

#include "../solver/slang_dispatch/curve_casteljau_dispatch.h"
#include "../solver/slang_dispatch/curve_generate_bezier_dispatch.h"
#include "../solver/slang_dispatch/curve_newton_dispatch.h"
#include "rdp_simplify.h"

#include "core/templates/vector.h"

namespace {

// Internal absolute-coordinate cubic Bezier. Mirrors the Cassie CubicBezier
// class. Storage is kept private to this file; output curves are emitted as
// Godot Curve3D via the anchor + offset conversion (see header).
struct CubicBezier {
	Vector3 p0;
	Vector3 p1;
	Vector3 p2;
	Vector3 p3;

	// Polynomial coefficients for fast eval: B(t) = c0 + c1 t + c2 t^2 + c3 t^3.
	Vector3 c0;
	Vector3 c1;
	Vector3 c2;
	Vector3 c3;

	void update() {
		c0 = p0;
		c1 = real_t(3.0) * (p1 - p0);
		c2 = real_t(3.0) * (p0 - real_t(2.0) * p1 + p2);
		c3 = -p0 + real_t(3.0) * p1 - real_t(3.0) * p2 + p3;
	}

	CubicBezier() = default;
	CubicBezier(const Vector3 &v0, const Vector3 &v1, const Vector3 &v2, const Vector3 &v3) :
			p0(v0), p1(v1), p2(v2), p3(v3) {
		update();
	}

	Vector3 calculate(real_t t) const {
		const real_t t2 = t * t;
		const real_t t3 = t2 * t;
		return c0 + c1 * t + c2 * t2 + c3 * t3;
	}

	Vector3 derivative1(real_t t) const {
		const real_t t2 = t * t;
		return c1 + real_t(2.0) * c2 * t + real_t(3.0) * c3 * t2;
	}

	Vector3 derivative2(real_t t) const {
		return real_t(2.0) * c2 + real_t(6.0) * c3 * t;
	}
};

// Chord-length parameterization: u[i] = cumulative chord distance / total.
// Result spans [0, 1] with u[0] = 0 and u[N-1] = 1.
static void chord_length_parameterize(const Vector<Vector3> &p_points, Vector<real_t> &r_u) {
	const int n = p_points.size();
	r_u.resize(n);
	real_t *u = r_u.ptrw();
	u[0] = 0.0;
	for (int i = 1; i < n; ++i) {
		u[i] = u[i - 1] + p_points[i].distance_to(p_points[i - 1]);
	}
	const real_t total = u[n - 1];
	if (total > 0.0) {
		const real_t inv = real_t(1.0) / total;
		for (int i = 1; i < n; ++i) {
			u[i] *= inv;
		}
	}
}

// Generate a single cubic Bezier fitting the data. Forwards to the
// Slang-emitted kernel (CassieAvbd.CurveGenerateBezier Lean module).
// The kernel runs in fp32; this wrapper packs/unpacks to keep the
// real_t-typed C++ callers untouched.
static CubicBezier generate_bezier(const Vector<Vector3> &p_points, const Vector<real_t> &p_u,
		const Vector3 &p_tangent_a, const Vector3 &p_tangent_b) {
	const int n = p_points.size();
	if (n < 2) {
		const Vector3 p0 = n >= 1 ? p_points[0] : Vector3();
		return CubicBezier(p0, p0, p0, p0);
	}
	LocalVector<float> in_pts;
	in_pts.resize(n * 3);
	LocalVector<float> in_u;
	in_u.resize(n);
	for (int i = 0; i < n; ++i) {
		in_pts[i * 3 + 0] = float(p_points[i].x);
		in_pts[i * 3 + 1] = float(p_points[i].y);
		in_pts[i * 3 + 2] = float(p_points[i].z);
		in_u[i] = float(p_u[i]);
	}
	const float ta[3] = { float(p_tangent_a.x), float(p_tangent_a.y), float(p_tangent_a.z) };
	const float tb[3] = { float(p_tangent_b.x), float(p_tangent_b.y), float(p_tangent_b.z) };
	float out_ctrl[12] = {};
	cassie_slang_dispatch::curve_generate_bezier(
			ta, tb, uint32_t(n), in_pts.ptr(), in_u.ptr(), out_ctrl);
	const Vector3 p0(out_ctrl[0], out_ctrl[1], out_ctrl[2]);
	const Vector3 p1(out_ctrl[3], out_ctrl[4], out_ctrl[5]);
	const Vector3 p2(out_ctrl[6], out_ctrl[7], out_ctrl[8]);
	const Vector3 p3(out_ctrl[9], out_ctrl[10], out_ctrl[11]);
	return CubicBezier(p0, p1, p2, p3);
}

// Max fit error and the index where it occurs. Mirrors ComputeMaxError.
static void compute_max_error(const Vector<Vector3> &p_points, const CubicBezier &p_b,
		const Vector<real_t> &p_u, real_t &r_max_dist, int &r_split_index) {
	const int n = p_points.size();
	r_max_dist = 0.0;
	r_split_index = n / 2;
	for (int i = 0; i < n; ++i) {
		const real_t d = p_b.calculate(p_u[i]).distance_to(p_points[i]);
		if (d > r_max_dist) {
			r_max_dist = d;
			r_split_index = i;
		}
	}
}

// Newton-Raphson reparameterisation pass. Forwards to the Slang-emitted
// kernel (CassieAvbd.CurveNewton Lean module). The kernel operates in
// fp32; this wrapper packs/unpacks per-precision and keeps the
// real_t-typed callers untouched.
static void reparameterize(const CubicBezier &p_b, const Vector<Vector3> &p_points,
		const Vector<real_t> &p_u, Vector<real_t> &r_u2) {
	const int n = p_points.size();
	r_u2.resize(n);
	if (n == 0) {
		return;
	}
	LocalVector<float> in_pts;
	in_pts.resize(n * 3);
	LocalVector<float> in_u;
	in_u.resize(n);
	for (int i = 0; i < n; ++i) {
		in_pts[i * 3 + 0] = float(p_points[i].x);
		in_pts[i * 3 + 1] = float(p_points[i].y);
		in_pts[i * 3 + 2] = float(p_points[i].z);
		in_u[i] = float(p_u[i]);
	}
	LocalVector<float> out_u;
	out_u.resize(n);
	const float a[3] = { float(p_b.p0.x), float(p_b.p0.y), float(p_b.p0.z) };
	const float b[3] = { float(p_b.p1.x), float(p_b.p1.y), float(p_b.p1.z) };
	const float c[3] = { float(p_b.p2.x), float(p_b.p2.y), float(p_b.p2.z) };
	const float d[3] = { float(p_b.p3.x), float(p_b.p3.y), float(p_b.p3.z) };
	cassie_slang_dispatch::curve_newton_reparameterize(
			a, b, c, d, uint32_t(n),
			in_pts.ptr(), in_u.ptr(), out_u.ptr());
	real_t *u2 = r_u2.ptrw();
	for (int i = 0; i < n; ++i) {
		u2[i] = real_t(out_u[i]);
	}
}

// Recursive Schneider fit. Mirrors the recursive BezierCurve.FitCurve overload.
static void fit_curve_recursive(const Vector<Vector3> &p_points, const Vector3 &p_tangent_a,
		const Vector3 &p_tangent_b, float p_error, Vector<CubicBezier> &r_out) {
	const int n = p_points.size();
	if (n < 2) {
		return;
	}
	if (n == 2) {
		r_out.push_back(CubicBezier(p_points[0], p_points[0], p_points[1], p_points[1]));
		return;
	}
	if (n == 3) {
		r_out.push_back(CubicBezier(p_points[0], p_points[1], p_points[1], p_points[2]));
		return;
	}

	const int max_iter = 20;

	Vector<real_t> u;
	chord_length_parameterize(p_points, u);

	CubicBezier b = generate_bezier(p_points, u, p_tangent_a, p_tangent_b);

	real_t max_err = 0.0;
	int split = 0;
	compute_max_error(p_points, b, u, max_err, split);
	if (max_err < p_error) {
		r_out.push_back(b);
		return;
	}

	if (max_err < p_error * real_t(10.0)) {
		for (int i = 0; i < max_iter; ++i) {
			Vector<real_t> u2;
			reparameterize(b, p_points, u, u2);
			b = generate_bezier(p_points, u2, p_tangent_a, p_tangent_b);
			compute_max_error(p_points, b, u2, max_err, split);
			if (max_err < p_error) {
				r_out.push_back(b);
				return;
			}
			u = u2;
		}
	}

	// Subdivide at the worst-fit point. Tangents at the split mirror across
	// the split for G1 continuity heuristic.
	const Vector3 t_left_local = (p_points[split - 1] - p_points[split]).normalized();
	const Vector3 t_right_local = (p_points[split] - p_points[split + 1]).normalized();
	const Vector3 t_split = ((t_left_local + t_right_local) * real_t(0.5)).normalized();
	const Vector3 t_left = t_split;
	const Vector3 t_right = -t_split;

	const int left_count = split < p_points.size() - 1 ? split + 1 : split;
	Vector<Vector3> left;
	left.resize(left_count);
	for (int i = 0; i < left_count; ++i) {
		left.write[i] = p_points[i];
	}
	Vector<Vector3> right;
	right.resize(n - split);
	for (int i = 0; i < n - split; ++i) {
		right.write[i] = p_points[split + i];
	}

	fit_curve_recursive(left, p_tangent_a, t_left, p_error, r_out);
	fit_curve_recursive(right, t_right, p_tangent_b, p_error, r_out);
}

// Convert the absolute-coord CubicBezier segments into a Curve3D using
// the anchor + (in/out offset) representation. See header comment for the
// derivation of the +/- arithmetic.
static Ref<Curve3D> build_curve3d_from_segments(const Vector<CubicBezier> &p_segs) {
	Ref<Curve3D> curve;
	curve.instantiate();
	if (p_segs.is_empty()) {
		return curve;
	}

	curve->add_point(p_segs[0].p0, Vector3(), p_segs[0].p1 - p_segs[0].p0);
	for (int i = 0; i < p_segs.size(); ++i) {
		const CubicBezier &b = p_segs[i];
		// On segment boundaries past the first, set the previous anchor's out
		// to this segment's first handle offset (P1 - P0). For the very first
		// segment this was already done by the initial add_point above.
		if (i > 0) {
			curve->set_point_out(curve->get_point_count() - 1, b.p1 - b.p0);
		}
		curve->add_point(b.p3, b.p2 - b.p3, Vector3());
	}
	return curve;
}

} // namespace

Ref<Curve3D> cassie_fit_line(const Vector3 &p_a, const Vector3 &p_b) {
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(p_a, Vector3(), Vector3());
	curve->add_point(p_b, Vector3(), Vector3());
	return curve;
}

namespace {

// De Casteljau split of one cubic Bezier segment (a, b, c, d) at u ∈ [0, 1].
// Returns the new control points of the left half (la..ld) and right half
// (ra..rd). After the split, la == a, rd == d, and ld == ra is the cut
// point on the curve.
//
// Forwards to the Lean-emitted Slang kernel at
// `modules/cassie/lean/CassieAvbd/CurveCasteljau.lean` (CPU-target slangc
// output included by curve_casteljau_dispatch.cpp). The kernel is float32;
// Vector3 round-trips through float arrays at the boundary. Editing-time
// precision is sub-anchor — well within the snap thresholds used by
// callers (cassie_curve_cut_at, cassie_curve_split_for_constraints).
void cubic_split(const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c, const Vector3 &p_d,
		real_t p_u,
		Vector3 &r_la, Vector3 &r_lb, Vector3 &r_lc, Vector3 &r_ld,
		Vector3 &r_ra, Vector3 &r_rb, Vector3 &r_rc, Vector3 &r_rd) {
	const float a[3] = { float(p_a.x), float(p_a.y), float(p_a.z) };
	const float b[3] = { float(p_b.x), float(p_b.y), float(p_b.z) };
	const float c[3] = { float(p_c.x), float(p_c.y), float(p_c.z) };
	const float d[3] = { float(p_d.x), float(p_d.y), float(p_d.z) };

	float la[3], lb[3], lc[3], ld[3], ra[3], rb[3], rc[3], rd[3];
	cassie_slang_dispatch::curve_casteljau(a, b, c, d, float(p_u),
			la, lb, lc, ld, ra, rb, rc, rd);

	auto load = [](const float src[3]) -> Vector3 {
		return Vector3(real_t(src[0]), real_t(src[1]), real_t(src[2]));
	};
	r_la = load(la);
	r_lb = load(lb);
	r_lc = load(lc);
	r_ld = load(ld);
	r_ra = load(ra);
	r_rb = load(rb);
	r_rc = load(rc);
	r_rd = load(rd);
}

} // namespace

Ref<Curve3D> cassie_curve_cut_at(const Ref<Curve3D> &p_curve, float p_t,
		bool p_throw_before, float p_snap_threshold) {
	Ref<Curve3D> result;
	result.instantiate();
	if (p_curve.is_null() || p_curve->get_point_count() < 2) {
		return result;
	}
	const int n_anchors = p_curve->get_point_count();
	const int n_segs = n_anchors - 1;
	if (n_segs < 1) {
		return result;
	}

	// Convert [0, 1] global t to (segment_idx, local u). Uniform anchor
	// spacing — matches BezierCurve.ConvertPolyBezierParameter.
	const real_t t_clamped = CLAMP(real_t(p_t), real_t(0.0), real_t(1.0));
	int seg = Math::floor(t_clamped * real_t(n_segs));
	if (seg >= n_segs) {
		seg = n_segs - 1;
	}
	real_t u = t_clamped * real_t(n_segs) - real_t(seg);
	if (u < 0.0) {
		u = 0.0;
	}
	if (u > 1.0) {
		u = 1.0;
	}

	// Anchor + handle positions for this segment.
	const Vector3 a0 = p_curve->get_point_position(seg);
	const Vector3 a1 = a0 + p_curve->get_point_out(seg);
	const Vector3 a3 = p_curve->get_point_position(seg + 1);
	const Vector3 a2 = a3 + p_curve->get_point_in(seg + 1);

	// Cut point candidate.
	Vector3 la, lb, lc, ld, ra, rb, rc, rd;
	cubic_split(a0, a1, a2, a3, u, la, lb, lc, ld, ra, rb, rc, rd);
	const Vector3 cut_pos = ld;

	// Snap to an existing anchor if within threshold.
	int split_anchor = -1; // anchor index BEFORE which the cut sits
	if (cut_pos.distance_to(a0) < p_snap_threshold) {
		split_anchor = seg;
	} else if (cut_pos.distance_to(a3) < p_snap_threshold) {
		split_anchor = seg + 1;
	}

	if (split_anchor >= 0) {
		// No split — just discard the unwanted side.
		if (p_throw_before) {
			for (int i = split_anchor; i < n_anchors; ++i) {
				const Vector3 in_h = (i == split_anchor) ? Vector3() : p_curve->get_point_in(i);
				const Vector3 out_h = (i == n_anchors - 1) ? Vector3() : p_curve->get_point_out(i);
				result->add_point(p_curve->get_point_position(i), in_h, out_h);
			}
		} else {
			for (int i = 0; i <= split_anchor; ++i) {
				const Vector3 in_h = (i == 0) ? Vector3() : p_curve->get_point_in(i);
				const Vector3 out_h = (i == split_anchor) ? Vector3() : p_curve->get_point_out(i);
				result->add_point(p_curve->get_point_position(i), in_h, out_h);
			}
		}
		return result;
	}

	// True De Casteljau split inside segment `seg`.
	if (p_throw_before) {
		// Keep right-of-cut + everything after.
		// New first anchor at cut point with out = rb - ra.
		result->add_point(ra, Vector3(), rb - ra);
		// Next anchor is the original segment's end anchor, with in = rc - rd.
		result->add_point(a3, rc - a3, seg + 1 < n_anchors - 1 ? p_curve->get_point_out(seg + 1) : Vector3());
		// Append the rest verbatim.
		for (int i = seg + 2; i < n_anchors; ++i) {
			const Vector3 in_h = p_curve->get_point_in(i);
			const Vector3 out_h = (i == n_anchors - 1) ? Vector3() : p_curve->get_point_out(i);
			result->add_point(p_curve->get_point_position(i), in_h, out_h);
		}
	} else {
		// Keep everything up to seg + left-of-cut.
		for (int i = 0; i <= seg; ++i) {
			const Vector3 in_h = (i == 0) ? Vector3() : p_curve->get_point_in(i);
			const Vector3 out_h = (i == seg) ? (lb - la) : p_curve->get_point_out(i);
			result->add_point(p_curve->get_point_position(i), in_h, out_h);
		}
		// New last anchor at cut point with in = lc - ld.
		result->add_point(ld, lc - ld, Vector3());
	}
	return result;
}

Ref<Curve3D> cassie_fit_curve(const PackedVector3Array &p_points, float p_error, float p_rdp_error) {
	const int input_n = p_points.size();
	if (input_n < 2) {
		return Ref<Curve3D>();
	}

	if (input_n == 2) {
		return cassie_fit_line(p_points[0], p_points[1]);
	}

	// Dedup + RDP pre-pass on the input polyline.
	const PackedVector3Array dedup = cassie_rdp_remove_duplicates(p_points);
	const int dn = dedup.size();
	if (dn < 2) {
		return Ref<Curve3D>();
	}
	if (dn == 2) {
		return cassie_fit_line(dedup[0], dedup[1]);
	}

	const PackedInt32Array keep_idx = cassie_rdp_reduce(dedup, p_rdp_error);
	Vector<Vector3> kept;
	kept.resize(keep_idx.size());
	for (int i = 0; i < keep_idx.size(); ++i) {
		kept.write[i] = dedup[keep_idx[i]];
	}
	const int kn = kept.size();
	if (kn < 2) {
		return Ref<Curve3D>();
	}
	if (kn == 2) {
		return cassie_fit_line(kept[0], kept[1]);
	}

	// Endpoint tangents from the first and last kept chord.
	const Vector3 tangent_a = (kept[1] - kept[0]).normalized();
	const Vector3 tangent_b = (kept[kn - 2] - kept[kn - 1]).normalized();

	Vector<CubicBezier> segments;
	fit_curve_recursive(kept, tangent_a, tangent_b, p_error, segments);

	return build_curve3d_from_segments(segments);
}

Vector<Ref<Curve3D>> cassie_curve_split_for_constraints(
		const Ref<Curve3D> &p_curve,
		const PackedFloat32Array &p_params,
		float p_snap_threshold) {
	Vector<Ref<Curve3D>> result;
	if (p_curve.is_null() || p_curve->get_point_count() < 2) {
		result.push_back(p_curve);
		return result;
	}

	const int n = p_params.size();
	if (n == 0) {
		result.push_back(p_curve);
		return result;
	}

	// Copy + sort the params so the renormalization-by-remainder walk is
	// well-defined even if the caller passed them out of order.
	Vector<real_t> params;
	params.resize(n);
	{
		real_t *w = params.ptrw();
		const float *src = p_params.ptr();
		for (int i = 0; i < n; ++i) {
			w[i] = real_t(src[i]);
		}
	}
	params.sort();

	Ref<Curve3D> remainder = p_curve;
	real_t last_t = real_t(0.0);
	for (int i = 0; i < n; ++i) {
		const real_t t = CLAMP(params[i], real_t(0.0), real_t(1.0));
		const real_t window = real_t(1.0) - last_t;
		// last_t == 1.0 means everything left to split sits on the final
		// anchor — push degenerate substrokes for the remaining params.
		real_t local_t = real_t(0.0);
		if (window > real_t(0.0)) {
			local_t = (t - last_t) / window;
			if (local_t < real_t(0.0)) {
				local_t = real_t(0.0);
			} else if (local_t > real_t(1.0)) {
				local_t = real_t(1.0);
			}
		}
		Ref<Curve3D> left = cassie_curve_cut_at(remainder, float(local_t),
				/*p_throw_before=*/false, p_snap_threshold);
		Ref<Curve3D> right = cassie_curve_cut_at(remainder, float(local_t),
				/*p_throw_before=*/true, p_snap_threshold);
		result.push_back(left);
		remainder = right;
		last_t = t;
	}
	result.push_back(remainder);
	return result;
}

float cassie_curve_project_point(
		const Ref<Curve3D> &p_curve,
		const Vector3 &p_target,
		Vector3 &r_position,
		int p_max_iter) {
	r_position = Vector3();
	if (p_curve.is_null() || p_curve->get_point_count() < 2) {
		return 0.0f;
	}
	const int n_anchors = p_curve->get_point_count();
	const int n_segs = n_anchors - 1;
	if (n_segs < 1) {
		return 0.0f;
	}

	// Rebuild CubicBeziers from anchor + handle data. Direct CubicBezier
	// math (calculate/derivative1/derivative2) is cheaper than going through
	// Curve3D::sample_baked inside the Newton loop.
	Vector<CubicBezier> segs;
	segs.resize(n_segs);
	for (int s = 0; s < n_segs; ++s) {
		const Vector3 a0 = p_curve->get_point_position(s);
		const Vector3 a3 = p_curve->get_point_position(s + 1);
		const Vector3 a1 = a0 + p_curve->get_point_out(s);
		const Vector3 a2 = a3 + p_curve->get_point_in(s + 1);
		segs.write[s] = CubicBezier(a0, a1, a2, a3);
	}

	// Coarse search: 8 samples per segment (plus endpoints inclusive on
	// segment 0, exclusive thereafter to avoid double counting anchors).
	const int samples_per_seg = 8;
	int best_seg = 0;
	real_t best_u = 0.0;
	real_t best_dist2 = real_t(1e30);
	for (int s = 0; s < n_segs; ++s) {
		for (int k = 0; k <= samples_per_seg; ++k) {
			if (s > 0 && k == 0) {
				continue;
			}
			const real_t u = real_t(k) / real_t(samples_per_seg);
			const Vector3 P = segs[s].calculate(u);
			const real_t d2 = (P - p_target).length_squared();
			if (d2 < best_dist2) {
				best_dist2 = d2;
				best_seg = s;
				best_u = u;
			}
		}
	}

	// Newton refinement on the chosen segment. Same residual + Jacobian as
	// reparameterize() above: residual = d · q1, jacobian = q1·q1 + d·q2.
	const CubicBezier &seg = segs[best_seg];
	real_t u = best_u;
	for (int iter = 0; iter < p_max_iter; ++iter) {
		const Vector3 d = seg.calculate(u) - p_target;
		const Vector3 q1 = seg.derivative1(u);
		const Vector3 q2 = seg.derivative2(u);
		const real_t num = d.dot(q1);
		const real_t den = q1.dot(q1) + d.dot(q2);
		if (Math::is_zero_approx(den)) {
			break;
		}
		real_t u_next = u - num / den;
		if (u_next < real_t(0.0)) {
			u_next = real_t(0.0);
		} else if (u_next > real_t(1.0)) {
			u_next = real_t(1.0);
		}
		const real_t delta = Math::abs(u_next - u);
		u = u_next;
		if (delta < real_t(1e-9)) {
			break;
		}
	}

	r_position = seg.calculate(u);
	const real_t t_global = (real_t(best_seg) + u) / real_t(n_segs);
	return float(t_global);
}

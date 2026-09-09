/**************************************************************************/
/*  cassie_constraint_solver.cpp                                          */
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

#include "cassie_constraint_solver.h"

#include "cassie_pcg.h"
#include "dense_matrix.h"
#include "fidelity_energy.h"
#include "g1_constraint.h"
#include "hard_constraint.h"
#include "on_surface_energy.h"
#include "planarity_constraint.h"
#include "position_constraint.h"
#include "self_intersection_constraint.h"
#include "soft_constraint.h"
#include "tangent_constraint.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/vector.h"

#include <memory>

namespace cassie_solver {

namespace {

// Flatten a Curve3D into the (P0, P1, P2, P3, P1, P2, P3, ...) control-point
// list used by the Cassie solver. Each segment contributes 3 new points:
// the anchor, its outgoing handle, the next anchor's incoming handle.
Vector<Vector3> flatten_curve_to_control_points(const Ref<Curve3D> &p_curve) {
	Vector<Vector3> out;
	if (p_curve.is_null() || p_curve->get_point_count() < 2) {
		return out;
	}
	const int n = p_curve->get_point_count();
	out.resize(3 * (n - 1) + 1);
	int j = 0;
	for (int i = 0; i < n - 1; ++i) {
		const Vector3 anchor_i = p_curve->get_point_position(i);
		const Vector3 out_handle = p_curve->get_point_out(i);
		const Vector3 next_in = p_curve->get_point_in(i + 1);
		const Vector3 next_anchor = p_curve->get_point_position(i + 1);
		out.write[j++] = anchor_i;
		out.write[j++] = anchor_i + out_handle;
		out.write[j++] = next_anchor + next_in;
	}
	out.write[j++] = p_curve->get_point_position(n - 1);
	return out;
}

Ref<Curve3D> build_curve3d_from_control_points(const Vector<Vector3> &p_cp) {
	Ref<Curve3D> curve;
	curve.instantiate();
	const int total = p_cp.size();
	if (total < 4 || (total - 1) % 3 != 0) {
		return curve;
	}
	const int nb = (total - 1) / 3;
	// First anchor.
	curve->add_point(p_cp[0], Vector3(), p_cp[1] - p_cp[0]);
	for (int seg = 0; seg < nb; ++seg) {
		const int base = 3 * seg;
		const Vector3 next_anchor = p_cp[base + 3];
		const Vector3 next_in = p_cp[base + 2] - next_anchor;
		Vector3 next_out;
		if (seg + 1 < nb) {
			next_out = p_cp[base + 4] - next_anchor;
		}
		curve->add_point(next_anchor, next_in, next_out);
	}
	return curve;
}

// Returns t such that B(t) is the closest point on the curve to p_point.
// Coarse-to-fine bisection — adequate for constraint projection where
// sub-mm precision is unnecessary.
double project_t(const Ref<Curve3D> &p_curve, const Vector3 &p_point) {
	if (p_curve.is_null() || p_curve->get_baked_length() <= 0.0) {
		return 0.0;
	}
	const double offset = double(p_curve->get_closest_offset(p_point));
	return offset / double(p_curve->get_baked_length());
}

Vector3 sample_tangent(const Ref<Curve3D> &p_curve, double p_offset) {
	if (p_curve.is_null() || p_curve->get_baked_length() <= 0.0) {
		return Vector3();
	}
	const Transform3D xform = p_curve->sample_baked_with_rotation(real_t(p_offset), true, true);
	return -xform.basis.get_column(Vector3::AXIS_Z);
}

struct ConstraintCandidate {
	Ref<CassieConstraint> constraint;
	double t = 0.0;
	double likelihood = 0.0;
	double score = 0.0;
	bool active = true;
	bool close_to_endpoint = false;
	bool aligned_tangents = false;
};

struct FitCandidate {
	Vector<Vector3> control_points;
	double energy = 1e30;
	HashMap<int, ConstraintCandidate> active_by_anchor;
	bool planar = false;
	bool close_loop = false;
};

struct ConstraintCandidateByT {
	bool operator()(const ConstraintCandidate &p_a, const ConstraintCandidate &p_b) const {
		return p_a.t < p_b.t;
	}
};

// Track 5 Phase B+C — Eigen-free Augmented Lagrangian. The bordered KKT
// path that used Eigen::FullPivLU is deleted; ALM (which keeps the
// augmented Hessian SPD) is the only path. Inner solve is dense Jacobi-
// PCG over `cassie_solver::DenseMatrix`, matching the sparse PCG path
// in the Profile Mover.
//
// Inputs: p_A_grad is the fidelity + soft Hessian (≤ 45 × 45, SPD).
// p_b_top is the corresponding RHS. p_hard stacks the row blocks C_i.
//
// Wins: unconditional stability — A + rho * C^T C is always SPD when A
// is SPD, even with dependent C rows the bordered KKT would choke on.
LocalVector<double> solve_kkt_avbd(const DenseMatrix &p_A_grad,
		const DenseVector &p_b_top,
		const LocalVector<std::unique_ptr<HardConstraintBlock>> &p_hard,
		int p_N, double p_rho, int p_max_iter, double p_tol) {
	const int n3 = 3 * p_N;

	LocalVector<double> x;
	x.resize(n3);
	for (int i = 0; i < n3; ++i) {
		x[i] = 0.0;
	}

	// Cache diag(A_grad) for Jacobi preconditioning — used in every
	// branch so it pays for itself even at small N.
	LocalVector<double> diag_inv;
	diag_inv.resize(n3);
	const double kDiagFloor = 1e-12;
	for (int i = 0; i < n3; ++i) {
		const double d = p_A_grad(i, i);
		diag_inv[i] = 1.0 / (d > kDiagFloor ? d : kDiagFloor);
	}

	if (p_hard.is_empty()) {
		double res = 0.0;
		cassie_pcg::solve_dense(p_A_grad.data(), n3,
				p_b_top.data(), x.ptr(),
				diag_inv.ptr(), p_max_iter, p_tol, &res);
		return x;
	}

	int total_rows = 0;
	for (const std::unique_ptr<HardConstraintBlock> &h : p_hard) {
		total_rows += h->row_count();
	}

	// Stack C (total_rows × n3) and b_hard from each hard block.
	DenseMatrix C;
	C.zero_resize(total_rows, n3);
	LocalVector<double> b_hard;
	b_hard.resize(total_rows);
	for (int i = 0; i < total_rows; ++i) {
		b_hard[i] = 0.0;
	}
	int row_cursor = 0;
	for (const std::unique_ptr<HardConstraintBlock> &h : p_hard) {
		DenseMatrix C_i;
		DenseVector b_i;
		h->get_blocks(C_i, b_i);
		const int rc = h->row_count();
		for (int i = 0; i < rc; ++i) {
			for (int j = 0; j < n3; ++j) {
				C(row_cursor + i, j) = C_i(i, j);
			}
			b_hard[row_cursor + i] = b_i[i];
		}
		row_cursor += rc;
	}

	// Augmented Hessian A_aug = A_grad + rho * C^T C. Factor at row level —
	// (C^T C)_{ij} = Σ_k C_{ki} C_{kj}.
	DenseMatrix A_aug;
	A_aug.zero_resize(n3, n3);
	for (int i = 0; i < n3; ++i) {
		for (int j = 0; j < n3; ++j) {
			A_aug(i, j) = p_A_grad(i, j);
		}
	}
	for (int i = 0; i < n3; ++i) {
		for (int j = 0; j < n3; ++j) {
			double s = 0.0;
			for (int k = 0; k < total_rows; ++k) {
				s += C(k, i) * C(k, j);
			}
			A_aug(i, j) += p_rho * s;
		}
	}

	// Update diag_inv to use A_aug's diagonal.
	for (int i = 0; i < n3; ++i) {
		const double d = A_aug(i, i);
		diag_inv[i] = 1.0 / (d > kDiagFloor ? d : kDiagFloor);
	}

	LocalVector<double> lambda;
	lambda.resize(total_rows);
	for (int i = 0; i < total_rows; ++i) {
		lambda[i] = 0.0;
	}
	LocalVector<double> b_aug;
	b_aug.resize(n3);
	LocalVector<double> Cx;
	Cx.resize(total_rows);

	for (int iter = 0; iter < p_max_iter; ++iter) {
		// b_aug = p_b_top - C^T (lambda - rho * b_hard)
		LocalVector<double> tmp;
		tmp.resize(total_rows);
		for (int k = 0; k < total_rows; ++k) {
			tmp[k] = lambda[k] - p_rho * b_hard[k];
		}
		// Ct * tmp (length n3).
		for (int j = 0; j < n3; ++j) {
			double s = 0.0;
			for (int k = 0; k < total_rows; ++k) {
				s += C(k, j) * tmp[k];
			}
			b_aug[j] = p_b_top[j] - s;
		}

		double res = 0.0;
		cassie_pcg::solve_dense(A_aug.data(), n3,
				b_aug.ptr(), x.ptr(),
				diag_inv.ptr(), 64, p_tol, &res);

		// residual = C x - b_hard ; lambda += rho * residual.
		double rnorm2 = 0.0;
		for (int k = 0; k < total_rows; ++k) {
			double s = 0.0;
			for (int j = 0; j < n3; ++j) {
				s += C(k, j) * x[j];
			}
			Cx[k] = s - b_hard[k];
			lambda[k] += p_rho * Cx[k];
			rnorm2 += Cx[k] * Cx[k];
		}
		if (rnorm2 < p_tol * p_tol) {
			break;
		}
	}
	return x;
}

FitCandidate fit_for_constraints(
		const Vector<Vector3> &p_initial_cp,
		const LocalVector<ConstraintCandidate> &p_subset,
		const PackedVector3Array &p_ortho_dirs,
		const Ref<CassieSolverParams> &p_params,
		bool p_is_closed,
		double p_all_constraints_score) {
	FitCandidate result;
	result.control_points = p_initial_cp;

	const int N = p_initial_cp.size();
	if (N < 4) {
		return result;
	}

	const double proximity = p_params.is_valid() ? p_params->get_proximity_threshold() : 0.02;
	const double mu_fidelity = p_params.is_valid() ? p_params->get_mu_fidelity() : 0.7;
	const double w_p = p_params.is_valid() ? p_params->get_w_p() : 0.5;
	const double w_t = p_params.is_valid() ? p_params->get_w_t() : 0.5;

	// Build constraint-by-anchor map. Each subset constraint snaps to its
	// nearest anchor in the initial control-point list. Tier-3 simplification:
	// no on-the-fly anchor splitting; the solver works on the input curve's
	// existing anchor topology. Full SplitForConstraints support can be added
	// later via cassie_curve_fit::split_for_constraints.
	HashMap<int, ConstraintCandidate> by_anchor;
	const int Nb = (N - 1) / 3;
	for (const ConstraintCandidate &c : p_subset) {
		// Map t in [0,1] to anchor index in [0, Nb].
		int anchor_k = int(Math::round(c.t * double(Nb)));
		anchor_k = CLAMP(anchor_k, 0, Nb);
		by_anchor[anchor_k] = c;
	}
	result.active_by_anchor = by_anchor;

	// Build solver-internal hard/soft constraint lists.
	LocalVector<std::unique_ptr<HardConstraintBlock>> hard;
	LocalVector<std::unique_ptr<SoftConstraintBlock>> soft;

	Vector3 start_end_tangent = (p_initial_cp[1] - p_initial_cp[0]).normalized();

	for (const KeyValue<int, ConstraintCandidate> &kv : by_anchor) {
		const int anchor_k = kv.key;
		const ConstraintCandidate &c = kv.value;
		const int ctrl_pt_idx = anchor_k * 3;
		if (ctrl_pt_idx >= N) {
			continue;
		}
		const Vector3 d = c.constraint->get_position() - p_initial_cp[ctrl_pt_idx];
		hard.push_back(std::make_unique<PositionConstraint>(ctrl_pt_idx, d, N));

		if (c.aligned_tangents) {
			Ref<CassieIntersectionConstraint> ic = c.constraint;
			Ref<CassieMirrorPlaneConstraint> mc = c.constraint;
			Vector3 T;
			if (ic.is_valid()) {
				T = ic->get_old_curve_tangent();
				if (ctrl_pt_idx == 0 || ctrl_pt_idx == N - 1) {
					start_end_tangent = T;
				}
			} else if (mc.is_valid()) {
				T = mc->get_plane_normal();
			}
			if (T.length() > 0.9) {
				soft.push_back(std::make_unique<TangentConstraint>(ctrl_pt_idx, T, p_initial_cp));
			}
		}
	}

	bool close_loop = false;
	if (p_is_closed && Nb > 1 &&
			!(by_anchor.has(0) && by_anchor.has(Nb))) {
		const Vector3 AB0 = p_initial_cp[3 * Nb] - p_initial_cp[0];
		hard.push_back(std::make_unique<SelfIntersectionConstraint>(0, Nb, AB0, N));
		close_loop = true;
	}
	result.close_loop = close_loop;

	if (N > 4) {
		std::unique_ptr<G1Constraint> g1 = std::make_unique<G1Constraint>(p_initial_cp);
		if (g1->get_joint_count() > 0) {
			hard.push_back(std::move(g1));
		}
	}

	// ENG-54 — OnSurfaceEnergy. Folds the surface projection into the
	// solver so it composes with planarity instead of fighting it in the
	// post-solve pass at cassie_beautifier.cpp:294-316.
	if (p_params.is_valid() && p_params->get_on_surface_weight() > 0.0) {
		const Callable cb = p_params->get_project_on_patch_callback();
		if (cb.is_valid()) {
			soft.push_back(std::make_unique<OnSurfaceEnergy>(
					p_initial_cp, cb, p_params->get_on_surface_weight()));
		}
	}

	// Planarity.
	if (p_params.is_valid() && p_params->get_planarity_allowed() && N >= 3) {
		double dist = 0.0;
		Plane plane = cassie_fit_plane(p_initial_cp, dist);
		if (dist < proximity) {
			result.planar = true;
			Vector3 n = plane.normal;
			// Ortho snap.
			const double cos_tol = Math::abs(Math::cos(p_params->get_angular_proximity_threshold()));
			for (int o = 0; o < p_ortho_dirs.size(); ++o) {
				const Vector3 axis = p_ortho_dirs[o].normalized();
				if (Math::abs(n.dot(axis)) > cos_tol) {
					n = axis * (n.dot(axis) >= 0.0 ? 1.0 : -1.0);
					break;
				}
			}
			soft.push_back(std::make_unique<PlanarityConstraint>(n, p_initial_cp));
		}
	}

	// Build KKT. Eigen-free post Track 5 Phase B+C — DenseMatrix + dense
	// PCG. The bordered KKT path that used FullPivLU is gone; ALM is
	// the only solver path (use_avbd is now a no-op kept for back-compat).
	FidelityEnergy fidelity(p_initial_cp, w_p, w_t, proximity);
	DenseMatrix A_grad;
	fidelity.get_block(A_grad);
	DenseVector b_top;
	b_top.zero_resize(3 * N);
	for (const std::unique_ptr<SoftConstraintBlock> &s : soft) {
		DenseMatrix A_c;
		DenseVector b_c;
		s->get_blocks(A_c, b_c);
		A_grad.add_into(A_c);
		b_top.add_into(b_c);
	}

	LocalVector<double> displacements;
	if (hard.is_empty() && soft.is_empty()) {
		displacements.resize(3 * N);
		for (int i = 0; i < 3 * N; ++i) {
			displacements[i] = 0.0;
		}
	} else {
		const double rho = p_params.is_valid() ? p_params->get_avbd_rho() : 10.0;
		const int max_iter = p_params.is_valid() ? p_params->get_avbd_max_iter() : 24;
		const double tol = p_params.is_valid() ? p_params->get_avbd_tol() : 1e-9;
		displacements = solve_kkt_avbd(A_grad, b_top, hard, N, rho, max_iter, tol);
	}

	// Apply displacements.
	Vector<Vector3> new_cp = p_initial_cp;
	Vector<Vector3> disp_v3;
	disp_v3.resize(N);
	for (int i = 0; i < N; ++i) {
		disp_v3.write[i] = Vector3(real_t(displacements[3 * i]),
				real_t(displacements[3 * i + 1]),
				real_t(displacements[3 * i + 2]));
		new_cp.write[i] = p_initial_cp[i] + disp_v3[i];
	}
	result.control_points = new_cp;

	// Energies.
	const double fitting_energy = fidelity.compute(disp_v3);
	double subset_score = 0.0;
	for (const ConstraintCandidate &c : p_subset) {
		subset_score += c.score;
	}
	const double constraint_e = p_all_constraints_score > 0.0
			? Math::exp(-(subset_score * subset_score) /
					  (p_all_constraints_score * p_all_constraints_score))
			: 0.0;
	result.energy = mu_fidelity * fitting_energy + (1.0 - mu_fidelity) * constraint_e;
	return result;
}

} // namespace

} // namespace cassie_solver

void CassieSolverParams::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mu_fidelity", "value"), &CassieSolverParams::set_mu_fidelity);
	ClassDB::bind_method(D_METHOD("get_mu_fidelity"), &CassieSolverParams::get_mu_fidelity);
	ClassDB::bind_method(D_METHOD("set_w_p", "value"), &CassieSolverParams::set_w_p);
	ClassDB::bind_method(D_METHOD("get_w_p"), &CassieSolverParams::get_w_p);
	ClassDB::bind_method(D_METHOD("set_w_t", "value"), &CassieSolverParams::set_w_t);
	ClassDB::bind_method(D_METHOD("get_w_t"), &CassieSolverParams::get_w_t);
	ClassDB::bind_method(D_METHOD("set_proximity_threshold", "value"),
			&CassieSolverParams::set_proximity_threshold);
	ClassDB::bind_method(D_METHOD("get_proximity_threshold"),
			&CassieSolverParams::get_proximity_threshold);
	ClassDB::bind_method(D_METHOD("set_angular_proximity_threshold", "value"),
			&CassieSolverParams::set_angular_proximity_threshold);
	ClassDB::bind_method(D_METHOD("get_angular_proximity_threshold"),
			&CassieSolverParams::get_angular_proximity_threshold);
	ClassDB::bind_method(D_METHOD("set_min_distance_between_anchors", "value"),
			&CassieSolverParams::set_min_distance_between_anchors);
	ClassDB::bind_method(D_METHOD("get_min_distance_between_anchors"),
			&CassieSolverParams::get_min_distance_between_anchors);
	ClassDB::bind_method(D_METHOD("set_planarity_allowed", "value"),
			&CassieSolverParams::set_planarity_allowed);
	ClassDB::bind_method(D_METHOD("get_planarity_allowed"),
			&CassieSolverParams::get_planarity_allowed);
	ClassDB::bind_method(D_METHOD("set_on_surface_weight", "value"),
			&CassieSolverParams::set_on_surface_weight);
	ClassDB::bind_method(D_METHOD("get_on_surface_weight"),
			&CassieSolverParams::get_on_surface_weight);
	ClassDB::bind_method(D_METHOD("set_use_avbd", "value"),
			&CassieSolverParams::set_use_avbd);
	ClassDB::bind_method(D_METHOD("get_use_avbd"),
			&CassieSolverParams::get_use_avbd);
	ClassDB::bind_method(D_METHOD("set_avbd_rho", "value"),
			&CassieSolverParams::set_avbd_rho);
	ClassDB::bind_method(D_METHOD("get_avbd_rho"),
			&CassieSolverParams::get_avbd_rho);
	ClassDB::bind_method(D_METHOD("set_avbd_max_iter", "value"),
			&CassieSolverParams::set_avbd_max_iter);
	ClassDB::bind_method(D_METHOD("get_avbd_max_iter"),
			&CassieSolverParams::get_avbd_max_iter);
	ClassDB::bind_method(D_METHOD("set_avbd_tol", "value"),
			&CassieSolverParams::set_avbd_tol);
	ClassDB::bind_method(D_METHOD("get_avbd_tol"),
			&CassieSolverParams::get_avbd_tol);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mu_fidelity"), "set_mu_fidelity", "get_mu_fidelity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "w_p"), "set_w_p", "get_w_p");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "w_t"), "set_w_t", "get_w_t");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "proximity_threshold"),
			"set_proximity_threshold", "get_proximity_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angular_proximity_threshold"),
			"set_angular_proximity_threshold", "get_angular_proximity_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_distance_between_anchors"),
			"set_min_distance_between_anchors", "get_min_distance_between_anchors");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "planarity_allowed"),
			"set_planarity_allowed", "get_planarity_allowed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "on_surface_weight"),
			"set_on_surface_weight", "get_on_surface_weight");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_avbd"),
			"set_use_avbd", "get_use_avbd");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "avbd_rho"),
			"set_avbd_rho", "get_avbd_rho");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "avbd_max_iter"),
			"set_avbd_max_iter", "get_avbd_max_iter");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "avbd_tol"),
			"set_avbd_tol", "get_avbd_tol");
}

Dictionary CassieConstraintSolver::solve(
		const Ref<Curve3D> &p_curve,
		const TypedArray<CassieConstraint> &p_constraints,
		const PackedVector3Array &p_ortho_directions,
		const Ref<CassieSolverParams> &p_params,
		bool p_is_closed) {
	Dictionary result;
	result["curve"] = p_curve;
	result["intersections"] = TypedArray<CassieIntersectionConstraint>();
	result["mirror_constraints"] = TypedArray<CassieMirrorPlaneConstraint>();
	result["applied_anchors"] = PackedInt32Array();
	result["rejected_count"] = 0;
	result["planar"] = false;
	result["is_closed_loop"] = false;

	if (p_curve.is_null() || p_curve->get_point_count() < 2) {
		return result;
	}

	const Vector<Vector3> initial_cp = cassie_solver::flatten_curve_to_control_points(p_curve);
	if (initial_cp.size() < 4) {
		return result;
	}

	// Build all candidates with t / score / alignment.
	LocalVector<cassie_solver::ConstraintCandidate> all;
	all.reserve(p_constraints.size());
	double total_score = 0.0;
	const double angular_thresh = p_params.is_valid()
			? p_params->get_angular_proximity_threshold()
			: 0.5;
	const double cos_angular = Math::cos(angular_thresh);

	for (int i = 0; i < p_constraints.size(); ++i) {
		Ref<CassieConstraint> c = p_constraints[i];
		if (c.is_null()) {
			continue;
		}
		const double t = cassie_solver::project_t(p_curve, c->get_position());
		const Vector3 tangent_on_curve = cassie_solver::sample_tangent(p_curve,
				t * double(p_curve->get_baked_length()));
		const bool close_to_end = t < 0.05 || t > 0.95;

		double score = 1.0;
		bool is_at_node = false;
		bool align_tangent = false;
		Ref<CassieIntersectionConstraint> ic = c;
		Ref<CassieMirrorPlaneConstraint> mc = c;
		if (ic.is_valid()) {
			is_at_node = ic->get_is_at_node();
			score = is_at_node ? 2.0 : 1.5;
			const Vector3 T = ic->get_old_curve_tangent();
			if (T.length() > 0.9 && tangent_on_curve.length() > 0.9 &&
					!(is_at_node && T.dot(tangent_on_curve) > 0.0)) {
				if (Math::abs(double(T.dot(tangent_on_curve))) > cos_angular) {
					align_tangent = true;
				}
			}
		} else if (mc.is_valid()) {
			score = 1.0;
			const Vector3 N = mc->get_plane_normal();
			if (N.length() > 0.9 && tangent_on_curve.length() > 0.9) {
				if (Math::abs(double(N.dot(tangent_on_curve))) > cos_angular) {
					align_tangent = true;
				}
			}
		}
		if (close_to_end) {
			score = MAX(score, 1.25);
		}
		if (align_tangent) {
			score += 0.5;
		}
		total_score += score;

		cassie_solver::ConstraintCandidate cc;
		cc.constraint = c;
		cc.t = t;
		cc.likelihood = 1.0; // No projection-distance likelihood in Tier 3.
		cc.score = score;
		cc.close_to_endpoint = close_to_end;
		cc.aligned_tangents = align_tangent;
		all.push_back(cc);
	}

	// Sort by ascending t for determinism.
	all.sort_custom<cassie_solver::ConstraintCandidateByT>();

	// Outer greedy loop. Start with all active, try removing each, keep
	// best removal if it lowers energy, repeat.
	auto build_subset = [&]() {
		LocalVector<cassie_solver::ConstraintCandidate> subset;
		for (const cassie_solver::ConstraintCandidate &c : all) {
			if (c.active) {
				subset.push_back(c);
			}
		}
		return subset;
	};

	cassie_solver::FitCandidate best = cassie_solver::fit_for_constraints(
			initial_cp, build_subset(), p_ortho_directions, p_params, p_is_closed,
			total_score);
	bool done = false;
	while (!done && !best.active_by_anchor.is_empty()) {
		cassie_solver::FitCandidate best_subset;
		int best_remove_index = -1;
		for (uint32_t i = 0; i < all.size(); ++i) {
			if (!all[i].active) {
				continue;
			}
			all[i].active = false;
			LocalVector<cassie_solver::ConstraintCandidate> trial = build_subset();
			cassie_solver::FitCandidate cand = cassie_solver::fit_for_constraints(
					initial_cp, trial, p_ortho_directions, p_params, p_is_closed,
					total_score);
			if (cand.energy < best_subset.energy) {
				best_subset = cand;
				best_remove_index = int(i);
			}
			all[i].active = true;
		}
		if (best_remove_index >= 0 && best_subset.energy < best.energy) {
			best = best_subset;
			all[best_remove_index].active = false;
		} else {
			done = true;
		}
	}

	// NaN guard.
	for (int i = 0; i < best.control_points.size(); ++i) {
		const Vector3 v = best.control_points[i];
		if (Math::is_nan(v.x) || Math::is_inf(v.x)) {
			return result; // Original curve returned.
		}
	}

	Ref<Curve3D> beautified = cassie_solver::build_curve3d_from_control_points(best.control_points);
	result["curve"] = beautified;
	result["planar"] = best.planar;
	result["is_closed_loop"] = best.close_loop;

	TypedArray<CassieIntersectionConstraint> inters;
	TypedArray<CassieMirrorPlaneConstraint> mirrors;
	PackedInt32Array anchors;
	for (const KeyValue<int, cassie_solver::ConstraintCandidate> &kv :
			best.active_by_anchor) {
		anchors.push_back(kv.key);
		Ref<CassieIntersectionConstraint> ic = kv.value.constraint;
		Ref<CassieMirrorPlaneConstraint> mc = kv.value.constraint;
		if (ic.is_valid()) {
			ic->project_on(beautified);
			inters.push_back(ic);
		} else if (mc.is_valid()) {
			mc->project_on(beautified);
			mirrors.push_back(mc);
		}
	}
	result["intersections"] = inters;
	result["mirror_constraints"] = mirrors;

	int rejected = 0;
	for (const cassie_solver::ConstraintCandidate &c : all) {
		if (!c.active) {
			++rejected;
		}
	}
	result["rejected_count"] = rejected;

	return result;
}

void CassieConstraintSolver::_bind_methods() {
	ClassDB::bind_method(D_METHOD("solve", "curve", "constraints",
								 "ortho_directions", "params", "is_closed"),
			&CassieConstraintSolver::solve);
}

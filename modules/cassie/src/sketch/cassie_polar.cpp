/**************************************************************************/
/*  cassie_polar.cpp                                                      */
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

#include "cassie_polar.h"

#include <cmath>

namespace cassie_polar {

namespace {

// Row-major 3×3 accessor.
inline double get(const double *p_M, int p_i, int p_j) {
	return p_M[p_i * 3 + p_j];
}

inline void set(double *p_M, int p_i, int p_j, double p_v) {
	p_M[p_i * 3 + p_j] = p_v;
}

inline void mat3_mul(const double *p_A, const double *p_B, double *p_C) {
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			double s = 0.0;
			for (int k = 0; k < 3; ++k) {
				s += p_A[i * 3 + k] * p_B[k * 3 + j];
			}
			p_C[i * 3 + j] = s;
		}
	}
}

inline double mat3_det(const double *p_M) {
	return get(p_M, 0, 0) * (get(p_M, 1, 1) * get(p_M, 2, 2) - get(p_M, 1, 2) * get(p_M, 2, 1)) - get(p_M, 0, 1) * (get(p_M, 1, 0) * get(p_M, 2, 2) - get(p_M, 1, 2) * get(p_M, 2, 0)) + get(p_M, 0, 2) * (get(p_M, 1, 0) * get(p_M, 2, 1) - get(p_M, 1, 1) * get(p_M, 2, 0));
}

// Eigenvector for a given eigenvalue λ of symmetric M, computed as
// the null vector of A = M - λI. Two cases:
//
//   * **Non-degenerate eigenvalue** (1D null space, rank-2 A): the null
//     vector is the unique direction perpendicular to all rows of A.
//     Recovered via pairwise row cross products; the largest-magnitude
//     result is numerically the most reliable.
//
//   * **Degenerate eigenvalue** (2D null space, rank-1 A): all rows of
//     A are scalar multiples of a single direction `r`. Row-pair cross
//     products all give zero (parallel vectors cross to 0). The null
//     space is everything perpendicular to `r` — pick any vector in
//     that 2D subspace by crossing `r` with the canonical axis least
//     parallel to it. This is the case that the prior version
//     misfired on (the `(1,0,0)` fallback wasn't actually in the null
//     space for cross-covariances with M = (I + (1/3)J)² and
//     eigenvalues (4, 1, 1) — caught by the random-rotation Wahba
//     test that uses the 4-tangent fixture {e_x, e_y, e_z, (1,1,1)/√3}).
//
// Returns false only when A is essentially the zero matrix (λ wasn't
// actually an eigenvalue) — caller falls back to an arbitrary axis.
bool eigenvector_for(const double *p_M, double p_lambda, double *p_out) {
	double A[9];
	for (int i = 0; i < 9; ++i) {
		A[i] = p_M[i];
	}
	A[0] -= p_lambda;
	A[4] -= p_lambda;
	A[8] -= p_lambda;

	// Case 1 — try pairwise row cross products. Works for rank-2 A.
	double best[3] = { 0.0, 0.0, 0.0 };
	double best_norm2 = 0.0;
	for (int a = 0; a < 3; ++a) {
		for (int b = a + 1; b < 3; ++b) {
			const double r1[3] = { A[a * 3 + 0], A[a * 3 + 1], A[a * 3 + 2] };
			const double r2[3] = { A[b * 3 + 0], A[b * 3 + 1], A[b * 3 + 2] };
			const double c[3] = {
				r1[1] * r2[2] - r1[2] * r2[1],
				r1[2] * r2[0] - r1[0] * r2[2],
				r1[0] * r2[1] - r1[1] * r2[0]
			};
			const double n2 = c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
			if (n2 > best_norm2) {
				best_norm2 = n2;
				best[0] = c[0];
				best[1] = c[1];
				best[2] = c[2];
			}
		}
	}
	// Tolerance scales with the matrix magnitude — squared row norms
	// can reach ~tr(M)², so a relative cutoff catches both float-noise
	// and genuine rank-1 cases.
	double max_row_norm2 = 0.0;
	for (int a = 0; a < 3; ++a) {
		const double n2 = A[a * 3 + 0] * A[a * 3 + 0] + A[a * 3 + 1] * A[a * 3 + 1] + A[a * 3 + 2] * A[a * 3 + 2];
		if (n2 > max_row_norm2) {
			max_row_norm2 = n2;
		}
	}
	const double cross_floor = max_row_norm2 * max_row_norm2 * 1e-20;
	if (best_norm2 > cross_floor && best_norm2 > 1e-30) {
		const double inv = 1.0 / std::sqrt(best_norm2);
		p_out[0] = best[0] * inv;
		p_out[1] = best[1] * inv;
		p_out[2] = best[2] * inv;
		return true;
	}

	// Case 2 — rank-1 A. All rows parallel to `r`. Pick the largest-
	// magnitude row as `r`, then construct a vector perpendicular to
	// it by crossing with the canonical axis least parallel to it.
	int best_row = 0;
	double best_row_norm2 = 0.0;
	for (int a = 0; a < 3; ++a) {
		const double n2 = A[a * 3 + 0] * A[a * 3 + 0] + A[a * 3 + 1] * A[a * 3 + 1] + A[a * 3 + 2] * A[a * 3 + 2];
		if (n2 > best_row_norm2) {
			best_row_norm2 = n2;
			best_row = a;
		}
	}
	if (best_row_norm2 < 1e-30) {
		// A is essentially zero — λ wasn't actually an eigenvalue. Any
		// vector is in the null space. Return canonical axis + false.
		p_out[0] = 1.0;
		p_out[1] = 0.0;
		p_out[2] = 0.0;
		return false;
	}
	const double r[3] = { A[best_row * 3 + 0], A[best_row * 3 + 1], A[best_row * 3 + 2] };
	const double abs_x = std::fabs(r[0]);
	const double abs_y = std::fabs(r[1]);
	const double abs_z = std::fabs(r[2]);
	double axis[3] = { 0.0, 0.0, 0.0 };
	if (abs_x <= abs_y && abs_x <= abs_z) {
		axis[0] = 1.0;
	} else if (abs_y <= abs_z) {
		axis[1] = 1.0;
	} else {
		axis[2] = 1.0;
	}
	const double v[3] = {
		r[1] * axis[2] - r[2] * axis[1],
		r[2] * axis[0] - r[0] * axis[2],
		r[0] * axis[1] - r[1] * axis[0]
	};
	const double vn2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	if (vn2 < 1e-30) {
		p_out[0] = 1.0;
		p_out[1] = 0.0;
		p_out[2] = 0.0;
		return false;
	}
	const double inv = 1.0 / std::sqrt(vn2);
	p_out[0] = v[0] * inv;
	p_out[1] = v[1] * inv;
	p_out[2] = v[2] * inv;
	return true;
}

} // namespace

void eigendecompose_3x3_symmetric(const double *p_M, double *r_lambda,
		double *r_V) {
	// Smith 1961 / Connelly 2008 closed-form eigenvalues of a 3×3
	// symmetric matrix via the cubic characteristic polynomial. p_M is
	// row-major symmetric.
	const double a = get(p_M, 0, 0);
	const double b = get(p_M, 1, 1);
	const double c = get(p_M, 2, 2);
	const double d = get(p_M, 0, 1);
	const double e = get(p_M, 1, 2);
	const double f = get(p_M, 0, 2);

	const double p1 = d * d + f * f + e * e;
	if (p1 < 1e-30) {
		// Diagonal — eigenvalues are diagonal entries; sort descending.
		double tmp_lam[3] = { a, b, c };
		int idx[3] = { 0, 1, 2 };
		for (int i = 0; i < 2; ++i) {
			for (int j = i + 1; j < 3; ++j) {
				if (tmp_lam[idx[j]] > tmp_lam[idx[i]]) {
					const int t = idx[i];
					idx[i] = idx[j];
					idx[j] = t;
				}
			}
		}
		for (int i = 0; i < 3; ++i) {
			r_lambda[i] = tmp_lam[idx[i]];
			for (int j = 0; j < 3; ++j) {
				set(r_V, j, i, j == idx[i] ? 1.0 : 0.0);
			}
		}
		return;
	}

	const double q = (a + b + c) / 3.0;
	const double p2 = (a - q) * (a - q) + (b - q) * (b - q) + (c - q) * (c - q) + 2.0 * p1;
	const double p = std::sqrt(p2 / 6.0);
	// B = (1/p) (M - q I)
	double B[9];
	for (int i = 0; i < 9; ++i) {
		B[i] = p_M[i];
	}
	B[0] -= q;
	B[4] -= q;
	B[8] -= q;
	for (int i = 0; i < 9; ++i) {
		B[i] /= p;
	}
	double r = 0.5 * mat3_det(B);
	// Clamp r to [-1, 1] to guard against floating-point drift.
	if (r < -1.0) {
		r = -1.0;
	}
	if (r > 1.0) {
		r = 1.0;
	}
	const double phi = std::acos(r) / 3.0;
	const double two_pi_over_3 = 2.0943951023931953; // 2π/3 high-precision constant
	// Descending order: eig1 is the largest.
	const double eig1 = q + 2.0 * p * std::cos(phi);
	const double eig3 = q + 2.0 * p * std::cos(phi + two_pi_over_3);
	const double eig2 = 3.0 * q - eig1 - eig3;
	r_lambda[0] = eig1;
	r_lambda[1] = eig2;
	r_lambda[2] = eig3;

	// Recover eigenvectors via null-space cross products.
	double v0[3], v1[3], v2[3];
	eigenvector_for(p_M, r_lambda[0], v0);
	eigenvector_for(p_M, r_lambda[2], v2);
	// v1 = v0 × v2 (orthonormal complement; cheaper than computing from M).
	v1[0] = v0[1] * v2[2] - v0[2] * v2[1];
	v1[1] = v0[2] * v2[0] - v0[0] * v2[2];
	v1[2] = v0[0] * v2[1] - v0[1] * v2[0];
	const double v1_n2 = v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2];
	if (v1_n2 > 1e-30) {
		const double inv = 1.0 / std::sqrt(v1_n2);
		v1[0] *= inv;
		v1[1] *= inv;
		v1[2] *= inv;
	} else {
		// v0 and v2 colinear (degenerate; usually means a repeated
		// eigenvalue). Compute v1 from M - eig2 I directly.
		eigenvector_for(p_M, r_lambda[1], v1);
	}
	// Pack V column-major into row-major storage: V[i][j] = v_j[i].
	for (int i = 0; i < 3; ++i) {
		set(r_V, i, 0, v0[i]);
		set(r_V, i, 1, v1[i]);
		set(r_V, i, 2, v2[i]);
	}
}

Basis wahba_align(const PackedVector3Array &p_projection_tangents,
		const PackedVector3Array &p_rest_tangents) {
	const int n = MIN(p_projection_tangents.size(), p_rest_tangents.size());
	if (n < 3) {
		return Basis();
	}

	// Cross-covariance H = sum q_i p_i^T (3×3, generally non-symmetric).
	// Note the q·p^T order — NOT p·q^T. For the polar decomp formulation
	// R = H · (H^T H)^{-1/2}, the correct cross-covariance puts the
	// target (rest) tangents on the LEFT outer-product side. Derivation:
	// with q_i = R · p_i, H = Σ q_i p_i^T = R · (Σ p_i p_i^T) = R · A.
	// Then H^T H = A² (symmetric PSD), M^{-1/2} = A^{-1}, and
	// R_polar = H · A^{-1} = R · A · A^{-1} = R. The wrong convention
	// (p · q^T) returns R^T — caught by the 4-tangent random-rotation
	// fixture whose A = I + (1/3)J is non-identity.
	double H[9] = { 0 };
	for (int i = 0; i < n; ++i) {
		const Vector3 p = p_projection_tangents[i];
		const Vector3 q = p_rest_tangents[i];
		const double px = double(p.x), py = double(p.y), pz = double(p.z);
		const double qx = double(q.x), qy = double(q.y), qz = double(q.z);
		H[0] += qx * px;
		H[1] += qx * py;
		H[2] += qx * pz;
		H[3] += qy * px;
		H[4] += qy * py;
		H[5] += qy * pz;
		H[6] += qz * px;
		H[7] += qz * py;
		H[8] += qz * pz;
	}

	// Two Lean-pinned facts from modules/cassie/lean/CassieAvbd/PolarDecomp.lean
	// shape the absence of a reflection branch here:
	//
	// 1. V-column flip is a no-op on V·diag(d)·V^T. The k=m contribution
	//    to entry (i,j) is V_{im}·d_m·V_{jm}; flipping column m sends
	//    V_{im}→-V_{im}, V_{jm}→-V_{jm}, and the product (-V_{im})·(-V_{jm})
	//    equals the original. So the polar formulation R = H·M^{-1/2}
	//    cannot fix det(R) < 0 via V manipulation — that requires the
	//    SVD U·V^T formulation where the sign sits between two distinct
	//    matrices.
	//
	// 2. For Wahba ground-truth inputs — tangent pairs (p_i, R·p_i) for
	//    some proper rotation R — det(H) ≥ 0. Proof: H = R·(Σ p_i p_i^T),
	//    R has det 1, and Σ p_i p_i^T is symmetric PSD so its det is ≥ 0.
	//    Multiplicativity gives det(H) ≥ 0.
	//
	// Together (1) + (2) mean: the prior "if det(R) < 0, flip V[:,2] and
	// recompute" branch did nothing on real inputs (no-op) and would do
	// nothing even if triggered (flip is invariant). Removed entirely.
	// On adversarial inputs with det(H) < 0 the function returns an
	// improper polar factor — the caller is responsible for not feeding
	// reflection-tagged tangent pairs.

	// M = H^T H (symmetric PSD).
	double M[9];
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			double s = 0.0;
			for (int k = 0; k < 3; ++k) {
				s += H[k * 3 + i] * H[k * 3 + j];
			}
			M[i * 3 + j] = s;
		}
	}

	// Closed-form eigendecomposition of M.
	double lambda[3];
	double V[9];
	eigendecompose_3x3_symmetric(M, lambda, V);

	// M^{-1/2} = V · diag(1/sqrt(λ_i)) · V^T. Clamp eigenvalues away
	// from zero — when H is singular (rank-deficient inputs) one
	// eigenvalue degenerates. The clamp keeps the polar identity
	// well-defined.
	double inv_sqrt[3];
	const double kFloor = 1e-12;
	for (int i = 0; i < 3; ++i) {
		const double l = lambda[i] > kFloor ? lambda[i] : kFloor;
		inv_sqrt[i] = 1.0 / std::sqrt(l);
	}
	double VinvSqrt[9];
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			VinvSqrt[i * 3 + j] = get(V, i, j) * inv_sqrt[j];
		}
	}
	// M^{-1/2} = VinvSqrt * V^T. The Lean fact (PolarDecomp.lean) that
	// V·diag·V^T is symmetric for any diagonal d enables a 6-entry
	// symmetry exploit here, but the unoptimized full 9-entry loop
	// stays — symmetry-exploit hand-off currently lives in the GPU
	// dispatch (Spmv/Saxpby compose the same symmetric product more
	// efficiently than a CPU two-write trick).
	double Minvhalf[9];
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			double s = 0.0;
			for (int k = 0; k < 3; ++k) {
				s += VinvSqrt[i * 3 + k] * get(V, j, k);
			}
			Minvhalf[i * 3 + j] = s;
		}
	}

	// R = H * M^{-1/2}. Per the Lean fact above (V-column flip is a
	// no-op on Minvhalf and Wahba ground-truth det(H) ≥ 0), no
	// reflection branch is needed for in-distribution inputs.
	double R[9];
	mat3_mul(H, Minvhalf, R);

	return Basis(
			Vector3(real_t(R[0]), real_t(R[3]), real_t(R[6])),
			Vector3(real_t(R[1]), real_t(R[4]), real_t(R[7])),
			Vector3(real_t(R[2]), real_t(R[5]), real_t(R[8])));
}

void polar_decompose_3x3_safe(const double *p_M, double *r_R) {
	// 1. H = M^T · M (symmetric PSD).
	double H[9];
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			double s = 0.0;
			for (int k = 0; k < 3; ++k) {
				s += p_M[k * 3 + i] * p_M[k * 3 + j];
			}
			H[i * 3 + j] = s;
		}
	}

	// 2. Eigendecompose. Reuses the same Smith 1961 closed form
	// `wahba_align` uses.
	double lambda[3];
	double V[9];
	eigendecompose_3x3_symmetric(H, lambda, V);

	// 3. Clamp eigenvalues — DDM-blended M can be rank-deficient
	// (pinned vertex, opposing weights flattening geometry). 1e-12 floor
	// keeps 1/√λ finite. The clamp matches `wahba_align`'s existing
	// kFloor at line 326.
	double d[3];
	const double kFloor = 1e-12;
	for (int i = 0; i < 3; ++i) {
		const double l = lambda[i] > kFloor ? lambda[i] : kFloor;
		d[i] = 1.0 / std::sqrt(l);
	}

	// 4. Build M^{-1/2} = V · diag(d) · V^T, then R_try = M · M^{-1/2}.
	auto compute_R = [&](const double *d_signed) {
		double VinvSqrt[9];
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				VinvSqrt[i * 3 + j] = get(V, i, j) * d_signed[j];
			}
		}
		double Minvhalf[9];
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				double s = 0.0;
				for (int k = 0; k < 3; ++k) {
					s += VinvSqrt[i * 3 + k] * get(V, j, k);
				}
				Minvhalf[i * 3 + j] = s;
			}
		}
		mat3_mul(p_M, Minvhalf, r_R);
	};
	compute_R(d);

	// 5. Reflection check. If det(R) < 0 the polar decomp produced an
	// improper rotation. Fix by flipping the inverse-sqrt of the smallest
	// eigenvalue (d_2 since eigendecompose returns descending order so
	// lambda[2] is smallest). This is the SVD-equivalent fix adapted to
	// polar: V-column flips on V·diag·V^T are no-ops (the sign cancels
	// across V_ik and V_jk in the symmetric product), but d-flips are not
	// (the sign appears once, in the middle factor).
	const double det_R = mat3_det(r_R);
	if (det_R < 0.0) {
		double d_flipped[3] = { d[0], d[1], -d[2] };
		compute_R(d_flipped);
	}
}

} // namespace cassie_polar

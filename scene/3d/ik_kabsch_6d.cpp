/**************************************************************************/
/*  ik_kabsch_6d.cpp                                                      */
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

#include "ik_kabsch_6d.h"

#include "core/math/math_funcs.h"

void IKKabsch6D::set_cross(double r_u[3][3], int p_j, int p_j1, int p_j2) {
	const double x = r_u[1][p_j1] * r_u[2][p_j2] - r_u[2][p_j1] * r_u[1][p_j2];
	const double y = r_u[2][p_j1] * r_u[0][p_j2] - r_u[0][p_j1] * r_u[2][p_j2];
	const double z = r_u[0][p_j1] * r_u[1][p_j2] - r_u[1][p_j1] * r_u[0][p_j2];
	const double n = Math::sqrt(x * x + y * y + z * z) + 1e-300;
	r_u[0][p_j] = x / n;
	r_u[1][p_j] = y / n;
	r_u[2][p_j] = z / n;
}

void IKKabsch6D::svd3(const double p_m[3][3], double r_u[3][3], double r_v[3][3], double r_sigma[3]) {
	double a[3][3];
	double v[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			a[i][j] = p_m[i][j];
		}
	}
	for (int sweep = 0; sweep < 40; sweep++) {
		bool converged = true;
		for (int p = 0; p < 3; p++) {
			for (int q = p + 1; q < 3; q++) {
				const double alpha = a[0][p] * a[0][p] + a[1][p] * a[1][p] + a[2][p] * a[2][p];
				const double beta = a[0][q] * a[0][q] + a[1][q] * a[1][q] + a[2][q] * a[2][q];
				const double gamma = a[0][p] * a[0][q] + a[1][p] * a[1][q] + a[2][p] * a[2][q];
				if (Math::abs(gamma) > 1e-17 * Math::sqrt(alpha * beta + 1e-300)) {
					converged = false;
					const double zeta = (beta - alpha) / (2.0 * gamma);
					const double t = (zeta >= 0.0 ? 1.0 : -1.0) / (Math::abs(zeta) + Math::sqrt(1.0 + zeta * zeta));
					const double c = 1.0 / Math::sqrt(1.0 + t * t);
					const double s = c * t;
					for (int k = 0; k < 3; k++) {
						const double ap = a[k][p];
						const double aq = a[k][q];
						a[k][p] = c * ap - s * aq;
						a[k][q] = s * ap + c * aq;
						const double vp = v[k][p];
						const double vq = v[k][q];
						v[k][p] = c * vp - s * vq;
						v[k][q] = s * vp + c * vq;
					}
				}
			}
		}
		if (converged) {
			break;
		}
	}
	double sig[3];
	for (int j = 0; j < 3; j++) {
		sig[j] = Math::sqrt(a[0][j] * a[0][j] + a[1][j] * a[1][j] + a[2][j] * a[2][j]);
	}
	int order[3] = { 0, 1, 2 };
	for (int i = 0; i < 3; i++) {
		for (int j = i + 1; j < 3; j++) {
			if (sig[order[j]] > sig[order[i]]) {
				const int tmp = order[i];
				order[i] = order[j];
				order[j] = tmp;
			}
		}
	}
	const double eps = 1e-12 * (sig[order[0]] + 1e-300);
	for (int j = 0; j < 3; j++) {
		const int o = order[j];
		r_sigma[j] = sig[o];
		for (int k = 0; k < 3; k++) {
			r_v[k][j] = v[k][o];
			r_u[k][j] = (sig[o] > eps) ? (a[k][o] / sig[o]) : 0.0;
		}
	}
	// Complete rank-deficient U columns into a right-handed orthonormal frame. sigma is
	// descending, so deficiency runs from the last column. Rank 1 (a single correspondence)
	// leaves the swing axis free -> any perpendicular completion is a valid rotation.
	if (r_sigma[0] <= eps) {
		for (int i = 0; i < 3; i++) {
			for (int j = 0; j < 3; j++) {
				r_u[i][j] = (i == j) ? 1.0 : 0.0;
			}
		}
	} else if (r_sigma[1] <= eps) {
		// Rank 1: the swing axis is pinned (u0 <- v0), the perpendicular (twist) plane is free. Any
		// perpendicular is a legal completion, so pick the CANONICAL one -- minimal twist -- by
		// carrying V's free axes v1,v2 through the shortest arc v0 -> u0. Then U = [u0, R0 v1, R0 v2]
		// and R = U V^T = R0, the swing-only (shortest-arc) rotation: deterministic and continuous,
		// instead of get_any_perpendicular()'s arbitrary, flip-prone pick. (Verified in
		// ../swing-twist-kusudama/KusKabsch.lean.)
		const Vector3 u0(r_u[0][0], r_u[1][0], r_u[2][0]);
		const Vector3 v0(r_v[0][0], r_v[1][0], r_v[2][0]);
		const Vector3 v1(r_v[0][1], r_v[1][1], r_v[2][1]);
		const Vector3 v2(r_v[0][2], r_v[1][2], r_v[2][2]);
		Vector3 c1;
		Vector3 c2;
		const Vector3 axis = v0.cross(u0);
		const double s = axis.length();
		const double c = v0.dot(u0);
		if (s < 1e-9) {
			if (c > 0.0) {
				c1 = v1; // v0 == u0: shortest arc is identity, carry V's free axes unchanged.
				c2 = v2;
			} else {
				c1 = u0.get_any_perpendicular().normalized(); // antipodal: 180deg, twist truly free.
				c2 = u0.cross(c1).normalized();
			}
		} else {
			const Vector3 k = axis / s; // unit rotation axis of the shortest arc v0 -> u0
			const double ang = Math::atan2(s, c);
			const double cs = Math::cos(ang);
			const double sn = Math::sin(ang);
			c1 = v1 * cs + k.cross(v1) * sn + k * (k.dot(v1) * (1.0 - cs)); // Rodrigues
			c2 = v2 * cs + k.cross(v2) * sn + k * (k.dot(v2) * (1.0 - cs));
		}
		r_u[0][1] = c1.x;
		r_u[1][1] = c1.y;
		r_u[2][1] = c1.z;
		r_u[0][2] = c2.x;
		r_u[1][2] = c2.y;
		r_u[2][2] = c2.z;
	} else if (r_sigma[2] <= eps) {
		set_cross(r_u, 2, 0, 1);
	}
}

Basis IKKabsch6D::kabsch(const Vector3 *p_rest, const Vector3 *p_tgt, int p_count) {
	// Cross-covariance M = sum tgt_i * rest_i^T (m[row=tgt][col=rest]).
	double m[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
	for (int i = 0; i < p_count; i++) {
		const Vector3 &r = p_rest[i];
		const Vector3 &t = p_tgt[i];
		m[0][0] += t.x * r.x;
		m[0][1] += t.x * r.y;
		m[0][2] += t.x * r.z;
		m[1][0] += t.y * r.x;
		m[1][1] += t.y * r.y;
		m[1][2] += t.y * r.z;
		m[2][0] += t.z * r.x;
		m[2][1] += t.z * r.y;
		m[2][2] += t.z * r.z;
	}
	double u[3][3];
	double v[3][3];
	double sigma[3];
	svd3(m, u, v, sigma);
	// det(U V^T); flip the smallest singular direction if it is a reflection.
	double uvt[3][3];
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			uvt[i][j] = u[i][0] * v[j][0] + u[i][1] * v[j][1] + u[i][2] * v[j][2];
		}
	}
	const double det = uvt[0][0] * (uvt[1][1] * uvt[2][2] - uvt[1][2] * uvt[2][1]) - uvt[0][1] * (uvt[1][0] * uvt[2][2] - uvt[1][2] * uvt[2][0]) + uvt[0][2] * (uvt[1][0] * uvt[2][1] - uvt[1][1] * uvt[2][0]);
	const double d = (det >= 0.0) ? 1.0 : -1.0;
	// R = U * diag(1,1,d) * V^T; columns ax/ay/az.
	const Vector3 ax(u[0][0] * v[0][0] + u[0][1] * v[0][1] + d * u[0][2] * v[0][2],
			u[1][0] * v[0][0] + u[1][1] * v[0][1] + d * u[1][2] * v[0][2],
			u[2][0] * v[0][0] + u[2][1] * v[0][1] + d * u[2][2] * v[0][2]);
	const Vector3 ay(u[0][0] * v[1][0] + u[0][1] * v[1][1] + d * u[0][2] * v[1][2],
			u[1][0] * v[1][0] + u[1][1] * v[1][1] + d * u[1][2] * v[1][2],
			u[2][0] * v[1][0] + u[2][1] * v[1][1] + d * u[2][2] * v[1][2]);
	const Vector3 az(u[0][0] * v[2][0] + u[0][1] * v[2][1] + d * u[0][2] * v[2][2],
			u[1][0] * v[2][0] + u[1][1] * v[2][1] + d * u[1][2] * v[2][2],
			u[2][0] * v[2][0] + u[2][1] * v[2][1] + d * u[2][2] * v[2][2]);
	return Basis(ax, ay, az);
}

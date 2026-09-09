/**************************************************************************/
/*  planarity_constraint.cpp                                              */
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

#include "planarity_constraint.h"

#include "cassie_eigen.h"

#include "core/math/math_funcs.h"

namespace cassie_solver {

PlanarityConstraint::PlanarityConstraint(const Vector3 &p_normal,
		const Vector<Vector3> &p_initial_control_points) {
	N = p_initial_control_points.size();
	normal[0] = double(p_normal.x);
	normal[1] = double(p_normal.y);
	normal[2] = double(p_normal.z);

	if (N < 2) {
		return;
	}
	tangent_norms2.resize(N - 1);
	T0.resize(N - 1);
	for (int i = 0; i < N - 1; ++i) {
		const Vector3 t = p_initial_control_points[i + 1] - p_initial_control_points[i];
		const Vector3 d = p_initial_control_points[i] - p_initial_control_points[i + 1];
		tangent_norms2.write[i] = double(d.dot(d));
		T0.write[i] = t;
	}
}

void PlanarityConstraint::get_blocks(DenseMatrix &r_A, DenseVector &r_b) const {
	r_A.zero_resize(3 * N, 3 * N);
	r_b.zero_resize(3 * N);
	if (N < 2) {
		return;
	}

	const double eps = 1e-8;
	const double factor = 2.0 / double(N - 1);

	// 3 × 3 outer product NN[i][j] = normal[i] * normal[j].
	double NN[9];
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			NN[i * 3 + j] = normal[i] * normal[j];
		}
	}

	for (int k = 0; k < N; ++k) {
		double b_k[3] = { 0.0, 0.0, 0.0 };

		if (k < N - 1 && tangent_norms2[k] > eps) {
			const double t0[3] = { double(T0[k].x), double(T0[k].y), double(T0[k].z) };
			const double inv = 1.0 / tangent_norms2[k];
			for (int i = 0; i < 3; ++i) {
				double s = 0.0;
				for (int j = 0; j < 3; ++j) {
					s += NN[i * 3 + j] * t0[j];
				}
				b_k[i] += factor * s * inv;
			}
		}
		if (k > 0 && tangent_norms2[k - 1] > eps) {
			const double t0[3] = { double(T0[k - 1].x), double(T0[k - 1].y), double(T0[k - 1].z) };
			const double inv = 1.0 / tangent_norms2[k - 1];
			for (int i = 0; i < 3; ++i) {
				double s = 0.0;
				for (int j = 0; j < 3; ++j) {
					s += NN[i * 3 + j] * t0[j];
				}
				b_k[i] -= factor * s * inv;
			}
		}

		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				if (k < N - 1) {
					const double inv = tangent_norms2[k] > eps ? 1.0 / tangent_norms2[k] : 0.0;
					r_A(3 * k + i, 3 * k + j) += factor * NN[i * 3 + j] * inv;
					r_A(3 * k + i, 3 * (k + 1) + j) -= factor * NN[i * 3 + j] * inv;
				}
				if (k > 0) {
					const double inv = tangent_norms2[k - 1] > eps ? 1.0 / tangent_norms2[k - 1] : 0.0;
					r_A(3 * k + i, 3 * k + j) += factor * NN[i * 3 + j] * inv;
					r_A(3 * k + i, 3 * (k - 1) + j) -= factor * NN[i * 3 + j] * inv;
				}
			}
			r_b[3 * k + i] = b_k[i];
		}
	}
}

Plane cassie_fit_plane(const Vector<Vector3> &p_points, double &r_avg_distance) {
	r_avg_distance = 0.0;
	const int n = p_points.size();
	if (n < 3) {
		return Plane(Vector3(0, 1, 0), 0.0);
	}

	// Centroid.
	Vector3 centroid;
	for (int i = 0; i < n; ++i) {
		centroid += p_points[i];
	}
	centroid /= real_t(n);

	// 3x3 symmetric covariance (row-major). Track 5 Phase D — Eigen-free
	// via cassie_eigen::smallest_eigenvector_3x3.
	double cov[9] = { 0 };
	for (int i = 0; i < n; ++i) {
		const Vector3 d = p_points[i] - centroid;
		const double dx = double(d.x), dy = double(d.y), dz = double(d.z);
		cov[0] += dx * dx;
		cov[1] += dx * dy;
		cov[2] += dx * dz;
		cov[4] += dy * dy;
		cov[5] += dy * dz;
		cov[8] += dz * dz;
	}
	cov[3] = cov[1];
	cov[6] = cov[2];
	cov[7] = cov[5];

	double n_xyz[3];
	cassie_eigen::smallest_eigenvector_3x3(cov, n_xyz);
	const Vector3 normal{ real_t(n_xyz[0]), real_t(n_xyz[1]), real_t(n_xyz[2]) };

	const Plane plane(normal.normalized(), centroid);

	// Average absolute distance to plane.
	double sum = 0.0;
	for (int i = 0; i < n; ++i) {
		sum += double(Math::abs(plane.distance_to(p_points[i])));
	}
	r_avg_distance = sum / double(n);
	return plane;
}

} // namespace cassie_solver

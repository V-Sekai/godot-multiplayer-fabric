/**************************************************************************/
/*  tangent_constraint.cpp                                                */
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

#include "tangent_constraint.h"

namespace cassie_solver {

TangentConstraint::TangentConstraint(int p_ctrl_pt_idx, const Vector3 &p_t_target,
		const Vector<Vector3> &p_initial_control_points) {
	N = p_initial_control_points.size();
	if (p_ctrl_pt_idx < N - 1) {
		idx_a = p_ctrl_pt_idx;
		idx_b = p_ctrl_pt_idx + 1;
	} else {
		idx_a = p_ctrl_pt_idx - 1;
		idx_b = p_ctrl_pt_idx;
	}
	T0 = p_initial_control_points[idx_b] - p_initial_control_points[idx_a];

	// Skew-symmetric cross-product matrix of t_target:
	//   [  0   -tz   ty ]
	//   [  tz   0   -tx ]
	//   [ -ty   tx   0  ]
	const double tx = double(p_t_target.x);
	const double ty = double(p_t_target.y);
	const double tz = double(p_t_target.z);
	t_cross[0 * 3 + 0] = 0.0;
	t_cross[0 * 3 + 1] = -tz;
	t_cross[0 * 3 + 2] = ty;
	t_cross[1 * 3 + 0] = tz;
	t_cross[1 * 3 + 1] = 0.0;
	t_cross[1 * 3 + 2] = -tx;
	t_cross[2 * 3 + 0] = -ty;
	t_cross[2 * 3 + 1] = tx;
	t_cross[2 * 3 + 2] = 0.0;
}

void TangentConstraint::get_blocks(DenseMatrix &r_A, DenseVector &r_b) const {
	r_A.zero_resize(3 * N, 3 * N);
	r_b.zero_resize(3 * N);

	const double eps = 1e-8;
	const double t0_norm2 = double(T0.dot(T0));
	const double factor = 2.0 / (t0_norm2 > eps ? t0_norm2 : eps);

	double b_i[3] = { 0.0, 0.0, 0.0 };
	const double t0[3] = { double(T0.x), double(T0.y), double(T0.z) };
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			b_i[i] -= factor * t_cross[i * 3 + j] * t0[j];
		}
	}

	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			const double v = factor * t_cross[i * 3 + j];
			r_A(3 * idx_a + i, 3 * idx_a + j) = -v;
			r_A(3 * idx_a + i, 3 * idx_b + j) = v;
			r_A(3 * idx_b + i, 3 * idx_b + j) = -v;
			r_A(3 * idx_b + i, 3 * idx_a + j) = v;
		}
		r_b[3 * idx_a + i] = b_i[i];
		r_b[3 * idx_b + i] = -b_i[i];
	}
}

} // namespace cassie_solver

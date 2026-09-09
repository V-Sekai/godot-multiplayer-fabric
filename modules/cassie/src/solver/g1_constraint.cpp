/**************************************************************************/
/*  g1_constraint.cpp                                                     */
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

#include "g1_constraint.h"

namespace cassie_solver {

G1Constraint::G1Constraint(const Vector<Vector3> &p_control_points) {
	N = p_control_points.size();
	if (N < 4) {
		return;
	}
	Nb = N / 3;
	if (Nb < 2) {
		return;
	}

	left_tangent_norms.resize(Nb - 1);
	right_tangent_norms.resize(Nb - 1);
	is_g1.resize(Nb - 1);

	const double eps = 1e-5;
	for (int i = 0; i < Nb - 1; ++i) {
		const int anchor_idx = 3 * (i + 1);
		const Vector3 to_left = p_control_points[anchor_idx] - p_control_points[anchor_idx - 1];
		const Vector3 to_right = p_control_points[anchor_idx] - p_control_points[anchor_idx + 1];
		left_tangent_norms.write[i] = double(to_left.length());
		right_tangent_norms.write[i] = double(to_right.length());

		// A joint is currently G1 iff the cross product magnitude is below eps.
		const double cross_mag = double(to_left.cross(to_right).length());
		const bool g1 = cross_mag < eps;
		is_g1.write[i] = g1;
		if (g1) {
			++n_joints;
		}
	}
}

void G1Constraint::get_blocks(DenseMatrix &r_C, DenseVector &r_b) const {
	const int n_lines = 3 * n_joints;
	r_C.zero_resize(n_lines, 3 * N);
	r_b.zero_resize(n_lines);
	if (n_joints == 0) {
		return;
	}

	const double eps = 1e-8;
	int i_c = 0;
	for (int i = 0; i < Nb - 1; ++i) {
		if (!is_g1[i]) {
			continue;
		}
		const int anchor_idx = 3 * (i + 1);
		const double l = left_tangent_norms[i];
		const double r = right_tangent_norms[i];
		if (l <= eps || r <= eps) {
			++i_c;
			continue;
		}
		const double center = (1.0 / l) + (1.0 / r);
		for (int k = 0; k < 3; ++k) {
			r_C(3 * i_c + k, 3 * anchor_idx + k) = center;
			r_C(3 * i_c + k, 3 * (anchor_idx - 1) + k) = -1.0 / l;
			r_C(3 * i_c + k, 3 * (anchor_idx + 1) + k) = -1.0 / r;
		}
		++i_c;
	}
}

} // namespace cassie_solver

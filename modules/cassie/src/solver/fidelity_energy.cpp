/**************************************************************************/
/*  fidelity_energy.cpp                                                   */
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

#include "fidelity_energy.h"

namespace cassie_solver {

FidelityEnergy::FidelityEnergy(const Vector<Vector3> &p_initial_control_points,
		double p_w_p, double p_w_t, double p_displacement_normalizer) {
	N = p_initial_control_points.size();
	if (N < 2) {
		return;
	}
	p_factor = p_w_p / double(N);
	t_factor = p_w_t / double(N - 1);

	tangent_norms2.resize(N - 1);
	for (int i = 0; i < N - 1; ++i) {
		const Vector3 d = p_initial_control_points[i] - p_initial_control_points[i + 1];
		tangent_norms2.write[i] = double(d.length_squared());
	}
	const double norm = p_displacement_normalizer;
	if (norm > 0.0) {
		p_factor /= norm * norm;
	}
}

void FidelityEnergy::get_block(DenseMatrix &r_A) const {
	r_A.zero_resize(3 * N, 3 * N);
	if (N <= 0) {
		return;
	}

	const double eps = 1e-8;
	for (int i = 0; i < N; ++i) {
		for (int k = 0; k < 3; ++k) {
			const int row = 3 * i + k;
			r_A(row, row) += 2.0 * p_factor;
			if (i > 0 && tangent_norms2[i - 1] > eps) {
				const double f = 2.0 * t_factor / tangent_norms2[i - 1];
				r_A(row, row) += f;
				r_A(row, 3 * (i - 1) + k) -= f;
			}
			if (i < N - 1 && tangent_norms2[i] > eps) {
				const double f = 2.0 * t_factor / tangent_norms2[i];
				r_A(row, row) += f;
				r_A(row, 3 * (i + 1) + k) -= f;
			}
		}
	}
}

double FidelityEnergy::compute(const Vector<Vector3> &p_displacements) const {
	if (N <= 0 || p_displacements.size() != N) {
		return 0.0;
	}
	const double eps = 1e-8;
	double Ep = 0.0;
	double Et = 0.0;
	for (int i = 0; i < N; ++i) {
		const Vector3 &x = p_displacements[i];
		Ep += double(x.dot(x));
		if (i < N - 1 && tangent_norms2[i] > eps) {
			const Vector3 dx = p_displacements[i] - p_displacements[i + 1];
			Et += double(dx.dot(dx)) / tangent_norms2[i];
		}
	}
	return p_factor * Ep + t_factor * Et;
}

} // namespace cassie_solver

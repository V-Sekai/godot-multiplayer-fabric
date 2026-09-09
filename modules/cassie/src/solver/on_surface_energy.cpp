/**************************************************************************/
/*  on_surface_energy.cpp                                                 */
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

#include "on_surface_energy.h"

#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

namespace cassie_solver {

OnSurfaceEnergy::OnSurfaceEnergy(const Vector<Vector3> &p_initial_control_points,
		const Callable &p_project_callback, double p_weight) {
	N = p_initial_control_points.size();
	w = p_weight;
	initial_cp = p_initial_control_points;
	targets.resize(N);
	active.resize(N);
	active_count = 0;
	for (int i = 0; i < N; ++i) {
		targets.write[i] = p_initial_control_points[i];
		active.write[i] = false;
	}
	if (!p_project_callback.is_valid() || N == 0 || w <= 0.0) {
		return;
	}
	for (int i = 0; i < N; ++i) {
		Variant result;
		Callable::CallError err;
		const Variant arg = p_initial_control_points[i];
		const Variant *args[1] = { &arg };
		p_project_callback.callp(args, 1, result, err);
		if (err.error != Callable::CallError::CALL_OK) {
			continue;
		}
		if (result.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary d = result;
		if (!bool(d.get("on_surface", false))) {
			continue;
		}
		targets.write[i] = d.get("projected", p_initial_control_points[i]);
		active.write[i] = true;
		++active_count;
	}
}

void OnSurfaceEnergy::get_blocks(DenseMatrix &r_A, DenseVector &r_b) const {
	r_A.zero_resize(3 * N, 3 * N);
	r_b.zero_resize(3 * N);
	if (w <= 0.0 || N == 0 || active_count == 0) {
		return;
	}
	// The solver works in displacement coordinates (x = p - p0). The energy
	// expands to w (x + (p0 - q))^T (x + (p0 - q)) — Hessian 2w I, gradient
	// -b = 2w (p0 - q), so b = 2w (q - p0). PlanarityConstraint follows the
	// same convention.
	const double two_w = 2.0 * w;
	for (int i = 0; i < N; ++i) {
		if (!active[i]) {
			continue;
		}
		const int base = 3 * i;
		r_A(base + 0, base + 0) += two_w;
		r_A(base + 1, base + 1) += two_w;
		r_A(base + 2, base + 2) += two_w;
		r_b[base + 0] += two_w * (double(targets[i].x) - double(initial_cp[i].x));
		r_b[base + 1] += two_w * (double(targets[i].y) - double(initial_cp[i].y));
		r_b[base + 2] += two_w * (double(targets[i].z) - double(initial_cp[i].z));
	}
}

} // namespace cassie_solver

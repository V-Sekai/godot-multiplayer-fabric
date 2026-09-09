/**************************************************************************/
/*  curve_newton_dispatch.cpp                                             */
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

// Slang-emitted Newton reparameterize + dispatch wrapper.
//
// Source-of-truth: `CassieAvbd.CurveNewton` Lean module.

#include "curve_newton_dispatch.h"

#include "../../../thirdparty/avbd/curve_newton.cpu.cpp"

namespace cassie_slang_dispatch {

void curve_newton_reparameterize(
		const float p_a[3], const float p_b[3],
		const float p_c[3], const float p_d[3],
		uint32_t p_count,
		const float *p_in_points_xyz,
		const float *p_in_u,
		float *r_u_out) {
	using namespace cassie_slang_curve_newton;

	if (p_count == 0 || p_in_points_xyz == nullptr || p_in_u == nullptr ||
			r_u_out == nullptr) {
		return;
	}

	// Pack input points into the Slang float3 layout.
	Vector<float, 3> *in_packed = new Vector<float, 3>[p_count];
	for (uint32_t i = 0; i < p_count; ++i) {
		in_packed[i] = Vector<float, 3>(
				p_in_points_xyz[i * 3 + 0],
				p_in_points_xyz[i * 3 + 1],
				p_in_points_xyz[i * 3 + 2]);
	}

	NewtonParams_0 params;
	params.a_0 = Vector<float, 3>(p_a[0], p_a[1], p_a[2]);
	params.b_0 = Vector<float, 3>(p_b[0], p_b[1], p_b[2]);
	params.c_0 = Vector<float, 3>(p_c[0], p_c[1], p_c[2]);
	params.d_0 = Vector<float, 3>(p_d[0], p_d[1], p_d[2]);
	params.count_0 = p_count;

	GlobalParams_0 g;
	g.params_0 = &params;
	g.in_points_0.data = in_packed;
	g.in_points_0.count = p_count;
	g.in_u_0.data = const_cast<float *>(p_in_u);
	g.in_u_0.count = p_count;
	g.out_u_0.data = r_u_out;
	g.out_u_0.count = p_count;

	ComputeThreadVaryingInput tvi;
	tvi.groupID = uint3(0u, 0u, 0u);
	tvi.groupThreadID = uint3(0u, 0u, 0u);
	_main_0(&tvi, nullptr, &g);

	delete[] in_packed;
}

} // namespace cassie_slang_dispatch

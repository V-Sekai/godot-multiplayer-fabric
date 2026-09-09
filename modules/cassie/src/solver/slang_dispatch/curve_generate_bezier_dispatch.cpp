/**************************************************************************/
/*  curve_generate_bezier_dispatch.cpp                                    */
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

// Slang-emitted Schneider Bezier generator + dispatch wrapper.
//
// Source-of-truth: `CassieAvbd.CurveGenerateBezier` Lean module.

#include "curve_generate_bezier_dispatch.h"

#include "../../../thirdparty/avbd/curve_generate_bezier.cpu.cpp"

namespace cassie_slang_dispatch {

void curve_generate_bezier(
		const float p_tangent_a[3], const float p_tangent_b[3],
		uint32_t p_count,
		const float *p_in_points_xyz,
		const float *p_in_u,
		float r_out_ctrl_xyz[12]) {
	using namespace cassie_slang_curve_generate_bezier;

	if (p_count < 2 || p_in_points_xyz == nullptr || p_in_u == nullptr ||
			r_out_ctrl_xyz == nullptr) {
		return;
	}

	Vector<float, 3> *in_packed = new Vector<float, 3>[p_count];
	for (uint32_t i = 0; i < p_count; ++i) {
		in_packed[i] = Vector<float, 3>(
				p_in_points_xyz[i * 3 + 0],
				p_in_points_xyz[i * 3 + 1],
				p_in_points_xyz[i * 3 + 2]);
	}

	Vector<float, 3> out_scratch[4] = {};

	GbParams_0 params;
	params.tangent_a_0 = Vector<float, 3>(p_tangent_a[0], p_tangent_a[1], p_tangent_a[2]);
	params.tangent_b_0 = Vector<float, 3>(p_tangent_b[0], p_tangent_b[1], p_tangent_b[2]);
	params.count_0 = p_count;

	GlobalParams_0 g;
	g.params_0 = &params;
	g.in_points_0.data = in_packed;
	g.in_points_0.count = p_count;
	g.in_u_0.data = const_cast<float *>(p_in_u);
	g.in_u_0.count = p_count;
	g.out_ctrl_0.data = out_scratch;
	g.out_ctrl_0.count = 4;

	ComputeThreadVaryingInput tvi;
	tvi.groupID = uint3(0u, 0u, 0u);
	tvi.groupThreadID = uint3(0u, 0u, 0u);
	_main_0(&tvi, nullptr, &g);

	for (int k = 0; k < 4; ++k) {
		r_out_ctrl_xyz[k * 3 + 0] = out_scratch[k].x;
		r_out_ctrl_xyz[k * 3 + 1] = out_scratch[k].y;
		r_out_ctrl_xyz[k * 3 + 2] = out_scratch[k].z;
	}

	delete[] in_packed;
}

} // namespace cassie_slang_dispatch

/**************************************************************************/
/*  curve_rdp_dispatch.cpp                                                */
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

// Slang-emitted RDP polyline simplifier + dispatch wrapper.
//
// Source-of-truth: `CassieAvbd.CurveRdp` Lean module.

#include "curve_rdp_dispatch.h"

#include "../../../thirdparty/avbd/curve_rdp.cpu.cpp"

namespace cassie_slang_dispatch {

uint32_t curve_rdp_reduce(const float *p_in_points_xyz,
		uint32_t p_in_count, float p_error,
		uint32_t *r_out_keep) {
	using namespace cassie_slang_curve_rdp;

	if (p_in_count == 0 || p_in_points_xyz == nullptr || r_out_keep == nullptr) {
		return 0;
	}

	// Pack input into Slang's float3 (16-byte aligned).
	// Heap-allocate via new[] to avoid pulling in Godot LocalVector here.
	Vector<float, 3> *in_packed = new Vector<float, 3>[p_in_count];
	for (uint32_t i = 0; i < p_in_count; ++i) {
		in_packed[i] = Vector<float, 3>(
				p_in_points_xyz[i * 3 + 0],
				p_in_points_xyz[i * 3 + 1],
				p_in_points_xyz[i * 3 + 2]);
	}

	for (uint32_t i = 0; i < p_in_count; ++i) {
		r_out_keep[i] = 0u;
	}

	uint32_t out_count_scratch = 0u;

	RdpParams_0 params;
	params.in_count_0 = p_in_count;
	params.tolerance_0 = p_error;

	GlobalParams_0 g;
	g.params_0 = &params;
	g.in_points_0.data = in_packed;
	g.in_points_0.count = p_in_count;
	g.out_keep_0.data = r_out_keep;
	g.out_keep_0.count = p_in_count;
	g.out_count_0.data = &out_count_scratch;
	g.out_count_0.count = 1;

	ComputeThreadVaryingInput tvi;
	tvi.groupID = uint3(0u, 0u, 0u);
	tvi.groupThreadID = uint3(0u, 0u, 0u);
	_main_0(&tvi, nullptr, &g);

	delete[] in_packed;
	return out_count_scratch;
}

} // namespace cassie_slang_dispatch

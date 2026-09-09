/**************************************************************************/
/*  curve_casteljau_dispatch.cpp                                          */
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

// Slang-emitted De Casteljau cubic split + dispatch wrapper.
//
// Pulls the namespaced .cpu.cpp into this TU so CasteljauParams_0,
// GlobalParams_0, and _main_0 are visible without re-declaration. One
// dispatch is one thread (numthreads(1,1,1)) — the kernel is a pure
// 8-output function, no SIMT fan-out.
//
// Source-of-truth: `CassieAvbd.CurveCasteljau` Lean module.

#include "curve_casteljau_dispatch.h"

#include "../../../thirdparty/avbd/curve_casteljau.cpu.cpp"

namespace cassie_slang_dispatch {

void curve_casteljau(
		const float p_a[3], const float p_b[3],
		const float p_c[3], const float p_d[3],
		float p_u,
		float r_la[3], float r_lb[3], float r_lc[3], float r_ld[3],
		float r_ra[3], float r_rb[3], float r_rc[3], float r_rd[3]) {
	using namespace cassie_slang_curve_casteljau;

	CasteljauParams_0 params;
	params.a_0 = Vector<float, 3>(p_a[0], p_a[1], p_a[2]);
	params.b_0 = Vector<float, 3>(p_b[0], p_b[1], p_b[2]);
	params.c_0 = Vector<float, 3>(p_c[0], p_c[1], p_c[2]);
	params.d_0 = Vector<float, 3>(p_d[0], p_d[1], p_d[2]);
	params.u_0 = p_u;

	Vector<float, 3> out_scratch[8] = {};

	GlobalParams_0 g;
	g.params_0 = &params;
	g.out_0.data = out_scratch;
	g.out_0.count = 8;

	ComputeThreadVaryingInput tvi;
	tvi.groupID = uint3(0u, 0u, 0u);
	tvi.groupThreadID = uint3(0u, 0u, 0u);
	_main_0(&tvi, nullptr, &g);

	auto store = [](float dst[3], const Vector<float, 3> &src) {
		dst[0] = src.x;
		dst[1] = src.y;
		dst[2] = src.z;
	};
	store(r_la, out_scratch[0]);
	store(r_lb, out_scratch[1]);
	store(r_lc, out_scratch[2]);
	store(r_ld, out_scratch[3]);
	store(r_ra, out_scratch[4]);
	store(r_rb, out_scratch[5]);
	store(r_rc, out_scratch[6]);
	store(r_rd, out_scratch[7]);
}

} // namespace cassie_slang_dispatch

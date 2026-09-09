/**************************************************************************/
/*  saxpby_dispatch.cpp                                                   */
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

// Slang-emitted SAXPBY kernel + dispatch wrapper.
//
// Same pattern as spmv_dispatch.cpp — pull the namespaced .cpu.cpp
// into this TU via #include so the internal types (SaxpbyParams_0,
// GlobalParams_0, _main_0) are visible without re-declaration. The
// dispatch loop simulates a SIMT compute dispatch over the kernel's
// numthreads(256, 1, 1) workgroup shape.
//
// Source-of-truth: `Cloth.SlangCodegen.Saxpby` Lean module.

#include "saxpby_dispatch.h"

#include "../../../thirdparty/avbd/saxpby.cpu.cpp"

namespace cassie_slang_dispatch {

void saxpby(int p_n, float p_alpha, float p_beta,
		const float *p_x,
		const float *p_y,
		float *r_dst) {
	using namespace cassie_slang_saxpby;

	SaxpbyParams_0 params;
	params.n_0 = uint32_t(p_n);
	params.alpha_0 = p_alpha;
	params.beta_0 = p_beta;

	GlobalParams_0 g;
	g.params_0 = &params;
	g.x_0.data = const_cast<float *>(p_x);
	g.x_0.count = size_t(p_n);
	g.y_0.data = const_cast<float *>(p_y);
	g.y_0.count = size_t(p_n);
	g.dst_0.data = r_dst;
	g.dst_0.count = size_t(p_n);

	const int threads_per_group = 256;
	const int num_groups = (p_n + threads_per_group - 1) / threads_per_group;

	for (int gi = 0; gi < num_groups; ++gi) {
		for (int ti = 0; ti < threads_per_group; ++ti) {
			ComputeThreadVaryingInput tvi;
			tvi.groupID = uint3(uint32_t(gi), 0u, 0u);
			tvi.groupThreadID = uint3(uint32_t(ti), 0u, 0u);
			_main_0(&tvi, nullptr, &g);
		}
	}
}

} // namespace cassie_slang_dispatch

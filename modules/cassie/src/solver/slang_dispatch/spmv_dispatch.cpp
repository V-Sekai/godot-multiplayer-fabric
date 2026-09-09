/**************************************************************************/
/*  spmv_dispatch.cpp                                                     */
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

// Slang-emitted SPMV kernel + dispatch wrapper.
//
// We #include the slangc -target cpp output (which lives under
// modules/cassie/thirdparty/avbd/) directly here. The .cpu.cpp file
// wraps its contents in `namespace cassie_slang_spmv { ... }` (post-
// processed by `lake exe avbd-codegen`), so pulling it in here makes
// `_main_0`, `GlobalParams_0`, `SpmvDf32Params_0` etc. visible in this
// TU only — no duplicate-symbol risk if no other TU includes the same
// file.
//
// The dispatch wrapper at the bottom simulates the SIMT workgroup loop
// that a GPU would run in parallel: ceil(rows/256) groups × 256 threads.
// Each thread invocation calls `_main_0` with the appropriate
// ComputeThreadVaryingInput. The kernel itself early-exits any thread
// whose global ID >= rows, so the trailing thread tail is harmless.
//
// The Slang kernel is `Cloth.SlangCodegen.SpmvDf32` — accumulates each
// row's sum as a (hi, lo) df32 pair via Knuth/Dekker EFTs and collapses
// to fp32 at write-out. Drops per-row error from ~7·ε to ~7·ε² so CG
// iterations don't hit a residual floor around 1e-4. Same binding ABI
// as the plain `Spmv` kernel, just the struct name changed to
// `SpmvDf32Params_0`.
//
// CPU dispatch matches the GPU SPIR-V dispatch bit-for-bit — both go
// through the same Slang source via slangc, just to different targets.
// This is the "Slang is single source of truth" property the Track 5
// follow-up is delivering.

#include "spmv_dispatch.h"

#include "../../../thirdparty/avbd/spmv.cpu.cpp"

namespace cassie_slang_dispatch {

void spmv(int p_rows,
		const int32_t *p_row_ptr,
		const int32_t *p_col_idx, int p_nnz,
		const float *p_values,
		const float *p_x, int p_cols,
		float *r_y) {
	using namespace cassie_slang_spmv;

	SpmvDf32Params_0 params;
	params.rows_0 = uint32_t(p_rows);

	GlobalParams_0 g;
	g.params_0 = &params;
	g.rowPtr_0.data = const_cast<int32_t *>(p_row_ptr);
	g.rowPtr_0.count = size_t(p_rows + 1);
	g.colIdx_0.data = const_cast<int32_t *>(p_col_idx);
	g.colIdx_0.count = size_t(p_nnz);
	g.values_0.data = const_cast<float *>(p_values);
	g.values_0.count = size_t(p_nnz);
	g.x_0.data = const_cast<float *>(p_x);
	g.x_0.count = size_t(p_cols);
	g.y_0.data = r_y;
	g.y_0.count = size_t(p_rows);

	const int threads_per_group = 256;
	const int num_groups = (p_rows + threads_per_group - 1) / threads_per_group;

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

/**************************************************************************/
/*  spmv_dispatch.h                                                       */
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

#pragma once

// Public C++ entry point to the Slang-emitted SPMV kernel. The
// implementation is in spmv_dispatch.cpp, which `#include`s the
// slangc-emitted modules/cassie/thirdparty/avbd/spmv.cpu.cpp directly.
//
// Single source of truth: this kernel is generated from the Lean
// `Cloth.SlangCodegen.Spmv` module → Slang source → slangc -target cpp.
// The same Lean source compiles to SPIR-V for GPU dispatch.
//
// Float32 throughout. CPU callers in `cassie_pcg.cpp` narrow doubles
// at the boundary; the precision tradeoff is documented in
// cassie_pcg.h.

#include <cstdint>

namespace cassie_slang_dispatch {

// y = A · x, sparse, float32, CSR layout. row_ptr is length rows + 1.
// col_idx and values are length nnz. x is length cols, y is length rows.
//
// The dispatch loop simulates a GPU compute dispatch over
// numthreads(256, 1, 1) — ceil(rows / 256) workgroups × 256 threads.
void spmv(int p_rows,
		const int32_t *p_row_ptr,
		const int32_t *p_col_idx, int p_nnz,
		const float *p_values,
		const float *p_x, int p_cols,
		float *r_y);

} // namespace cassie_slang_dispatch

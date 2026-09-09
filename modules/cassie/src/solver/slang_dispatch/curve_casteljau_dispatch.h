/**************************************************************************/
/*  curve_casteljau_dispatch.h                                            */
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

// Public C++ entry point to the Slang-emitted De Casteljau cubic split
// kernel. Same pattern as saxpby_dispatch.h — implementation in
// curve_casteljau_dispatch.cpp `#include`s the slangc -target cpp output
// directly.
//
// Splits the cubic Bezier (a, b, c, d) at parameter u ∈ [0, 1]. After
// the split: la == a, rd == d, ld == ra is the cut point on the curve.
// All inputs and outputs are float3 (precision loss vs the prior fp64
// hand implementation is acceptable at editing-time scale).
//
// Source-of-truth: `CassieAvbd.CurveCasteljau` Lean module.

namespace cassie_slang_dispatch {

void curve_casteljau(
		const float p_a[3], const float p_b[3],
		const float p_c[3], const float p_d[3],
		float p_u,
		float r_la[3], float r_lb[3], float r_lc[3], float r_ld[3],
		float r_ra[3], float r_rb[3], float r_rc[3], float r_rd[3]);

} // namespace cassie_slang_dispatch

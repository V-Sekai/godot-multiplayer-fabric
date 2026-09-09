/**************************************************************************/
/*  curve_newton_dispatch.h                                               */
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

#include <cstdint>

// Public C++ entry point to the Slang-emitted Newton-Raphson
// reparameterize kernel. Raw-pointer signature for the same TU-isolation
// reasons as curve_rdp_dispatch.h.
//
// Given a cubic Bezier (p_a, p_b, p_c, p_d) and N (point, u_in) pairs,
// writes refined u values into r_u_out. Bug-for-bug compatible with
// the prior C++ reparameterize.
//
// Source-of-truth: `CassieAvbd.CurveNewton` Lean module.

namespace cassie_slang_dispatch {

void curve_newton_reparameterize(
		const float p_a[3], const float p_b[3],
		const float p_c[3], const float p_d[3],
		uint32_t p_count,
		const float *p_in_points_xyz, // 3 · count floats
		const float *p_in_u, // count floats
		float *r_u_out); // count floats

} // namespace cassie_slang_dispatch

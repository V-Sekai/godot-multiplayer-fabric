/**************************************************************************/
/*  curve_generate_bezier_dispatch.h                                      */
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

// Public C++ entry point to the Slang-emitted Schneider Bezier
// generator. Raw-pointer signature (same TU-isolation reason as the
// other curve_*_dispatch).
//
// Given p_count points + p_count u parameters (chord-length normalized
// to [0, 1]) and two endpoint tangents, writes the four control points
// of the LSQ-fit cubic Bezier to r_out_ctrl_xyz (12 floats: P0, P1, P2, P3).
// Falls back to (segment_length / 3)-tangent control points when the
// 2×2 normal equations are singular or yield a non-positive α.
//
// Source-of-truth: `CassieAvbd.CurveGenerateBezier` Lean module.

namespace cassie_slang_dispatch {

void curve_generate_bezier(
		const float p_tangent_a[3], const float p_tangent_b[3],
		uint32_t p_count,
		const float *p_in_points_xyz, // 3 · count floats
		const float *p_in_u, // count floats
		float r_out_ctrl_xyz[12]); // 4 float3 control points

} // namespace cassie_slang_dispatch

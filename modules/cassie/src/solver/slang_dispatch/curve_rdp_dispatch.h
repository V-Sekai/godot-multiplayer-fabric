/**************************************************************************/
/*  curve_rdp_dispatch.h                                                  */
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

// Public C++ entry point to the Slang-emitted Ramer-Douglas-Peucker
// polyline simplifier. Same pattern as curve_casteljau_dispatch.h —
// raw-pointer signature so the slang-cpp-prelude.h types don't leak
// into the same TU as Godot's PackedInt32Array (the two collide on
// the `Vector` template name).
//
// Bug-for-bug compatible with the prior C++ rdp_recursive — same
// "split == 0 means none found" sentinel.
//
// Source-of-truth: `CassieAvbd.CurveRdp` Lean module.

namespace cassie_slang_dispatch {

// Fills r_out_keep with 1 for kept indices, 0 for dropped. r_out_keep
// must point to at least p_in_count uint32_t entries. Returns the
// number of kept indices (= sum of the bitmask). Caller iterates
// r_out_keep[0..p_in_count) and collects the 1-marked indices.
uint32_t curve_rdp_reduce(const float *p_in_points_xyz,
		uint32_t p_in_count, float p_error,
		uint32_t *r_out_keep);

} // namespace cassie_slang_dispatch

/**************************************************************************/
/*  rdp_simplify.cpp                                                      */
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

#include "rdp_simplify.h"

#include "../solver/slang_dispatch/curve_rdp_dispatch.h"

#include "core/templates/local_vector.h"

PackedVector3Array cassie_rdp_remove_duplicates(const PackedVector3Array &p_pts) {
	const int n = p_pts.size();
	if (n < 2) {
		return p_pts;
	}

	int dup_count = 0;
	Vector3 prev = p_pts[0];
	for (int i = 1; i < n; ++i) {
		const Vector3 cur = p_pts[i];
		if (prev == cur) {
			++dup_count;
		} else {
			prev = cur;
		}
	}
	if (dup_count == 0) {
		return p_pts;
	}

	PackedVector3Array dst;
	dst.resize(n - dup_count);
	Vector3 *w = dst.ptrw();
	w[0] = p_pts[0];
	int j = 1;
	prev = p_pts[0];
	for (int i = 1; i < n; ++i) {
		const Vector3 cur = p_pts[i];
		if (prev != cur) {
			w[j++] = cur;
			prev = cur;
		}
	}
	return dst;
}

PackedInt32Array cassie_rdp_reduce(const PackedVector3Array &p_pts, float p_error) {
	PackedInt32Array kept;
	const int n = p_pts.size();
	if (n == 0) {
		return kept;
	}
	if (n == 1) {
		kept.push_back(0);
		return kept;
	}
	if (n < 3) {
		kept.push_back(0);
		kept.push_back(1);
		return kept;
	}

	// Pack input float3s into a flat float buffer (3·N floats) for the
	// Slang-emitted kernel, then translate the returned bitmask into a
	// kept-indices array the existing callers expect.
	LocalVector<float> in_flat;
	in_flat.resize(n * 3);
	for (int i = 0; i < n; ++i) {
		in_flat[i * 3 + 0] = float(p_pts[i].x);
		in_flat[i * 3 + 1] = float(p_pts[i].y);
		in_flat[i * 3 + 2] = float(p_pts[i].z);
	}
	LocalVector<uint32_t> out_keep;
	out_keep.resize(n);

	const uint32_t kept_count = cassie_slang_dispatch::curve_rdp_reduce(
			in_flat.ptr(), uint32_t(n), p_error, out_keep.ptr());

	kept.resize(int(kept_count));
	int *w = kept.ptrw();
	int j = 0;
	for (int i = 0; i < n; ++i) {
		if (out_keep[i] == 1u && j < int(kept_count)) {
			w[j++] = i;
		}
	}
	return kept;
}

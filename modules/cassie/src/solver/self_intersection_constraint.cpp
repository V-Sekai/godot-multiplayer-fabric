/**************************************************************************/
/*  self_intersection_constraint.cpp                                      */
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

#include "self_intersection_constraint.h"

namespace cassie_solver {

SelfIntersectionConstraint::SelfIntersectionConstraint(int p_anchor_idx_a, int p_anchor_idx_b,
		const Vector3 &p_AB0, int p_N) :
		anchor_idx_a(p_anchor_idx_a), anchor_idx_b(p_anchor_idx_b), AB0(p_AB0), N(p_N) {}

void SelfIntersectionConstraint::get_blocks(DenseMatrix &r_C, DenseVector &r_b) const {
	r_C.zero_resize(3, 3 * N);
	for (int i = 0; i < 3; ++i) {
		r_C(i, 9 * anchor_idx_a + i) = 1.0;
		r_C(i, 9 * anchor_idx_b + i) = -1.0;
	}
	r_b.zero_resize(3);
	r_b[0] = double(AB0.x);
	r_b[1] = double(AB0.y);
	r_b[2] = double(AB0.z);
}

} // namespace cassie_solver

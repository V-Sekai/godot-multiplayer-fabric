/**************************************************************************/
/*  position_constraint.cpp                                               */
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

#include "position_constraint.h"

namespace cassie_solver {

PositionConstraint::PositionConstraint(int p_ctrl_pt_idx, const Vector3 &p_displacement, int p_N) :
		ctrl_pt_idx(p_ctrl_pt_idx), displacement(p_displacement), N(p_N) {}

void PositionConstraint::get_blocks(DenseMatrix &r_C, DenseVector &r_b) const {
	r_C.zero_resize(3, 3 * N);
	for (int i = 0; i < 3; ++i) {
		r_C(i, 3 * ctrl_pt_idx + i) = 1.0;
	}
	r_b.zero_resize(3);
	r_b[0] = double(displacement.x);
	r_b[1] = double(displacement.y);
	r_b[2] = double(displacement.z);
}

} // namespace cassie_solver

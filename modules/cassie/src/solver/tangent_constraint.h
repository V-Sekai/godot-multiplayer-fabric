/**************************************************************************/
/*  tangent_constraint.h                                                  */
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

#include "soft_constraint.h"

#include "core/math/vector3.h"
#include "core/templates/vector.h"

namespace cassie_solver {

// TangentConstraint is a soft penalty that aligns the inter-anchor tangent
// at index ctrl_pt_idx with a target direction t_target. Port of
// E:\cassie\Assets\Scripts\Create\Sketch\Beautify\TangentConstraint.cs.
//
// Uses the skew-symmetric cross-product matrix of t_target; the energy is
// proportional to | (P_idxB - P_idxA) × t_target |² scaled by 2/||T0||².
class TangentConstraint : public SoftConstraintBlock {
public:
	TangentConstraint(int p_ctrl_pt_idx, const Vector3 &p_t_target,
			const Vector<Vector3> &p_initial_control_points);

	void get_blocks(DenseMatrix &r_A, DenseVector &r_b) const override;

private:
	int idx_a = 0;
	int idx_b = 0;
	int N = 0;
	Vector3 T0;
	double t_cross[9] = { 0.0 };
};

} // namespace cassie_solver

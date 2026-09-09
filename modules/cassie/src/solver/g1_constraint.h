/**************************************************************************/
/*  g1_constraint.h                                                       */
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

#include "hard_constraint.h"

#include "core/math/vector3.h"
#include "core/templates/vector.h"

namespace cassie_solver {

// G1Constraint enforces collinearity at every joint of the composite
// cubic Bezier that was originally G1 (cross product magnitude below eps).
// Port of E:\cassie\Assets\Scripts\Create\Sketch\Beautify\G1Constraint.cs.
//
// For each preserved-G1 joint at anchor index = 3*(i+1) in control-point
// space, 3 rows are added that enforce:
//   p_anchor (1/||T_left|| + 1/||T_right||)
//   - p_left  / ||T_left||
//   - p_right / ||T_right|| = 0
//
// This is the linearization of "p_anchor lies on the segment from p_left
// to p_right with the original tangent ratio".
class G1Constraint : public HardConstraintBlock {
public:
	explicit G1Constraint(const Vector<Vector3> &p_control_points);

	void get_blocks(DenseMatrix &r_C, DenseVector &r_b) const override;
	int row_count() const override { return 3 * n_joints; }

	int get_joint_count() const { return n_joints; }

private:
	int N = 0;
	int Nb = 0; // Number of individual Bezier segments.
	int n_joints = 0;
	Vector<double> left_tangent_norms;
	Vector<double> right_tangent_norms;
	Vector<bool> is_g1;
};

} // namespace cassie_solver

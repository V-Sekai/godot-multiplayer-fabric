/**************************************************************************/
/*  self_intersection_constraint.h                                        */
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

namespace cassie_solver {

// SelfIntersectionConstraint pins two anchors A and B to maintain their
// initial separation AB0. Port of E:/cassie/Assets/Scripts/Create/Sketch/
// Beautify\SelfIntersectionConstraint.cs.
//
// Note on indexing: Unity uses `3 * 3 * anchorIdx` which translates the
// anchor index into the flattened control-point address. Anchor k occupies
// control-point index 3*k; flattening to 3-dim coords gives offset 9*k.
// Despite looking like a bug at first glance, this matches the rest of the
// solver's convention (PositionConstraint uses cp-index times 3; here the
// argument is already in anchor-index space).
class SelfIntersectionConstraint : public HardConstraintBlock {
public:
	SelfIntersectionConstraint(int p_anchor_idx_a, int p_anchor_idx_b,
			const Vector3 &p_AB0, int p_N);

	void get_blocks(DenseMatrix &r_C, DenseVector &r_b) const override;
	int row_count() const override { return 3; }

private:
	int anchor_idx_a;
	int anchor_idx_b;
	Vector3 AB0;
	int N;
};

} // namespace cassie_solver

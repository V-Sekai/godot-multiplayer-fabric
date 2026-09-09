/**************************************************************************/
/*  planarity_constraint.h                                                */
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

#include "core/math/plane.h"
#include "core/math/vector3.h"
#include "core/templates/vector.h"

namespace cassie_solver {

// PlanarityConstraint is a soft penalty that drives the curve toward a
// given plane. Port of E:/cassie/Assets/Scripts/Create/Sketch/Beautify/
// PlanarityConstraint.cs.
//
// Sums  Σᵢ (1/||T0ᵢ||²) * (normal · (P_{i+1} - Pᵢ))²  with a factor
// 2/(N-1) so the energy scales with control-point count.
class PlanarityConstraint : public SoftConstraintBlock {
public:
	PlanarityConstraint(const Vector3 &p_normal, const Vector<Vector3> &p_initial_control_points);

	void get_blocks(DenseMatrix &r_A, DenseVector &r_b) const override;

private:
	int N = 0;
	double normal[3] = { 0.0, 0.0, 0.0 };
	Vector<double> tangent_norms2;
	Vector<Vector3> T0;
};

// Best-fit plane through a point set via the 3 × 3 covariance + smallest
// eigenvector. Returns the plane normal as the smallest-eigenvalue
// eigenvector and the centroid as a point on the plane; the average
// signed distance of input points to the plane is returned in
// r_avg_distance.
Plane cassie_fit_plane(const Vector<Vector3> &p_points, double &r_avg_distance);

} // namespace cassie_solver

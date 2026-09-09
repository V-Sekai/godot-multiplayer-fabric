/**************************************************************************/
/*  fidelity_energy.h                                                     */
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

// Solver-internal header — Track 5 Eigen-free.

#include "dense_matrix.h"

#include "core/math/vector3.h"
#include "core/templates/vector.h"

namespace cassie_solver {

// FidelityEnergy is the SPD term in the KKT system's top-left block.
// Port of E:\cassie\Assets\Scripts\Create\Sketch\Beautify\FidelityEnergy.cs.
//
// Penalizes (a) displacement of each control point from its initial
// position (weight w_p / N) and (b) variation of the inter-anchor tangents
// (weight w_t / (N-1)). p_factor is also scaled by the proximity threshold
// squared to make displacement amplitudes commensurate with the constraint
// tolerance.
class FidelityEnergy {
public:
	FidelityEnergy(const Vector<Vector3> &p_initial_control_points,
			double p_w_p, double p_w_t, double p_displacement_normalizer);

	// Fills r_A with the 3N × 3N Hessian block (resizes to fit).
	void get_block(DenseMatrix &r_A) const;

	// Computes the scalar energy at the given displacement vector
	// (length N, displacements applied to the initial control points).
	double compute(const Vector<Vector3> &p_displacements) const;

	int control_point_count() const { return N; }

private:
	int N = 0;
	double p_factor = 0.0;
	double t_factor = 0.0;
	Vector<double> tangent_norms2;
};

} // namespace cassie_solver

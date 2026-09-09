/**************************************************************************/
/*  on_surface_energy.h                                                   */
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
#include "core/variant/callable.h"

namespace cassie_solver {

// OnSurfaceEnergy — ENG-54 (Track 4 step 4.2). Soft penalty that pulls
// every control point p_i toward its projection q_i onto a target
// surface S. The Gauss-Newton linearization freezes q_i during one
// solver call (the patch is locally planar, so curvature terms drop):
//
//   E = w · Σ_i |p_i - q_i|²
//   gradient_i = 2 w (p_i - q_i)
//   Hessian_i  ≈ 2 w I_3
//
// Block contributions to the augmented KKT system:
//   A_c at (3i, 3i) .. (3i+2, 3i+2) = 2 w I_3
//   b_c at (3i .. 3i+2)             = 2 w q_i
//
// At the optimum the augmented gradient `A_grad x - b_top` vanishes, so
// `2 w x_i = 2 w q_i` ⇒ `x_i = q_i`. Combined with the fidelity energy
// the result is the Gauss-Newton compromise between staying close to
// the input curve and snapping to the patch.
//
// Per Phase 1 exploration: this composes with PlanarityConstraint
// rather than fighting it the way the post-solve projection at
// cassie_beautifier.cpp:294-316 currently does.
class OnSurfaceEnergy : public SoftConstraintBlock {
public:
	// p_project_callback is the CassieSketchContext::project_on_patch_callback
	// already threaded through CassieBeautifier; it returns the Dictionary
	// CassieSurfacePatch::project produces ("on_surface", "projected", ...).
	OnSurfaceEnergy(const Vector<Vector3> &p_initial_control_points,
			const Callable &p_project_callback,
			double p_weight);

	void get_blocks(DenseMatrix &r_A, DenseVector &r_b) const override;

	// Number of control points where the projection callback reported
	// on_surface = true — i.e. the energy actually has a target there.
	int get_active_count() const { return active_count; }

private:
	int N = 0;
	double w = 0.0;
	Vector<Vector3> initial_cp; // p0_i — needed for the displacement form of b
	Vector<Vector3> targets; // q_i = project(p0_i)
	Vector<bool> active;
	int active_count = 0;
};

} // namespace cassie_solver

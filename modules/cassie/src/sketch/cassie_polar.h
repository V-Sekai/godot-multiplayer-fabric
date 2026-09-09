/**************************************************************************/
/*  cassie_polar.h                                                        */
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

// Closed-form polar decomposition for the 3×3 Wahba's problem.
// Replaces the prior QCP path (which held a 4-element quaternion in
// memory) with a direct R = H * (H^T H)^{-1/2} computation that stays
// in 3×3 matrix-land throughout.
//
// Why closed-form: the 3×3 symmetric eigendecomposition of (H^T H) has
// a published closed form via the cubic characteristic polynomial
// (Smith 1961 / Connelly 2008 trig substitution) — no Newton iteration,
// no Jacobi sweeps. Combined with the polar identity
// R = H * V * diag(1/sqrt(λ_i)) * V^T this gives a single-pass solve.
//
// Reflection fix: H from Wahba is a cross-covariance; if det(H) < 0
// the polar decomp produces an improper rotation. We flip the sign of
// the smallest-eigenvalue eigenvector before reassembly so the output
// has det(R) = +1. Equivalent to the SVD-era sign correction
// M(2,2) = sign(det(V·U^T)).
//
// The Lean spec at modules/cassie/lean/CassieAvbd/PolarDecomp.lean
// ports this algorithm verbatim and pins concrete fixtures via
// native_decide — see that file for the formal verification side.

#include "core/math/basis.h"
#include "core/math/vector3.h"
#include "core/variant/variant.h" // PackedVector3Array

namespace cassie_polar {

// Closed-form 3×3 symmetric eigendecomposition. p_M is row-major
// 9-element symmetric matrix; on return:
//   r_lambda[0..2] are the eigenvalues (descending order)
//   r_V is the row-major orthonormal eigenvector matrix; column i is
//     the eigenvector for r_lambda[i]
//
// Smith 1961 / Connelly 2008 trig substitution. Closed-form, no
// iteration. Cost: ~80 flops + 2 trig calls.
void eigendecompose_3x3_symmetric(const double *p_M, double *r_lambda,
		double *r_V);

// Wahba solver via closed-form polar decomposition. No quaternion
// intermediate — operates entirely on 3×3 matrices.
Basis wahba_align(const PackedVector3Array &p_projection_tangents,
		const PackedVector3Array &p_rest_tangents);

// Safeguarded polar decomposition of a general 3×3 matrix M (ENG-68).
//
// Returns R such that R is orthogonal with det(R) = +1, approximating
// the SVD's U·V^T rotation. Used by DDM runtime where the input matrix
// can have det(M) < 0 from extreme weighted-blend pathologies — wahba_align
// only handles det(H) ≥ 0 inputs from proper-rotation Wahba pairs.
//
// Algorithm:
//   1. H = M^T · M (symmetric PSD)
//   2. eigendecompose H → λ, V; clamp λ_i to max(λ_i, 1e-12)
//   3. Build R = M · V · diag(1/√λ_i) · V^T
//   4. If det(R) < 0 (reflection): flip the sign of d_3 = 1/√λ_3
//      (smallest eigenvalue's inverse-sqrt) and recompute R.
//      THIS is the SVD-equivalent fix — V-column flips are no-ops on
//      V·diag·V^T (the M^{-1/2} formula); d-flips are not.
//
// Inputs:
//   p_M — row-major 9-element 3×3 matrix, may be non-symmetric
// Outputs:
//   r_R — row-major 9-element rotation matrix, det(R) = +1 guaranteed
//
// Cost: ~360 flops worst case (~335 base + ~25 for reflection branch).
void polar_decompose_3x3_safe(const double *p_M, double *r_R);

} // namespace cassie_polar

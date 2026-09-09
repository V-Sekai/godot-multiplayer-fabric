/**************************************************************************/
/*  cassie_eigen.h                                                        */
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

// Track 5 Phase D — small symmetric-eigenvalue solver. Used for:
//   * cassie_fit_plane (3x3 covariance → smallest-eigenvalue eigenvector
//     = plane normal).
//   * Wahba's QCP solver (4x4 K matrix → largest-eigenvalue eigenvector
//     → rotation matrix via direct symbolic substitution).
//
// Eigen-free. Pure-double-precision Jacobi sweeps; no external deps.

namespace cassie_eigen {

// In-place Jacobi eigendecomposition for a fixed-size symmetric matrix.
// p_A is row-major n*n; on return its diagonal carries the eigenvalues
// and the off-diagonal is zeroed (to tolerance). p_V is row-major n*n;
// on return its columns are the orthonormal eigenvectors. Stops after
// p_max_sweeps full sweeps OR when the off-diagonal Frobenius norm
// falls below p_tol.
//
// Cost: O(n^3 * sweeps). At n=3 or 4 this is ~150 flops per sweep so
// well under any other CASSIE cost.
void jacobi_sym(double *p_A, double *p_V, int p_n,
		int p_max_sweeps = 50, double p_tol = 1e-14);

// Convenience: smallest-eigenvalue eigenvector of a 3x3 symmetric matrix.
// p_A_in is left untouched (the routine makes its own copy). p_normal
// receives the eigenvector with the smallest absolute eigenvalue.
void smallest_eigenvector_3x3(const double *p_A_in, double *p_normal);

// Convenience: largest-eigenvalue eigenvector of a 4x4 symmetric matrix.
// p_q_out is length 4, ordered (w, x, y, z) — the standard QCP
// quaternion convention. The sign of p_q_out is normalized so that
// p_q_out[0] (the w component) is non-negative — picks the unique
// representative of the SO(3) double cover without continuity flips.
void largest_eigenvector_4x4(const double *p_A_in, double *p_q_out);

} // namespace cassie_eigen

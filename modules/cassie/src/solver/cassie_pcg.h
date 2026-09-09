/**************************************************************************/
/*  cassie_pcg.h                                                          */
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

#include "core/templates/local_vector.h"

// Conjugate-Gradient utilities for CASSIE — the Eigen-free replacement
// for Eigen::SimplicialLDLT (sparse) and Eigen::LDLT (dense) per the
// Track 5 Eigen-removal plan.
//
// Algorithmic shape mirrors the vendored Cloth.SlangCodegen kernels at
// modules/cassie/lean/Cloth/SlangCodegen/{Spmv,Saxpby,DotReduce,
// CGAlpha,CGBeta}.lean. The C++ here is the source-of-truth CPU path
// (doctest runs without a RenderingDevice); the GPU dispatch over the
// SPIR-V slangc just emitted is the perf follow-up.
//
// Jacobi-preconditioned: M^-1 = diag(A)^-1. Cotangent Laplacians and
// the AVBD augmented Hessian are SPD by construction → CG converges.
// Tolerance is relative to ||b||; max_iter caps wall time.
//
// Reuses LocalVector storage. No external deps.

namespace cassie_pcg {

// Sparse Compressed-Row layout. NNZ is row_ptr[rows]. Column indices
// within a row don't need to be sorted, but they must be unique per
// row to keep the diagonal extraction unambiguous.
struct CSRMatrix {
	int rows = 0;
	int cols = 0;
	LocalVector<int> row_ptr; // size rows + 1
	LocalVector<int> col_idx; // size nnz
	LocalVector<double> val; // size nnz
};

// y = A * x where A is rows x cols. x has length cols; y has length rows.
void spmv(const CSRMatrix &p_A, const double *p_x, double *p_y);

// Extract the diagonal of an n x n CSR matrix into p_diag (length n).
// Missing diagonal entries are reported as 0.
void extract_diagonal(const CSRMatrix &p_A, double *p_diag);

// Solve A x = b with Jacobi-PCG, where A is sparse SPD. Returns the
// iteration count used. *r_residual is set to the final ||r||/||b||
// ratio. p_diag_inv must equal 1/diag(A) (clamped to avoid /0).
//
// p_x is BOTH the initial guess (for warm-start) AND the output. Pass
// a zero buffer for a cold solve.
int solve_sparse(const CSRMatrix &p_A,
		const double *p_b, double *p_x,
		const double *p_diag_inv,
		int p_max_iter, double p_tol,
		double *r_residual);

// Dense Jacobi-PCG. p_A is row-major n x n SPD. Used by Phase C's
// AVBD inner solve (≤ 45 x 45) — Spmv-as-dense-mvm is ~2k flops and
// CSR indirection overhead would dominate at that scale.
int solve_dense(const double *p_A, int p_n,
		const double *p_b, double *p_x,
		const double *p_diag_inv,
		int p_max_iter, double p_tol,
		double *r_residual);

} // namespace cassie_pcg

/**************************************************************************/
/*  cassie_pcg.cpp                                                        */
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

#include "cassie_pcg.h"

#include "slang_dispatch/spmv_dispatch.h"

#include "core/math/math_funcs.h"

#include <cmath>

namespace cassie_pcg {

namespace {

inline double dot(const double *p_a, const double *p_b, int p_n) {
	double s = 0.0;
	for (int i = 0; i < p_n; ++i) {
		s += p_a[i] * p_b[i];
	}
	return s;
}

inline void axpy(double p_alpha, const double *p_x, double *p_y, int p_n) {
	for (int i = 0; i < p_n; ++i) {
		p_y[i] += p_alpha * p_x[i];
	}
}

[[maybe_unused]] inline void copy(const double *p_src, double *p_dst, int p_n) {
	for (int i = 0; i < p_n; ++i) {
		p_dst[i] = p_src[i];
	}
}

inline double l2_norm(const double *p_v, int p_n) {
	return std::sqrt(dot(p_v, p_v, p_n));
}

inline void mvm_dense(const double *p_A, int p_n, const double *p_x, double *p_y) {
	for (int i = 0; i < p_n; ++i) {
		double s = 0.0;
		const double *row = p_A + size_t(i) * size_t(p_n);
		for (int j = 0; j < p_n; ++j) {
			s += row[j] * p_x[j];
		}
		p_y[i] = s;
	}
}

} // namespace

void spmv(const CSRMatrix &p_A, const double *p_x, double *p_y) {
	// Delegate to the Slang-emitted kernel (slangc -target cpp output of
	// `Cloth.SlangCodegen.SpmvDf32`). The kernel accumulates each row
	// as a (hi, lo) df32 pair via Knuth/Dekker error-free transforms
	// and collapses to fp32 at write-out. Per-row error is ~7·ε² rather
	// than ~7·ε, so CG iterations don't plateau at ~1e-4 the way the
	// naive single-float spmv would. Boundary is still float32 in/out
	// (matching the GPU SPIR-V dispatch bit-for-bit); the precision
	// recovery is internal to each row sum.
	const int rows = p_A.rows;
	const int cols = p_A.cols;
	const int nnz = int(p_A.val.size());
	if (rows == 0) {
		return;
	}

	LocalVector<float> values_f;
	values_f.resize(nnz);
	for (int i = 0; i < nnz; ++i) {
		values_f[i] = float(p_A.val[i]);
	}
	LocalVector<float> x_f;
	x_f.resize(cols);
	for (int i = 0; i < cols; ++i) {
		x_f[i] = float(p_x[i]);
	}
	LocalVector<float> y_f;
	y_f.resize(rows);

	cassie_slang_dispatch::spmv(rows,
			p_A.row_ptr.ptr(),
			p_A.col_idx.ptr(), nnz,
			values_f.ptr(),
			x_f.ptr(), cols,
			y_f.ptr());

	for (int i = 0; i < rows; ++i) {
		p_y[i] = double(y_f[i]);
	}
}

void extract_diagonal(const CSRMatrix &p_A, double *p_diag) {
	for (int i = 0; i < p_A.rows; ++i) {
		double d = 0.0;
		const int row_start = p_A.row_ptr[i];
		const int row_end = p_A.row_ptr[i + 1];
		for (int k = row_start; k < row_end; ++k) {
			if (p_A.col_idx[k] == i) {
				d = p_A.val[k];
				break;
			}
		}
		p_diag[i] = d;
	}
}

int solve_sparse(const CSRMatrix &p_A,
		const double *p_b, double *p_x,
		const double *p_diag_inv,
		int p_max_iter, double p_tol,
		double *r_residual) {
	const int n = p_A.rows;
	if (n == 0) {
		if (r_residual) {
			*r_residual = 0.0;
		}
		return 0;
	}

	LocalVector<double> r;
	LocalVector<double> z;
	LocalVector<double> p;
	LocalVector<double> Ap;
	r.resize(n);
	z.resize(n);
	p.resize(n);
	Ap.resize(n);

	// r = b - A x   (uses the warm-start x if non-zero).
	spmv(p_A, p_x, r.ptr());
	for (int i = 0; i < n; ++i) {
		r[i] = p_b[i] - r[i];
	}

	const double b_norm = l2_norm(p_b, n);
	const double b_norm_safe = b_norm > 1e-30 ? b_norm : 1.0;
	double r_norm = l2_norm(r.ptr(), n);
	if (r_norm / b_norm_safe < p_tol) {
		if (r_residual) {
			*r_residual = r_norm / b_norm_safe;
		}
		return 0;
	}

	// z = M^-1 r ;  p = z
	for (int i = 0; i < n; ++i) {
		z[i] = p_diag_inv[i] * r[i];
		p[i] = z[i];
	}
	double rz = dot(r.ptr(), z.ptr(), n);

	int iter = 0;
	for (; iter < p_max_iter; ++iter) {
		spmv(p_A, p.ptr(), Ap.ptr());
		const double pAp = dot(p.ptr(), Ap.ptr(), n);
		if (pAp <= 0.0 || !std::isfinite(pAp)) {
			break; // SPD-broken or numerical breakdown.
		}
		const double alpha = rz / pAp;
		axpy(alpha, p.ptr(), p_x, n); // x += alpha * p
		axpy(-alpha, Ap.ptr(), r.ptr(), n); // r -= alpha * Ap

		r_norm = l2_norm(r.ptr(), n);
		if (r_norm / b_norm_safe < p_tol) {
			++iter;
			break;
		}

		// z = M^-1 r
		for (int i = 0; i < n; ++i) {
			z[i] = p_diag_inv[i] * r[i];
		}
		const double rz_new = dot(r.ptr(), z.ptr(), n);
		const double beta = rz_new / (rz != 0.0 ? rz : 1.0);
		// p = z + beta * p
		for (int i = 0; i < n; ++i) {
			p[i] = z[i] + beta * p[i];
		}
		rz = rz_new;
	}

	if (r_residual) {
		*r_residual = r_norm / b_norm_safe;
	}
	return iter;
}

int solve_dense(const double *p_A, int p_n,
		const double *p_b, double *p_x,
		const double *p_diag_inv,
		int p_max_iter, double p_tol,
		double *r_residual) {
	if (p_n == 0) {
		if (r_residual) {
			*r_residual = 0.0;
		}
		return 0;
	}

	LocalVector<double> r;
	LocalVector<double> z;
	LocalVector<double> p;
	LocalVector<double> Ap;
	r.resize(p_n);
	z.resize(p_n);
	p.resize(p_n);
	Ap.resize(p_n);

	mvm_dense(p_A, p_n, p_x, r.ptr());
	for (int i = 0; i < p_n; ++i) {
		r[i] = p_b[i] - r[i];
	}

	const double b_norm = l2_norm(p_b, p_n);
	const double b_norm_safe = b_norm > 1e-30 ? b_norm : 1.0;
	double r_norm = l2_norm(r.ptr(), p_n);
	if (r_norm / b_norm_safe < p_tol) {
		if (r_residual) {
			*r_residual = r_norm / b_norm_safe;
		}
		return 0;
	}

	for (int i = 0; i < p_n; ++i) {
		z[i] = p_diag_inv[i] * r[i];
		p[i] = z[i];
	}
	double rz = dot(r.ptr(), z.ptr(), p_n);

	int iter = 0;
	for (; iter < p_max_iter; ++iter) {
		mvm_dense(p_A, p_n, p.ptr(), Ap.ptr());
		const double pAp = dot(p.ptr(), Ap.ptr(), p_n);
		if (pAp <= 0.0 || !std::isfinite(pAp)) {
			break;
		}
		const double alpha = rz / pAp;
		axpy(alpha, p.ptr(), p_x, p_n);
		axpy(-alpha, Ap.ptr(), r.ptr(), p_n);

		r_norm = l2_norm(r.ptr(), p_n);
		if (r_norm / b_norm_safe < p_tol) {
			++iter;
			break;
		}

		for (int i = 0; i < p_n; ++i) {
			z[i] = p_diag_inv[i] * r[i];
		}
		const double rz_new = dot(r.ptr(), z.ptr(), p_n);
		const double beta = rz_new / (rz != 0.0 ? rz : 1.0);
		for (int i = 0; i < p_n; ++i) {
			p[i] = z[i] + beta * p[i];
		}
		rz = rz_new;
	}

	if (r_residual) {
		*r_residual = r_norm / b_norm_safe;
	}
	return iter;
}

} // namespace cassie_pcg

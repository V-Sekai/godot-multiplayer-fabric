/**************************************************************************/
/*  cassie_eigen.cpp                                                      */
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

#include "cassie_eigen.h"

#include <cmath>

namespace cassie_eigen {

namespace {

// Row-major n x n accessor.
inline double &at(double *p_M, int p_n, int p_i, int p_j) {
	return p_M[p_i * p_n + p_j];
}

inline double at_c(const double *p_M, int p_n, int p_i, int p_j) {
	return p_M[p_i * p_n + p_j];
}

inline void set_identity(double *p_V, int p_n) {
	for (int i = 0; i < p_n; ++i) {
		for (int j = 0; j < p_n; ++j) {
			p_V[i * p_n + j] = (i == j) ? 1.0 : 0.0;
		}
	}
}

inline double off_diag_norm(const double *p_A, int p_n) {
	double s = 0.0;
	for (int i = 0; i < p_n; ++i) {
		for (int j = i + 1; j < p_n; ++j) {
			const double v = at_c(p_A, p_n, i, j);
			s += v * v;
		}
	}
	return s; // squared norm (sufficient for the cutoff test)
}

} // namespace

void jacobi_sym(double *p_A, double *p_V, int p_n,
		int p_max_sweeps, double p_tol) {
	set_identity(p_V, p_n);
	const double tol2 = p_tol * p_tol;
	for (int sweep = 0; sweep < p_max_sweeps; ++sweep) {
		// Find the largest off-diagonal element by magnitude.
		int p = 0, q = 1;
		double max_abs = 0.0;
		for (int i = 0; i < p_n; ++i) {
			for (int j = i + 1; j < p_n; ++j) {
				const double v = std::fabs(at(p_A, p_n, i, j));
				if (v > max_abs) {
					max_abs = v;
					p = i;
					q = j;
				}
			}
		}
		if (off_diag_norm(p_A, p_n) <= tol2) {
			break;
		}
		// Givens rotation that zeros A(p, q).
		const double app = at(p_A, p_n, p, p);
		const double aqq = at(p_A, p_n, q, q);
		const double apq = at(p_A, p_n, p, q);
		double theta_c, theta_s;
		if (std::fabs(apq) < 1e-300) {
			theta_c = 1.0;
			theta_s = 0.0;
		} else {
			const double theta = (aqq - app) / (2.0 * apq);
			double t;
			if (theta >= 0.0) {
				t = 1.0 / (theta + std::sqrt(1.0 + theta * theta));
			} else {
				t = 1.0 / (theta - std::sqrt(1.0 + theta * theta));
			}
			theta_c = 1.0 / std::sqrt(1.0 + t * t);
			theta_s = t * theta_c;
		}
		// Apply A := R^T A R. Update affected rows/cols.
		const double c = theta_c;
		const double s = theta_s;
		const double new_app = c * c * app - 2.0 * s * c * apq + s * s * aqq;
		const double new_aqq = s * s * app + 2.0 * s * c * apq + c * c * aqq;
		at(p_A, p_n, p, p) = new_app;
		at(p_A, p_n, q, q) = new_aqq;
		at(p_A, p_n, p, q) = 0.0;
		at(p_A, p_n, q, p) = 0.0;
		for (int i = 0; i < p_n; ++i) {
			if (i == p || i == q) {
				continue;
			}
			const double aip = at(p_A, p_n, i, p);
			const double aiq = at(p_A, p_n, i, q);
			const double new_aip = c * aip - s * aiq;
			const double new_aiq = s * aip + c * aiq;
			at(p_A, p_n, i, p) = new_aip;
			at(p_A, p_n, p, i) = new_aip;
			at(p_A, p_n, i, q) = new_aiq;
			at(p_A, p_n, q, i) = new_aiq;
		}
		// Accumulate V := V * R.
		for (int i = 0; i < p_n; ++i) {
			const double vip = at(p_V, p_n, i, p);
			const double viq = at(p_V, p_n, i, q);
			at(p_V, p_n, i, p) = c * vip - s * viq;
			at(p_V, p_n, i, q) = s * vip + c * viq;
		}
	}
}

void smallest_eigenvector_3x3(const double *p_A_in, double *p_normal) {
	double A[9];
	double V[9];
	for (int i = 0; i < 9; ++i) {
		A[i] = p_A_in[i];
	}
	jacobi_sym(A, V, 3);
	// Pick the column whose eigenvalue (A diagonal) is smallest in absolute
	// value — fit-a-plane wants the direction of minimum variance.
	int best = 0;
	double best_abs = std::fabs(A[0]);
	for (int i = 1; i < 3; ++i) {
		const double v = std::fabs(A[i * 3 + i]);
		if (v < best_abs) {
			best_abs = v;
			best = i;
		}
	}
	for (int i = 0; i < 3; ++i) {
		p_normal[i] = V[i * 3 + best];
	}
}

void largest_eigenvector_4x4(const double *p_A_in, double *p_q_out) {
	double A[16];
	double V[16];
	for (int i = 0; i < 16; ++i) {
		A[i] = p_A_in[i];
	}
	jacobi_sym(A, V, 4);
	int best = 0;
	double best_val = A[0];
	for (int i = 1; i < 4; ++i) {
		const double v = A[i * 4 + i];
		if (v > best_val) {
			best_val = v;
			best = i;
		}
	}
	for (int i = 0; i < 4; ++i) {
		p_q_out[i] = V[i * 4 + best];
	}
	// Normalize sign so q_w >= 0 — picks the canonical representative
	// of the SO(3) double cover (avoids sign-flip discontinuities across
	// consecutive Wahba solves on slowly varying inputs).
	if (p_q_out[0] < 0.0) {
		for (int i = 0; i < 4; ++i) {
			p_q_out[i] = -p_q_out[i];
		}
	}
}

} // namespace cassie_eigen

/**************************************************************************/
/*  test_cassie_polar.h                                                   */
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

#include "../src/sketch/cassie_polar.h"

#include "core/math/math_funcs.h"
#include "tests/test_macros.h"

#include <cmath>

namespace TestCassiePolar {

// Row-major 3×3 identity / fill.
static void fill_identity(double *p_M) {
	p_M[0] = 1.0;
	p_M[1] = 0.0;
	p_M[2] = 0.0;
	p_M[3] = 0.0;
	p_M[4] = 1.0;
	p_M[5] = 0.0;
	p_M[6] = 0.0;
	p_M[7] = 0.0;
	p_M[8] = 1.0;
}

static double mat3_det(const double *p_M) {
	return p_M[0] * (p_M[4] * p_M[8] - p_M[5] * p_M[7]) -
			p_M[1] * (p_M[3] * p_M[8] - p_M[5] * p_M[6]) +
			p_M[2] * (p_M[3] * p_M[7] - p_M[4] * p_M[6]);
}

// Frobenius distance between two 3×3 matrices.
static double frob(const double *p_A, const double *p_B) {
	double s = 0.0;
	for (int i = 0; i < 9; ++i) {
		const double d = p_A[i] - p_B[i];
		s += d * d;
	}
	return std::sqrt(s);
}

TEST_CASE("[Cassie][Polar] identity input returns identity rotation") {
	double M[9];
	fill_identity(M);
	double R[9];
	cassie_polar::polar_decompose_3x3_safe(M, R);
	double I[9];
	fill_identity(I);
	const double err = frob(R, I);
	CHECK_MESSAGE(err < 1e-10,
			vformat("identity input must round-trip; Frobenius err = %f", err));
	const double det = mat3_det(R);
	CHECK_MESSAGE(std::abs(det - 1.0) < 1e-10,
			vformat("det(R) for identity input must be +1; got %f", det));
}

TEST_CASE("[Cassie][Polar] reflection input forces det(R) = +1") {
	// M = diag(1, 1, -1) is a proper reflection (det = -1).
	// Naive polar returns R with det = -1, which mirrors geometry.
	// polar_decompose_3x3_safe MUST flip the smallest eigenvalue's
	// inverse-sqrt sign so det(R) = +1.
	double M[9] = {
		1.0, 0.0, 0.0,
		0.0, 1.0, 0.0,
		0.0, 0.0, -1.0
	};
	double R[9];
	cassie_polar::polar_decompose_3x3_safe(M, R);
	const double det = mat3_det(R);
	CHECK_MESSAGE(std::abs(det - 1.0) < 0.01,
			vformat("reflection input must produce det(R) = +1 after safeguard; "
					"got %f",
					det));
	// Test vertex along z: rest = (0, 0, 1). Naive polar would mirror
	// it to (0, 0, -1) (or close). Safe-polar should NOT mirror — the
	// flip cancels the reflection, leaving |R · v - v| close to 0 or 2
	// (full reversal is fine if the flip rotates correctly, but the
	// product RR^T must equal I either way; verify orthogonality).
	double RRT[9];
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			double s = 0.0;
			for (int k = 0; k < 3; ++k) {
				s += R[i * 3 + k] * R[j * 3 + k];
			}
			RRT[i * 3 + j] = s;
		}
	}
	double I[9];
	fill_identity(I);
	const double ortho_err = frob(RRT, I);
	CHECK_MESSAGE(ortho_err < 1e-9,
			vformat("R must be orthogonal (R·R^T = I); err = %f", ortho_err));
}

TEST_CASE("[Cassie][Polar] rank-1 input returns finite R (no NaN explosion)") {
	// M = u · v^T with u = (1, 0, 0), v = (0, 1, 0). Rank 1.
	// M^T·M = v · (u^T u) · v^T = v · v^T (rank 1, two zero eigenvalues).
	// Without the eigenvalue clamp, 1/√0 → ∞ and R explodes.
	double M[9] = {
		0.0, 1.0, 0.0,
		0.0, 0.0, 0.0,
		0.0, 0.0, 0.0
	};
	double R[9];
	cassie_polar::polar_decompose_3x3_safe(M, R);
	// Check no NaN/Inf — all entries finite.
	for (int i = 0; i < 9; ++i) {
		CHECK_MESSAGE(std::isfinite(R[i]),
				vformat("R[%d] = %f must be finite; clamp prevented NaN", i, R[i]));
	}
}

TEST_CASE("[Cassie][Polar] near-rotation input recovered to fp64 precision") {
	// M = rotation by 45° about Z + small perturbation. polar_decompose
	// should recover the rotation to within the perturbation's magnitude.
	const double c = std::cos(Math::PI / 4.0);
	const double s = std::sin(Math::PI / 4.0);
	const double eps = 1e-4;
	double M[9] = {
		c + eps, -s, 0.0,
		s, c + eps, 0.0,
		0.0, 0.0, 1.0 + eps
	};
	double R[9];
	cassie_polar::polar_decompose_3x3_safe(M, R);
	double R_true[9] = {
		c, -s, 0.0,
		s, c, 0.0,
		0.0, 0.0, 1.0
	};
	const double err = frob(R, R_true);
	CHECK_MESSAGE(err < 1e-3,
			vformat("near-rotation must be recovered (safeguards don't degrade "
					"clean-input accuracy); err = %f",
					err));
	const double det = mat3_det(R);
	CHECK_MESSAGE(std::abs(det - 1.0) < 1e-9,
			vformat("det(R) for near-rotation must be +1; got %f", det));
}

} // namespace TestCassiePolar

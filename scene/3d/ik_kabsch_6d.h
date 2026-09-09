/**************************************************************************/
/*  ik_kabsch_6d.h                                                        */
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

#include "core/math/basis.h"
#include "core/math/vector3.h"

// Kabsch/Procrustes 6-DOF rotation fit. Recovers the rotation R that minimizes
// sum |R * rest_i - tgt_i|^2 via a 3x3 SVD with the reflection-killing determinant
// correction (== Umeyama 1991 / numpy gesdd). Given a bone's rest child directions and
// their solved (target) directions, it recovers the bone's FULL rotation -- swing AND
// twist (>= 2 non-colinear correspondences) -- which a single-child aim cannot.
//
// Ported from the verified shader-motion fullbody_6d solver (matches numpy gesdd to 1e-14,
// in-engine self-test < 1 fm). Kept engine-internal for the IK rotational-matching pass.
class IKKabsch6D {
	// 3x3 SVD via Hestenes one-sided Jacobi; descending-sigma sort + cross-product
	// completion of rank-deficient columns. r_u / r_v are returned column-major.
	static void svd3(const double p_m[3][3], double r_u[3][3], double r_v[3][3], double r_sigma[3]);
	static void set_cross(double r_u[3][3], int p_j, int p_j1, int p_j2);

public:
	// Best-fit proper rotation mapping the rest vectors onto the target vectors.
	static Basis kabsch(const Vector3 *p_rest, const Vector3 *p_tgt, int p_count);
};

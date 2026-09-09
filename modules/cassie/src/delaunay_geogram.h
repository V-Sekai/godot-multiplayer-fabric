/**************************************************************************/
/*  delaunay_geogram.h                                                    */
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

// Windows headers leak LEFT/RIGHT/UP/DOWN/INF macros that collide
// with Godot's core/math symbols.  Undefine them before the includes.
#ifdef LEFT
#undef LEFT
#endif
#ifdef RIGHT
#undef RIGHT
#endif
#ifdef UP
#undef UP
#endif
#ifdef DOWN
#undef DOWN
#endif
#ifdef INF
#undef INF
#endif
#ifdef I
#undef I
#endif
#ifdef J
#undef J
#endif
#ifdef K
#undef K
#endif

// Geogram-backed Delaunay shims that replace Godot's built-in
// Delaunay2D::triangulate / Delaunay3D::tetrahedralize. Backed by
// GEO::Delaunay::create(3, "BDEL") / create(2, "BDEL2d"). Mirrors the
// proven cassie-triangulation/src/Utility/DelaunayFaces wrapper but
// produces godot-cpp containers instead of std::vector<>.

#include "core/templates/vector.h"
#include "core/variant/variant.h"

namespace cassie {

// Triangle output for the 2D path (replaces Delaunay2D::Triangle).
struct DelaunayTriangle2D {
	int points[3];
};

// Tetrahedron output for the 3D path (replaces Delaunay3D::OutputSimplex).
struct DelaunayTet3D {
	int points[4];
};

// 2D Delaunay triangulation. Returns triangles whose three indices
// refer back into p_points. Empty result on degenerate input
// (fewer than 3 points or all colinear).
Vector<DelaunayTriangle2D> delaunay_triangulate_2d(const PackedVector2Array &p_points);

// 3D Delaunay tetrahedralization. Returns tetrahedra whose four
// indices refer back into p_points. Empty result on degenerate
// input (fewer than 4 points or all coplanar).
Vector<DelaunayTet3D> delaunay_tetrahedralize_3d(const PackedVector3Array &p_points);

// Non-godot-cpp overload for callers that have stride-3 doubles
// already (e.g. mwt::DMWT's coplanar branch). `p_xy_coords` has
// length 2*n_points; each consecutive pair is (x, y).
//
// Returns the flat face index list (`*out_face_indices`, length
// 3 * *out_face_count) — caller owns and must `delete[]` it.
// Returns false on degenerate input.
bool delaunay_triangulate_2d_raw(const double *p_xy_coords, int n_points,
		int **out_face_indices, int *out_face_count);

} // namespace cassie

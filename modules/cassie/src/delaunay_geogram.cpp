/**************************************************************************/
/*  delaunay_geogram.cpp                                                  */
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

#include "delaunay_geogram.h"

// Implementation now backed by Godot's built-in Delaunay2D and Delaunay3D.
// Godot uses R128 (128-bit fixed-point) for circumsphere tests — strictly more
// precise than Geogram's double arithmetic. This fixes the 1mm-scale failure
// where Geogram's double precision produced "No solution!" from DMWT.

#include "core/math/delaunay_2d.h"
#include "core/math/delaunay_3d.h"

namespace cassie {

Vector<DelaunayTriangle2D>
delaunay_triangulate_2d(const PackedVector2Array &p_points) {
	Vector<DelaunayTriangle2D> out;
	const int n = p_points.size();
	if (n < 3) {
		return out;
	}
	Vector<Vector2> pts;
	pts.resize(n);
	for (int i = 0; i < n; ++i) {
		pts.write[i] = p_points[i];
	}
	Vector<Delaunay2D::Triangle> tris = Delaunay2D::triangulate(pts);
	out.resize(tris.size());
	for (int i = 0; i < tris.size(); ++i) {
		out.write[i].points[0] = tris[i].points[0];
		out.write[i].points[1] = tris[i].points[1];
		out.write[i].points[2] = tris[i].points[2];
	}
	return out;
}

Vector<DelaunayTet3D>
delaunay_tetrahedralize_3d(const PackedVector3Array &p_points) {
	Vector<DelaunayTet3D> out;
	const int n = p_points.size();
	if (n < 4) {
		return out;
	}
	Vector<Vector3> pts;
	pts.resize(n);
	for (int i = 0; i < n; ++i) {
		pts.write[i] = p_points[i];
	}
	Vector<Delaunay3D::OutputSimplex> tets = Delaunay3D::tetrahedralize(pts);
	out.resize(tets.size());
	for (int i = 0; i < tets.size(); ++i) {
		out.write[i].points[0] = int(tets[i].points[0]);
		out.write[i].points[1] = int(tets[i].points[1]);
		out.write[i].points[2] = int(tets[i].points[2]);
		out.write[i].points[3] = int(tets[i].points[3]);
	}
	return out;
}

bool delaunay_triangulate_2d_raw(const double *p_xy_coords, int n_points,
		int **out_face_indices, int *out_face_count) {
	*out_face_indices = nullptr;
	*out_face_count = 0;
	if (p_xy_coords == nullptr || n_points < 3) {
		return false;
	}
	Vector<Vector2> pts;
	pts.resize(n_points);
	for (int i = 0; i < n_points; ++i) {
		pts.write[i] = Vector2(
				float(p_xy_coords[i * 2]),
				float(p_xy_coords[i * 2 + 1]));
	}
	Vector<Delaunay2D::Triangle> tris = Delaunay2D::triangulate(pts);
	if (tris.is_empty()) {
		return false;
	}
	const int nf = tris.size();
	int *indices = new int[std::size_t(nf) * 3u];
	for (int i = 0; i < nf; ++i) {
		indices[i * 3 + 0] = tris[i].points[0];
		indices[i * 3 + 1] = tris[i].points[1];
		indices[i * 3 + 2] = tris[i].points[2];
	}
	*out_face_indices = indices;
	*out_face_count = nf;
	return true;
}

} // namespace cassie

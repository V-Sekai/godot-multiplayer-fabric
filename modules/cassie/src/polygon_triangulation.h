/**************************************************************************/
/*  polygon_triangulation.h                                               */
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

#include "core/object/ref_counted.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

namespace mwt {
class DMWT;
}

// Godot-cpp wrapper around mwt::DMWT (the cassie-triangulation
// algorithm). Replaces the previous in-tree V-Sekai DMWT — that
// DMWT couldn't tolerate MingCurve's perturbation points and
// segfaulted on thin elongated boundaries (e.g. cassie-triangulation
// tests/shapes.cpp's "sleeve"). Backed by the cassie-triangulation
// implementation, MingCurve + DMWT is a validated combination that
// handles the full input distribution CASSIE users draw.
//
// Class name and public method names match the engine-module
// PolygonTriangulation class so existing consumers
// (PolygonTriangulationGodot, CassieSurface, CassieTriangulator)
// keep working without churn.
class PolygonTriangulation : public RefCounted {
	GDCLASS(PolygonTriangulation, RefCounted);

protected:
	static void _bind_methods();

public:
	static Ref<PolygonTriangulation> _create_with_degenerates(int ptn, double *pts, double *deGenPts, bool isdegen);
	static Ref<PolygonTriangulation> _create_with_normals(int ptn, double *pts, double *deGenPts, float *norms, bool isdegen);

	PolygonTriangulation();
	~PolygonTriangulation();

	void preprocess();
	bool start();
	void set_weights(float wtri, float wedge, float wbitri, float wtribd, float wwst);
	void statistics();
	void set_round(int r);
	void set_dot(bool isdot1);
	void clear_tiling();
	void set_point_limit(int limit);

	void get_result(double **outFaces, int *outNum, double **outPoints,
			float **outNorms, int *outPn, bool dosmooth, int subd, int laps);

	// Fill flat Godot Vector<int> with out_tri_count * 3 vertex indices.
	void get_indexed_result(Vector<int> &out_face_indices,
			int &out_tri_count, int &out_point_count) const;

	// Internal accessor — not bound to GDScript.
	mwt::DMWT *get_dmwt() const { return dmwt; }

	// Public for parity with the engine-module class (consumers read
	// these directly). Kept in sync with the internal mwt::DMWT.
	float optimalCost;
	bool EXPSTOP;

private:
	mwt::DMWT *dmwt;
};

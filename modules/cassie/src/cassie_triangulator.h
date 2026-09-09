/**************************************************************************/
/*  cassie_triangulator.h                                                 */
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
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

// Single-call multipolygon triangulator + refinement, mirroring the
// `Triangulate(double*, int, float, double**, int**, int*, int*)` flat C
// ABI from E:\cassie-triangulation\src\Triangulation.cpp, but exposed
// through godot-cpp instead of `extern "C"`.
//
// Pipeline:
//   1. Serialize concurrent callers (Geogram has process-global RNG state).
//   2. Reset the Point3 perturb RNG for call-to-call determinism.
//   3. nB == 3 fast path: skip DMWT, refine the input triangle directly.
//   4. Otherwise: MingCurve edge protection -> DMWT -> cassie_remesh.
//
// Returns a Dictionary with keys:
//   - "success":  bool
//   - "vertices": PackedVector3Array
//   - "faces":    PackedInt32Array (length 3 * num_triangles)
class CassieTriangulator : public RefCounted {
	GDCLASS(CassieTriangulator, RefCounted);

protected:
	static void _bind_methods();

public:
	static Dictionary triangulate(const PackedVector3Array &p_boundary, float p_target_edge_length);

	CassieTriangulator() = default;
	~CassieTriangulator() = default;
};

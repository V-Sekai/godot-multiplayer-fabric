/**************************************************************************/
/*  cassie_beautifier.h                                                   */
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

#include "cassie_beautifier_params.h"
#include "constraints/cassie_constraint.h"
#include "constraints/cassie_intersection_constraint.h"
#include "constraints/cassie_mirror_plane_constraint.h"
#include "sketch/cassie_final_stroke.h"
#include "sketch/cassie_input_stroke.h"

#include "core/io/resource.h"
#include "core/math/plane.h"
#include "core/object/ref_counted.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"
#include "scene/resources/curve.h"

// CassieSketchContext aggregates the existing-state inputs the beautifier
// needs: prior strokes (to detect intersections against), the mirror plane
// configuration, and orthogonal snap directions for planarity.
class CassieSketchContext : public Resource {
	GDCLASS(CassieSketchContext, Resource);

	TypedArray<CassieFinalStroke> existing_strokes;
	PackedVector3Array ortho_directions;
	Plane mirror_plane;
	bool mirror_enabled = false;
	Callable project_on_patch_callback;

protected:
	static void _bind_methods();

public:
	CassieSketchContext() = default;

	void set_existing_strokes(const TypedArray<CassieFinalStroke> &p_strokes) { existing_strokes = p_strokes; }
	TypedArray<CassieFinalStroke> get_existing_strokes() const { return existing_strokes; }

	void set_ortho_directions(const PackedVector3Array &p_dirs) { ortho_directions = p_dirs; }
	PackedVector3Array get_ortho_directions() const { return ortho_directions; }

	void set_mirror_plane(const Plane &p_plane) { mirror_plane = p_plane; }
	Plane get_mirror_plane() const { return mirror_plane; }

	void set_mirror_enabled(bool p_v) { mirror_enabled = p_v; }
	bool is_mirror_enabled() const { return mirror_enabled; }

	void set_project_on_patch_callback(const Callable &p_callback) { project_on_patch_callback = p_callback; }
	Callable get_project_on_patch_callback() const { return project_on_patch_callback; }
};

// CassieBeautifier wraps the Tier-1…3 funnel into a single GDScript-facing
// entry point. Port of E:/cassie/Assets/Scripts/Create/Sketch/Beautify/
// Beautifier.cs Beautify() (lines 67–317), simplified to the essentials:
//   1. fit the input stroke into a Curve3D
//   2. detect intersections against context.existing_strokes
//   3. detect mirror-plane crossing when mirror_enabled
//   4. run CassieConstraintSolver with the collected constraints
//   5. return Dictionary with the beautified curve and metadata
class CassieBeautifier : public Resource {
	GDCLASS(CassieBeautifier, Resource);

protected:
	static void _bind_methods();

public:
	CassieBeautifier() = default;

	// Result keys:
	//   "curve"               : Curve3D (beautified)
	//   "intersections"       : TypedArray<CassieIntersectionConstraint>
	//   "mirror_constraints"  : TypedArray<CassieMirrorPlaneConstraint>
	//   "applied_anchors"     : PackedInt32Array
	//   "rejected_count"      : int
	//   "planar"              : bool
	//   "is_closed_loop"      : bool
	//   "is_valid"            : bool (false if the stroke failed the
	//                           is_valid threshold and no work was done)
	//   "is_short_or_linear"  : bool (line-fit fast path used)
	Dictionary beautify(
			const Ref<CassieInputStroke> &p_stroke,
			const Ref<CassieSketchContext> &p_context,
			const Ref<CassieBeautifierParams> &p_params,
			bool p_fit_to_constraints,
			bool p_mirror);
};

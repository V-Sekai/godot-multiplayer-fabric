/**************************************************************************/
/*  cassie_curvenet_knot.h                                                */
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

#include "core/io/resource.h"
#include "core/math/transform_3d.h"
#include "core/variant/variant.h"

// CassieCurvenetKnot is the per-knot animation primitive — the analog of
// Pixar's "Curvenet Adjustment" from Nguyen et al., "Shaping the Elements"
// (SIGGRAPH '23 Talks). Each knot exposes translate / rotate / scale
// direct manipulators with surface-aligned orientation; the orientation is
// computed by CassieCurvenet::compute_orientations() (Step 1.3, ENG-45)
// from the projection-pose vs rest-pose tangent set.
//
// Fields are stored in two poses:
//
//   - projection pose: the snapshot taken when the curvenet binds to the
//     base mesh (the "T-pose" the paper names). Persistent — the binding
//     never re-runs after build_from_graph.
//   - rest pose: the current frame's deformation. Re-evaluated every time
//     a knot transform changes, by update_rest_pose() + compute_orientations().
//
// is_intersection is true when the graph node hosting this knot has
// degree ≥ 3 (the Pixar definition of a curvenet intersection). The Step
// 1.3 algorithm branches on this flag: intersections solve Wahba's
// problem from the tangent sets; non-intersections parallel-transport
// from the nearest two intersections and inverse-distance blend.
class CassieCurvenetKnot : public Resource {
	GDCLASS(CassieCurvenetKnot, Resource);

	int graph_node_id = -1;
	Transform3D projection_pose_transform;
	Transform3D rest_pose_transform;
	bool is_intersection = false;
	PackedVector3Array projection_pose_tangents;
	PackedVector3Array rest_pose_tangents;
	bool exposes_translate = true;
	bool exposes_rotate = true;
	bool exposes_scale = false;
	bool needs_setup = false;

protected:
	static void _bind_methods();

public:
	CassieCurvenetKnot() = default;

	void set_graph_node_id(int p_id) { graph_node_id = p_id; }
	int get_graph_node_id() const { return graph_node_id; }

	void set_projection_pose_transform(const Transform3D &p_t) { projection_pose_transform = p_t; }
	Transform3D get_projection_pose_transform() const { return projection_pose_transform; }

	void set_rest_pose_transform(const Transform3D &p_t) { rest_pose_transform = p_t; }
	Transform3D get_rest_pose_transform() const { return rest_pose_transform; }

	void set_is_intersection(bool p_v) { is_intersection = p_v; }
	bool get_is_intersection() const { return is_intersection; }

	void set_projection_pose_tangents(const PackedVector3Array &p_t) { projection_pose_tangents = p_t; }
	PackedVector3Array get_projection_pose_tangents() const { return projection_pose_tangents; }

	void set_rest_pose_tangents(const PackedVector3Array &p_t) { rest_pose_tangents = p_t; }
	PackedVector3Array get_rest_pose_tangents() const { return rest_pose_tangents; }

	void set_exposes_translate(bool p_v) { exposes_translate = p_v; }
	bool get_exposes_translate() const { return exposes_translate; }
	void set_exposes_rotate(bool p_v) { exposes_rotate = p_v; }
	bool get_exposes_rotate() const { return exposes_rotate; }
	void set_exposes_scale(bool p_v) { exposes_scale = p_v; }
	bool get_exposes_scale() const { return exposes_scale; }

	void set_needs_setup(bool p_v) { needs_setup = p_v; }
	bool get_needs_setup() const { return needs_setup; }

	// Returns the manipulator's world-space transform — the user-facing
	// gizmo basis. For Step 1.2 this just returns rest_pose_transform;
	// Step 1.3 populates rest_pose_transform by running the Pixar
	// orientation algorithm on the tangent arrays. Kept as a separate
	// method so future implementations can fold in scale handling without
	// changing callers.
	Transform3D solve_world_transform() const { return rest_pose_transform; }
};

/**************************************************************************/
/*  cassie_intersection_finder.h                                          */
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

#include "../sketch/cassie_final_stroke.h"
#include "cassie_intersection_constraint.h"
#include "cassie_mirror_plane_constraint.h"

#include "core/math/plane.h"
#include "core/math/vector3.h"
#include "core/templates/local_vector.h"
#include "core/templates/vector.h"
#include "core/variant/callable.h"
#include "core/variant/typed_array.h"
#include "scene/resources/curve.h"

// Tier 2 constraint detection helpers.
// Free functions in the global namespace, matching the cassie_remesh /
// cassie_fit_curve style established in modules/cassie/src/.

// Walks p_new tessellated into a polyline, then for each existing stroke
// tessellated similarly, finds segment-segment proximities below
// p_proximity_threshold and emits one IntersectionConstraint per cluster.
// When an intersection lies within p_snap_to_node_threshold of an anchor
// on the existing curve, snap-to-node is recorded on the constraint.
TypedArray<CassieIntersectionConstraint> cassie_find_intersections(
		const Ref<Curve3D> &p_new,
		const TypedArray<CassieFinalStroke> &p_existing,
		real_t p_proximity_threshold,
		real_t p_snap_to_node_threshold,
		int p_tessellate_max_stages = 5,
		real_t p_tessellate_tolerance = 0.05f);

// If the new curve crosses the mirror plane within p_proximity_threshold of
// the plane (segment endpoints straddle the plane within tolerance),
// returns a single MirrorPlaneConstraint at the crossing point. Otherwise
// returns null.
Ref<CassieMirrorPlaneConstraint> cassie_detect_mirror_plane_intersection(
		const Ref<Curve3D> &p_new,
		const Plane &p_mirror,
		real_t p_proximity_threshold,
		int p_tessellate_max_stages = 5,
		real_t p_tessellate_tolerance = 0.05f);

// Tier 4 slice: split p_stroke at each constraint's old_curve_offset. The
// offsets are interpreted on the stroke's own curve (baked-curve length in
// meters, matching CassieFinalStroke::get_constraint), converted to
// chord-uniform t ∈ [0, 1], sorted ascending, and fed to
// cassie_curve_split_for_constraints. Each output substroke's Curve3D is
// wrapped in a new CassieFinalStroke; metadata other than the curve (id,
// input samples, closed-loop flag) is NOT carried — the curvenet graph
// layer owns substroke identity.
//
// Returns a single-element array containing p_stroke unchanged when the
// constraint list is empty or the stroke has no curve.
TypedArray<CassieFinalStroke> cassie_split_stroke_at_constraints(
		const Ref<CassieFinalStroke> &p_stroke,
		const TypedArray<CassieIntersectionConstraint> &p_constraints,
		real_t p_snap_threshold);

// Surface-proximity detection. p_patch_projection_callback is a Callable
// of the form `func(Vector3 point) -> Dictionary { "on_surface": bool,
// "patch_id": int, "projected": Vector3 }`. Returns a list of patch IDs the
// stroke entered along with the entry positions; the Tier 2 implementation
// is conservative and only fires one event per contiguous run of samples on
// the same patch.
TypedArray<Vector3> cassie_detect_surface_proximity(
		const Ref<Curve3D> &p_new,
		const Callable &p_patch_projection_callback,
		int p_tessellate_max_stages = 5,
		real_t p_tessellate_tolerance = 0.05f);

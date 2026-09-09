/**************************************************************************/
/*  cassie_edge_collider.h                                                */
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

#include "cassie_surface_patch.h"

#include "core/object/ref_counted.h"
#include "core/variant/array.h"
#include "scene/resources/curve.h"

// CassieEdgeCollider — ENG-55 (Track 4 step 4.3). Discrete-proximity
// edge-edge collision between a stroke (a Curve3D's segments) and an
// obstacle CassieSurfacePatch (every mesh edge). Per-frame, not CCD.
//
// Algorithm (per query):
//   1. Sample the stroke at fixed arc-length spacing.
//   2. For each stroke segment (sample_i, sample_{i+1}), query the patch's
//      DynamicBVH with an inflated AABB to find candidate triangles.
//   3. For each candidate triangle's three edges, call
//      Geometry3D::get_closest_points_between_segments to recover the
//      closest-point pair + scalar distance.
//   4. When distance < proximity_threshold, emit a penetration record:
//      { "control_pt_idx", "displacement" } where the displacement pushes
//      the stroke sample out along (stroke_point - mesh_point).
//
// The downstream solver path can convert each penetration record into a
// PositionConstraint (HardConstraintBlock) — same shape the rest of the
// solver already consumes.
class CassieEdgeCollider : public RefCounted {
	GDCLASS(CassieEdgeCollider, RefCounted);

	Ref<CassieSurfacePatch> patch;
	real_t proximity_threshold = real_t(0.05);
	int samples_per_segment = 8;

protected:
	static void _bind_methods();

public:
	CassieEdgeCollider() = default;

	void set_patch(const Ref<CassieSurfacePatch> &p_patch) { patch = p_patch; }
	Ref<CassieSurfacePatch> get_patch() const { return patch; }

	void set_proximity_threshold(real_t p_v) { proximity_threshold = p_v; }
	real_t get_proximity_threshold() const { return proximity_threshold; }

	void set_samples_per_segment(int p_v) { samples_per_segment = p_v; }
	int get_samples_per_segment() const { return samples_per_segment; }

	// Returns an Array of Dictionary entries:
	//   { "sample_t": real_t,         -- parameter along the curve [0, 1]
	//     "stroke_point": Vector3,    -- the sampled stroke position
	//     "mesh_point": Vector3,      -- the closest mesh-edge point
	//     "displacement": Vector3,    -- push from mesh toward stroke,
	//                                    magnitude = (threshold - distance)
	//     "penetration_depth": real_t }
	// One entry per penetrating sample (deduped across mesh edges; the
	// deepest displacement at a given sample wins).
	Array find_penetrations(const Ref<Curve3D> &p_curve) const;
};

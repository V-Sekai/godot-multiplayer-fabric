/**************************************************************************/
/*  cassie_input_stroke.h                                                 */
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
#include "core/math/vector3.h"
#include "core/object/ref_counted.h"
#include "core/templates/local_vector.h"
#include "core/variant/typed_array.h"
#include "core/variant/variant.h"

// TypedArray<CassieSurfaceConstraint> below needs the complete type (GetTypeInfo),
// so this must be a full include, not a forward declaration.
#include "../constraints/cassie_surface_constraint.h"

class CassieConstraint;

// CassieInputStroke owns the raw sample buffer produced by a single
// stroke (one trigger press in XR, one mouse drag on flatscreen).
// Port of E:\cassie\Assets\Scripts\Data\Strokes\InputStroke.cs.
//
// Tier 1 covers: add_sample, get_points, get_safe_points, get_weights,
// is_valid, average_drawing_speed, get_g1_sections.
// Tier 2 will add: add_constraint, in_constrain_to_surface,
// out_constrain_to_surface (deferred to the constraint detection tier).

struct CassieSample {
	Vector3 position;
	float creation_time = 0.0f;
	float pressure = 0.0f;

	CassieSample() = default;
	CassieSample(const Vector3 &p_pos, float p_time, float p_pressure) :
			position(p_pos), creation_time(p_time), pressure(p_pressure) {}
};

class CassieInputStroke : public Resource {
	GDCLASS(CassieInputStroke, Resource);

	LocalVector<CassieSample> samples;
	real_t length = 0.0;
	TypedArray<CassieConstraint> constraints;
	TypedArray<CassieSurfaceConstraint> surface_constraints;

protected:
	static void _bind_methods();

public:
	CassieInputStroke() = default;

	void add_sample(const Vector3 &p_position, float p_creation_time, float p_pressure);

	int get_sample_count() const { return int(samples.size()); }
	real_t get_length() const { return length; }

	// Returns the position of every sample in order.
	PackedVector3Array get_points(float p_rdp_error = 0.0f) const;

	// Returns positions of samples after the temporal-ablation pass:
	// drops samples whose creation_time is within p_ablation_duration of
	// the stroke's start or end. Endpoint samples are always kept.
	// Returns the full sample positions if the ablation window would empty
	// the result.
	PackedVector3Array get_safe_points(float p_ablation_duration = 0.01f) const;

	// G1 sectioning + hook removal. Splits the temporally-safe sample
	// list at angular discontinuities and trims short hook segments at
	// both ends. Returns a TypedArray<PackedVector3Array> where each
	// entry is one G1-continuous section.
	TypedArray<PackedVector3Array> get_g1_sections(
			float p_discontinuity_angular_threshold,
			float p_hook_discontinuity_angular_threshold,
			float p_ablation_duration,
			float p_min_section_length,
			float p_max_hook_length,
			float p_max_hook_stroke_ratio) const;

	float average_drawing_speed() const;

	PackedFloat32Array get_weights() const;

	bool is_valid(float p_min_sketching_time, float p_min_stroke_size) const;

	void clear();

	// Tier 2 — Constraint accumulation.
	//
	// add_constraint runs the priority-dedup port of
	// InputStroke.AddConstraint: if the new candidate is within
	// p_proximity_threshold of the last constraint, the older one is
	// dropped unless it is a curve/curve intersection that the newer one
	// is not. Curve/curve intersections also outrank when the older was
	// at-node and the newer is not. Consecutive mirror-plane constraints
	// within 2 × p_proximity_threshold collapse.
	void add_constraint(const Ref<CassieConstraint> &p_constraint, real_t p_proximity_threshold);

	TypedArray<CassieConstraint> get_constraints() const { return constraints; }
	int get_constraint_count() const { return constraints.size(); }

	// Begins / continues / ends a SurfaceConstraint for the given patch.
	// Mirrors InConstrainToSurface / OutConstrainToSurface.
	void in_constrain_to_surface(int p_patch_id, const Vector3 &p_position);
	void out_constrain_to_surface(int p_patch_id, const Vector3 &p_position);

	TypedArray<CassieSurfaceConstraint> get_surface_constraints() const { return surface_constraints; }
};

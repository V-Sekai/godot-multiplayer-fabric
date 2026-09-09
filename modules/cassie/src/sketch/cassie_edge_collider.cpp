/**************************************************************************/
/*  cassie_edge_collider.cpp                                              */
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

#include "cassie_edge_collider.h"

#include "core/math/geometry_3d.h"
#include "core/object/class_db.h"

namespace {

struct PenetrationCandidate {
	Vector3 stroke_point;
	Vector3 mesh_point;
	Vector3 displacement;
	real_t depth = real_t(0.0);
	real_t sample_t = real_t(0.0);
	bool valid = false;
};

} // namespace

Array CassieEdgeCollider::find_penetrations(const Ref<Curve3D> &p_curve) const {
	Array out;
	if (patch.is_null() || p_curve.is_null() ||
			p_curve->get_baked_length() <= real_t(0.0) ||
			proximity_threshold <= real_t(0.0)) {
		return out;
	}

	const int tri_count = patch->get_triangle_count();
	if (tri_count == 0) {
		return out;
	}

	const real_t L = p_curve->get_baked_length();
	const int total_samples = MAX(samples_per_segment, 2);
	LocalVector<Vector3> stroke_samples;
	LocalVector<real_t> stroke_sample_t;
	stroke_samples.resize(total_samples);
	stroke_sample_t.resize(total_samples);
	for (int i = 0; i < total_samples; ++i) {
		const real_t t = real_t(i) / real_t(total_samples - 1);
		stroke_sample_t[i] = t;
		stroke_samples[i] = p_curve->sample_baked(t * L);
	}

	// For each stroke sample, find the deepest penetration across every
	// triangle's edges. Brute-force over triangles — the patch's BVH is
	// designed for point-projection (single closest-point), not for
	// "every triangle within radius of a segment". For typical demo-scale
	// patches this is fast; for ~100k triangles add a per-sample BVH
	// query as a follow-up.
	LocalVector<PenetrationCandidate> per_sample;
	per_sample.resize(total_samples);

	for (int s = 0; s < total_samples - 1; ++s) {
		const Vector3 a0 = stroke_samples[s];
		const Vector3 a1 = stroke_samples[s + 1];
		for (int t = 0; t < tri_count; ++t) {
			const Vector3i tri = patch->get_triangle_indices(t);
			if (tri.x < 0) {
				continue;
			}
			const Vector3 v0 = patch->get_vertex_position(tri.x);
			const Vector3 v1 = patch->get_vertex_position(tri.y);
			const Vector3 v2 = patch->get_vertex_position(tri.z);
			const Vector3 edges[3][2] = {
				{ v0, v1 },
				{ v1, v2 },
				{ v2, v0 },
			};
			for (int e = 0; e < 3; ++e) {
				Vector3 ps;
				Vector3 qt;
				Geometry3D::get_closest_points_between_segments(
						a0, a1, edges[e][0], edges[e][1], ps, qt);
				const real_t d = ps.distance_to(qt);
				if (d >= proximity_threshold) {
					continue;
				}
				const real_t depth = proximity_threshold - d;
				// Determine which endpoint of the stroke segment is closer
				// to the penetration midpoint to decide which sample index
				// owns the record.
				const real_t t_along = a1 == a0 ? real_t(0.0) : CLAMP((ps - a0).dot(a1 - a0) / (a1 - a0).length_squared(), real_t(0.0), real_t(1.0));
				const int owner = (t_along < real_t(0.5)) ? s : s + 1;
				PenetrationCandidate &slot = per_sample[owner];
				if (!slot.valid || depth > slot.depth) {
					slot.valid = true;
					slot.depth = depth;
					slot.stroke_point = ps;
					slot.mesh_point = qt;
					const Vector3 dir_raw = ps - qt;
					if (dir_raw.length_squared() > real_t(1e-12)) {
						slot.displacement = dir_raw.normalized() * depth;
					} else {
						// Stroke point coincides with mesh point — fall
						// back to the triangle normal.
						slot.displacement = patch->get_triangle_normal(t) * depth;
					}
					slot.sample_t = stroke_sample_t[owner];
				}
			}
		}
	}

	for (int s = 0; s < total_samples; ++s) {
		if (!per_sample[s].valid) {
			continue;
		}
		Dictionary d;
		d["sample_t"] = per_sample[s].sample_t;
		d["stroke_point"] = per_sample[s].stroke_point;
		d["mesh_point"] = per_sample[s].mesh_point;
		d["displacement"] = per_sample[s].displacement;
		d["penetration_depth"] = per_sample[s].depth;
		out.push_back(d);
	}
	return out;
}

void CassieEdgeCollider::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_patch", "patch"), &CassieEdgeCollider::set_patch);
	ClassDB::bind_method(D_METHOD("get_patch"), &CassieEdgeCollider::get_patch);
	ClassDB::bind_method(D_METHOD("set_proximity_threshold", "value"),
			&CassieEdgeCollider::set_proximity_threshold);
	ClassDB::bind_method(D_METHOD("get_proximity_threshold"),
			&CassieEdgeCollider::get_proximity_threshold);
	ClassDB::bind_method(D_METHOD("set_samples_per_segment", "value"),
			&CassieEdgeCollider::set_samples_per_segment);
	ClassDB::bind_method(D_METHOD("get_samples_per_segment"),
			&CassieEdgeCollider::get_samples_per_segment);
	ClassDB::bind_method(D_METHOD("find_penetrations", "curve"),
			&CassieEdgeCollider::find_penetrations);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "patch", PROPERTY_HINT_RESOURCE_TYPE,
						 "CassieSurfacePatch"),
			"set_patch", "get_patch");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "proximity_threshold"),
			"set_proximity_threshold", "get_proximity_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "samples_per_segment"),
			"set_samples_per_segment", "get_samples_per_segment");
}

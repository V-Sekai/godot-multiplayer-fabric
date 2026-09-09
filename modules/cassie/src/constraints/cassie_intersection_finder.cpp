/**************************************************************************/
/*  cassie_intersection_finder.cpp                                        */
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

#include "cassie_intersection_finder.h"

#include "../curves/cassie_curve_fit.h"

#include "core/math/geometry_3d.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

namespace {

PackedVector3Array tessellate_polyline(const Ref<Curve3D> &p_curve,
		int p_max_stages, real_t p_tolerance) {
	PackedVector3Array empty;
	if (p_curve.is_null() || p_curve->get_point_count() < 2) {
		return empty;
	}
	return p_curve->tessellate(p_max_stages, p_tolerance);
}

} // namespace

TypedArray<CassieIntersectionConstraint> cassie_find_intersections(
		const Ref<Curve3D> &p_new,
		const TypedArray<CassieFinalStroke> &p_existing,
		real_t p_proximity_threshold,
		real_t p_snap_to_node_threshold,
		int p_tessellate_max_stages,
		real_t p_tessellate_tolerance) {
	TypedArray<CassieIntersectionConstraint> out;
	if (p_new.is_null() || p_existing.is_empty()) {
		return out;
	}
	const PackedVector3Array new_poly = tessellate_polyline(p_new,
			p_tessellate_max_stages, p_tessellate_tolerance);
	if (new_poly.size() < 2) {
		return out;
	}

	const real_t threshold_sq = p_proximity_threshold * p_proximity_threshold;

	for (int s_idx = 0; s_idx < p_existing.size(); ++s_idx) {
		Ref<CassieFinalStroke> existing = p_existing[s_idx];
		if (existing.is_null() || existing->get_curve().is_null()) {
			continue;
		}
		const PackedVector3Array old_poly = tessellate_polyline(existing->get_curve(),
				p_tessellate_max_stages, p_tessellate_tolerance);
		if (old_poly.size() < 2) {
			continue;
		}

		bool emitted_for_this_stroke = false;

		// Walk every pair of segments, brute-force. Acceptable for Tier 2 —
		// typical strokes tessellate to ~50 points and we only do this on
		// stroke completion, not per-frame. A BVH speedup is a Tier 5
		// performance ticket if needed.
		for (int a = 0; a + 1 < new_poly.size() && !emitted_for_this_stroke; ++a) {
			const Vector3 a0 = new_poly[a];
			const Vector3 a1 = new_poly[a + 1];
			for (int b = 0; b + 1 < old_poly.size(); ++b) {
				const Vector3 b0 = old_poly[b];
				const Vector3 b1 = old_poly[b + 1];

				Vector3 closest_on_a, closest_on_b;
				Geometry3D::get_closest_points_between_segments(a0, a1, b0, b1,
						closest_on_a, closest_on_b);

				const real_t dist_sq = closest_on_a.distance_squared_to(closest_on_b);
				if (dist_sq <= threshold_sq) {
					// Snap mid-point of the closest pair as the canonical
					// intersection position.
					const Vector3 intersection_pos = (closest_on_a + closest_on_b) * real_t(0.5);
					Ref<CassieIntersectionConstraint> c = existing->get_constraint(
							intersection_pos, p_snap_to_node_threshold);
					// project_on for the new curve happens once the new
					// curve has anchors (Tier 4); the candidate position
					// alone is already usable.
					c->set_position(intersection_pos);
					out.push_back(c);
					emitted_for_this_stroke = true;
					break;
				}
			}
		}
	}

	return out;
}

Ref<CassieMirrorPlaneConstraint> cassie_detect_mirror_plane_intersection(
		const Ref<Curve3D> &p_new,
		const Plane &p_mirror,
		real_t p_proximity_threshold,
		int p_tessellate_max_stages,
		real_t p_tessellate_tolerance) {
	Ref<CassieMirrorPlaneConstraint> empty;
	if (p_new.is_null()) {
		return empty;
	}
	const PackedVector3Array poly = tessellate_polyline(p_new,
			p_tessellate_max_stages, p_tessellate_tolerance);
	if (poly.size() < 2) {
		return empty;
	}

	for (int i = 0; i + 1 < poly.size(); ++i) {
		const Vector3 p0 = poly[i];
		const Vector3 p1 = poly[i + 1];
		const real_t d0 = p_mirror.distance_to(p0);
		const real_t d1 = p_mirror.distance_to(p1);

		// Crossing the plane: signed distances change sign, OR one of the
		// endpoints lies within the proximity threshold of the plane.
		const bool crosses = (d0 * d1) <= 0.0;
		const bool grazes = Math::abs(d0) < p_proximity_threshold ||
				Math::abs(d1) < p_proximity_threshold;
		if (!crosses && !grazes) {
			continue;
		}

		// Interpolate the crossing point.
		Vector3 hit;
		if (crosses && Math::abs(d1 - d0) > 0.0) {
			const real_t t = d0 / (d0 - d1);
			hit = p0.lerp(p1, t);
		} else {
			hit = Math::abs(d0) < Math::abs(d1) ? p0 : p1;
		}

		Ref<CassieMirrorPlaneConstraint> c;
		c.instantiate();
		c->set_position(hit);
		c->set_plane_normal(p_mirror.normal);
		return c;
	}

	return empty;
}

TypedArray<CassieFinalStroke> cassie_split_stroke_at_constraints(
		const Ref<CassieFinalStroke> &p_stroke,
		const TypedArray<CassieIntersectionConstraint> &p_constraints,
		real_t p_snap_threshold) {
	TypedArray<CassieFinalStroke> out;
	if (p_stroke.is_null()) {
		return out;
	}
	const Ref<Curve3D> curve = p_stroke->get_curve();
	// Divide-by-zero guard for the baked_length conversion below; null/short
	// curves are handled by cassie_curve_split_for_constraints itself.
	const real_t baked_length = curve.is_valid() ? curve->get_baked_length() : real_t(0.0);
	if (baked_length <= 0.0) {
		out.push_back(p_stroke);
		return out;
	}

	// old_curve_offset is in baked-curve length units (meters); cut_at expects
	// chord-uniform t ∈ [0, 1]. Convert; the downstream splitter sorts.
	PackedFloat32Array params;
	for (int i = 0; i < p_constraints.size(); ++i) {
		Ref<CassieIntersectionConstraint> c = p_constraints[i];
		if (c.is_null()) {
			continue;
		}
		const real_t t = c->get_old_curve_offset() / baked_length;
		params.push_back(float(CLAMP(t, real_t(0.0), real_t(1.0))));
	}

	Vector<Ref<Curve3D>> substrokes = cassie_curve_split_for_constraints(
			curve, params, float(p_snap_threshold));
	for (int i = 0; i < substrokes.size(); ++i) {
		Ref<CassieFinalStroke> piece;
		piece.instantiate();
		piece->set_curve(substrokes[i], false);
		out.push_back(piece);
	}
	return out;
}

TypedArray<Vector3> cassie_detect_surface_proximity(
		const Ref<Curve3D> &p_new,
		const Callable &p_patch_projection_callback,
		int p_tessellate_max_stages,
		real_t p_tessellate_tolerance) {
	TypedArray<Vector3> out;
	if (p_new.is_null() || !p_patch_projection_callback.is_valid()) {
		return out;
	}
	const PackedVector3Array poly = tessellate_polyline(p_new,
			p_tessellate_max_stages, p_tessellate_tolerance);

	int current_patch = -1;
	for (int i = 0; i < poly.size(); ++i) {
		const Variant ret = p_patch_projection_callback.call(poly[i]);
		if (ret.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary d = ret;
		const bool on_surface = bool(d.get("on_surface", false));
		const int patch_id = int(d.get("patch_id", -1));
		if (!on_surface) {
			current_patch = -1;
			continue;
		}
		if (patch_id != current_patch) {
			current_patch = patch_id;
			out.push_back(poly[i]);
		}
	}
	return out;
}

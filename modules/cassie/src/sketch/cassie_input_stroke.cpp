/**************************************************************************/
/*  cassie_input_stroke.cpp                                               */
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

#include "cassie_input_stroke.h"

#include "../constraints/cassie_constraint.h"
#include "../constraints/cassie_intersection_constraint.h"
#include "../constraints/cassie_mirror_plane_constraint.h"
#include "../constraints/cassie_surface_constraint.h"
#include "../curves/rdp_simplify.h"

#include "core/object/class_db.h"

void CassieInputStroke::add_sample(const Vector3 &p_position, float p_creation_time, float p_pressure) {
	if (!samples.is_empty()) {
		length += p_position.distance_to(samples[samples.size() - 1].position);
	}
	samples.push_back(CassieSample(p_position, p_creation_time, p_pressure));
}

PackedVector3Array CassieInputStroke::get_points(float p_rdp_error) const {
	PackedVector3Array pts;
	pts.resize(samples.size());
	for (uint32_t i = 0; i < samples.size(); ++i) {
		pts.write[i] = samples[i].position;
	}
	if (p_rdp_error == 0.0f) {
		return pts;
	}
	const PackedInt32Array keep = cassie_rdp_reduce(pts, p_rdp_error);
	PackedVector3Array out;
	out.resize(keep.size());
	for (int i = 0; i < keep.size(); ++i) {
		out.write[i] = pts[keep[i]];
	}
	return out;
}

PackedVector3Array CassieInputStroke::get_safe_points(float p_ablation_duration) const {
	PackedVector3Array pts;
	const int n = int(samples.size());
	if (n == 0) {
		return pts;
	}

	const float start_time = samples[0].creation_time;
	const float end_time = samples[n - 1].creation_time;

	// If ablating the window would empty the safe region, return all
	// sample positions (mirrors Cassie's behavior).
	if (start_time + p_ablation_duration * 2.0f >= end_time) {
		pts.resize(n);
		for (int i = 0; i < n; ++i) {
			pts.write[i] = samples[i].position;
		}
		return pts;
	}

	for (int i = 0; i < n; ++i) {
		const float t = samples[i].creation_time;
		if (t == start_time || t == end_time ||
				(t > start_time + p_ablation_duration && t < end_time - p_ablation_duration)) {
			pts.push_back(samples[i].position);
		}
	}
	return pts;
}

TypedArray<PackedVector3Array> CassieInputStroke::get_g1_sections(
		float p_discontinuity_angular_threshold,
		float p_hook_discontinuity_angular_threshold,
		float p_ablation_duration,
		float p_min_section_length,
		float p_max_hook_length,
		float p_max_hook_stroke_ratio) const {
	const float cos_threshold = Math::cos(p_discontinuity_angular_threshold);
	const float cos_hook_threshold = Math::cos(p_hook_discontinuity_angular_threshold);

	PackedVector3Array safe = get_safe_points(p_ablation_duration);
	TypedArray<PackedVector3Array> sections;

	if (safe.size() <= 4) {
		sections.push_back(safe);
		return sections;
	}

	// Hook detection from the start.
	float current_length = float(safe[0].distance_to(safe[1]));
	int i_rel = 2;
	int corrected_start_idx = 0;
	while (current_length < p_max_hook_length &&
			current_length < length * p_max_hook_stroke_ratio &&
			i_rel + 2 < safe.size()) {
		const Vector3 u = real_t(0.5) *
				((safe[i_rel] - safe[i_rel - 2]).normalized() +
						(safe[i_rel] - safe[i_rel - 1]).normalized());
		const Vector3 v = real_t(0.5) *
				((safe[i_rel + 2] - safe[i_rel]).normalized() +
						(safe[i_rel + 1] - safe[i_rel]).normalized());
		current_length += float(safe[i_rel].distance_to(safe[i_rel - 1]));
		if (u.dot(v) < cos_hook_threshold) {
			corrected_start_idx = i_rel;
		}
		++i_rel;
	}

	// Hook detection from the end.
	current_length = float(safe[safe.size() - 1].distance_to(safe[safe.size() - 2]));
	i_rel = safe.size() - 3;
	int corrected_end_idx = safe.size() - 1;
	while (current_length < p_max_hook_length &&
			current_length < length * p_max_hook_stroke_ratio &&
			i_rel - 2 >= 0) {
		const Vector3 u = real_t(0.5) *
				((safe[i_rel] - safe[i_rel - 2]).normalized() +
						(safe[i_rel] - safe[i_rel - 1]).normalized());
		const Vector3 v = real_t(0.5) *
				((safe[i_rel + 2] - safe[i_rel]).normalized() +
						(safe[i_rel + 1] - safe[i_rel]).normalized());
		current_length += float(safe[i_rel].distance_to(safe[i_rel + 1]));
		if (u.dot(v) < cos_hook_threshold) {
			corrected_end_idx = i_rel;
		}
		--i_rel;
	}

	PackedVector3Array trimmed;
	if (corrected_end_idx - corrected_start_idx > 4) {
		const int count = corrected_end_idx + 1 - corrected_start_idx;
		trimmed.resize(count);
		for (int i = 0; i < count; ++i) {
			trimmed.write[i] = safe[corrected_start_idx + i];
		}
	} else {
		trimmed = safe;
	}

	// Walk the trimmed list, splitting at angular corners.
	PackedVector3Array current_section;
	float current_section_length = float(trimmed[0].distance_to(trimmed[1]));
	current_section.push_back(trimmed[0]);
	current_section.push_back(trimmed[1]);
	for (int i = 2; i < trimmed.size() - 2; ++i) {
		current_section.push_back(trimmed[i]);
		current_section_length += float(trimmed[i].distance_to(trimmed[i - 1]));

		const Vector3 u = real_t(0.5) *
				((trimmed[i] - trimmed[i - 2]).normalized() +
						(trimmed[i] - trimmed[i - 1]).normalized());
		const Vector3 v = real_t(0.5) *
				((trimmed[i + 2] - trimmed[i]).normalized() +
						(trimmed[i + 1] - trimmed[i]).normalized());

		if (u.dot(v) < cos_threshold && current_section.size() >= 4 &&
				current_section_length > p_min_section_length) {
			sections.push_back(current_section);
			current_section = PackedVector3Array();
			current_section_length = 0.0f;
			current_section.push_back(trimmed[i]);
		}
	}
	current_section.push_back(trimmed[trimmed.size() - 2]);
	current_section.push_back(trimmed[trimmed.size() - 1]);
	current_section_length += float(trimmed[trimmed.size() - 2].distance_to(trimmed[trimmed.size() - 1]));

	if (sections.is_empty() ||
			(current_section.size() >= 4 && current_section_length > p_min_section_length)) {
		sections.push_back(current_section);
	}
	return sections;
}

float CassieInputStroke::average_drawing_speed() const {
	if (samples.size() < 2) {
		return 0.0f;
	}
	const float dt = samples[samples.size() - 1].creation_time - samples[0].creation_time;
	return dt > 1e-7f ? float(length) / dt : 0.0f;
}

PackedFloat32Array CassieInputStroke::get_weights() const {
	PackedFloat32Array w;
	w.resize(samples.size());
	for (uint32_t i = 0; i < samples.size(); ++i) {
		w.write[i] = samples[i].pressure;
	}
	return w;
}

bool CassieInputStroke::is_valid(float p_min_sketching_time, float p_min_stroke_size) const {
	if (samples.size() < 2) {
		return false;
	}
	const int n = int(samples.size());
	if (samples[n - 1].creation_time - samples[0].creation_time < p_min_sketching_time &&
			samples[n - 1].position.distance_to(samples[0].position) < p_min_stroke_size) {
		return false;
	}
	real_t max_dist = 0.0;
	for (int i = 1; i < n; ++i) {
		const real_t d = samples[0].position.distance_to(samples[i].position);
		if (d > max_dist) {
			max_dist = d;
		}
	}
	return max_dist >= p_min_stroke_size;
}

void CassieInputStroke::clear() {
	samples.clear();
	length = 0.0;
	constraints.clear();
	surface_constraints.clear();
}

void CassieInputStroke::add_constraint(const Ref<CassieConstraint> &p_constraint,
		real_t p_proximity_threshold) {
	ERR_FAIL_COND(p_constraint.is_null());

	if (!constraints.is_empty()) {
		Ref<CassieConstraint> old = constraints[constraints.size() - 1];
		const real_t dist = old->get_position().distance_to(p_constraint->get_position());

		if (dist < p_proximity_threshold) {
			Ref<CassieIntersectionConstraint> old_inter = old;
			Ref<CassieIntersectionConstraint> new_inter = p_constraint;
			if (old_inter.is_valid()) {
				if (new_inter.is_null()) {
					// Curve/curve intersection outranks the newer non-inter
					// candidate: drop the new one entirely.
					return;
				}
				if (old_inter->get_is_at_node() && !new_inter->get_is_at_node()) {
					// At-node intersection outranks an off-node intersection.
					return;
				}
			}
			// Otherwise the new candidate wins; remove the older one.
			constraints.remove_at(constraints.size() - 1);
		} else if (dist < p_proximity_threshold * real_t(2.0)) {
			Ref<CassieMirrorPlaneConstraint> old_mirror = old;
			Ref<CassieMirrorPlaneConstraint> new_mirror = p_constraint;
			if (old_mirror.is_valid() && new_mirror.is_valid()) {
				constraints.remove_at(constraints.size() - 1);
			}
		}
	}

	constraints.push_back(p_constraint);
}

void CassieInputStroke::in_constrain_to_surface(int p_patch_id, const Vector3 &p_position) {
	if (!surface_constraints.is_empty()) {
		Ref<CassieSurfaceConstraint> last = surface_constraints[surface_constraints.size() - 1];
		if (last.is_valid() && last->get_patch_id() == p_patch_id &&
				!last->has_left_mid_stroke()) {
			// Already on this patch — no duplicate event.
			return;
		}
	}
	Ref<CassieSurfaceConstraint> sc;
	sc.instantiate();
	sc->set_patch_id(p_patch_id);
	sc->set_start_position(p_position);
	surface_constraints.push_back(sc);
}

void CassieInputStroke::out_constrain_to_surface(int p_patch_id, const Vector3 &p_position) {
	if (surface_constraints.is_empty()) {
		return;
	}
	Ref<CassieSurfaceConstraint> last = surface_constraints[surface_constraints.size() - 1];
	if (last.is_valid() && last->get_patch_id() == p_patch_id) {
		last->leave(p_position);
	}
}

void CassieInputStroke::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_sample", "position", "creation_time", "pressure"),
			&CassieInputStroke::add_sample);
	ClassDB::bind_method(D_METHOD("get_sample_count"), &CassieInputStroke::get_sample_count);
	ClassDB::bind_method(D_METHOD("get_length"), &CassieInputStroke::get_length);
	ClassDB::bind_method(D_METHOD("get_points", "rdp_error"),
			&CassieInputStroke::get_points, DEFVAL(0.0f));
	ClassDB::bind_method(D_METHOD("get_safe_points", "ablation_duration"),
			&CassieInputStroke::get_safe_points, DEFVAL(0.01f));
	ClassDB::bind_method(D_METHOD("get_g1_sections",
								 "discontinuity_angular_threshold",
								 "hook_discontinuity_angular_threshold",
								 "ablation_duration",
								 "min_section_length",
								 "max_hook_length",
								 "max_hook_stroke_ratio"),
			&CassieInputStroke::get_g1_sections);
	ClassDB::bind_method(D_METHOD("average_drawing_speed"),
			&CassieInputStroke::average_drawing_speed);
	ClassDB::bind_method(D_METHOD("get_weights"), &CassieInputStroke::get_weights);
	ClassDB::bind_method(D_METHOD("is_valid", "min_sketching_time", "min_stroke_size"),
			&CassieInputStroke::is_valid);
	ClassDB::bind_method(D_METHOD("clear"), &CassieInputStroke::clear);

	ClassDB::bind_method(D_METHOD("add_constraint", "constraint", "proximity_threshold"),
			&CassieInputStroke::add_constraint);
	ClassDB::bind_method(D_METHOD("get_constraints"), &CassieInputStroke::get_constraints);
	ClassDB::bind_method(D_METHOD("get_constraint_count"),
			&CassieInputStroke::get_constraint_count);

	ClassDB::bind_method(D_METHOD("in_constrain_to_surface", "patch_id", "position"),
			&CassieInputStroke::in_constrain_to_surface);
	ClassDB::bind_method(D_METHOD("out_constrain_to_surface", "patch_id", "position"),
			&CassieInputStroke::out_constrain_to_surface);
	ClassDB::bind_method(D_METHOD("get_surface_constraints"),
			&CassieInputStroke::get_surface_constraints);
}

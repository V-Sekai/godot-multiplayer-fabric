/**************************************************************************/
/*  cassie_beautifier_params.cpp                                          */
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

#include "cassie_beautifier_params.h"

#include "core/object/class_db.h"

void CassieBeautifierParams::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_samples_ablation_duration", "value"), &CassieBeautifierParams::set_samples_ablation_duration);
	ClassDB::bind_method(D_METHOD("get_samples_ablation_duration"), &CassieBeautifierParams::get_samples_ablation_duration);
	ClassDB::bind_method(D_METHOD("set_small_distance", "value"), &CassieBeautifierParams::set_small_distance);
	ClassDB::bind_method(D_METHOD("get_small_distance"), &CassieBeautifierParams::get_small_distance);
	ClassDB::bind_method(D_METHOD("set_small_angle", "value"), &CassieBeautifierParams::set_small_angle);
	ClassDB::bind_method(D_METHOD("get_small_angle"), &CassieBeautifierParams::get_small_angle);
	ClassDB::bind_method(D_METHOD("set_bezier_fitting_error", "value"), &CassieBeautifierParams::set_bezier_fitting_error);
	ClassDB::bind_method(D_METHOD("get_bezier_fitting_error"), &CassieBeautifierParams::get_bezier_fitting_error);
	ClassDB::bind_method(D_METHOD("set_rdp_error", "value"), &CassieBeautifierParams::set_rdp_error);
	ClassDB::bind_method(D_METHOD("get_rdp_error"), &CassieBeautifierParams::get_rdp_error);
	ClassDB::bind_method(D_METHOD("set_mu_fidelity", "value"), &CassieBeautifierParams::set_mu_fidelity);
	ClassDB::bind_method(D_METHOD("get_mu_fidelity"), &CassieBeautifierParams::get_mu_fidelity);
	ClassDB::bind_method(D_METHOD("set_proximity_threshold", "value"), &CassieBeautifierParams::set_proximity_threshold);
	ClassDB::bind_method(D_METHOD("get_proximity_threshold"), &CassieBeautifierParams::get_proximity_threshold);
	ClassDB::bind_method(D_METHOD("set_angular_proximity_threshold", "value"), &CassieBeautifierParams::set_angular_proximity_threshold);
	ClassDB::bind_method(D_METHOD("get_angular_proximity_threshold"), &CassieBeautifierParams::get_angular_proximity_threshold);
	ClassDB::bind_method(D_METHOD("set_min_distance_between_anchors", "value"), &CassieBeautifierParams::set_min_distance_between_anchors);
	ClassDB::bind_method(D_METHOD("get_min_distance_between_anchors"), &CassieBeautifierParams::get_min_distance_between_anchors);
	ClassDB::bind_method(D_METHOD("set_discontinuity_angular_threshold", "value"), &CassieBeautifierParams::set_discontinuity_angular_threshold);
	ClassDB::bind_method(D_METHOD("get_discontinuity_angular_threshold"), &CassieBeautifierParams::get_discontinuity_angular_threshold);
	ClassDB::bind_method(D_METHOD("set_hook_discontinuity_angular_threshold", "value"), &CassieBeautifierParams::set_hook_discontinuity_angular_threshold);
	ClassDB::bind_method(D_METHOD("get_hook_discontinuity_angular_threshold"), &CassieBeautifierParams::get_hook_discontinuity_angular_threshold);
	ClassDB::bind_method(D_METHOD("set_min_section_length", "value"), &CassieBeautifierParams::set_min_section_length);
	ClassDB::bind_method(D_METHOD("get_min_section_length"), &CassieBeautifierParams::get_min_section_length);
	ClassDB::bind_method(D_METHOD("set_max_hook_length", "value"), &CassieBeautifierParams::set_max_hook_length);
	ClassDB::bind_method(D_METHOD("get_max_hook_length"), &CassieBeautifierParams::get_max_hook_length);
	ClassDB::bind_method(D_METHOD("set_max_hook_stroke_ratio", "value"), &CassieBeautifierParams::set_max_hook_stroke_ratio);
	ClassDB::bind_method(D_METHOD("get_max_hook_stroke_ratio"), &CassieBeautifierParams::get_max_hook_stroke_ratio);
	ClassDB::bind_method(D_METHOD("set_min_sketching_time", "value"), &CassieBeautifierParams::set_min_sketching_time);
	ClassDB::bind_method(D_METHOD("get_min_sketching_time"), &CassieBeautifierParams::get_min_sketching_time);
	ClassDB::bind_method(D_METHOD("set_min_stroke_size", "value"), &CassieBeautifierParams::set_min_stroke_size);
	ClassDB::bind_method(D_METHOD("get_min_stroke_size"), &CassieBeautifierParams::get_min_stroke_size);
	ClassDB::bind_method(D_METHOD("set_max_beziers_for_solver", "value"), &CassieBeautifierParams::set_max_beziers_for_solver);
	ClassDB::bind_method(D_METHOD("get_max_beziers_for_solver"), &CassieBeautifierParams::get_max_beziers_for_solver);
	ClassDB::bind_method(D_METHOD("set_project_to_surface_distance_threshold", "value"), &CassieBeautifierParams::set_project_to_surface_distance_threshold);
	ClassDB::bind_method(D_METHOD("get_project_to_surface_distance_threshold"), &CassieBeautifierParams::get_project_to_surface_distance_threshold);
	ClassDB::bind_method(D_METHOD("set_project_to_mirror_distance_threshold", "value"), &CassieBeautifierParams::set_project_to_mirror_distance_threshold);
	ClassDB::bind_method(D_METHOD("get_project_to_mirror_distance_threshold"), &CassieBeautifierParams::get_project_to_mirror_distance_threshold);
	ClassDB::bind_method(D_METHOD("set_snap_to_existing_node_threshold", "value"), &CassieBeautifierParams::set_snap_to_existing_node_threshold);
	ClassDB::bind_method(D_METHOD("get_snap_to_existing_node_threshold"), &CassieBeautifierParams::get_snap_to_existing_node_threshold);
	ClassDB::bind_method(D_METHOD("set_planarity_allowed", "value"), &CassieBeautifierParams::set_planarity_allowed);
	ClassDB::bind_method(D_METHOD("get_planarity_allowed"), &CassieBeautifierParams::get_planarity_allowed);
	ClassDB::bind_method(D_METHOD("set_on_surface_weight", "value"), &CassieBeautifierParams::set_on_surface_weight);
	ClassDB::bind_method(D_METHOD("get_on_surface_weight"), &CassieBeautifierParams::get_on_surface_weight);
	ClassDB::bind_method(D_METHOD("set_project_on_surface", "value"), &CassieBeautifierParams::set_project_on_surface);
	ClassDB::bind_method(D_METHOD("get_project_on_surface"), &CassieBeautifierParams::get_project_on_surface);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "samples_ablation_duration"),
			"set_samples_ablation_duration", "get_samples_ablation_duration");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "small_distance"),
			"set_small_distance", "get_small_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "small_angle"),
			"set_small_angle", "get_small_angle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bezier_fitting_error"),
			"set_bezier_fitting_error", "get_bezier_fitting_error");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rdp_error"),
			"set_rdp_error", "get_rdp_error");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mu_fidelity"),
			"set_mu_fidelity", "get_mu_fidelity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "proximity_threshold"),
			"set_proximity_threshold", "get_proximity_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angular_proximity_threshold"),
			"set_angular_proximity_threshold", "get_angular_proximity_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_distance_between_anchors"),
			"set_min_distance_between_anchors", "get_min_distance_between_anchors");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "discontinuity_angular_threshold"),
			"set_discontinuity_angular_threshold", "get_discontinuity_angular_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hook_discontinuity_angular_threshold"),
			"set_hook_discontinuity_angular_threshold", "get_hook_discontinuity_angular_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_section_length"),
			"set_min_section_length", "get_min_section_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_hook_length"),
			"set_max_hook_length", "get_max_hook_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_hook_stroke_ratio"),
			"set_max_hook_stroke_ratio", "get_max_hook_stroke_ratio");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_sketching_time"),
			"set_min_sketching_time", "get_min_sketching_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_stroke_size"),
			"set_min_stroke_size", "get_min_stroke_size");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "planarity_allowed"),
			"set_planarity_allowed", "get_planarity_allowed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "on_surface_weight"),
			"set_on_surface_weight", "get_on_surface_weight");
}

/**************************************************************************/
/*  cassie_beautifier_params.h                                            */
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

// Public Tier-4 parameter bundle for CassieBeautifier. Defaults track the
// Unity CASSIEParameters constants. All fields exposed to GDScript via
// inspector.
class CassieBeautifierParams : public Resource {
	GDCLASS(CassieBeautifierParams, Resource);

	float samples_ablation_duration = 0.01f;
	float small_distance = 0.02f;
	float small_angle = 0.5f;
	float bezier_fitting_error = 0.01f;
	float rdp_error = 0.002f;
	float mu_fidelity = 0.7f;
	float proximity_threshold = 0.02f;
	float angular_proximity_threshold = 0.5f;
	float min_distance_between_anchors = 0.01f;
	float discontinuity_angular_threshold = 0.7f;
	float hook_discontinuity_angular_threshold = 0.6f;
	float min_section_length = 0.05f;
	float max_hook_length = 0.06f;
	float max_hook_stroke_ratio = 0.15f;
	float min_sketching_time = 0.02f;
	float min_stroke_size = 0.005f;
	int max_beziers_for_solver = 15;
	float project_to_surface_distance_threshold = 0.05f;
	float project_to_mirror_distance_threshold = 0.05f;
	float snap_to_existing_node_threshold = 0.01f;
	bool planarity_allowed = true;
	bool project_on_surface = true;
	// ENG-54 — when > 0, the solver runs an OnSurfaceEnergy soft block
	// that folds patch projection into the KKT/AVBD system. The beautifier
	// passes this through to CassieSolverParams::on_surface_weight along
	// with the context's project_on_patch_callback. Default 0 preserves
	// the post-solve-projection-only behavior at lines 254-316.
	double on_surface_weight = 0.0;

protected:
	static void _bind_methods();

public:
	CassieBeautifierParams() = default;

	float get_samples_ablation_duration() const { return samples_ablation_duration; }
	void set_samples_ablation_duration(float p_v) { samples_ablation_duration = p_v; }
	float get_small_distance() const { return small_distance; }
	void set_small_distance(float p_v) { small_distance = p_v; }
	float get_small_angle() const { return small_angle; }
	void set_small_angle(float p_v) { small_angle = p_v; }
	float get_bezier_fitting_error() const { return bezier_fitting_error; }
	void set_bezier_fitting_error(float p_v) { bezier_fitting_error = p_v; }
	float get_rdp_error() const { return rdp_error; }
	void set_rdp_error(float p_v) { rdp_error = p_v; }
	float get_mu_fidelity() const { return mu_fidelity; }
	void set_mu_fidelity(float p_v) { mu_fidelity = p_v; }
	float get_proximity_threshold() const { return proximity_threshold; }
	void set_proximity_threshold(float p_v) { proximity_threshold = p_v; }
	float get_angular_proximity_threshold() const { return angular_proximity_threshold; }
	void set_angular_proximity_threshold(float p_v) { angular_proximity_threshold = p_v; }
	float get_min_distance_between_anchors() const { return min_distance_between_anchors; }
	void set_min_distance_between_anchors(float p_v) { min_distance_between_anchors = p_v; }
	float get_discontinuity_angular_threshold() const { return discontinuity_angular_threshold; }
	void set_discontinuity_angular_threshold(float p_v) { discontinuity_angular_threshold = p_v; }
	float get_hook_discontinuity_angular_threshold() const { return hook_discontinuity_angular_threshold; }
	void set_hook_discontinuity_angular_threshold(float p_v) { hook_discontinuity_angular_threshold = p_v; }
	float get_min_section_length() const { return min_section_length; }
	void set_min_section_length(float p_v) { min_section_length = p_v; }
	float get_max_hook_length() const { return max_hook_length; }
	void set_max_hook_length(float p_v) { max_hook_length = p_v; }
	float get_max_hook_stroke_ratio() const { return max_hook_stroke_ratio; }
	void set_max_hook_stroke_ratio(float p_v) { max_hook_stroke_ratio = p_v; }
	float get_min_sketching_time() const { return min_sketching_time; }
	void set_min_sketching_time(float p_v) { min_sketching_time = p_v; }
	float get_min_stroke_size() const { return min_stroke_size; }
	void set_min_stroke_size(float p_v) { min_stroke_size = p_v; }
	int get_max_beziers_for_solver() const { return max_beziers_for_solver; }
	void set_max_beziers_for_solver(int p_v) { max_beziers_for_solver = p_v; }
	float get_project_to_surface_distance_threshold() const { return project_to_surface_distance_threshold; }
	void set_project_to_surface_distance_threshold(float p_v) { project_to_surface_distance_threshold = p_v; }
	float get_project_to_mirror_distance_threshold() const { return project_to_mirror_distance_threshold; }
	void set_project_to_mirror_distance_threshold(float p_v) { project_to_mirror_distance_threshold = p_v; }
	float get_snap_to_existing_node_threshold() const { return snap_to_existing_node_threshold; }
	void set_snap_to_existing_node_threshold(float p_v) { snap_to_existing_node_threshold = p_v; }
	bool get_planarity_allowed() const { return planarity_allowed; }
	void set_planarity_allowed(bool p_v) { planarity_allowed = p_v; }
	bool get_project_on_surface() const { return project_on_surface; }
	void set_project_on_surface(bool p_v) { project_on_surface = p_v; }
	double get_on_surface_weight() const { return on_surface_weight; }
	void set_on_surface_weight(double p_v) { on_surface_weight = p_v; }
};

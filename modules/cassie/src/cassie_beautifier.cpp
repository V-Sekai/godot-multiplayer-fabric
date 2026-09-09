/**************************************************************************/
/*  cassie_beautifier.cpp                                                 */
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

#include "cassie_beautifier.h"

#include "constraints/cassie_intersection_finder.h"
#include "constraints/cassie_surface_constraint.h"
#include "curves/cassie_curve_fit.h"
#include "solver/cassie_constraint_solver.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/variant/callable.h"

void CassieSketchContext::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_existing_strokes", "strokes"),
			&CassieSketchContext::set_existing_strokes);
	ClassDB::bind_method(D_METHOD("get_existing_strokes"),
			&CassieSketchContext::get_existing_strokes);
	ClassDB::bind_method(D_METHOD("set_ortho_directions", "directions"),
			&CassieSketchContext::set_ortho_directions);
	ClassDB::bind_method(D_METHOD("get_ortho_directions"),
			&CassieSketchContext::get_ortho_directions);
	ClassDB::bind_method(D_METHOD("set_mirror_plane", "plane"),
			&CassieSketchContext::set_mirror_plane);
	ClassDB::bind_method(D_METHOD("get_mirror_plane"),
			&CassieSketchContext::get_mirror_plane);
	ClassDB::bind_method(D_METHOD("set_mirror_enabled", "enabled"),
			&CassieSketchContext::set_mirror_enabled);
	ClassDB::bind_method(D_METHOD("is_mirror_enabled"),
			&CassieSketchContext::is_mirror_enabled);
	ClassDB::bind_method(D_METHOD("set_project_on_patch_callback", "callback"),
			&CassieSketchContext::set_project_on_patch_callback);
	ClassDB::bind_method(D_METHOD("get_project_on_patch_callback"),
			&CassieSketchContext::get_project_on_patch_callback);

	ADD_PROPERTY(PropertyInfo(Variant::PLANE, "mirror_plane"),
			"set_mirror_plane", "get_mirror_plane");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "mirror_enabled"),
			"set_mirror_enabled", "is_mirror_enabled");
}

Dictionary CassieBeautifier::beautify(
		const Ref<CassieInputStroke> &p_stroke,
		const Ref<CassieSketchContext> &p_context,
		const Ref<CassieBeautifierParams> &p_params,
		bool p_fit_to_constraints,
		bool p_mirror) {
	Dictionary result;
	result["is_valid"] = false;
	result["is_short_or_linear"] = false;
	result["curve"] = Ref<Curve3D>();
	result["intersections"] = TypedArray<CassieIntersectionConstraint>();
	result["mirror_constraints"] = TypedArray<CassieMirrorPlaneConstraint>();
	result["applied_anchors"] = PackedInt32Array();
	result["rejected_count"] = 0;
	result["planar"] = false;
	result["is_closed_loop"] = false;

	if (p_stroke.is_null() || p_params.is_null()) {
		return result;
	}

	const float min_time = p_params->get_min_sketching_time();
	const float min_size = p_params->get_min_stroke_size();
	if (!p_stroke->is_valid(min_time, min_size)) {
		return result;
	}
	result["is_valid"] = true;

	const PackedVector3Array safe_points = p_stroke->get_safe_points(
			p_params->get_samples_ablation_duration());
	if (safe_points.size() < 2) {
		return result;
	}

	// Short-stroke fast path → straight line fit.
	if (safe_points.size() == 2 || p_stroke->get_length() < p_params->get_small_distance()) {
		Ref<Curve3D> line = cassie_fit_line(safe_points[0], safe_points[safe_points.size() - 1]);
		result["curve"] = line;
		result["is_short_or_linear"] = true;
		return result;
	}

	// Schneider fitter.
	Ref<Curve3D> fitted = cassie_fit_curve(safe_points,
			p_params->get_bezier_fitting_error(), p_params->get_rdp_error());
	if (fitted.is_null() || fitted->get_point_count() < 2) {
		return result;
	}
	result["curve"] = fitted;

	if (!p_fit_to_constraints) {
		return result;
	}

	// Collect intersection candidates, then refine them via CorrectIntersections.
	// Track refined intersections separately so the Tier 4 split + commit flow
	// can consume them directly via result["intersections"] without a downcast
	// off the base-typed `candidates` array.
	TypedArray<CassieConstraint> candidates;
	TypedArray<CassieIntersectionConstraint> raw_inters;
	TypedArray<CassieIntersectionConstraint> refined_inters;
	TypedArray<CassieMirrorPlaneConstraint> refined_mirrors;
	if (p_context.is_valid()) {
		raw_inters = cassie_find_intersections(
				fitted, p_context->get_existing_strokes(),
				p_params->get_proximity_threshold(),
				p_params->get_min_distance_between_anchors());

		// CorrectIntersections: for each intersection, search a zone on the
		// intersected curve for a position closer to the new curve. Port of
		// Beautifier.CorrectIntersections (Beautifier.cs:482-556).
		const real_t search_distance = p_params->get_small_distance();
		const int N_steps = 5;
		for (int i = 0; i < raw_inters.size(); ++i) {
			Ref<CassieIntersectionConstraint> c = raw_inters[i];
			if (c.is_null() || c->get_intersected_stroke().is_null()) {
				candidates.push_back(c);
				refined_inters.push_back(c);
				continue;
			}
			Ref<Curve3D> intersected = c->get_intersected_stroke()->get_curve();
			if (intersected.is_null() || intersected->get_baked_length() <= 0.0) {
				candidates.push_back(c);
				refined_inters.push_back(c);
				continue;
			}
			const real_t baked_old = intersected->get_baked_length();
			const real_t offset_old = c->get_old_curve_offset();
			if (offset_old <= 0.0 || offset_old >= baked_old) {
				candidates.push_back(c);
				refined_inters.push_back(c);
				continue;
			}
			// Walk a small zone +/- search_distance/2 around the initial
			// intersection offset and pick the offset whose projection onto
			// the new fitted curve lands closest to the old curve point.
			const real_t half = search_distance * real_t(0.5);
			const real_t zone_start = MAX(real_t(0.0), offset_old - half);
			const real_t zone_end = MIN(baked_old, offset_old + half);
			if (zone_end - zone_start < search_distance * real_t(0.1)) {
				candidates.push_back(c);
				refined_inters.push_back(c);
				continue;
			}
			const real_t step = (zone_end - zone_start) / real_t(N_steps);
			Vector3 best_pos = c->get_old_curve_position();
			real_t best_dist = best_pos.distance_to(fitted->get_closest_point(best_pos));
			for (int k = 0; k <= N_steps; ++k) {
				const real_t o = zone_start + step * real_t(k);
				const Vector3 p = intersected->sample_baked(o);
				const Vector3 proj = fitted->get_closest_point(p);
				const real_t d = proj.distance_to(p);
				if (d < best_dist) {
					best_dist = d;
					best_pos = p;
				}
			}
			// Rebuild constraint at the better point.
			Ref<CassieIntersectionConstraint> refined = c->get_intersected_stroke()->get_constraint(
					best_pos, p_params->get_snap_to_existing_node_threshold());
			candidates.push_back(refined);
			refined_inters.push_back(refined);
		}

		if (p_mirror && p_context->is_mirror_enabled()) {
			Ref<CassieMirrorPlaneConstraint> mirror = cassie_detect_mirror_plane_intersection(
					fitted, p_context->get_mirror_plane(),
					p_params->get_proximity_threshold());
			if (mirror.is_valid()) {
				candidates.push_back(mirror);
				refined_mirrors.push_back(mirror);
			}
		}
	}
	// Pre-solver detections so Tier 4 demo callers can split strokes at the
	// raw crossing points. The solver's authoritative `result["intersections"]`
	// (populated later by the merge from solver->solve) keeps only the
	// "active" constraints it chose to apply — those are not what the demo
	// flow wants for splitting existing strokes.
	result["detected_intersections"] = refined_inters;
	result["detected_mirror_constraints"] = refined_mirrors;

	// Closed-loop detection: endpoints coincide within proximity threshold.
	{
		const Vector3 first = fitted->get_point_position(0);
		const Vector3 last = fitted->get_point_position(fitted->get_point_count() - 1);
		if (first.distance_to(last) < p_params->get_proximity_threshold() && fitted->get_point_count() > 2) {
			result["is_closed_loop"] = true;
		}
	}
	bool is_closed = bool(result["is_closed_loop"]);

	// Endpoint-overlap cut: if the first/last intersection sits at the
	// stroke's start/end with aligned tangents, trim the new curve at that
	// point. Port of Beautifier.cs:163-206.
	if (!is_closed && candidates.size() > 0) {
		const real_t cos_align = Math::cos(p_params->get_small_angle());
		const real_t prox = p_params->get_proximity_threshold();
		const real_t snap_anchor = p_params->get_small_distance() * real_t(0.1);

		Ref<CassieIntersectionConstraint> first_ic = candidates[0];
		if (first_ic.is_valid()) {
			const Vector3 start_pos = fitted->get_point_position(0);
			const Vector3 start_tan = fitted->get_point_out(0).normalized();
			const Vector3 old_tan = first_ic->get_old_curve_tangent();
			if (first_ic->get_position().distance_to(start_pos) < prox &&
					old_tan.length() > 0.0 &&
					Math::abs(double(old_tan.dot(start_tan))) > cos_align) {
				const real_t baked = fitted->get_baked_length();
				if (baked > 0.0) {
					const real_t t_cut = fitted->get_closest_offset(first_ic->get_position()) / baked;
					Ref<Curve3D> trimmed = cassie_curve_cut_at(fitted, float(t_cut), true, float(snap_anchor));
					if (trimmed.is_valid() && trimmed->get_point_count() >= 2) {
						fitted = trimmed;
					}
				}
			}
		}

		Ref<CassieIntersectionConstraint> last_ic = candidates[candidates.size() - 1];
		if (last_ic.is_valid()) {
			const Vector3 end_pos = fitted->get_point_position(fitted->get_point_count() - 1);
			const Vector3 end_tan = (-fitted->get_point_in(fitted->get_point_count() - 1)).normalized();
			const Vector3 old_tan = last_ic->get_old_curve_tangent();
			if (last_ic->get_position().distance_to(end_pos) < prox &&
					old_tan.length() > 0.0 &&
					Math::abs(double(old_tan.dot(end_tan))) > cos_align) {
				const real_t baked = fitted->get_baked_length();
				if (baked > 0.0) {
					const real_t t_cut = fitted->get_closest_offset(last_ic->get_position()) / baked;
					Ref<Curve3D> trimmed = cassie_curve_cut_at(fitted, float(t_cut), false, float(snap_anchor));
					if (trimmed.is_valid() && trimmed->get_point_count() >= 2) {
						fitted = trimmed;
					}
				}
			}
		}
		result["curve"] = fitted;
	}

	// Large-stroke bypass: if the fitted curve already has more than
	// max_beziers_for_solver segments, skip the solver and return the
	// fitted (and possibly endpoint-cut) curve. Port of Beautifier.cs:215-218.
	const int n_segs = fitted->get_point_count() - 1;
	if (n_segs > p_params->get_max_beziers_for_solver()) {
		result["is_bypassed"] = true;
		return result;
	}
	result["is_bypassed"] = false;

	// Configure and run the solver.
	Ref<CassieSolverParams> solver_params;
	solver_params.instantiate();
	solver_params->set_mu_fidelity(double(p_params->get_mu_fidelity()));
	solver_params->set_proximity_threshold(double(p_params->get_proximity_threshold()));
	solver_params->set_angular_proximity_threshold(double(p_params->get_angular_proximity_threshold()));
	solver_params->set_min_distance_between_anchors(double(p_params->get_min_distance_between_anchors()));
	solver_params->set_planarity_allowed(p_params->get_planarity_allowed());

	// ENG-54 — thread the on-surface energy weight + projection callback
	// into the solver so the projection composes with planarity inside
	// the KKT/AVBD system instead of fighting it in the post-solve pass.
	solver_params->set_on_surface_weight(p_params->get_on_surface_weight());
	if (p_context.is_valid()) {
		solver_params->set_project_on_patch_callback(
				p_context->get_project_on_patch_callback());
	}

	Ref<CassieConstraintSolver> solver;
	solver.instantiate();
	PackedVector3Array ortho;
	if (p_context.is_valid()) {
		ortho = p_context->get_ortho_directions();
	}

	Dictionary solved = solver->solve(fitted, candidates, ortho, solver_params, is_closed);
	for (const Variant *key = solved.next(nullptr); key != nullptr; key = solved.next(key)) {
		result[*key] = solved[*key];
	}

	// ProjectOnSurfaces: snap anchor positions of solved curve to surface
	// patches identified by the input stroke's SurfaceConstraints. Port of
	// Beautifier.ProjectOnSurfaces (Beautifier.cs:319-469). The patch
	// projection itself is delegated to a GDScript Callable on the context,
	// matching the plan's "deferred to Callable" design.
	if (p_params->get_project_on_surface() && p_context.is_valid() &&
			p_context->get_project_on_patch_callback().is_valid() &&
			p_stroke->get_surface_constraints().size() > 0) {
		Ref<Curve3D> solved_curve = result["curve"];
		if (solved_curve.is_valid()) {
			Ref<Curve3D> projected;
			projected.instantiate();
			bool any_full_projection = false;
			const Callable cb = p_context->get_project_on_patch_callback();
			const real_t accept_dist = p_params->get_project_to_surface_distance_threshold();
			const int anchor_n = solved_curve->get_point_count();
			const int n_segs_proj = anchor_n - 1;

			TypedArray<CassieSurfaceConstraint> surf_cs = p_stroke->get_surface_constraints();
			int last_constrained = 0;
			for (int s = 0; s < surf_cs.size(); ++s) {
				Ref<CassieSurfaceConstraint> sc = surf_cs[s];
				if (sc.is_null()) {
					continue;
				}
				// Find the anchor span this surface constraint covers.
				const real_t baked_proj = solved_curve->get_baked_length();
				if (baked_proj <= 0.0) {
					continue;
				}
				const real_t start_t = solved_curve->get_closest_offset(sc->get_start_position()) / baked_proj;
				const real_t end_t = sc->has_left_mid_stroke()
						? solved_curve->get_closest_offset(sc->get_end_position()) / baked_proj
						: real_t(1.0);
				int start_anchor = MAX(last_constrained, int(Math::round(start_t * real_t(n_segs_proj))));
				int end_anchor = MAX(last_constrained, int(Math::round(end_t * real_t(n_segs_proj))));
				if (start_anchor < 0) {
					start_anchor = 0;
				}
				if (end_anchor > n_segs_proj) {
					end_anchor = n_segs_proj;
				}
				last_constrained = end_anchor;

				if (start_anchor == 0 && end_anchor == n_segs_proj) {
					any_full_projection = true;
				}
			}
			// Apply projection: each anchor goes through the callback; if
			// the callback returns a dict with "projected" within accept_dist
			// of the input, we use it.
			for (int i = 0; i < anchor_n; ++i) {
				const Vector3 anchor_pos = solved_curve->get_point_position(i);
				Vector3 final_pos = anchor_pos;
				if (any_full_projection || i == 0 || i == anchor_n - 1) {
					const Variant ret = cb.call(anchor_pos);
					if (ret.get_type() == Variant::DICTIONARY) {
						const Dictionary d = ret;
						const bool on_surf = bool(d.get("on_surface", false));
						if (on_surf) {
							const Vector3 proj = d.get("projected", anchor_pos);
							if (proj.distance_to(anchor_pos) < accept_dist) {
								final_pos = proj;
							}
						}
					}
				}
				const Vector3 in_h = (i == 0) ? Vector3() : solved_curve->get_point_in(i);
				const Vector3 out_h = (i == anchor_n - 1) ? Vector3() : solved_curve->get_point_out(i);
				projected->add_point(final_pos, in_h, out_h);
			}
			result["curve"] = projected;
			result["on_surface"] = any_full_projection;
		}
	} else {
		result["on_surface"] = false;
	}

	return result;
}

void CassieBeautifier::_bind_methods() {
	ClassDB::bind_method(D_METHOD("beautify", "stroke", "context", "params",
								 "fit_to_constraints", "mirror"),
			&CassieBeautifier::beautify);
}

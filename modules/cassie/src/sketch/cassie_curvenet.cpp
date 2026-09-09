/**************************************************************************/
/*  cassie_curvenet.cpp                                                   */
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

#include "cassie_curvenet.h"

#include "cassie_polar.h"

#include "core/math/math_funcs.h"
#include "core/math/quaternion.h"
#include "core/object/class_db.h"
#include "core/variant/dictionary.h"
#include "scene/resources/curve.h"

#include <cfloat>

namespace {

// Pick the closer endpoint of curve c to point p, return the outgoing
// tangent (P1 - P0 at the start, or -(P_n - P_{n-1}) at the end) plus the
// arc-length offset at that endpoint.
struct CurveEndpointSample {
	Vector3 tangent;
	real_t offset = 0.0;
	bool at_start = true;
	bool valid = false;
};

CurveEndpointSample sample_curve_endpoint(const Ref<Curve3D> &p_curve, const Vector3 &p_pos) {
	CurveEndpointSample s;
	if (p_curve.is_null() || p_curve->get_point_count() < 2) {
		return s;
	}
	const int last = p_curve->get_point_count() - 1;
	const Vector3 a0 = p_curve->get_point_position(0);
	const Vector3 aN = p_curve->get_point_position(last);
	const bool at_start = p_pos.distance_squared_to(a0) <= p_pos.distance_squared_to(aN);
	s.at_start = at_start;
	s.tangent = at_start
			? p_curve->get_point_out(0)
			: -p_curve->get_point_in(last);
	if (s.tangent.length_squared() <= real_t(1e-12)) {
		// Straight-line fits (cassie_fit_line) have zero in/out handles —
		// fall back to chord direction so adjacency still populates.
		if (at_start) {
			s.tangent = p_curve->get_point_position(1) - p_curve->get_point_position(0);
		} else {
			s.tangent = p_curve->get_point_position(last - 1) - p_curve->get_point_position(last);
		}
		if (s.tangent.length_squared() <= real_t(1e-12)) {
			return s;
		}
	}
	s.tangent.normalize();
	s.offset = at_start ? real_t(0.0) : p_curve->get_baked_length();
	s.valid = true;
	return s;
}

} // namespace

Basis CassieCurvenet::wahba_align(const PackedVector3Array &p_projection_tangents,
		const PackedVector3Array &p_rest_tangents) {
	// Track 5 — closed-form polar decomposition. R = H * (H^T H)^{-1/2}
	// with the 3x3 symmetric eigendecomposition of (H^T H) via
	// Smith 1961's trig cubic substitution. No 4-element intermediate;
	// the algorithm stays in 3x3 matrix-land throughout.
	//
	// Lean spec + native_decide fixtures: see
	// modules/cassie/lean/CassieAvbd/PolarDecomp.lean.
	return cassie_polar::wahba_align(p_projection_tangents, p_rest_tangents);
}

Basis CassieCurvenet::parallel_transport_along(const Ref<Curve3D> &p_curve,
		real_t p_src_offset, const Basis &p_src_basis, real_t p_dest_offset) {
	if (p_curve.is_null() || p_curve->get_baked_length() <= real_t(0.0)) {
		return p_src_basis;
	}
	// Coincident source and destination: transport is the identity, so the
	// source basis is returned unchanged. Short-circuiting also avoids the
	// frame_dst * frame_src.inverse() round-trip, which under single
	// precision leaves ~1e-6 drift that would otherwise perturb R.
	if (p_src_offset == p_dest_offset) {
		return p_src_basis;
	}
	const Transform3D frame_src = p_curve->sample_baked_with_rotation(p_src_offset, true, true);
	const Transform3D frame_dst = p_curve->sample_baked_with_rotation(p_dest_offset, true, true);
	// p_src_basis is the user's R at the source frame. Express it as a
	// rotation in the source frame's local coordinates, then re-express
	// in the destination frame.
	const Basis local = frame_src.basis.inverse() * p_src_basis;
	return frame_dst.basis * local;
}

void CassieCurvenet::build_from_graph(const Dictionary &p_graph_data) {
	knots.clear();
	knot_incident_curves.clear();
	curve_endpoint_knots.clear();

	const Variant curves_var = p_graph_data.get("curves", Variant());
	if (curves_var.get_type() == Variant::ARRAY) {
		curves = TypedArray<CassieFinalStroke>(curves_var);
	} else {
		curves.clear();
	}

	curve_endpoint_knots.resize(curves.size());
	for (uint32_t i = 0; i < curve_endpoint_knots.size(); ++i) {
		curve_endpoint_knots[i] = Vector2i(-1, -1);
	}

	const Array nodes = p_graph_data.get("nodes", Array());
	knot_incident_curves.resize(nodes.size());

	for (int i = 0; i < nodes.size(); ++i) {
		const Dictionary node = nodes[i];
		Ref<CassieCurvenetKnot> knot;
		knot.instantiate();
		knot->set_graph_node_id(int(node.get("id", -1)));

		Transform3D t;
		t.origin = node.get("position", Vector3());
		knot->set_projection_pose_transform(t);
		knot->set_rest_pose_transform(t);

		const PackedInt32Array incident = node.get("incident_curve_ids", PackedInt32Array());
		knot_incident_curves[i] = incident;
		knot->set_is_intersection(incident.size() >= 3);

		// Snapshot tangents from incident curves' anchor handles, plus
		// record which curve endpoint this knot is.
		PackedVector3Array tangents;
		for (int j = 0; j < incident.size(); ++j) {
			const int curve_id = incident[j];
			if (curve_id < 0 || curve_id >= curves.size()) {
				continue;
			}
			Ref<CassieFinalStroke> stroke = curves[curve_id];
			if (stroke.is_null()) {
				continue;
			}
			Ref<Curve3D> c = stroke->get_curve();
			const CurveEndpointSample s = sample_curve_endpoint(c, t.origin);
			if (!s.valid) {
				continue;
			}
			tangents.push_back(s.tangent);
			// Record adjacency.
			Vector2i &endpoints = curve_endpoint_knots[curve_id];
			if (s.at_start) {
				endpoints.x = i;
			} else {
				endpoints.y = i;
			}
		}
		knot->set_projection_pose_tangents(tangents);
		knot->set_rest_pose_tangents(tangents);
		knots.push_back(knot);
	}
}

void CassieCurvenet::update_rest_pose(const Ref<CassieSurfacePatch> &p_patch) {
	for (int i = 0; i < knots.size(); ++i) {
		Ref<CassieCurvenetKnot> knot = knots[i];
		if (knot.is_null()) {
			continue;
		}
		Transform3D rest = knot->get_rest_pose_transform();
		if (p_patch.is_valid()) {
			const Dictionary projection = p_patch->project(rest.origin);
			if (bool(projection.get("on_surface", false))) {
				rest.origin = projection.get("projected", rest.origin);
			}
		}
		knot->set_rest_pose_transform(rest);
	}
}

void CassieCurvenet::compute_orientations() {
	// Pass 1 — intersection knots via Wahba SVD.
	for (int i = 0; i < knots.size(); ++i) {
		Ref<CassieCurvenetKnot> knot = knots[i];
		if (knot.is_null() || !knot->get_is_intersection()) {
			continue;
		}
		Transform3D rest = knot->get_rest_pose_transform();
		if (knot->get_projection_pose_tangents().size() >= 3 &&
				knot->get_rest_pose_tangents().size() >= 3) {
			rest.basis = wahba_align(knot->get_projection_pose_tangents(),
					knot->get_rest_pose_tangents());
		}
		// If a knot is_intersection=true but lacks 3 tangents (degenerate
		// case for the test "2-tangent fallback"), leave rest.basis at the
		// projection-pose basis and let the parallel-transport pass treat
		// it as non-intersection.
		knot->set_rest_pose_transform(rest);
		knot->set_needs_setup(false);
	}

	// Pass 2 — non-intersection knots via parallel-transport + inverse-
	// distance blend. Walks the first incident curve to find adjacent
	// intersections.
	for (int i = 0; i < knots.size(); ++i) {
		Ref<CassieCurvenetKnot> knot = knots[i];
		if (knot.is_null()) {
			continue;
		}
		const bool wahba_solved = knot->get_is_intersection() &&
				knot->get_projection_pose_tangents().size() >= 3;
		if (wahba_solved) {
			continue;
		}
		if (uint32_t(i) >= knot_incident_curves.size()) {
			knot->set_needs_setup(true);
			continue;
		}
		const PackedInt32Array incident = knot_incident_curves[i];
		Transform3D rest = knot->get_rest_pose_transform();

		// For each incident curve, walk to its other endpoint and check if
		// that endpoint is an intersection with a valid Wahba-solved basis.
		// Collect contributions; if we find one, transport. If two, slerp.
		struct Contribution {
			Basis transported;
			real_t arc_dist;
			bool valid = false;
		};
		Contribution best_a, best_b;
		for (int j = 0; j < incident.size(); ++j) {
			const int curve_id = incident[j];
			if (curve_id < 0 || curve_id >= curves.size()) {
				continue;
			}
			Ref<CassieFinalStroke> stroke = curves[curve_id];
			if (stroke.is_null()) {
				continue;
			}
			Ref<Curve3D> c = stroke->get_curve();
			if (c.is_null() || c->get_baked_length() <= 0.0) {
				continue;
			}
			const Vector2i ep = curve_endpoint_knots[curve_id];
			const int my_end = (ep.x == i) ? 0 : 1;
			const int other_knot_idx = (my_end == 0) ? ep.y : ep.x;
			if (other_knot_idx < 0 || other_knot_idx >= knots.size() ||
					other_knot_idx == i) {
				continue;
			}
			Ref<CassieCurvenetKnot> other = knots[other_knot_idx];
			if (other.is_null() || !other->get_is_intersection()) {
				continue;
			}
			if (other->get_projection_pose_tangents().size() < 3 ||
					other->get_rest_pose_tangents().size() < 3) {
				continue;
			}
			// Transport other's basis along the curve to this knot.
			const real_t my_offset = (my_end == 0) ? real_t(0.0) : c->get_baked_length();
			const real_t other_offset = (my_end == 0) ? c->get_baked_length() : real_t(0.0);
			const Basis transported = parallel_transport_along(c,
					other_offset, other->get_rest_pose_transform().basis, my_offset);
			const real_t arc_dist = c->get_baked_length();
			Contribution cand{ transported, arc_dist, true };
			if (!best_a.valid) {
				best_a = cand;
			} else if (!best_b.valid) {
				best_b = cand;
			} else {
				// Already have two — keep the two with smallest arc distance.
				if (arc_dist < best_a.arc_dist) {
					best_b = best_a;
					best_a = cand;
				} else if (arc_dist < best_b.arc_dist) {
					best_b = cand;
				}
			}
		}

		if (!best_a.valid) {
			// Isolated knot — no neighboring intersection. Identity branch.
			rest.basis = Basis();
			knot->set_rest_pose_transform(rest);
			knot->set_needs_setup(true);
			continue;
		}

		// d == 0 short-circuit for the coincident case.
		const real_t kEps = real_t(1e-9);
		if (best_a.arc_dist < kEps) {
			rest.basis = best_a.transported;
			knot->set_rest_pose_transform(rest);
			knot->set_needs_setup(false);
			continue;
		}

		if (!best_b.valid) {
			rest.basis = best_a.transported;
			knot->set_rest_pose_transform(rest);
			knot->set_needs_setup(false);
			continue;
		}

		// Inverse-distance blend: t = d_a / (d_a + d_b).
		const real_t denom = best_a.arc_dist + best_b.arc_dist;
		const real_t t = denom > kEps ? best_a.arc_dist / denom : real_t(0.5);
		const Quaternion qa = best_a.transported.get_rotation_quaternion();
		const Quaternion qb = best_b.transported.get_rotation_quaternion();
		const Quaternion blended = qa.slerp(qb, t);
		rest.basis = Basis(blended);
		knot->set_rest_pose_transform(rest);
		knot->set_needs_setup(false);
	}
}

bool CassieCurvenet::set_knot_transform(int p_knot_idx, const Transform3D &p_transform) {
	if (p_knot_idx < 0 || p_knot_idx >= knots.size()) {
		return false;
	}
	Ref<CassieCurvenetKnot> knot = knots[p_knot_idx];
	if (knot.is_null()) {
		return false;
	}
	knot->set_rest_pose_transform(p_transform);
	return true;
}

int CassieCurvenet::add_curve(const Ref<CassieFinalStroke> &p_curve,
		int p_start_knot_idx, int p_end_knot_idx) {
	if (p_curve.is_null()) {
		return -1;
	}
	const int new_curve_idx = curves.size();
	curves.push_back(p_curve);
	curve_endpoint_knots.push_back(Vector2i(p_start_knot_idx, p_end_knot_idx));

	auto append_incidence = [&](int knot_idx) {
		if (knot_idx < 0 || knot_idx >= knots.size()) {
			return;
		}
		if (uint32_t(knot_idx) >= knot_incident_curves.size()) {
			knot_incident_curves.resize(knot_idx + 1);
		}
		PackedInt32Array list = knot_incident_curves[knot_idx];
		list.push_back(new_curve_idx);
		knot_incident_curves[knot_idx] = list;
		Ref<CassieCurvenetKnot> knot = knots[knot_idx];
		if (knot.is_valid()) {
			knot->set_is_intersection(list.size() >= 3);
		}
	};
	append_incidence(p_start_knot_idx);
	if (p_end_knot_idx != p_start_knot_idx) {
		append_incidence(p_end_knot_idx);
	}
	return new_curve_idx;
}

bool CassieCurvenet::delete_curve(int p_curve_idx) {
	if (p_curve_idx < 0 || p_curve_idx >= curves.size()) {
		return false;
	}
	curves.remove_at(p_curve_idx);
	curve_endpoint_knots.remove_at(p_curve_idx);

	// Rewrite every knot's incidence: drop p_curve_idx if present, shift
	// higher indices down by 1.
	for (uint32_t k = 0; k < knot_incident_curves.size(); ++k) {
		PackedInt32Array list = knot_incident_curves[k];
		PackedInt32Array rewritten;
		for (int i = 0; i < list.size(); ++i) {
			const int c = list[i];
			if (c == p_curve_idx) {
				continue;
			}
			rewritten.push_back(c > p_curve_idx ? c - 1 : c);
		}
		knot_incident_curves[k] = rewritten;
		Ref<CassieCurvenetKnot> knot = knots[k];
		if (knot.is_valid()) {
			knot->set_is_intersection(rewritten.size() >= 3);
		}
	}
	return true;
}

bool CassieCurvenet::replace_curve(int p_curve_idx, const Ref<CassieFinalStroke> &p_new_curve) {
	if (p_curve_idx < 0 || p_curve_idx >= curves.size() || p_new_curve.is_null()) {
		return false;
	}
	curves[p_curve_idx] = p_new_curve;
	// Adjacency intentionally preserved.
	return true;
}

Vector2i CassieCurvenet::get_curve_endpoint_knots(int p_curve_idx) const {
	if (p_curve_idx < 0 || uint32_t(p_curve_idx) >= curve_endpoint_knots.size()) {
		return Vector2i(-1, -1);
	}
	return curve_endpoint_knots[p_curve_idx];
}

void CassieCurvenet::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_curves", "curves"), &CassieCurvenet::set_curves);
	ClassDB::bind_method(D_METHOD("get_curves"), &CassieCurvenet::get_curves);
	ClassDB::bind_method(D_METHOD("set_knots", "knots"), &CassieCurvenet::set_knots);
	ClassDB::bind_method(D_METHOD("get_knots"), &CassieCurvenet::get_knots);
	ClassDB::bind_method(D_METHOD("get_curve_count"), &CassieCurvenet::get_curve_count);
	ClassDB::bind_method(D_METHOD("get_knot_count"), &CassieCurvenet::get_knot_count);
	ClassDB::bind_method(D_METHOD("set_mode", "mode"), &CassieCurvenet::set_mode);
	ClassDB::bind_method(D_METHOD("get_mode"), &CassieCurvenet::get_mode);
	ClassDB::bind_method(D_METHOD("set_bound_patch", "patch"), &CassieCurvenet::set_bound_patch);
	ClassDB::bind_method(D_METHOD("get_bound_patch"), &CassieCurvenet::get_bound_patch);
	ClassDB::bind_method(D_METHOD("build_from_graph", "graph_data"),
			&CassieCurvenet::build_from_graph);
	ClassDB::bind_method(D_METHOD("update_rest_pose", "patch"),
			&CassieCurvenet::update_rest_pose);
	ClassDB::bind_method(D_METHOD("compute_orientations"),
			&CassieCurvenet::compute_orientations);
	ClassDB::bind_method(D_METHOD("get_curve_endpoint_knots", "curve_idx"),
			&CassieCurvenet::get_curve_endpoint_knots);
	ClassDB::bind_method(D_METHOD("set_knot_transform", "knot_idx", "transform"),
			&CassieCurvenet::set_knot_transform);
	ClassDB::bind_method(D_METHOD("add_curve", "curve", "start_knot_idx", "end_knot_idx"),
			&CassieCurvenet::add_curve);
	ClassDB::bind_method(D_METHOD("delete_curve", "curve_idx"),
			&CassieCurvenet::delete_curve);
	ClassDB::bind_method(D_METHOD("replace_curve", "curve_idx", "new_curve"),
			&CassieCurvenet::replace_curve);

	BIND_ENUM_CONSTANT(MODE_EDIT);
	BIND_ENUM_CONSTANT(MODE_POSE);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Edit,Pose"),
			"set_mode", "get_mode");
}

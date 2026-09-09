/**************************************************************************/
/*  cassie_sketcher.cpp                                                   */
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

#include "cassie_sketcher.h"

#include "cassie_stroke_packet.h"
#include "constraints/cassie_intersection_constraint.h"
#include "constraints/cassie_intersection_finder.h"

#include "core/object/class_db.h"

CassieSketcher::CassieSketcher() {
	_ensure_owned_state();
}

void CassieSketcher::_ensure_owned_state() {
	if (beautifier.is_null()) {
		beautifier.instantiate();
	}
	if (beautifier_params.is_null()) {
		beautifier_params.instantiate();
	}
	if (sketch_context.is_null()) {
		sketch_context.instantiate();
	}
	if (sketch_graph.is_null()) {
		sketch_graph.instantiate();
	}
	if (surface_manager.is_null()) {
		surface_manager.instantiate();
		surface_manager->set_graph(sketch_graph);
		// Determinism contract: multiplayer peers must see patches
		// materialize on the same call. Async triangulation defers to
		// later frames and breaks that contract. Single-player demos
		// can opt back in with set_async_triangulation(true).
		surface_manager->set_async_triangulation(false);
	}
}

void CassieSketcher::set_beautifier_params(const Ref<CassieBeautifierParams> &p_params) {
	if (p_params.is_valid()) {
		beautifier_params = p_params;
	}
}

void CassieSketcher::set_async_triangulation(bool p_enable) {
	_ensure_owned_state();
	surface_manager->set_async_triangulation(p_enable);
}

bool CassieSketcher::get_async_triangulation() const {
	if (surface_manager.is_null()) {
		return false;
	}
	return surface_manager->get_async_triangulation();
}

int CassieSketcher::begin_stroke(const Vector3 &p_local_position, float p_pressure) {
	_ensure_owned_state();
	const int sid = next_stroke_id++;
	InFlightStroke s;
	s.input.instantiate();
	s.sample_index = 0;
	const float t = float(s.sample_index) * sample_dt;
	s.input->add_sample(p_local_position, t, p_pressure);
	++s.sample_index;
	in_flight.insert(sid, s);
	return sid;
}

void CassieSketcher::add_sample(int p_stroke_id, const Vector3 &p_local_position, float p_pressure) {
	HashMap<int, InFlightStroke>::Iterator it = in_flight.find(p_stroke_id);
	if (!it) {
		return;
	}
	const float t = float(it->value.sample_index) * sample_dt;
	it->value.input->add_sample(p_local_position, t, p_pressure);
	++it->value.sample_index;
}

// Internal helper: runs the FITTING + SOLVING + commit-and-split + patch
// update chain synchronously. Identical input bits produce identical
// output bits.
Dictionary CassieSketcher::_run_chain_locally(
		const Ref<CassieInputStroke> &p_input, bool p_emit_signals) {
	Dictionary result;
	result["ok"] = false;
	result["is_valid"] = false;
	result["final_stroke"] = Variant();
	result["new_patches"] = TypedArray<CassieSurfacePatch>();
	result["removed_patches"] = TypedArray<CassieSurfacePatch>();

	if (p_input.is_null()) {
		return result;
	}
	_ensure_owned_state();

	// FITTING pass — fit_to_constraints = false.
	const Dictionary fit = beautifier->beautify(
			p_input, sketch_context, beautifier_params, false, false);
	if (!bool(fit.get("is_valid", false))) {
		return result;
	}
	Ref<Curve3D> fit_curve = fit.get("curve", Variant());
	if (fit_curve.is_null()) {
		return result;
	}
	result["is_valid"] = true;

	// SOLVING pass — fit_to_constraints = true.
	const bool mirror = sketch_context->is_mirror_enabled();
	const Dictionary solved = beautifier->beautify(
			p_input, sketch_context, beautifier_params, true, mirror);
	if (!bool(solved.get("is_valid", false))) {
		return result;
	}
	Ref<Curve3D> solved_curve = solved.get("curve", Variant());
	if (solved_curve.is_null()) {
		return result;
	}

	// Commit-and-split (port of _commit_and_split in xr_sketch_controller.gd).
	// For each detected intersection, split the previously-committed
	// stroke at that point; replace it with the substrokes.
	const real_t snap_threshold = MAX(
			real_t(beautifier_params->get_min_distance_between_anchors()),
			real_t(0.001));
	TypedArray<CassieIntersectionConstraint> detected =
			solved.get("detected_intersections", Variant());
	for (int i = 0; i < detected.size(); ++i) {
		Ref<CassieIntersectionConstraint> ic = detected[i];
		if (ic.is_null()) {
			continue;
		}
		Ref<CassieFinalStroke> old_stroke = ic->get_intersected_stroke();
		if (old_stroke.is_null()) {
			continue;
		}
		int found = -1;
		for (int j = 0; j < committed_strokes.size(); ++j) {
			Ref<CassieFinalStroke> existing = committed_strokes[j];
			if (existing == old_stroke) {
				found = j;
				break;
			}
		}
		if (found < 0) {
			continue;
		}
		TypedArray<CassieIntersectionConstraint> single;
		single.push_back(ic);
		TypedArray<CassieFinalStroke> pieces =
				cassie_split_stroke_at_constraints(old_stroke, single, snap_threshold);
		committed_strokes.remove_at(found);
		for (int k = 0; k < pieces.size(); ++k) {
			committed_strokes.push_back(pieces[k]);
		}
	}

	const bool is_closed = bool(solved.get("is_closed_loop", false));
	Ref<CassieFinalStroke> new_stroke;
	new_stroke.instantiate();
	new_stroke->set_curve(solved_curve, is_closed);
	new_stroke->set_input_samples(p_input->get_points());
	committed_strokes.push_back(new_stroke);

	// Refresh the context for subsequent strokes' Beautify passes.
	sketch_context->set_existing_strokes(committed_strokes);

	// Push the new stroke into the planar sketch graph (per-edge), then
	// run the reactive patch lifecycle.
	PackedVector3Array points;
	const int baked_steps = 32;
	const real_t baked_len = solved_curve->get_baked_length();
	if (baked_len > 0.0) {
		for (int s = 0; s <= baked_steps; ++s) {
			const real_t t = real_t(s) / real_t(baked_steps);
			points.push_back(solved_curve->sample_baked(t * baked_len));
		}
	} else {
		for (int s = 0; s < solved_curve->get_point_count(); ++s) {
			points.push_back(solved_curve->get_point_position(s));
		}
	}
	PackedVector3Array empty_normals;
	if (points.size() >= 2) {
		sketch_graph->add_stroke(points, empty_normals);
	}

	const Dictionary patch_update = surface_manager->update();
	TypedArray<CassieSurfacePatch> new_patches =
			patch_update.get("new_patches", TypedArray<CassieSurfacePatch>());
	TypedArray<CassieSurfacePatch> removed_patches =
			patch_update.get("removed_patches", TypedArray<CassieSurfacePatch>());

	result["ok"] = true;
	result["final_stroke"] = new_stroke;
	result["new_patches"] = new_patches;
	result["removed_patches"] = removed_patches;

	if (p_emit_signals) {
		emit_signal("stroke_committed", new_stroke);
		for (int i = 0; i < new_patches.size(); ++i) {
			emit_signal("patch_added", new_patches[i]);
		}
		for (int i = 0; i < removed_patches.size(); ++i) {
			emit_signal("patch_removed", removed_patches[i]);
		}
	}
	return result;
}

Dictionary CassieSketcher::commit_stroke(int p_stroke_id) {
	Dictionary result;
	result["ok"] = false;

	HashMap<int, InFlightStroke>::Iterator it = in_flight.find(p_stroke_id);
	if (!it) {
		return result;
	}
	Ref<CassieInputStroke> input = it->value.input;
	in_flight.remove(it);

	result = _run_chain_locally(input, true);

	// Stash the encoded packet for the caller's convenience.
	PackedVector3Array positions = input->get_points();
	PackedFloat32Array pressures = input->get_weights();
	last_encoded_packet[p_stroke_id] = CassieStrokePacket::encode(
			peer_id, p_stroke_id, broadcast_seq++,
			bool(result.get("is_valid", false)) &&
					Ref<CassieFinalStroke>(result.get("final_stroke", Variant())).is_valid() &&
					Ref<CassieFinalStroke>(result.get("final_stroke", Variant()))->is_closed_loop(),
			positions, pressures);
	return result;
}

Dictionary CassieSketcher::apply_remote_samples(const PackedByteArray &p_packet) {
	Dictionary result;
	result["ok"] = false;

	const Dictionary decoded = CassieStrokePacket::decode(p_packet);
	if (!bool(decoded.get("ok", false))) {
		return result;
	}
	PackedVector3Array positions = decoded.get("positions", PackedVector3Array());
	PackedFloat32Array pressures = decoded.get("pressures", PackedFloat32Array());
	if (positions.size() < 2) {
		return result;
	}

	_ensure_owned_state();
	Ref<CassieInputStroke> input;
	input.instantiate();
	for (int i = 0; i < positions.size(); ++i) {
		const float t = float(i) * sample_dt;
		const float pr = i < pressures.size() ? pressures[i] : 0.0f;
		input->add_sample(positions[i], t, pr);
	}
	return _run_chain_locally(input, true);
}

PackedByteArray CassieSketcher::encode_stroke_packet(int p_stroke_id) {
	HashMap<int, PackedByteArray>::Iterator it = last_encoded_packet.find(p_stroke_id);
	if (!it) {
		return PackedByteArray();
	}
	return it->value;
}

void CassieSketcher::clear() {
	in_flight.clear();
	last_encoded_packet.clear();
	committed_strokes.clear();
	if (sketch_graph.is_valid()) {
		sketch_graph->clear();
	}
	if (surface_manager.is_valid()) {
		surface_manager->clear();
	}
	if (sketch_context.is_valid()) {
		sketch_context->set_existing_strokes(committed_strokes);
	}
}

void CassieSketcher::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
			_ensure_owned_state();
			break;
		default:
			break;
	}
}

void CassieSketcher::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_beautifier_params", "params"),
			&CassieSketcher::set_beautifier_params);
	ClassDB::bind_method(D_METHOD("get_beautifier_params"),
			&CassieSketcher::get_beautifier_params);
	ClassDB::bind_method(D_METHOD("set_sample_dt", "dt"),
			&CassieSketcher::set_sample_dt);
	ClassDB::bind_method(D_METHOD("get_sample_dt"),
			&CassieSketcher::get_sample_dt);
	ClassDB::bind_method(D_METHOD("set_peer_id", "id"),
			&CassieSketcher::set_peer_id);
	ClassDB::bind_method(D_METHOD("get_peer_id"),
			&CassieSketcher::get_peer_id);
	ClassDB::bind_method(D_METHOD("set_async_triangulation", "enable"),
			&CassieSketcher::set_async_triangulation);
	ClassDB::bind_method(D_METHOD("get_async_triangulation"),
			&CassieSketcher::get_async_triangulation);
	ClassDB::bind_method(D_METHOD("get_sketch_graph"),
			&CassieSketcher::get_sketch_graph);
	ClassDB::bind_method(D_METHOD("get_surface_manager"),
			&CassieSketcher::get_surface_manager);
	ClassDB::bind_method(D_METHOD("get_sketch_context"),
			&CassieSketcher::get_sketch_context);
	ClassDB::bind_method(D_METHOD("get_committed_strokes"),
			&CassieSketcher::get_committed_strokes);

	ClassDB::bind_method(D_METHOD("begin_stroke", "local_position", "pressure"),
			&CassieSketcher::begin_stroke);
	ClassDB::bind_method(D_METHOD("add_sample", "stroke_id", "local_position", "pressure"),
			&CassieSketcher::add_sample);
	ClassDB::bind_method(D_METHOD("commit_stroke", "stroke_id"),
			&CassieSketcher::commit_stroke);
	ClassDB::bind_method(D_METHOD("apply_remote_samples", "packet"),
			&CassieSketcher::apply_remote_samples);
	ClassDB::bind_method(D_METHOD("encode_stroke_packet", "stroke_id"),
			&CassieSketcher::encode_stroke_packet);
	ClassDB::bind_method(D_METHOD("clear"),
			&CassieSketcher::clear);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sample_dt"),
			"set_sample_dt", "get_sample_dt");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "peer_id"),
			"set_peer_id", "get_peer_id");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "async_triangulation"),
			"set_async_triangulation", "get_async_triangulation");

	ADD_SIGNAL(MethodInfo("stroke_committed",
			PropertyInfo(Variant::OBJECT, "final_stroke",
					PROPERTY_HINT_RESOURCE_TYPE, "CassieFinalStroke")));
	ADD_SIGNAL(MethodInfo("patch_added",
			PropertyInfo(Variant::OBJECT, "patch",
					PROPERTY_HINT_RESOURCE_TYPE, "CassieSurfacePatch")));
	ADD_SIGNAL(MethodInfo("patch_removed",
			PropertyInfo(Variant::OBJECT, "patch",
					PROPERTY_HINT_RESOURCE_TYPE, "CassieSurfacePatch")));
}

// _drain_patches is reserved for the async path; not used in the
// default synchronous mode.
Dictionary CassieSketcher::_drain_patches() {
	Dictionary r;
	if (surface_manager.is_valid()) {
		r = surface_manager->update();
	}
	return r;
}

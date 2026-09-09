/**************************************************************************/
/*  xr_grid_xr_pinch.cpp                                                  */
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

#include "xr_grid_xr_pinch.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"

XRGridXRPinch::XRGridXRPinch() {
	world_grab.instantiate();
	left_hand_just_grabbed.instantiate();
	right_hand_just_grabbed.instantiate();
	left_hand_just_ungrabbed.instantiate();
	right_hand_just_ungrabbed.instantiate();
}

void XRGridXRPinch::_handle_debounce(double p_current_grab, double p_dt, bool p_is_left) {
	double &timer = p_is_left ? hand_left_grab_debounce_timer : hand_right_grab_debounce_timer;
	bool &last_state = p_is_left ? last_hand_left_grab_state : last_hand_right_grab_state;
	const bool is_grabbing = p_current_grab > 0.0;
	if (is_grabbing != last_state) {
		timer += p_dt;
		if (timer >= debounce_duration) {
			last_state = is_grabbing;
			timer = 0.0;
		}
	} else {
		timer = 0.0;
	}
}

void XRGridXRPinch::_apply_velocity(double p_dt) {
	linear_velocity *= real_t(1.0 - linear_dampening);
	target_transform.origin += linear_velocity * real_t(p_dt);
	const real_t angular_speed = angular_velocity.length();
	if (angular_speed != 0) {
		const Vector3 rotation_axis = angular_velocity.normalized();
		target_transform = target_transform.rotated(rotation_axis, angular_speed * real_t(p_dt));
		angular_velocity *= real_t(1.0 - angular_dampening);
	}
}

void XRGridXRPinch::_store_velocity(const Transform3D &p_prev,
		const Transform3D &p_cur, double p_dt) {
	if (p_dt <= 0.0) {
		return;
	}
	const Vector3 displacement = p_cur.origin - p_prev.origin;
	linear_velocity = displacement / real_t(p_dt);
	const Quaternion prev_q = p_prev.basis.get_rotation_quaternion();
	const Quaternion cur_q = p_cur.basis.get_rotation_quaternion();
	const Quaternion diff = cur_q * prev_q.inverse();
	const Vector3 axis = diff.get_axis();
	const real_t amount = diff.get_angle();
	angular_velocity = axis * (amount / real_t(p_dt));
}

bool XRGridXRPinch::_both_hands_just_grabbed() const {
	return left_hand_just_grabbed.is_valid() && right_hand_just_grabbed.is_valid() &&
			left_hand_just_grabbed->get_value() && right_hand_just_grabbed->get_value();
}

void XRGridXRPinch::_update_hand_grab_status(bool p_hand_grab,
		double p_prev_hand_grab,
		const Ref<XRGridBoolTimer> &p_just_grabbed,
		const Ref<XRGridBoolTimer> &p_just_ungrabbed) {
	if (p_hand_grab && p_prev_hand_grab == 0.0) {
		p_just_grabbed->set_true(MAX_PINCH_TIME);
	}
	if (!p_hand_grab && p_prev_hand_grab != 0.0) {
		p_just_ungrabbed->set_true(MAX_PINCH_TIME);
	}
}

void XRGridXRPinch::_set_pinch_pivot_and_transform(const Vector3 &p_prev_l,
		const Vector3 &p_prev_r, const Vector3 &p_cur_l, const Vector3 &p_cur_r) {
	from_pivot = (p_prev_l + p_prev_r) * real_t(0.5);
	to_pivot = (p_cur_l + p_cur_r) * real_t(0.5);
	delta_transform = world_grab->get_pinch_transform(p_prev_l, p_prev_r, p_cur_l, p_cur_r);
}

void XRGridXRPinch::_set_orbit_pivot_and_transform(const Vector3 &p_prev_l,
		const Vector3 &p_prev_r, const Vector3 &p_cur_l, const Vector3 &p_cur_r) {
	from_pivot = p_prev_l;
	to_pivot = p_prev_r;
	delta_transform = world_grab->get_orbit_transform(p_prev_l, p_prev_r, p_cur_l, p_cur_r);
}

void XRGridXRPinch::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!hand_left_path.is_empty()) {
				hand_left = Object::cast_to<XRController3D>(get_node_or_null(hand_left_path));
			}
			if (!hand_right_path.is_empty()) {
				hand_right = Object::cast_to<XRController3D>(get_node_or_null(hand_right_path));
			}
			target_transform = get_transform();
			set_process(true);
		} break;
		case NOTIFICATION_PROCESS: {
			if (hand_left == nullptr || hand_right == nullptr) {
				return;
			}
			const double dt = get_process_delta_time();
			const double hl_grab = hand_left->get_float("grip");
			const double hr_grab = hand_right->get_float("grip");

			_handle_debounce(hl_grab, dt, true);
			_handle_debounce(hr_grab, dt, false);

			delta_transform = delta_transform.interpolate_with(Transform3D(), damping * dt);

			_update_hand_grab_status(last_hand_left_grab_state, prev_hand_left_grab,
					left_hand_just_grabbed, left_hand_just_ungrabbed);
			_update_hand_grab_status(last_hand_right_grab_state, prev_hand_right_grab,
					right_hand_just_grabbed, right_hand_just_ungrabbed);

			if (_both_hands_just_grabbed()) {
				state = MODE_PINCH;
			}

			if (!(hl_grab && hr_grab)) {
				if (state != MODE_NONE) {
					state = MODE_NONE;
					delta_transform = Transform3D();
					_apply_velocity(dt);
				}
			}

			if (state == MODE_PINCH) {
				if (!(hl_grab && hr_grab)) {
					state = MODE_NONE;
				}
				if (hl_grab && hr_grab &&
						hand_left_grab_debounce_timer >= debounce_duration &&
						hand_right_grab_debounce_timer >= debounce_duration) {
					_set_pinch_pivot_and_transform(prev_hand_left_transform.origin,
							prev_hand_right_transform.origin,
							hand_left->get_transform().origin,
							hand_right->get_transform().origin);
					_store_velocity(prev_hand_left_transform, hand_left->get_transform(), dt);
					_store_velocity(prev_hand_right_transform, hand_right->get_transform(), dt);
				}
			} else if (state == MODE_ORBIT) {
				if (!(hl_grab && hr_grab)) {
					state = MODE_NONE;
				}
				if (hl_grab && hr_grab &&
						hand_left_grab_debounce_timer >= debounce_duration &&
						hand_right_grab_debounce_timer >= debounce_duration) {
					_set_orbit_pivot_and_transform(prev_hand_left_transform.origin,
							prev_hand_right_transform.origin,
							hand_left->get_transform().origin,
							hand_right->get_transform().origin);
					_store_velocity(prev_hand_left_transform, hand_left->get_transform(), dt);
					_store_velocity(prev_hand_right_transform, hand_right->get_transform(), dt);
				}
			}

			target_transform = delta_transform * target_transform;
			const Transform3D pivot_xform = get_transform() * target_transform.affine_inverse();
			const Vector3 from_pivot_world = pivot_xform.xform(to_pivot);
			const Transform3D blended = world_grab->split_blend(
					get_transform(), target_transform, 0.3, 0.1, 0.1,
					from_pivot_world,
					to_pivot);
			set_transform(blended);

			prev_hand_left_transform = hand_left->get_transform();
			prev_hand_right_transform = hand_right->get_transform();
			prev_hand_left_grab = hl_grab;
			prev_hand_right_grab = hr_grab;
		} break;
		default:
			break;
	}
}

void XRGridXRPinch::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_hand_left_path", "p"),
			&XRGridXRPinch::set_hand_left_path);
	ClassDB::bind_method(D_METHOD("get_hand_left_path"),
			&XRGridXRPinch::get_hand_left_path);
	ClassDB::bind_method(D_METHOD("set_hand_right_path", "p"),
			&XRGridXRPinch::set_hand_right_path);
	ClassDB::bind_method(D_METHOD("get_hand_right_path"),
			&XRGridXRPinch::get_hand_right_path);
	ClassDB::bind_method(D_METHOD("set_debounce_duration", "v"),
			&XRGridXRPinch::set_debounce_duration);
	ClassDB::bind_method(D_METHOD("get_debounce_duration"),
			&XRGridXRPinch::get_debounce_duration);
	ClassDB::bind_method(D_METHOD("set_linear_dampening", "v"),
			&XRGridXRPinch::set_linear_dampening);
	ClassDB::bind_method(D_METHOD("get_linear_dampening"),
			&XRGridXRPinch::get_linear_dampening);
	ClassDB::bind_method(D_METHOD("set_angular_dampening", "v"),
			&XRGridXRPinch::set_angular_dampening);
	ClassDB::bind_method(D_METHOD("get_angular_dampening"),
			&XRGridXRPinch::get_angular_dampening);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "hand_left_path"),
			"set_hand_left_path", "get_hand_left_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "hand_right_path"),
			"set_hand_right_path", "get_hand_right_path");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "debounce_duration"),
			"set_debounce_duration", "get_debounce_duration");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "linear_dampening"),
			"set_linear_dampening", "get_linear_dampening");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angular_dampening"),
			"set_angular_dampening", "get_angular_dampening");

	BIND_ENUM_CONSTANT(MODE_NONE);
	BIND_ENUM_CONSTANT(MODE_GRAB);
	BIND_ENUM_CONSTANT(MODE_PINCH);
	BIND_ENUM_CONSTANT(MODE_ORBIT);
}

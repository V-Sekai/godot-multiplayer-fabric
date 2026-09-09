/**************************************************************************/
/*  xr_grid_flatscreen_controller.cpp                                     */
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

#include "xr_grid_flatscreen_controller.h"

#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "servers/xr/xr_interface.h"
#include "servers/xr/xr_server.h"

void XRGridFlatscreenController::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			Node *origin = get_parent();
			if (origin) {
				cam = Object::cast_to<Camera3D>(origin->get_node_or_null(NodePath("XRCamera3D")));
				hand_l = Object::cast_to<Node3D>(origin->get_node_or_null(NodePath("hand_left")));
				hand_r = Object::cast_to<Node3D>(origin->get_node_or_null(NodePath("hand_right")));
			}
			Ref<XRInterface> xr = XRServer::get_singleton()->find_interface("OpenXR");
			active = !xr.is_valid() || !xr->is_initialized();
			if (active) {
				Input::get_singleton()->set_mouse_mode(Input::MouseMode::MOUSE_MODE_CAPTURED);
			}
			set_process(true);
			set_process_unhandled_input(true);
		} break;
		case NOTIFICATION_PROCESS: {
			if (!active) {
				return;
			}
			Node3D *origin = Object::cast_to<Node3D>(get_parent());
			if (origin == nullptr) {
				return;
			}
			Input *input = Input::get_singleton();
			const double dt = get_process_delta_time();
			// Right stick look (controller 0).
			const double look_x = input->get_joy_axis(0, JoyAxis::RIGHT_X);
			const double look_y = input->get_joy_axis(0, JoyAxis::RIGHT_Y);
			if (Math::abs(look_x) > 0.1) {
				yaw -= look_x * TURN_SPEED * dt;
			}
			if (Math::abs(look_y) > 0.1) {
				pitch = CLAMP(pitch - look_y * TURN_SPEED * dt,
						-Math::PI / 2.0, Math::PI / 2.0);
			}
			const Basis cam_basis = Basis(Vector3(0, 1, 0), real_t(yaw)) *
					Basis(Vector3(1, 0, 0), real_t(pitch));
			if (cam) {
				Transform3D ct = cam->get_transform();
				ct.basis = cam_basis;
				cam->set_transform(ct);
			}
			// Left stick + WASD move.
			double move_x = input->get_joy_axis(0, JoyAxis::LEFT_X);
			double move_y = input->get_joy_axis(0, JoyAxis::LEFT_Y);
			if (input->is_key_pressed(Key::W)) {
				move_y -= 1.0;
			}
			if (input->is_key_pressed(Key::S)) {
				move_y += 1.0;
			}
			if (input->is_key_pressed(Key::A)) {
				move_x -= 1.0;
			}
			if (input->is_key_pressed(Key::D)) {
				move_x += 1.0;
			}
			Vector3 move_dir(real_t(move_x), 0, real_t(move_y));
			if (move_dir.length() > 0.1) {
				move_dir = move_dir.normalized();
				Vector3 forward = cam_basis.xform(Vector3(0, 0, -1));
				forward.y = 0;
				if (forward.length_squared() > 0.0) {
					forward = forward.normalized();
				}
				Vector3 right = cam_basis.xform(Vector3(1, 0, 0));
				right.y = 0;
				if (right.length_squared() > 0.0) {
					right = right.normalized();
				}
				Vector3 pos = origin->get_position();
				pos += (forward * -move_dir.z + right * move_dir.x) * real_t(MOVE_SPEED * dt);
				origin->set_position(pos);
			}
			const Vector3 head_pos = origin->get_position() + Vector3(0, 1.7, 0);
			const Vector3 hand_l_off(-0.3, -0.3, -0.5);
			const Vector3 hand_r_off(0.3, -0.3, -0.5);
			if (hand_l) {
				hand_l->set_global_transform(Transform3D(cam_basis,
						head_pos + cam_basis.xform(hand_l_off)));
			}
			if (hand_r) {
				hand_r->set_global_transform(Transform3D(cam_basis,
						head_pos + cam_basis.xform(hand_r_off)));
			}
			if (cam) {
				cam->set_global_transform(Transform3D(cam_basis, head_pos));
			}
		} break;
		default:
			break;
	}
}

void XRGridFlatscreenController::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_active"), &XRGridFlatscreenController::is_active);
	ClassDB::bind_method(D_METHOD("get_yaw"), &XRGridFlatscreenController::get_yaw);
	ClassDB::bind_method(D_METHOD("get_pitch"), &XRGridFlatscreenController::get_pitch);
}

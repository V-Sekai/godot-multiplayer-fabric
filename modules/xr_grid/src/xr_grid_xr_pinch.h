/**************************************************************************/
/*  xr_grid_xr_pinch.h                                                    */
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

#include "xr_grid_bool_timer.h"
#include "xr_grid_world_grab.h"

#include "scene/3d/node_3d.h"
#include "scene/3d/xr/xr_nodes.h"

class XRGridXRPinch : public Node3D {
	GDCLASS(XRGridXRPinch, Node3D);

public:
	enum Mode {
		MODE_NONE = 0,
		MODE_GRAB = 1,
		MODE_PINCH = 2,
		MODE_ORBIT = 3,
	};

private:
	NodePath hand_left_path;
	NodePath hand_right_path;
	XRController3D *hand_left = nullptr;
	XRController3D *hand_right = nullptr;

	Transform3D prev_hand_left_transform;
	Transform3D prev_hand_right_transform;
	double prev_hand_left_grab = 0.0;
	double prev_hand_right_grab = 0.0;

	Ref<XRGridWorldGrab> world_grab;

	Vector3 from_pivot;
	Vector3 to_pivot;
	Vector3 grab_pivot;
	Transform3D delta_transform;
	Transform3D target_transform;

	double damping = 6.0;
	static constexpr double MAX_PINCH_TIME = 0.2;
	Mode state = MODE_NONE;

	Ref<XRGridBoolTimer> left_hand_just_grabbed;
	Ref<XRGridBoolTimer> right_hand_just_grabbed;
	Ref<XRGridBoolTimer> left_hand_just_ungrabbed;
	Ref<XRGridBoolTimer> right_hand_just_ungrabbed;

	Vector3 linear_velocity;
	Vector3 angular_velocity;

	double debounce_duration = 0.4;
	double hand_left_grab_debounce_timer = 0.0;
	double hand_right_grab_debounce_timer = 0.0;
	bool last_hand_left_grab_state = false;
	bool last_hand_right_grab_state = false;

	double linear_dampening = 0.45;
	double angular_dampening = 0.45;

	void _handle_debounce(double p_current_grab, double p_dt, bool p_is_left);
	void _apply_velocity(double p_dt);
	void _store_velocity(const Transform3D &p_prev, const Transform3D &p_cur, double p_dt);
	bool _both_hands_just_grabbed() const;
	void _update_hand_grab_status(bool p_hand_grab, double p_prev_hand_grab,
			const Ref<XRGridBoolTimer> &p_just_grabbed,
			const Ref<XRGridBoolTimer> &p_just_ungrabbed);
	void _set_pinch_pivot_and_transform(const Vector3 &p_prev_l,
			const Vector3 &p_prev_r, const Vector3 &p_cur_l, const Vector3 &p_cur_r);
	void _set_orbit_pivot_and_transform(const Vector3 &p_prev_l,
			const Vector3 &p_prev_r, const Vector3 &p_cur_l, const Vector3 &p_cur_r);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	XRGridXRPinch();

	void set_hand_left_path(const NodePath &p_path) { hand_left_path = p_path; }
	NodePath get_hand_left_path() const { return hand_left_path; }
	void set_hand_right_path(const NodePath &p_path) { hand_right_path = p_path; }
	NodePath get_hand_right_path() const { return hand_right_path; }
	void set_debounce_duration(double p_v) { debounce_duration = p_v; }
	double get_debounce_duration() const { return debounce_duration; }
	void set_linear_dampening(double p_v) { linear_dampening = p_v; }
	double get_linear_dampening() const { return linear_dampening; }
	void set_angular_dampening(double p_v) { angular_dampening = p_v; }
	double get_angular_dampening() const { return angular_dampening; }
};

VARIANT_ENUM_CAST(XRGridXRPinch::Mode);

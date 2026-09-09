/**************************************************************************/
/*  xr_grid_flatscreen_controller.h                                       */
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

#include "scene/3d/camera_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/main/node.h"

class XRGridFlatscreenController : public Node {
	GDCLASS(XRGridFlatscreenController, Node);

	Camera3D *cam = nullptr;
	Node3D *hand_l = nullptr;
	Node3D *hand_r = nullptr;
	double yaw = 0.0;
	double pitch = 0.0;
	bool active = false;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	XRGridFlatscreenController() = default;

	static constexpr double MOVE_SPEED = 3.0;
	static constexpr double TURN_SPEED = 2.0;
	static constexpr double MOUSE_SENS = 0.002;

	bool is_active() const { return active; }
	double get_yaw() const { return yaw; }
	double get_pitch() const { return pitch; }
};

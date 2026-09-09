/**************************************************************************/
/*  xr_grid_strokes.h                                                     */
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

#include "xr_grid_simple_sketch.h"

#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/xr/xr_nodes.h"

class XRGridStrokes : public MeshInstance3D {
	GDCLASS(XRGridStrokes, MeshInstance3D);

	NodePath hand_left_path;
	NodePath hand_right_path;
	XRController3D *hand_left = nullptr;
	XRController3D *hand_right = nullptr;
	Ref<XRGridSimpleSketch> simple_sketch;
	Transform3D prev_hand_left_transform;
	Transform3D prev_hand_right_transform;
	double prev_hand_left_pressed = 0.0;
	double prev_hand_right_pressed = 0.0;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	XRGridStrokes();

	void set_hand_left_path(const NodePath &p_path) { hand_left_path = p_path; }
	NodePath get_hand_left_path() const { return hand_left_path; }
	void set_hand_right_path(const NodePath &p_path) { hand_right_path = p_path; }
	NodePath get_hand_right_path() const { return hand_right_path; }
};

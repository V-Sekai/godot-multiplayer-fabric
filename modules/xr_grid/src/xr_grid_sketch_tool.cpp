/**************************************************************************/
/*  xr_grid_sketch_tool.cpp                                               */
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

#include "xr_grid_sketch_tool.h"

#include "core/object/class_db.h"
#include "scene/3d/mesh_instance_3d.h"

XRGridSketchTool::XRGridSketchTool() {
	simple_sketch.instantiate();
}

void XRGridSketchTool::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!canvas_path.is_empty()) {
				canvas = Object::cast_to<Node3D>(get_node_or_null(canvas_path));
			}
			if (canvas) {
				MeshInstance3D *strokes_mi = Object::cast_to<MeshInstance3D>(
						canvas->get_node_or_null(NodePath("strokes")));
				if (strokes_mi) {
					Ref<ArrayMesh> am = strokes_mi->get_mesh();
					if (am.is_valid()) {
						simple_sketch->set_target_mesh(am);
					}
				}
			}
			set_process(true);
		} break;
		case NOTIFICATION_PROCESS: {
			if (active && !prev_active) {
				simple_sketch->stroke_begin();
			}
			if (active && prev_active && canvas) {
				const Vector3 point = canvas->to_local(get_global_transform().origin);
				const double scale_x = canvas->get_scale().x;
				const double width = (scale_x > 0.0) ? (pressure / scale_x) : pressure;
				simple_sketch->stroke_add(point, width, color);
			}
			if (!active && prev_active) {
				simple_sketch->stroke_end();
			}
			prev_active = active;
		} break;
		default:
			break;
	}
}

void XRGridSketchTool::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_canvas_path", "p"),
			&XRGridSketchTool::set_canvas_path);
	ClassDB::bind_method(D_METHOD("get_canvas_path"),
			&XRGridSketchTool::get_canvas_path);
	ClassDB::bind_method(D_METHOD("set_active", "v"), &XRGridSketchTool::set_active);
	ClassDB::bind_method(D_METHOD("is_active"), &XRGridSketchTool::is_active);
	ClassDB::bind_method(D_METHOD("set_pressure", "v"), &XRGridSketchTool::set_pressure);
	ClassDB::bind_method(D_METHOD("get_pressure"), &XRGridSketchTool::get_pressure);
	ClassDB::bind_method(D_METHOD("set_color", "v"), &XRGridSketchTool::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &XRGridSketchTool::get_color);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "canvas_path"),
			"set_canvas_path", "get_canvas_path");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "active"),
			"set_active", "is_active");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pressure"),
			"set_pressure", "get_pressure");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"),
			"set_color", "get_color");
}

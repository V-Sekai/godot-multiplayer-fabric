/**************************************************************************/
/*  xr_grid_hand.cpp                                                      */
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

#include "xr_grid_hand.h"

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"
#include "scene/resources/mesh.h"

void XRGridHand::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY:
			set_process(true);
			break;
		case NOTIFICATION_PROCESS: {
			Node *st_node = sketch_tool_path.is_empty()
					? get_node_or_null(NodePath("SketchTool"))
					: get_node_or_null(sketch_tool_path);
			if (st_node == nullptr) {
				return;
			}
			const double trigger = get_float("trigger");
			const bool active = trigger > 0.05;
			if (st_node->has_method("set_active")) {
				st_node->call("set_active", active);
			} else {
				st_node->set("active", active);
			}
			if (active) {
				const double pressure = trigger * max_pressure_size;
				if (st_node->has_method("set_pressure")) {
					st_node->call("set_pressure", pressure);
				} else {
					st_node->set("pressure", pressure);
				}
			}
		} break;
		default:
			break;
	}
}

void XRGridHand::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_sketch_tool_path", "path"),
			&XRGridHand::set_sketch_tool_path);
	ClassDB::bind_method(D_METHOD("get_sketch_tool_path"),
			&XRGridHand::get_sketch_tool_path);
	ClassDB::bind_method(D_METHOD("set_save_path", "p"),
			&XRGridHand::set_save_path);
	ClassDB::bind_method(D_METHOD("get_save_path"),
			&XRGridHand::get_save_path);
	ClassDB::bind_method(D_METHOD("set_max_pressure_size", "v"),
			&XRGridHand::set_max_pressure_size);
	ClassDB::bind_method(D_METHOD("get_max_pressure_size"),
			&XRGridHand::get_max_pressure_size);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sketch_tool_path"),
			"set_sketch_tool_path", "get_sketch_tool_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "save_path"),
			"set_save_path", "get_save_path");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_pressure_size"),
			"set_max_pressure_size", "get_max_pressure_size");
}

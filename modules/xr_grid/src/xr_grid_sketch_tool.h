/**************************************************************************/
/*  xr_grid_sketch_tool.h                                                 */
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

#include "scene/3d/node_3d.h"

class XRGridSketchTool : public Node3D {
	GDCLASS(XRGridSketchTool, Node3D);

	NodePath canvas_path;
	Node3D *canvas = nullptr;
	bool active = false;
	double pressure = 0.0;
	Color color = Color(0, 0, 0);
	Ref<XRGridSimpleSketch> simple_sketch;
	bool prev_active = false;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	XRGridSketchTool();

	void set_canvas_path(const NodePath &p_path) { canvas_path = p_path; }
	NodePath get_canvas_path() const { return canvas_path; }
	void set_active(bool p_v) { active = p_v; }
	bool is_active() const { return active; }
	void set_pressure(double p_v) { pressure = p_v; }
	double get_pressure() const { return pressure; }
	void set_color(const Color &p_v) { color = p_v; }
	Color get_color() const { return color; }
};

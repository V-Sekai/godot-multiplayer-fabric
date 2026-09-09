/**************************************************************************/
/*  xr_grid_procedural_grid_3d.h                                          */
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

#include "scene/3d/node_3d.h"
#include "scene/resources/gradient_texture.h"

class XRGridProceduralGrid3D : public Node3D {
	GDCLASS(XRGridProceduralGrid3D, Node3D);

	Ref<GradientTexture1D> level_color;
	double distance_between_points = 0.5;
	NodePath focus_node_path;
	NodePath grid1_path = NodePath("BaseProceduralGrid3D");
	NodePath grid2_path = NodePath("BaseProceduralGrid3D/BaseProceduralGrid3D2");

	Node3D *grid1 = nullptr;
	Node3D *grid2 = nullptr;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	XRGridProceduralGrid3D() = default;

	void set_level_color(const Ref<GradientTexture1D> &p_t) { level_color = p_t; }
	Ref<GradientTexture1D> get_level_color() const { return level_color; }
	void set_distance_between_points(double p_v) { distance_between_points = p_v; }
	double get_distance_between_points() const { return distance_between_points; }
	void set_focus_node_path(const NodePath &p_path) { focus_node_path = p_path; }
	NodePath get_focus_node_path() const { return focus_node_path; }
	void set_grid1_path(const NodePath &p_path) { grid1_path = p_path; }
	NodePath get_grid1_path() const { return grid1_path; }
	void set_grid2_path(const NodePath &p_path) { grid2_path = p_path; }
	NodePath get_grid2_path() const { return grid2_path; }
};

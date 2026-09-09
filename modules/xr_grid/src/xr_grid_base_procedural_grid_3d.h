/**************************************************************************/
/*  xr_grid_base_procedural_grid_3d.h                                     */
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

#include "scene/3d/multimesh_instance_3d.h"

class XRGridBaseProceduralGrid3D : public MultiMeshInstance3D {
	GDCLASS(XRGridBaseProceduralGrid3D, MultiMeshInstance3D);

	int points_per_dimension = 6;
	int n_vertex_circle = 32;
	double fade_zone = 0.5;
	double far_fade = 1.0;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	XRGridBaseProceduralGrid3D() = default;

	void set_points_per_dimension(int p_v);
	int get_points_per_dimension() const { return points_per_dimension; }
	void set_n_vertex_circle(int p_v) { n_vertex_circle = p_v; }
	int get_n_vertex_circle() const { return n_vertex_circle; }
	void set_fade_zone(double p_v) { fade_zone = p_v; }
	double get_fade_zone() const { return fade_zone; }
	void set_far_fade(double p_v) { far_fade = p_v; }
	double get_far_fade() const { return far_fade; }

	void set_points_per_dimension_from_fade();
	void regenerate_mesh();
};

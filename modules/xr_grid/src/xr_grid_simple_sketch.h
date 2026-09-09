/**************************************************************************/
/*  xr_grid_simple_sketch.h                                               */
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

#include "core/object/ref_counted.h"
#include "scene/resources/mesh.h"

class XRGridSimpleSketch : public RefCounted {
	GDCLASS(XRGridSimpleSketch, RefCounted);

	Ref<ArrayMesh> target_mesh;
	bool is_beginning = false;
	Vector3 prev_point;
	double prev_size = 0.0;
	Color prev_color;
	Vector3 prev_tangent;

protected:
	static void _bind_methods();

public:
	XRGridSimpleSketch() = default;

	void set_target_mesh(const Ref<ArrayMesh> &p_mesh) { target_mesh = p_mesh; }
	Ref<ArrayMesh> get_target_mesh() const { return target_mesh; }

	void stroke_begin();
	void stroke_add(const Vector3 &p_point, double p_size, const Color &p_color);
	void stroke_end();

	void add_line(const Vector3 &p_from, const Vector3 &p_to,
			double p_from_size, double p_to_size,
			const Color &p_from_color, const Color &p_to_color,
			bool p_begin_stroke);
};

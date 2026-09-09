/**************************************************************************/
/*  xr_grid_simple_sketch.cpp                                             */
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

#include "xr_grid_simple_sketch.h"

#include "core/object/class_db.h"

void XRGridSimpleSketch::stroke_begin() {
	is_beginning = true;
}

void XRGridSimpleSketch::stroke_add(const Vector3 &p_point, double p_size,
		const Color &p_color) {
	if (is_beginning) {
		add_line(p_point, p_point, p_size, p_size, p_color, p_color, true);
		is_beginning = false;
	} else {
		add_line(prev_point, p_point, prev_size, p_size, prev_color, p_color, false);
	}
	prev_point = p_point;
	prev_size = p_size;
	prev_color = p_color;
}

void XRGridSimpleSketch::stroke_end() {
	is_beginning = false;
}

void XRGridSimpleSketch::add_line(const Vector3 &p_from, const Vector3 &p_to,
		double p_from_size, double p_to_size,
		const Color &p_from_color, const Color &p_to_color,
		bool p_begin_stroke) {
	if (target_mesh.is_null()) {
		return;
	}
	Vector3 from_tangent = prev_tangent;
	const Vector3 to_tangent = p_to - p_from;
	if (p_begin_stroke) {
		from_tangent = to_tangent;
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	if (target_mesh->get_surface_count() > 0) {
		arrays = target_mesh->surface_get_arrays(0);
	} else {
		arrays[Mesh::ARRAY_VERTEX] = PackedVector3Array();
		arrays[Mesh::ARRAY_TANGENT] = PackedFloat32Array();
		arrays[Mesh::ARRAY_TEX_UV] = PackedVector2Array();
		arrays[Mesh::ARRAY_COLOR] = PackedColorArray();
	}

	PackedVector3Array verts = arrays[Mesh::ARRAY_VERTEX];
	PackedFloat32Array tangents = arrays[Mesh::ARRAY_TANGENT];
	PackedVector2Array uvs = arrays[Mesh::ARRAY_TEX_UV];
	PackedColorArray colors = arrays[Mesh::ARRAY_COLOR];

	auto push_vertex = [&](const Vector3 &p_v, const Vector3 &p_t,
							   const Vector2 &p_uv, const Color &p_c) {
		verts.push_back(p_v);
		tangents.push_back(real_t(p_t.x));
		tangents.push_back(real_t(p_t.y));
		tangents.push_back(real_t(p_t.z));
		tangents.push_back(1.0f);
		uvs.push_back(p_uv);
		colors.push_back(p_c);
	};

	// Two triangles A-B-D and A-D-C as the upstream — six vertices.
	push_vertex(p_from, from_tangent, Vector2(0, real_t(-p_from_size)), p_from_color);
	push_vertex(p_from, from_tangent, Vector2(0, real_t(p_from_size)), p_from_color);
	push_vertex(p_to, to_tangent, Vector2(0, real_t(p_to_size)), p_to_color);
	push_vertex(p_from, from_tangent, Vector2(0, real_t(-p_from_size)), p_from_color);
	push_vertex(p_to, to_tangent, Vector2(0, real_t(p_to_size)), p_to_color);
	push_vertex(p_to, to_tangent, Vector2(0, real_t(-p_to_size)), p_to_color);

	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_TANGENT] = tangents;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_COLOR] = colors;

	target_mesh->clear_surfaces();
	target_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

	prev_tangent = to_tangent;
}

void XRGridSimpleSketch::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_target_mesh", "mesh"),
			&XRGridSimpleSketch::set_target_mesh);
	ClassDB::bind_method(D_METHOD("get_target_mesh"),
			&XRGridSimpleSketch::get_target_mesh);
	ClassDB::bind_method(D_METHOD("stroke_begin"),
			&XRGridSimpleSketch::stroke_begin);
	ClassDB::bind_method(D_METHOD("stroke_add", "point", "size", "color"),
			&XRGridSimpleSketch::stroke_add);
	ClassDB::bind_method(D_METHOD("stroke_end"),
			&XRGridSimpleSketch::stroke_end);
	ClassDB::bind_method(D_METHOD("add_line", "from", "to",
								 "from_size", "to_size", "from_color", "to_color",
								 "begin_stroke"),
			&XRGridSimpleSketch::add_line);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "target_mesh",
						 PROPERTY_HINT_RESOURCE_TYPE, "ArrayMesh"),
			"set_target_mesh", "get_target_mesh");
}

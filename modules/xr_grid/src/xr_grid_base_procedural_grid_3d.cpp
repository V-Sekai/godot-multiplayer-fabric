/**************************************************************************/
/*  xr_grid_base_procedural_grid_3d.cpp                                   */
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

#include "xr_grid_base_procedural_grid_3d.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/multimesh.h"

void XRGridBaseProceduralGrid3D::set_points_per_dimension(int p_v) {
	points_per_dimension = MAX(1, p_v);
	Ref<MultiMesh> mm = get_multimesh();
	if (mm.is_valid()) {
		mm->set_instance_count(points_per_dimension * points_per_dimension * points_per_dimension);
	}
	regenerate_mesh();
}

void XRGridBaseProceduralGrid3D::set_points_per_dimension_from_fade() {
	set_points_per_dimension(int((fade_zone + far_fade) * 2.0));
}

void XRGridBaseProceduralGrid3D::regenerate_mesh() {
	Ref<MultiMesh> mm = get_multimesh();
	if (mm.is_null()) {
		mm.instantiate();
		mm->set_transform_format(MultiMesh::TRANSFORM_3D);
		mm->set_instance_count(points_per_dimension * points_per_dimension * points_per_dimension);
		set_multimesh(mm);
	}
	Ref<ArrayMesh> am;
	am.instantiate();

	// Circle.
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	PackedVector3Array verts, normals;
	PackedVector2Array uvs;
	Vector3 circle_p(1, 0, 0);
	const double angle = Math::TAU * 1.0 / double(n_vertex_circle);
	for (int i = 1; i <= n_vertex_circle; ++i) {
		const Vector3 on_circle(real_t(Math::cos(i * angle)),
				real_t(Math::sin(i * angle)), 0.0);
		verts.push_back(circle_p);
		normals.push_back(Vector3());
		uvs.push_back(Vector2());
		verts.push_back(Vector3());
		normals.push_back(Vector3());
		uvs.push_back(Vector2());
		verts.push_back(on_circle);
		normals.push_back(Vector3());
		uvs.push_back(Vector2());
		circle_p = on_circle;
	}
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	am->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

	// 3 axis quads.
	static const Vector3 axes[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };
	for (int i = 0; i < 3; ++i) {
		Array a;
		a.resize(Mesh::ARRAY_MAX);
		PackedVector3Array av, an;
		PackedVector2Array au;
		av.push_back(Vector3(0, -0.5, 0));
		av.push_back(Vector3(0, 0.5, 0));
		av.push_back(Vector3(1, -0.5, 0));
		av.push_back(Vector3(1, 0.5, 0));
		for (int j = 0; j < 4; ++j) {
			an.push_back(axes[i]);
			au.push_back(Vector2(1, 0));
		}
		a[Mesh::ARRAY_VERTEX] = av;
		a[Mesh::ARRAY_NORMAL] = an;
		a[Mesh::ARRAY_TEX_UV] = au;
		am->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLE_STRIP, a);
	}

	mm->set_mesh(am);

	const int ppd = points_per_dimension;
	const int half = ppd / 2 - 1;
	for (int i = 0; i < mm->get_instance_count(); ++i) {
		Transform3D t;
		t.origin = Vector3(
				real_t(i % ppd),
				real_t((i / ppd) % ppd),
				real_t(i / (ppd * ppd)));
		t.origin -= Vector3(real_t(half), real_t(half), real_t(half));
		mm->set_instance_transform(i, t);
	}
}

void XRGridBaseProceduralGrid3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY:
			if (get_multimesh().is_null()) {
				regenerate_mesh();
			}
			set_process(true);
			break;
		case NOTIFICATION_PROCESS: {
			Ref<ShaderMaterial> sm = get_material_override();
			if (sm.is_valid()) {
				sm->set_shader_parameter("_GridCenter", get_global_position());
			}
		} break;
		default:
			break;
	}
}

void XRGridBaseProceduralGrid3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_points_per_dimension", "v"),
			&XRGridBaseProceduralGrid3D::set_points_per_dimension);
	ClassDB::bind_method(D_METHOD("get_points_per_dimension"),
			&XRGridBaseProceduralGrid3D::get_points_per_dimension);
	ClassDB::bind_method(D_METHOD("set_n_vertex_circle", "v"),
			&XRGridBaseProceduralGrid3D::set_n_vertex_circle);
	ClassDB::bind_method(D_METHOD("get_n_vertex_circle"),
			&XRGridBaseProceduralGrid3D::get_n_vertex_circle);
	ClassDB::bind_method(D_METHOD("set_fade_zone", "v"),
			&XRGridBaseProceduralGrid3D::set_fade_zone);
	ClassDB::bind_method(D_METHOD("get_fade_zone"),
			&XRGridBaseProceduralGrid3D::get_fade_zone);
	ClassDB::bind_method(D_METHOD("set_far_fade", "v"),
			&XRGridBaseProceduralGrid3D::set_far_fade);
	ClassDB::bind_method(D_METHOD("get_far_fade"),
			&XRGridBaseProceduralGrid3D::get_far_fade);
	ClassDB::bind_method(D_METHOD("set_points_per_dimension_from_fade"),
			&XRGridBaseProceduralGrid3D::set_points_per_dimension_from_fade);
	ClassDB::bind_method(D_METHOD("regenerate_mesh"),
			&XRGridBaseProceduralGrid3D::regenerate_mesh);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "points_per_dimension"),
			"set_points_per_dimension", "get_points_per_dimension");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "n_vertex_circle"),
			"set_n_vertex_circle", "get_n_vertex_circle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_zone"),
			"set_fade_zone", "get_fade_zone");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "far_fade"),
			"set_far_fade", "get_far_fade");
}

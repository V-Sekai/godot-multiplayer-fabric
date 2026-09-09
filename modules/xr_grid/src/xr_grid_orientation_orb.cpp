/**************************************************************************/
/*  xr_grid_orientation_orb.cpp                                           */
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

#include "xr_grid_orientation_orb.h"

#include "core/object/class_db.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"

void XRGridOrientationOrb::setup(const Color &p_player_color) {
	player_color = p_player_color;
	if (!initialized) {
		_build();
	} else {
		// Refresh material colors if rebuild not needed.
		if (!spheres.is_empty() && spheres[0]) {
			Ref<StandardMaterial3D> m = spheres[0]->get_material_override();
			if (m.is_valid()) {
				m->set_albedo(player_color);
			}
		}
		if (lines) {
			Ref<StandardMaterial3D> m = lines->get_material_override();
			if (m.is_valid()) {
				Color c = player_color.lightened(0.3);
				c.a = 0.5;
				m->set_albedo(c);
			}
		}
	}
}

void XRGridOrientationOrb::_build() {
	if (initialized) {
		return;
	}
	initialized = true;
	static const Color axis_colors[6] = {
		Color(1, 0, 0),
		Color(0.5, 0, 0),
		Color(0, 1, 0),
		Color(0, 0.5, 0),
		Color(0, 0, 1),
		Color(0, 0, 0.5),
	};

	Ref<SphereMesh> sphere_mesh;
	sphere_mesh.instantiate();
	sphere_mesh->set_radius(SPHERE_RADIUS);
	sphere_mesh->set_height(SPHERE_RADIUS * 2.0);

	// Origin sphere (idx 0).
	MeshInstance3D *origin_sphere = memnew(MeshInstance3D);
	origin_sphere->set_mesh(sphere_mesh);
	Ref<StandardMaterial3D> origin_mat;
	origin_mat.instantiate();
	origin_mat->set_albedo(player_color);
	origin_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	origin_sphere->set_material_override(origin_mat);
	add_child(origin_sphere);
	spheres.push_back(origin_sphere);

	// 6 axis-endpoint spheres.
	for (int i = 0; i < 6; ++i) {
		MeshInstance3D *s = memnew(MeshInstance3D);
		s->set_mesh(sphere_mesh);
		Ref<StandardMaterial3D> mat;
		mat.instantiate();
		mat->set_albedo(axis_colors[i]);
		mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		s->set_material_override(mat);
		add_child(s);
		spheres.push_back(s);
	}

	// Axis lines container.
	lines = memnew(MeshInstance3D);
	Ref<StandardMaterial3D> line_mat;
	line_mat.instantiate();
	Color line_color = player_color.lightened(0.3);
	line_color.a = 0.5;
	line_mat->set_albedo(line_color);
	line_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	line_mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	lines->set_material_override(line_mat);
	add_child(lines);
}

void XRGridOrientationOrb::update_from_basis(const Basis &p_basis) {
	if (spheres.size() < 7) {
		return;
	}
	spheres[0]->set_position(Vector3());
	for (int axis = 0; axis < 3; ++axis) {
		const Vector3 col = p_basis.get_column(axis) * AXIS_RADIUS;
		spheres[1 + axis * 2]->set_position(col);
		spheres[2 + axis * 2]->set_position(-col);
	}
	Ref<ImmediateMesh> im;
	im.instantiate();
	for (int axis = 0; axis < 3; ++axis) {
		const Vector3 col = p_basis.get_column(axis) * AXIS_RADIUS;
		im->surface_begin(Mesh::PRIMITIVE_LINES);
		im->surface_add_vertex(-col);
		im->surface_add_vertex(col);
		im->surface_end();
	}
	if (lines) {
		lines->set_mesh(im);
	}
}

void XRGridOrientationOrb::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY && !initialized) {
		_build();
	}
}

void XRGridOrientationOrb::_bind_methods() {
	ClassDB::bind_method(D_METHOD("setup", "player_color"),
			&XRGridOrientationOrb::setup);
	ClassDB::bind_method(D_METHOD("update_from_basis", "basis"),
			&XRGridOrientationOrb::update_from_basis);
}

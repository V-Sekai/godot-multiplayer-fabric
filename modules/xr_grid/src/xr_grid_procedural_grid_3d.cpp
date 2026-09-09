/**************************************************************************/
/*  xr_grid_procedural_grid_3d.cpp                                        */
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

#include "xr_grid_procedural_grid_3d.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "scene/resources/material.h"

void XRGridProceduralGrid3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY:
			grid1 = Object::cast_to<Node3D>(get_node_or_null(grid1_path));
			grid2 = Object::cast_to<Node3D>(get_node_or_null(grid2_path));
			set_process(true);
			break;
		case NOTIFICATION_PROCESS: {
			if (grid1 == nullptr) {
				return;
			}
			const double basis_x_len = get_global_transform().basis.get_column(0).length();
			if (basis_x_len <= 0.0) {
				return;
			}
			const double exponent = Math::log(basis_x_len) / Math::log(2.0);
			const double level = Math::floor(exponent);
			const double scale = distance_between_points / Math::pow(2.0, level);
			grid1->set_scale(Vector3(real_t(scale), real_t(scale), real_t(scale)));

			if (level_color.is_valid() && level_color->get_gradient().is_valid()) {
				Ref<Gradient> g = level_color->get_gradient();
				Color c1 = g->get_color_at_offset(real_t(level * 0.5 + 0.5));
				c1.a *= CLAMP(real_t(2.0 - (exponent - Math::floor(exponent)) * 2.0), 0.0f, 1.0f);
				Ref<ShaderMaterial> sm1 = grid1->call("get_material_override");
				if (sm1.is_valid()) {
					sm1->set_shader_parameter("_Color", c1);
				}
				if (grid2) {
					Color c2 = g->get_color_at_offset(real_t((level + 1) * 0.5 + 0.5));
					c2.a *= CLAMP(real_t((exponent - Math::floor(exponent)) * 2.0), 0.0f, 1.0f);
					Ref<ShaderMaterial> sm2 = grid2->call("get_material_override");
					if (sm2.is_valid()) {
						sm2->set_shader_parameter("_Color", c2);
					}
				}
			}
			if (!focus_node_path.is_empty()) {
				Node3D *fn = Object::cast_to<Node3D>(get_node_or_null(focus_node_path));
				if (fn) {
					const Vector3 focus = fn->get_global_position();
					Ref<ShaderMaterial> sm1 = grid1->call("get_material_override");
					if (sm1.is_valid()) {
						sm1->set_shader_parameter("_FocusPoint", focus);
					}
					if (grid2) {
						Ref<ShaderMaterial> sm2 = grid2->call("get_material_override");
						if (sm2.is_valid()) {
							sm2->set_shader_parameter("_FocusPoint", focus);
						}
					}
				}
			}
		} break;
		default:
			break;
	}
}

void XRGridProceduralGrid3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_level_color", "t"),
			&XRGridProceduralGrid3D::set_level_color);
	ClassDB::bind_method(D_METHOD("get_level_color"),
			&XRGridProceduralGrid3D::get_level_color);
	ClassDB::bind_method(D_METHOD("set_distance_between_points", "v"),
			&XRGridProceduralGrid3D::set_distance_between_points);
	ClassDB::bind_method(D_METHOD("get_distance_between_points"),
			&XRGridProceduralGrid3D::get_distance_between_points);
	ClassDB::bind_method(D_METHOD("set_focus_node_path", "p"),
			&XRGridProceduralGrid3D::set_focus_node_path);
	ClassDB::bind_method(D_METHOD("get_focus_node_path"),
			&XRGridProceduralGrid3D::get_focus_node_path);
	ClassDB::bind_method(D_METHOD("set_grid1_path", "p"),
			&XRGridProceduralGrid3D::set_grid1_path);
	ClassDB::bind_method(D_METHOD("get_grid1_path"),
			&XRGridProceduralGrid3D::get_grid1_path);
	ClassDB::bind_method(D_METHOD("set_grid2_path", "p"),
			&XRGridProceduralGrid3D::set_grid2_path);
	ClassDB::bind_method(D_METHOD("get_grid2_path"),
			&XRGridProceduralGrid3D::get_grid2_path);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "level_color",
						 PROPERTY_HINT_RESOURCE_TYPE, "GradientTexture1D"),
			"set_level_color", "get_level_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distance_between_points"),
			"set_distance_between_points", "get_distance_between_points");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "focus_node_path"),
			"set_focus_node_path", "get_focus_node_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "grid1_path"),
			"set_grid1_path", "get_grid1_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "grid2_path"),
			"set_grid2_path", "get_grid2_path");
}

/**************************************************************************/
/*  xr_grid_capsule_persona.cpp                                           */
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

#include "xr_grid_capsule_persona.h"

#include "core/object/class_db.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"

void XRGridCapsulePersona::_rebuild() {
	Ref<CapsuleMesh> capsule;
	capsule.instantiate();
	capsule->set_height(height);
	capsule->set_radius(radius);
	set_mesh(capsule);

	Ref<StandardMaterial3D> mat;
	mat.instantiate();
	mat->set_albedo(body_color);
	mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_PER_PIXEL);
	set_material_override(mat);
}

void XRGridCapsulePersona::set_height(double p_v) {
	height = MAX(0.1, p_v);
	_rebuild();
}

void XRGridCapsulePersona::set_radius(double p_v) {
	radius = MAX(0.01, p_v);
	_rebuild();
}

void XRGridCapsulePersona::set_body_color(const Color &p_c) {
	body_color = p_c;
	Ref<StandardMaterial3D> mat = get_material_override();
	if (mat.is_valid()) {
		mat->set_albedo(body_color);
	} else {
		_rebuild();
	}
}

void XRGridCapsulePersona::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		if (get_mesh().is_null()) {
			_rebuild();
		}
	}
}

void XRGridCapsulePersona::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_height", "v"), &XRGridCapsulePersona::set_height);
	ClassDB::bind_method(D_METHOD("get_height"), &XRGridCapsulePersona::get_height);
	ClassDB::bind_method(D_METHOD("set_radius", "v"), &XRGridCapsulePersona::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &XRGridCapsulePersona::get_radius);
	ClassDB::bind_method(D_METHOD("set_body_color", "c"),
			&XRGridCapsulePersona::set_body_color);
	ClassDB::bind_method(D_METHOD("get_body_color"),
			&XRGridCapsulePersona::get_body_color);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height"),
			"set_height", "get_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius"),
			"set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "body_color"),
			"set_body_color", "get_body_color");
}

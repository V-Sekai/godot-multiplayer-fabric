/**************************************************************************/
/*  cassie_surface_constraint.cpp                                         */
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

#include "cassie_surface_constraint.h"

#include "core/object/class_db.h"

void CassieSurfaceConstraint::leave(const Vector3 &p_position) {
	left_mid_stroke = true;
	end_position = p_position;
}

void CassieSurfaceConstraint::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_patch_id", "id"), &CassieSurfaceConstraint::set_patch_id);
	ClassDB::bind_method(D_METHOD("get_patch_id"), &CassieSurfaceConstraint::get_patch_id);
	ClassDB::bind_method(D_METHOD("set_start_position", "position"),
			&CassieSurfaceConstraint::set_start_position);
	ClassDB::bind_method(D_METHOD("get_start_position"), &CassieSurfaceConstraint::get_start_position);
	ClassDB::bind_method(D_METHOD("has_left_mid_stroke"),
			&CassieSurfaceConstraint::has_left_mid_stroke);
	ClassDB::bind_method(D_METHOD("set_end_position", "position"),
			&CassieSurfaceConstraint::set_end_position);
	ClassDB::bind_method(D_METHOD("get_end_position"), &CassieSurfaceConstraint::get_end_position);
	ClassDB::bind_method(D_METHOD("leave", "position"), &CassieSurfaceConstraint::leave);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "patch_id"), "set_patch_id", "get_patch_id");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "start_position"),
			"set_start_position", "get_start_position");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "end_position"),
			"set_end_position", "get_end_position");
}

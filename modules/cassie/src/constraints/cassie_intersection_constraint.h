/**************************************************************************/
/*  cassie_intersection_constraint.h                                      */
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

#include "../sketch/cassie_final_stroke.h"
#include "cassie_constraint.h"

#include "core/math/vector3.h"
#include "core/object/ref_counted.h"

// CassieIntersectionConstraint represents a snap from a new stroke onto an
// existing FinalStroke at a specific point. Port of E:/cassie/Assets/
// Scripts\Data\Constraints\IntersectionConstraint.cs.

class CassieIntersectionConstraint : public CassieConstraint {
	GDCLASS(CassieIntersectionConstraint, CassieConstraint);

	Ref<CassieFinalStroke> intersected_stroke;
	Vector3 old_curve_position;
	Vector3 old_curve_tangent;
	real_t old_curve_offset = 0.0;
	bool is_at_node = false;

protected:
	static void _bind_methods();

public:
	CassieIntersectionConstraint() = default;

	void set_intersected_stroke(const Ref<CassieFinalStroke> &p_stroke) { intersected_stroke = p_stroke; }
	Ref<CassieFinalStroke> get_intersected_stroke() const { return intersected_stroke; }

	void set_old_curve_position(const Vector3 &p_pos) { old_curve_position = p_pos; }
	Vector3 get_old_curve_position() const { return old_curve_position; }

	void set_old_curve_tangent(const Vector3 &p_tan) { old_curve_tangent = p_tan; }
	Vector3 get_old_curve_tangent() const { return old_curve_tangent; }

	void set_old_curve_offset(real_t p_offset) { old_curve_offset = p_offset; }
	real_t get_old_curve_offset() const { return old_curve_offset; }

	void set_is_at_node(bool p_v) { is_at_node = p_v; }
	bool get_is_at_node() const { return is_at_node; }
};

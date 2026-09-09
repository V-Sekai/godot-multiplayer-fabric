/**************************************************************************/
/*  xr_grid_world_grab.cpp                                                */
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

#include "xr_grid_world_grab.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"

Transform3D XRGridWorldGrab::get_grab_transform(const Transform3D &p_from,
		const Transform3D &p_to) {
	return p_to * p_from.affine_inverse();
}

Transform3D XRGridWorldGrab::get_orbit_transform(const Vector3 &p_from_pivot,
		const Vector3 &p_from_b, const Vector3 &p_to_pivot,
		const Vector3 &p_to_b) {
	const Vector3 fb = p_from_b - p_from_pivot;
	const Vector3 tb = p_to_b - p_to_pivot;
	Vector3 axis = fb.cross(tb);
	if (axis == Vector3()) {
		axis = Vector3(1, 0, 0);
	}
	const real_t angle = fb.angle_to(tb);
	Transform3D t;
	t = t.translated(-p_from_pivot);
	t = t.rotated(axis.normalized(), angle);
	t = t.translated(p_to_pivot);
	return t;
}

Transform3D XRGridWorldGrab::get_pinch_transform(const Vector3 &p_from_a,
		const Vector3 &p_from_b, const Vector3 &p_to_a, const Vector3 &p_to_b) {
	const real_t from_len_sq = (p_from_b - p_from_a).length_squared();
	const real_t to_len_sq = (p_to_b - p_to_a).length_squared();
	const real_t delta_scale = (from_len_sq > 0.0)
			? Math::sqrt(to_len_sq / from_len_sq)
			: real_t(1.0);
	Transform3D t = get_orbit_transform(p_from_a, p_from_b, p_to_a, p_to_b);
	t = t.translated(-p_to_a);
	t = t.scaled(Vector3(delta_scale, delta_scale, delta_scale));
	t = t.translated(p_to_a);
	return t;
}

Transform3D XRGridWorldGrab::split_blend(const Transform3D &p_from,
		const Transform3D &p_to, real_t p_pos_weight, real_t /*p_rot_weight*/,
		real_t /*p_scale_weight*/, const Vector3 &p_from_pivot,
		const Vector3 &p_to_pivot) {
	// Matches upstream's actually-active early-return: a simple
	// pivot-anchored origin blend. Upstream's rotation/scale path is
	// preserved as dead code there too; we replicate only the active
	// branch.
	Transform3D new_from(p_from.basis, p_from_pivot);
	Transform3D new_to(p_to.basis, p_to_pivot);
	new_to = new_from.interpolate_with(new_to, p_pos_weight);
	return get_grab_transform(new_from, new_to) * p_from;
}

void XRGridWorldGrab::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_grab_transform", "from", "to"),
			&XRGridWorldGrab::get_grab_transform);
	ClassDB::bind_method(D_METHOD("get_orbit_transform",
								 "from_pivot", "from_b", "to_pivot", "to_b"),
			&XRGridWorldGrab::get_orbit_transform);
	ClassDB::bind_method(D_METHOD("get_pinch_transform",
								 "from_a", "from_b", "to_a", "to_b"),
			&XRGridWorldGrab::get_pinch_transform);
	ClassDB::bind_method(D_METHOD("split_blend",
								 "from", "to", "pos_weight", "rot_weight",
								 "scale_weight", "from_pivot", "to_pivot"),
			&XRGridWorldGrab::split_blend);
}

/**************************************************************************/
/*  xr_grid_world_grab.h                                                  */
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

#include "core/math/transform_3d.h"
#include "core/math/vector3.h"
#include "core/object/ref_counted.h"

class XRGridWorldGrab : public RefCounted {
	GDCLASS(XRGridWorldGrab, RefCounted);

protected:
	static void _bind_methods();

public:
	XRGridWorldGrab() = default;

	Transform3D get_grab_transform(const Transform3D &p_from, const Transform3D &p_to);
	Transform3D get_orbit_transform(const Vector3 &p_from_pivot,
			const Vector3 &p_from_b, const Vector3 &p_to_pivot,
			const Vector3 &p_to_b);
	Transform3D get_pinch_transform(const Vector3 &p_from_a, const Vector3 &p_from_b,
			const Vector3 &p_to_a, const Vector3 &p_to_b);
	Transform3D split_blend(const Transform3D &p_from, const Transform3D &p_to,
			real_t p_pos_weight, real_t p_rot_weight, real_t p_scale_weight,
			const Vector3 &p_from_pivot, const Vector3 &p_to_pivot);
};

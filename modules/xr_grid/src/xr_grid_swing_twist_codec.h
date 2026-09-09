/**************************************************************************/
/*  xr_grid_swing_twist_codec.h                                           */
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

#include "core/math/quaternion.h"
#include "core/math/vector3.h"
#include "core/object/ref_counted.h"
#include "core/variant/typed_array.h"

class XRGridSwingTwistCodec : public RefCounted {
	GDCLASS(XRGridSwingTwistCodec, RefCounted);

protected:
	static void _bind_methods();

public:
	XRGridSwingTwistCodec() = default;

	static constexpr double QUANTIZE_SCALE = 32767.0;

	// vec is interpreted as (twist_x_radians, swing_y_radians, swing_z_radians).
	static Quaternion swing_twist_to_quat(const Vector3 &p_vec);
	static Vector3 quat_to_swing_twist(const Quaternion &p_q);

	// Returns [twist_x_i16, swing_y_i16, swing_z_i16] as ints in [-32767, 32767].
	static PackedInt32Array encode_rotation_i16(const Quaternion &p_q);

	static Quaternion decode_rotation_i16(int p_twist_x_i16,
			int p_swing_y_i16, int p_swing_z_i16);
};

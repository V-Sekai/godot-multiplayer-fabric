/**************************************************************************/
/*  xr_grid_swing_twist_codec.cpp                                         */
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

#include "xr_grid_swing_twist_codec.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"

namespace {
constexpr double EPSILON_TINY = 1e-8;

inline bool _is_zero_approx_d(double v) {
	return Math::is_zero_approx(v);
}
} // namespace

Quaternion XRGridSwingTwistCodec::swing_twist_to_quat(const Vector3 &p_vec) {
	const double x = p_vec.x;
	const double y = p_vec.y;
	const double z = p_vec.z;
	const double yz = Math::sqrt(y * y + z * z);
	const double sinc = (Math::abs(yz) < EPSILON_TINY)
			? 0.5
			: Math::sin(yz / 2.0) / yz;
	const double swing_w = Math::cos(yz / 2.0);
	const double twist_w = Math::cos(x / 2.0);
	const double twist_x = Math::sin(x / 2.0);
	return Quaternion(
			swing_w * twist_x,
			(z * twist_x + y * twist_w) * sinc,
			(z * twist_w - y * twist_x) * sinc,
			swing_w * twist_w);
}

Vector3 XRGridSwingTwistCodec::quat_to_swing_twist(const Quaternion &p_q) {
	const double a = p_q.x;
	const double b = p_q.y;
	const double c = p_q.z;
	const double d = p_q.w;
	double twist_x = 0.0;
	const double denom = a * a + d * d;
	if (!_is_zero_approx_d(denom)) {
		twist_x = Math::sqrt(a * a / denom);
	}
	twist_x = MIN(twist_x, 1.0);
	if (a < 0) {
		twist_x *= -1;
	}
	if (d < 0) {
		twist_x *= -1;
	}
	const double twist_w = Math::sqrt(1.0 - twist_x * twist_x);
	double swing_w;
	if (_is_zero_approx_d(twist_x)) {
		swing_w = (!_is_zero_approx_d(twist_w)) ? (d / twist_w) : 1.0;
	} else {
		swing_w = a / twist_x;
	}
	const double x = Math::asin(CLAMP(twist_x, -1.0, 1.0)) * 2.0;
	const double yz = Math::acos(CLAMP(swing_w, -1.0, 1.0)) * 2.0;
	const double sinc = (Math::abs(yz) < EPSILON_TINY)
			? 0.5
			: Math::sin(yz / 2.0) / yz;
	const double safe_twist_x = _is_zero_approx_d(twist_x) ? EPSILON_TINY : twist_x;
	const double safe_twist_w = _is_zero_approx_d(twist_w) ? EPSILON_TINY : twist_w;
	const double y = (b / safe_twist_x - c / safe_twist_w) /
			(safe_twist_w / safe_twist_x + safe_twist_x / safe_twist_w) / sinc;
	const double z = (b / safe_twist_w + c / safe_twist_x) /
			(safe_twist_x / safe_twist_w + safe_twist_w / safe_twist_x) / sinc;
	return Vector3(real_t(x), real_t(y), real_t(z));
}

PackedInt32Array XRGridSwingTwistCodec::encode_rotation_i16(const Quaternion &p_q) {
	const Vector3 v = quat_to_swing_twist(p_q.normalized());
	PackedInt32Array out;
	out.resize(3);
	int *w = out.ptrw();
	w[0] = int(CLAMP(double(v.x) / Math::PI, -1.0, 1.0) * QUANTIZE_SCALE);
	w[1] = int(CLAMP(double(v.y) / Math::PI, -1.0, 1.0) * QUANTIZE_SCALE);
	w[2] = int(CLAMP(double(v.z) / Math::PI, -1.0, 1.0) * QUANTIZE_SCALE);
	return out;
}

Quaternion XRGridSwingTwistCodec::decode_rotation_i16(int p_twist_x_i16,
		int p_swing_y_i16, int p_swing_z_i16) {
	const Vector3 v(
			real_t(double(p_twist_x_i16) / QUANTIZE_SCALE * Math::PI),
			real_t(double(p_swing_y_i16) / QUANTIZE_SCALE * Math::PI),
			real_t(double(p_swing_z_i16) / QUANTIZE_SCALE * Math::PI));
	return swing_twist_to_quat(v);
}

void XRGridSwingTwistCodec::_bind_methods() {
	ClassDB::bind_static_method("XRGridSwingTwistCodec",
			D_METHOD("swing_twist_to_quat", "vec"),
			&XRGridSwingTwistCodec::swing_twist_to_quat);
	ClassDB::bind_static_method("XRGridSwingTwistCodec",
			D_METHOD("quat_to_swing_twist", "q"),
			&XRGridSwingTwistCodec::quat_to_swing_twist);
	ClassDB::bind_static_method("XRGridSwingTwistCodec",
			D_METHOD("encode_rotation_i16", "q"),
			&XRGridSwingTwistCodec::encode_rotation_i16);
	ClassDB::bind_static_method("XRGridSwingTwistCodec",
			D_METHOD("decode_rotation_i16", "twist_x_i16", "swing_y_i16", "swing_z_i16"),
			&XRGridSwingTwistCodec::decode_rotation_i16);
}

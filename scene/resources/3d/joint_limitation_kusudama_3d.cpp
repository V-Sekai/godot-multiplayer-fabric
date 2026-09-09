/**************************************************************************/
/*  joint_limitation_kusudama_3d.cpp                                      */
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

#include "joint_limitation_kusudama_3d.h"

#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "scene/resources/3d/kusudama_gizmo_shader.h"
#include "scene/resources/surface_tool.h"
#endif

void JointLimitationKusudama3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_cones", "cones"), &JointLimitationKusudama3D::set_cones);
	ClassDB::bind_method(D_METHOD("get_cones"), &JointLimitationKusudama3D::get_cones);

	ClassDB::bind_method(D_METHOD("set_cone_count", "count"), &JointLimitationKusudama3D::set_cone_count);
	ClassDB::bind_method(D_METHOD("get_cone_count"), &JointLimitationKusudama3D::get_cone_count);
	ClassDB::bind_method(D_METHOD("set_cone_center", "index", "center"), &JointLimitationKusudama3D::set_cone_center);
	ClassDB::bind_method(D_METHOD("get_cone_center", "index"), &JointLimitationKusudama3D::get_cone_center);
	ClassDB::bind_method(D_METHOD("set_cone_axes", "index", "axes"), &JointLimitationKusudama3D::set_cone_axes);
	ClassDB::bind_method(D_METHOD("get_cone_axes", "index"), &JointLimitationKusudama3D::get_cone_axes);
	ClassDB::bind_method(D_METHOD("set_cone_radius", "index", "radius"), &JointLimitationKusudama3D::set_cone_radius);
	ClassDB::bind_method(D_METHOD("get_cone_radius", "index"), &JointLimitationKusudama3D::get_cone_radius);

	ClassDB::bind_method(D_METHOD("set_twist_from", "radians"), &JointLimitationKusudama3D::set_twist_from);
	ClassDB::bind_method(D_METHOD("get_twist_from"), &JointLimitationKusudama3D::get_twist_from);
	ClassDB::bind_method(D_METHOD("set_twist_to", "radians"), &JointLimitationKusudama3D::set_twist_to);
	ClassDB::bind_method(D_METHOD("get_twist_to"), &JointLimitationKusudama3D::get_twist_to);
	ClassDB::bind_method(D_METHOD("set_cushion", "radians"), &JointLimitationKusudama3D::set_cushion);
	ClassDB::bind_method(D_METHOD("get_cushion"), &JointLimitationKusudama3D::get_cushion);
	ClassDB::bind_method(D_METHOD("set_strength", "strength"), &JointLimitationKusudama3D::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &JointLimitationKusudama3D::get_strength);
	ClassDB::bind_method(D_METHOD("has_twist_limit"), &JointLimitationKusudama3D::has_twist_limit);
	ClassDB::bind_method(D_METHOD("clamp_twist_angle", "angle"), &JointLimitationKusudama3D::clamp_twist_angle);
	ClassDB::bind_method(D_METHOD("limit_twist", "rotation", "axis"), &JointLimitationKusudama3D::limit_twist);
	ClassDB::bind_method(D_METHOD("swing_to_normalized", "direction"), &JointLimitationKusudama3D::swing_to_normalized);
	ClassDB::bind_method(D_METHOD("swing_from_normalized", "normalized"), &JointLimitationKusudama3D::swing_from_normalized);
	ClassDB::bind_method(D_METHOD("twist_to_normalized", "angle"), &JointLimitationKusudama3D::twist_to_normalized);
	ClassDB::bind_method(D_METHOD("twist_from_normalized", "normalized"), &JointLimitationKusudama3D::twist_from_normalized);

	// Swing limit: the cones are stored data, edited through the gizmo / set_cone_* API.
	ADD_GROUP("Swing", "");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "cones", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_cones", "get_cones");
	// Twist limit: a window about the forward axis. Stored/scripted in radians; the inspector
	// shows and edits degrees (range is in degrees).
	ADD_GROUP("Twist", "twist_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "twist_from", PROPERTY_HINT_RANGE, "-360,360,0.1,radians_as_degrees"), "set_twist_from", "get_twist_from");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "twist_to", PROPERTY_HINT_RANGE, "-360,360,0.1,radians_as_degrees"), "set_twist_to", "get_twist_to");
	// Softness: how the limit behaves at its edge (cushion) and how strongly it applies (strength).
	ADD_GROUP("Softness", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cushion", PROPERTY_HINT_RANGE, "0.5,30,0.1,radians_as_degrees"), "set_cushion", "get_cushion");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_strength", "get_strength");
}

void JointLimitationKusudama3D::set_cones(const Vector<Vector4> &p_cones) {
	cones.clear();
	int n = MIN((int)p_cones.size(), MAX_KUSUDAMA_CONES);
	for (int i = 0; i < n; i++) {
		cones.push_back(p_cones[i]);
	}
	_invalidate_normalized_cache();
	_invalidate_polygon_cache();
	emit_changed();
}

Vector<Vector4> JointLimitationKusudama3D::get_cones() const {
	Vector<Vector4> r;
	r.resize(cones.size());
	for (uint32_t i = 0; i < cones.size(); i++) {
		r.write[i] = cones[i];
	}
	return r;
}

void JointLimitationKusudama3D::set_cone_count(int p_count) {
	if (p_count < 0) {
		p_count = 0;
	}
	if (p_count > MAX_KUSUDAMA_CONES) {
		p_count = MAX_KUSUDAMA_CONES;
	}
	uint32_t old_size = cones.size();
	if (old_size == (uint32_t)p_count) {
		return;
	}
	cones.resize(p_count);
	for (uint32_t i = old_size; i < cones.size(); i++) {
		cones[i] = Vector4(0, 1, 0, Math::PI * 0.25);
	}
	_invalidate_normalized_cache();
	_invalidate_polygon_cache();
	notify_property_list_changed();
	emit_changed();
}

int JointLimitationKusudama3D::get_cone_count() const {
	return cones.size();
}

void JointLimitationKusudama3D::set_cone_center(int p_index, const Vector3 &p_center) {
	ERR_FAIL_INDEX(p_index, (int)cones.size());
	Vector4 &cone = cones[p_index];
	cone.x = p_center.x;
	cone.y = p_center.y;
	cone.z = p_center.z;
	_invalidate_normalized_cache();
	_invalidate_polygon_cache();
	emit_changed();
}

Vector3 JointLimitationKusudama3D::get_cone_center(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, (int)cones.size(), Vector3::UP);
	const Vector4 &cone_data = cones[p_index];
	return Vector3(cone_data.x, cone_data.y, cone_data.z);
}

void JointLimitationKusudama3D::set_cone_axes(int p_index, const Vector2 &p_axes) {
	ERR_FAIL_INDEX(p_index, (int)cones.size());
	const real_t x = CLAMP(p_axes.x, (real_t)-1.0, (real_t)1.0); // right (basis X)
	const real_t z = CLAMP(p_axes.y, (real_t)-1.0, (real_t)1.0); // forward (basis Z)
	const real_t y = Math::sqrt(MAX((real_t)0.0, (real_t)1.0 - x * x - z * z)); // up (basis Y), implied
	set_cone_center(p_index, Vector3(x, y, z));
}

Vector2 JointLimitationKusudama3D::get_cone_axes(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, (int)cones.size(), Vector2());
	const Vector3 c = _get_cone_center_normalized(p_index);
	return Vector2(c.x, c.z);
}

void JointLimitationKusudama3D::_invalidate_normalized_cache() const {
	_normalized_cone_centers_cache.clear();
}

void JointLimitationKusudama3D::_invalidate_polygon_cache() const {
	_polygon_dirty = true;
}

Vector3 JointLimitationKusudama3D::_get_cone_center_normalized(int p_index) const {
	if (_normalized_cone_centers_cache.size() != cones.size()) {
		_normalized_cone_centers_cache.resize(cones.size());
		for (uint32_t i = 0; i < cones.size(); i++) {
			Vector3 raw(cones[i].x, cones[i].y, cones[i].z);
			if (raw.is_zero_approx()) {
				_normalized_cone_centers_cache[i] = Vector3::UP;
			} else {
				_normalized_cone_centers_cache[i] = raw.normalized();
			}
		}
	}
	ERR_FAIL_INDEX_V(p_index, (int)_normalized_cone_centers_cache.size(), Vector3::UP);
	return _normalized_cone_centers_cache[p_index];
}

void JointLimitationKusudama3D::set_cone_radius(int p_index, real_t p_radius) {
	ERR_FAIL_INDEX(p_index, (int)cones.size());
	cones[p_index].w = p_radius;
	_invalidate_polygon_cache();
	emit_changed();
}

real_t JointLimitationKusudama3D::get_cone_radius(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, (int)cones.size(), 0.0);
	return cones[p_index].w;
}

// --- Twist limit (about the bone forward axis). Verified in Lean+Plausible:
//     in-range identity, output always in range, swing preserved, idempotent. ---
void JointLimitationKusudama3D::set_twist_from(real_t p_radians) {
	twist_from = p_radians;
}
real_t JointLimitationKusudama3D::get_twist_from() const {
	return twist_from;
}
void JointLimitationKusudama3D::set_twist_to(real_t p_radians) {
	twist_to = p_radians;
}
real_t JointLimitationKusudama3D::get_twist_to() const {
	return twist_to;
}
void JointLimitationKusudama3D::set_cushion(real_t p_radians) {
	cushion = MAX((real_t)0.0, p_radians);
}
real_t JointLimitationKusudama3D::get_cushion() const {
	return cushion;
}
void JointLimitationKusudama3D::set_strength(real_t p_strength) {
	strength = CLAMP(p_strength, (real_t)0.0, (real_t)1.0);
}
real_t JointLimitationKusudama3D::get_strength() const {
	return strength;
}
bool JointLimitationKusudama3D::has_twist_limit() const {
	// The twist range IS the constraint: twist_to > twist_from is a window; twist_to ==
	// twist_from is LOCKED (a zero-width limit, e.g. a hinge knee/finger that must not spin);
	// an inverted range (twist_to < twist_from) is the "no twist limit / free" sentinel.
	return twist_to >= twist_from;
}

real_t JointLimitationKusudama3D::clamp_twist_angle(real_t p_angle) const {
	if (!has_twist_limit()) {
		return p_angle;
	}
	const real_t clamped = CLAMP(p_angle, twist_from, twist_to);
	// Strength softens the twist limit the same way it softens the swing cones.
	if (strength >= (real_t)1.0) {
		return clamped;
	}
	if (strength <= (real_t)0.0) {
		return p_angle;
	}
	return Math::lerp(p_angle, clamped, strength);
}

Quaternion JointLimitationKusudama3D::limit_twist(const Quaternion &p_rotation, const Vector3 &p_axis) const {
	if (!has_twist_limit()) {
		return p_rotation;
	}
	const Vector3 a = p_axis.normalized();
	// Swing-twist decomposition about a: twist angle = 2*atan2(q.vec . a, q.w).
	const real_t d = p_rotation.x * a.x + p_rotation.y * a.y + p_rotation.z * a.z;
	const real_t t = 2.0 * Math::atan2(d, p_rotation.w);
	const real_t tc = CLAMP(t, twist_from, twist_to);
	// A 180deg swing about an axis PERPENDICULAR to `a` gives d ~ 0 and w ~ 0, so the twist part is a
	// zero-length quaternion -- normalizing it would yield NaN and poison the result. There is no
	// twist to clamp in that pose, so return the rotation unchanged. (Breakage shown in
	// ../swing-twist-kusudama/KusGuards.lean.)
	if (d * d + p_rotation.w * p_rotation.w < (real_t)CMP_EPSILON) {
		return p_rotation;
	}
	Quaternion twist_q = Quaternion(d * a.x, d * a.y, d * a.z, p_rotation.w).normalized();
	const Quaternion swing = p_rotation * twist_q.inverse();
	const real_t h = tc * 0.5;
	const real_t s = Math::sin(h);
	const Quaternion clamped(a.x * s, a.y * s, a.z * s, Math::cos(h));
	return (swing * clamped).normalized();
}

real_t JointLimitationKusudama3D::twist_to_normalized(real_t p_angle) const {
	const real_t span = twist_to - twist_from;
	if (!has_twist_limit() || span <= (real_t)1e-9) {
		return 0.0; // free, or LOCKED (zero-width) -> no meaningful normalized position
	}
	return CLAMP((p_angle - twist_from) / span, (real_t)0.0, (real_t)1.0);
}

real_t JointLimitationKusudama3D::twist_from_normalized(real_t p_normalized) const {
	const real_t span = twist_to - twist_from;
	if (!has_twist_limit() || span <= (real_t)1e-9) {
		return twist_from; // LOCKED -> the single angle; free -> twist_from (0)
	}
	return twist_from + CLAMP(p_normalized, (real_t)0.0, (real_t)1.0) * span;
}

// Deterministic tangent basis at a swing axis: build a Basis that rotates the rest
// forward (+Y) onto the axis with minimal twist, and take its right (X) and forward (Z)
// columns. This gives a CONSISTENT swing-axis convention (e1 = right, e2 = forward) for
// every cone, instead of an arbitrary perpendicular.
static void swing_axis_basis(const Vector3 &p_center, Vector3 &r_e1, Vector3 &r_e2) {
	const Basis frame(Quaternion(Vector3(0, 1, 0), p_center));
	r_e1 = frame.get_column(0);
	r_e2 = frame.get_column(2);
}

// --- Swing normalization to a unit-disk coord (per-axis normalized): 0 at the ROM center,
//     magnitude 1 on the ROM boundary, in the cone's canonical space. ---
Vector3 JointLimitationKusudama3D::_swing_center() const {
	Vector3 m;
	for (uint32_t i = 0; i < cones.size(); i++) {
		m += Vector3(cones[i].x, cones[i].y, cones[i].z).normalized();
	}
	if (m.length() < (real_t)1e-9) {
		return Vector3(0, 1, 0);
	}
	return m.normalized();
}

bool JointLimitationKusudama3D::_point_in_cone_union(const Vector3 &p_point) const {
	for (uint32_t i = 0; i < cones.size(); i++) {
		if (is_point_in_cone(p_point, Vector3(cones[i].x, cones[i].y, cones[i].z).normalized(), cones[i].w)) {
			return true;
		}
	}
	return false;
}

real_t JointLimitationKusudama3D::_swing_boundary(const Vector3 &p_center, const Vector3 &p_tangent) const {
	// Largest beta in (0, PI) with cos(beta)*center + sin(beta)*tangent still inside the cone union.
	const int N = 64;
	real_t last_in = 0.0;
	for (int i = 1; i <= N; i++) {
		const real_t b = Math::PI * (real_t)i / (real_t)N;
		const Vector3 p = Math::cos(b) * p_center + Math::sin(b) * p_tangent;
		if (_point_in_cone_union(p)) {
			last_in = b;
		} else if (last_in > 0.0) {
			break;
		}
	}
	if (last_in <= 0.0) {
		return Math::deg_to_rad((real_t)1.0); // degenerate; avoid divide-by-zero
	}
	real_t a = last_in;
	real_t b = MIN(last_in + Math::PI / (real_t)N, (real_t)Math::PI);
	for (int i = 0; i < 24; i++) {
		const real_t mid = 0.5 * (a + b);
		const Vector3 p = Math::cos(mid) * p_center + Math::sin(mid) * p_tangent;
		if (_point_in_cone_union(p)) {
			a = mid;
		} else {
			b = mid;
		}
	}
	return a;
}

Vector2 JointLimitationKusudama3D::swing_to_normalized(const Vector3 &p_direction) const {
	if (cones.is_empty()) {
		return Vector2();
	}
	const Vector3 center = _swing_center();
	const Vector3 dir = p_direction.normalized();
	const real_t cs = CLAMP(center.dot(dir), (real_t)-1.0, (real_t)1.0);
	const real_t theta = Math::acos(cs);
	Vector3 t = dir - cs * center;
	if (t.length() < (real_t)1e-9) {
		return Vector2();
	}
	t.normalize();
	Vector3 e1, e2;
	swing_axis_basis(center, e1, e2);
	const real_t boundary = _swing_boundary(center, t);
	const real_t m = theta / boundary;
	return Vector2(m * t.dot(e1), m * t.dot(e2));
}

Vector3 JointLimitationKusudama3D::swing_from_normalized(const Vector2 &p_normalized) const {
	if (cones.is_empty()) {
		return Vector3(0, 1, 0);
	}
	const Vector3 center = _swing_center();
	const real_t m = p_normalized.length();
	if (m < (real_t)1e-9) {
		return center;
	}
	Vector3 e1, e2;
	swing_axis_basis(center, e1, e2);
	const Vector3 t = (e1 * (p_normalized.x / m) + e2 * (p_normalized.y / m)).normalized();
	const real_t theta = m * _swing_boundary(center, t);
	return (Math::cos(theta) * center + Math::sin(theta) * t).normalized();
}

bool JointLimitationKusudama3D::_set(const StringName &p_name, const Variant &p_value) {
	String prop_name = p_name;
	if (prop_name == "cone_count") {
		set_cone_count(p_value);
		return true;
	}
	if (prop_name.begins_with("cones/")) {
		int index = prop_name.get_slicec('/', 1).to_int();
		String what = prop_name.get_slicec('/', 2);
		if (what == "swing_x") { // basis right (X) axis in [-1,1]
			set_cone_axes(index, Vector2(p_value, get_cone_axes(index).y));
			return true;
		}
		if (what == "swing_z") { // basis forward (Z) axis in [-1,1]
			set_cone_axes(index, Vector2(get_cone_axes(index).x, p_value));
			return true;
		}
		if (what == "radius") {
			set_cone_radius(index, p_value);
			return true;
		}
	}
	return false;
}

bool JointLimitationKusudama3D::_get(const StringName &p_name, Variant &r_ret) const {
	String prop_name = p_name;
	if (prop_name == "cone_count") {
		r_ret = get_cone_count();
		return true;
	}
	if (prop_name.begins_with("cones/")) {
		int index = prop_name.get_slicec('/', 1).to_int();
		String what = prop_name.get_slicec('/', 2);
		if (what == "swing_x") {
			r_ret = get_cone_axes(index).x;
			return true;
		}
		if (what == "swing_z") {
			r_ret = get_cone_axes(index).y;
			return true;
		}
		if (what == "radius") {
			r_ret = get_cone_radius(index);
			return true;
		}
	}
	return false;
}

void JointLimitationKusudama3D::_get_property_list(List<PropertyInfo> *p_list) const {
	p_list->push_back(PropertyInfo(Variant::INT, PNAME("cone_count"), PROPERTY_HINT_RANGE, "0,30,1", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_ARRAY, "Cones," + String(PNAME("cones")) + "/"));
	for (int i = 0; i < get_cone_count(); i++) {
		const String prefix = vformat("%s/%d/", PNAME("cones"), i);
		// Cone center as two basis axes (right/X and forward/Z) in [-1,1]; up/Y is implied.
		p_list->push_back(PropertyInfo(Variant::FLOAT, prefix + PNAME("swing_x"), PROPERTY_HINT_RANGE, "-1,1,0.001"));
		p_list->push_back(PropertyInfo(Variant::FLOAT, prefix + PNAME("swing_z"), PROPERTY_HINT_RANGE, "-1,1,0.001"));
		p_list->push_back(PropertyInfo(Variant::FLOAT, prefix + PNAME("radius"), PROPERTY_HINT_RANGE, "1,180,0.1,radians_as_degrees"));
	}
}

void JointLimitationKusudama3D::_compute_hull_order() const {
	uint32_t n = cones.size();
	_hull_order.resize(n);
	for (uint32_t i = 0; i < n; i++) {
		_hull_order[i] = i;
	}
	if (n <= 2) {
		return;
	}
	// Sort by angle around a reference axis derived from the first two cones.
	// This avoids centroid computation (which degenerates with equidistant cones).
	Vector3 ref = _get_cone_center_normalized(0).cross(_get_cone_center_normalized(1));
	if (ref.is_zero_approx()) {
		ref = _get_cone_center_normalized(0).get_any_perpendicular();
	}
	ref.normalize();
	Vector3 u = ref.get_any_perpendicular().normalized();
	Vector3 v = ref.cross(u).normalized();

	LocalVector<real_t> angles;
	angles.resize(n);
	for (uint32_t i = 0; i < n; i++) {
		Vector3 c = _get_cone_center_normalized(i);
		angles[i] = Math::atan2(c.dot(v), c.dot(u));
	}
	for (uint32_t i = 1; i < n; i++) {
		uint32_t key_idx = _hull_order[i];
		real_t key_angle = angles[key_idx];
		int j = (int)i - 1;
		while (j >= 0 && angles[_hull_order[j]] > key_angle) {
			_hull_order[j + 1] = _hull_order[j];
			j--;
		}
		_hull_order[j + 1] = key_idx;
	}
}

void JointLimitationKusudama3D::_rebuild_polygon_cache() const {
	if (!_polygon_dirty) {
		return;
	}
	_polygon_dirty = false;
	_polygon_vertices.clear();
	_polygon_normals.clear();
	_tangent_centers_1.clear();
	_tangent_centers_2.clear();
	_tangent_radii.clear();

	uint32_t n = cones.size();
	if (n < 2) {
		return;
	}

	// Use input order (matching the shader) — no hull reordering.
	_hull_order.resize(n);
	for (uint32_t i = 0; i < n; i++) {
		_hull_order[i] = i;
	}

	// Compute tangent circles for each adjacent pair in input order (open chain).
	uint32_t num_pairs = n - 1;
	_tangent_centers_1.resize(num_pairs);
	_tangent_centers_2.resize(num_pairs);
	_tangent_radii.resize(num_pairs);
	for (uint32_t i = 0; i < num_pairs; i++) {
		compute_tangent_circles(
				_get_cone_center_normalized(i), cones[i].w,
				_get_cone_center_normalized(i + 1), cones[i + 1].w,
				_tangent_centers_1[i], _tangent_centers_2[i], _tangent_radii[i]);
	}

	// Polygon vertices = cone centers in input order.
	_polygon_vertices.resize(n);
	for (uint32_t i = 0; i < n; i++) {
		_polygon_vertices[i] = _get_cone_center_normalized(i);
	}
}

bool JointLimitationKusudama3D::_is_in_tangent_path(const Vector3 &p_point, uint32_t p_pair_index) const {
	Vector3 center1 = _get_cone_center_normalized(p_pair_index);
	Vector3 center2 = _get_cone_center_normalized(p_pair_index + 1);
	Vector3 tan1 = _tangent_centers_1[p_pair_index];
	Vector3 tan2 = _tangent_centers_2[p_pair_index];
	real_t tan_radius_cos = Math::cos(_tangent_radii[p_pair_index]);

	// Inside a tangent circle = forbidden.
	if (p_point.dot(tan1) > tan_radius_cos) {
		return false;
	}
	if (p_point.dot(tan2) > tan_radius_cos) {
		return false;
	}

	// Check which side of the arc (center1 × center2) we're on, then verify
	// the point is inside the tangent triangle on that side.
	Vector3 arc_normal = center1.cross(center2);
	real_t side = p_point.dot(arc_normal);

	if (side < 0.0) {
		return p_point.dot(center1.cross(tan1)) > 0 && p_point.dot(tan1.cross(center2)) > 0;
	} else {
		return p_point.dot(tan2.cross(center1)) > 0 && p_point.dot(center2.cross(tan2)) > 0;
	}
}

bool JointLimitationKusudama3D::_polygon_contains(const Vector3 &p_point) const {
	// Half-space intersection test (unchanged — this is exact on the sphere).
	uint32_t m = _polygon_normals.size();
	for (uint32_t i = 0; i < m; i++) {
		if (p_point.dot(_polygon_normals[i]) < 0) {
			return false;
		}
	}
	return true;
}

Vector3 JointLimitationKusudama3D::_closest_on_small_circle(const Vector3 &p_point, const Vector3 &p_center, real_t p_radius) const {
	// Closest point to p on the small circle of angular radius p_radius around
	// p_center: swing p onto the circle along the geodesic. Continuous in p.
	Vector3 perp = p_point - p_center * p_point.dot(p_center);
	if (perp.is_zero_approx()) {
		perp = p_center.get_any_perpendicular();
	}
	perp.normalize();
	return (p_center * Math::cos(p_radius) + perp * Math::sin(p_radius)).normalized();
}

// Append a boundary closest-point candidate, tracking the nearest (anchor). (File-static helpers
// instead of lambdas, per the no-lambda/no-auto style.)
static void k_add_candidate(LocalVector<Vector3> &p_candidates, LocalVector<real_t> &p_distances, real_t &p_min_dist, uint32_t &p_anchor, const Vector3 &p_point, const Vector3 &q) {
	const real_t d = p_point.angle_to(q);
	p_candidates.push_back(q);
	p_distances.push_back(d);
	if (d < p_min_dist) {
		p_min_dist = d;
		p_anchor = p_candidates.size() - 1;
	}
}

// Soft-saturate p_point's angle to one boundary circle and add the resulting candidate. keep_in:
// cone (angle -> limit from below); else tangent circle (angle -> limit from above).
static void k_soft_saturated(const Vector3 &p_point, real_t p_soft_band, LocalVector<Vector3> &p_candidates, LocalVector<real_t> &p_distances, real_t &p_min_dist, uint32_t &p_anchor, const Vector3 &p_center, real_t p_limit, bool p_keep_in) {
	const real_t th = p_point.angle_to(p_center);
	real_t th_sat = th;
	if (p_keep_in) {
		if (th > p_limit - p_soft_band) {
			th_sat = p_limit - p_soft_band * Math::exp(-(th - (p_limit - p_soft_band)) / p_soft_band);
		}
	} else {
		if (th < p_limit + p_soft_band) {
			th_sat = p_limit + p_soft_band * Math::exp(-((p_limit + p_soft_band) - th) / p_soft_band);
		}
	}
	Vector3 perp = p_point - p_center * p_center.dot(p_point);
	if (perp.is_zero_approx()) {
		perp = p_center.get_any_perpendicular();
	}
	perp.normalize();
	k_add_candidate(p_candidates, p_distances, p_min_dist, p_anchor, p_point, (p_center * Math::cos(th_sat) + perp * Math::sin(th_sat)).normalized());
}

Vector3 JointLimitationKusudama3D::_continuous_project(const Vector3 &p_point) const {
	// EWBIK boundary-curve projection. The allowed region's boundary is a smooth
	// curve made of limit-cone boundary arcs joined by tangent-circle arcs: each
	// tangent circle is, by construction, tangent to the two cones it bridges, so
	// where a cone arc ends and a tangent arc begins the two meet with a common
	// tangent — the boundary is C1. Projecting onto that curve therefore gives a
	// C1-continuous output (low jerk / low added twist) that lands exactly on the
	// region boundary (never outside the hard limit).
	//
	// Procedure: project p onto the nearest limit cone's boundary circle; if that
	// point falls inside a neighboring tangent circle's forbidden lens, the real
	// boundary there is the tangent-circle arc, so re-project p onto it instead.
	// The hand-off happens exactly at the tangent point (where the lens meets the
	// cone), so it is seamless.
	const uint32_t n = cones.size();
	if (n == 0) {
		return p_point;
	}

	// Gather every region-boundary closest-point (each cone arc + each tangent
	// circle arc), then blend them with a one-step weighted Karcher (spherical)
	// mean in the tangent space at the nearest one. A hard nearest-boundary
	// projection snaps across the medial axis (teleports); the soft geodesic
	// blend instead rounds the corner toward the interior — continuous (no
	// teleport) and inside the hard bound. The temperature controls how tightly
	// it tracks the nearest boundary vs. rounds seams.
	const real_t temperature = 0.22;
	LocalVector<Vector3> candidates;
	LocalVector<real_t> distances;
	real_t min_dist = 1e30;
	uint32_t anchor = 0;
	// Each candidate is the input direction with its angle to one boundary circle
	// *soft-saturated* — a calculus limit. Cones are "keep-in" (output angle ->
	// radius from below, never exceeding it); tangent circles are "keep-out"
	// (output angle -> tangent radius from above, never entering the forbidden
	// lens). Deep inside an allowed region the saturation is the identity, so the
	// matching candidate is exactly p and dominates the blend (identity); near a
	// boundary it bends smoothly, removing the hard identity<->clamp slope jump
	// (the boundary-crossing jerk) with zero overshoot. Both the inside-cone and
	// inside-tangent-path "fast accept" cases are folded in here, so solve() is C1
	// across every seam while staying within the hard limit.
	// The cushion is the soft-limit band; floored so the projection stays C1 (no boundary
	// teleport) even if the animator dials it toward zero.
	const real_t SOFT_BAND = MAX((real_t)0.01, cushion);
	for (uint32_t i = 0; i < n; i++) {
		k_soft_saturated(p_point, SOFT_BAND, candidates, distances, min_dist, anchor, _get_cone_center_normalized(i), cones[i].w, true);
	}
	const uint32_t num_pairs = (n >= 1) ? n - 1 : 0;
	for (uint32_t i = 0; i < num_pairs; i++) {
		k_soft_saturated(p_point, SOFT_BAND, candidates, distances, min_dist, anchor, _tangent_centers_1[i], _tangent_radii[i], false);
		k_soft_saturated(p_point, SOFT_BAND, candidates, distances, min_dist, anchor, _tangent_centers_2[i], _tangent_radii[i], false);
	}

	// Deep inside an allowed region the matching soft-saturation is the identity,
	// so its candidate equals p exactly (min_dist == 0): return p untouched, exact
	// identity. (At the band edge the soft-sat has unit slope, so handing off to
	// the blend just outside is still C1.)
	if (min_dist < (real_t)1e-5) {
		return p_point;
	}

	const Vector3 a = candidates[anchor];
	Vector3 tangent;
	real_t weight_sum = 0.0;
	for (uint32_t i = 0; i < candidates.size(); i++) {
		real_t w = Math::exp(-(distances[i] - min_dist) / temperature);
		weight_sum += w;
		const Vector3 &c = candidates[i];
		if (a.angle_to(c) > 1e-6) {
			Vector3 dir = (c - a * a.dot(c));
			if (!dir.is_zero_approx()) {
				tangent += dir.normalized() * (a.angle_to(c) * w);
			}
		}
	}
	if (weight_sum <= 0.0) {
		return p_point;
	}
	tangent /= weight_sum;
	real_t tlen = tangent.length();
	if (tlen < 1e-9) {
		return a;
	}
	return (a * Math::cos(tlen) + (tangent / tlen) * Math::sin(tlen)).normalized();
}

Vector3 JointLimitationKusudama3D::_solve(const Vector3 &p_direction) const {
	const Vector3 p = p_direction.normalized();
	const uint32_t n = cones.size();
	if (n == 0) {
		return p;
	}

	Vector3 result;
	if (n == 1) {
		// Single cone: identity inside, hard projection to the boundary outside.
		const Vector3 c = _get_cone_center_normalized(0);
		const real_t r = cones[0].w;
		if (is_point_in_cone(p, c, r)) {
			result = p;
		} else {
			Vector3 ortho = (p - p.project(c)).normalized();
			if (!ortho.is_finite()) {
				ortho = (Math::abs(c.z) < 0.9f) ? c.cross(Vector3(0, 0, 1)).normalized() : c.cross(Vector3(1, 0, 0)).normalized();
			}
			result = c * Math::cos(r) + ortho * Math::sin(r);
		}
	} else {
		_rebuild_polygon_cache();
		// Everything (inside a cone, inside a tangent path, or out of bounds) goes through the one
		// soft-saturated projection. The per-cone keep-in and per-tangent-circle keep-out
		// saturations return the input to float precision deep inside an allowed region, and bend
		// smoothly toward the limit near a boundary. solve() is C1 across every seam (no
		// boundary-crossing jerk) while never crossing the hard limit. Stateless: a pure function.
		result = _continuous_project(p);
	}

	// Strength: blend the input toward the constrained result. 1 fully enforces the limit; 0
	// passes the direction through; in between softens it. slerp keeps the output on the sphere.
	if (strength >= (real_t)1.0) {
		return result;
	}
	if (strength <= (real_t)0.0) {
		return p;
	}
	return p.slerp(result, strength).normalized();
}

// Helper functions for kusudama solving

#ifdef TOOLS_ENABLED

int JointLimitationKusudama3D::get_cone_sequence_for_shader(PackedVector4Array &r_cone_sequence) const {
	r_cone_sequence.clear();
	uint32_t n = cones.size();
	if (n == 0) {
		return 0;
	}
	// Input order, open chain — this defines the correct constraint.
	for (uint32_t i = 0; i < n; i++) {
		Vector3 center_i = _get_cone_center_normalized(i);
		real_t radius_i = cones[i].w;
		if (i == 0) {
			r_cone_sequence.push_back(Vector4(center_i.x, center_i.y, center_i.z, radius_i));
		}
		if (i + 1 < n) {
			Vector3 center_next = _get_cone_center_normalized(i + 1);
			real_t radius_next = cones[i + 1].w;
			Vector3 tan1, tan2;
			real_t trad;
			compute_tangent_circles(center_i, radius_i, center_next, radius_next, tan1, tan2, trad);
			r_cone_sequence.push_back(Vector4(tan1.x, tan1.y, tan1.z, trad));
			r_cone_sequence.push_back(Vector4(tan2.x, tan2.y, tan2.z, trad));
			r_cone_sequence.push_back(Vector4(center_next.x, center_next.y, center_next.z, radius_next));
		}
	}
	return n;
}

void JointLimitationKusudama3D::get_kusudama_fill_mesh_and_material(const Transform3D &p_transform, float p_bone_length, const Color &p_color, int p_bone_index, Transform3D &r_mesh_to_skeleton_rest, Ref<ArrayMesh> &r_mesh, Ref<Material> &r_material) const {
	r_mesh.unref();
	r_material.unref();
	if (cones.size() == 0) {
		return;
	}
	real_t sphere_r = p_bone_length * (real_t)0.25;
	const int rings = 16;
	const int radial_segments = 16;
	Vector<Vector3> points;
	Vector<Vector3> normals;
	Vector<int> indices;
	int thisrow = 0;
	int prevrow = 0;
	int point = 0;
	for (int j = 0; j <= (rings + 1); j++) {
		float v = (float)j / (float)(rings + 1);
		float w = Math::sin(Math::PI * v);
		float y = Math::cos(Math::PI * v);
		for (int i = 0; i <= radial_segments; i++) {
			float u = (float)i / (float)radial_segments;
			float x = Math::sin(u * Math::TAU);
			float z = Math::cos(u * Math::TAU);
			Vector3 p = Vector3(x * w, y, z * w);
			points.push_back(p.normalized());
			normals.push_back(p.normalized());
			point++;
			if (i > 0 && j > 0) {
				indices.push_back(prevrow + i - 1);
				indices.push_back(prevrow + i);
				indices.push_back(thisrow + i - 1);
				indices.push_back(prevrow + i);
				indices.push_back(thisrow + i);
				indices.push_back(thisrow + i - 1);
			}
		}
		prevrow = thisrow;
		thisrow = point;
	}
	if (indices.is_empty()) {
		return;
	}
	const bool use_skin = (p_bone_index >= 0);
	Ref<SurfaceTool> st;
	st.instantiate();
	st->begin(Mesh::PRIMITIVE_TRIANGLES);
	st->set_custom_format(0, SurfaceTool::CUSTOM_RGBA_HALF);
	if (use_skin) {
		PackedInt32Array bones;
		PackedFloat32Array weights;
		bones.resize(Mesh::ARRAY_WEIGHTS_SIZE);
		weights.resize(Mesh::ARRAY_WEIGHTS_SIZE);
		for (int k = 0; k < Mesh::ARRAY_WEIGHTS_SIZE; k++) {
			bones.write[k] = (k == 0) ? p_bone_index : 0;
			weights.write[k] = (k == 0) ? 1.0f : 0.0f;
		}
		st->set_bones(bones);
		st->set_weights(weights);
	}
	// Build cone sequence for shader (input order, matching solver).
	PackedVector4Array cone_sequence;
	int cone_count_shader = 0;
	{
		uint32_t nc = cones.size();
		for (uint32_t i = 0; i < nc; i++) {
			Vector3 ci = _get_cone_center_normalized(i);
			real_t ri = cones[i].w;
			if (i == 0) {
				cone_sequence.push_back(Vector4(ci.x, ci.y, ci.z, ri));
			}
			if (i + 1 < nc) {
				Vector3 cn = _get_cone_center_normalized(i + 1);
				real_t rn = cones[i + 1].w;
				Vector3 tan1, tan2;
				real_t trad;
				compute_tangent_circles(ci, ri, cn, rn, tan1, tan2, trad);
				cone_sequence.push_back(Vector4(tan1.x, tan1.y, tan1.z, trad));
				cone_sequence.push_back(Vector4(tan2.x, tan2.y, tan2.z, trad));
				cone_sequence.push_back(Vector4(cn.x, cn.y, cn.z, rn));
			}
		}
		cone_count_shader = nc;
	}

	for (int idx = 0; idx < points.size(); idx++) {
		// CUSTOM0 carries the classification direction the shader tests against cone_sequence,
		// which is in the CANONICAL (+Y forward) cone frame. It must stay canonical: baking the
		// swing rotation (p_transform.basis) into it would rotate the allowed region off the
		// bone forward, so the forward vertex would land in the forbidden (teal) region.
		const Vector3 canonical_dir = normals[idx];
		Vector3 n = normals[idx];
		Vector3 pos = points[idx];
		if (use_skin) {
			pos = p_transform.xform(pos * sphere_r);
			n = p_transform.basis.xform(n).normalized(); // render normal only (placement/lighting)
		}
		Color c;
		c.r = canonical_dir.x;
		c.g = canonical_dir.y;
		c.b = canonical_dir.z;
		c.a = 0.0f;
		st->set_custom(0, c);
		st->set_normal(n);
		st->add_vertex(pos);
	}
	for (int idx : indices) {
		st->add_index(idx);
	}
	r_mesh = st->commit();

	Ref<Shader> sh;
	sh.instantiate();
	sh->set_code(KUSUDAMA_GIZMO_SHADER);
	Ref<ShaderMaterial> mat;
	mat.instantiate();
	mat->set_shader(sh);
	Color boundary_color;
	boundary_color.set_ok_hsl(
			p_color.get_ok_hsl_h(),
			p_color.get_ok_hsl_s(),
			CLAMP((float)p_color.get_ok_hsl_l() - 0.25f, 0.0f, 1.0f),
			p_color.a);

	mat->set_shader_parameter("cone_count", cone_count_shader);
	mat->set_shader_parameter("cone_sequence", cone_sequence);
	mat->set_shader_parameter("kusudama_color", p_color);
	mat->set_shader_parameter("boundary_outline_color", boundary_color);
	r_material = mat;

	if (use_skin) {
		r_mesh_to_skeleton_rest = Transform3D();
	} else {
		r_mesh_to_skeleton_rest = p_transform;
		r_mesh_to_skeleton_rest.basis.scale(Vector3(sphere_r, sphere_r, sphere_r));
	}
}

void JointLimitationKusudama3D::append_extra_gizmo_meshes(const Transform3D &p_transform, float p_bone_length, const Color &p_color, Vector<ExtraMeshEntry> &r_extra_meshes, int p_bone_index) const {
	ExtraMeshEntry e;
	get_kusudama_fill_mesh_and_material(p_transform, p_bone_length, p_color, p_bone_index, e.transform, e.mesh, e.material);
	if (e.mesh.is_valid()) {
		r_extra_meshes.push_back(e);
	}
}
#endif // TOOLS_ENABLED

// Helper function implementations
bool JointLimitationKusudama3D::is_point_in_cone(const Vector3 &p_point, const Vector3 &p_cone_center, real_t p_cone_radius) const {
	if (p_point.is_zero_approx()) {
		return false;
	}
	return p_point.normalized().angle_to(p_cone_center) <= p_cone_radius;
}

void JointLimitationKusudama3D::extend_ray(Vector3 &r_start, Vector3 &r_end, real_t p_amount) const {
	Vector3 mid_point = r_start.lerp(r_end, (real_t)0.5);
	r_start += mid_point.direction_to(r_start) * p_amount;
	r_end += mid_point.direction_to(r_end) * p_amount;
}

int JointLimitationKusudama3D::ray_sphere_intersection_full(const Vector3 &p_ray_start, const Vector3 &p_ray_end, const Vector3 &p_sphere_center, real_t p_radius, Vector3 *r_intersection1, Vector3 *r_intersection2) const {
	Vector3 ray_start_rel = p_ray_start - p_sphere_center;
	Vector3 ray_end_rel = p_ray_end - p_sphere_center;
	Vector3 ray_dir_normalized = ray_start_rel.direction_to(ray_end_rel);
	Vector3 ray_to_center = -ray_start_rel;
	real_t ray_dot_center = ray_dir_normalized.dot(ray_to_center);
	real_t radius_squared = p_radius * p_radius;
	real_t center_dist_squared = ray_to_center.length_squared();
	real_t ray_dot_squared = ray_dot_center * ray_dot_center;
	real_t discriminant = radius_squared - center_dist_squared + ray_dot_squared;

	if (discriminant < 0.0) {
		return 0; // No intersection
	}

	real_t sqrt_discriminant = Math::sqrt(discriminant);
	real_t t1 = ray_dot_center - sqrt_discriminant;
	real_t t2 = ray_dot_center + sqrt_discriminant;

	if (r_intersection1) {
		*r_intersection1 = p_ray_start + ray_dir_normalized * t1;
	}
	if (r_intersection2) {
		*r_intersection2 = p_ray_start + ray_dir_normalized * t2;
	}

	return discriminant > 0.0 ? 2 : 1; // Two intersections or one (tangent)
}

void JointLimitationKusudama3D::compute_tangent_circles(const Vector3 &p_center1, real_t p_radius1, const Vector3 &p_center2, real_t p_radius2, Vector3 &r_tangent1, Vector3 &r_tangent2, real_t &r_tangent_radius) const {
	Vector3 center1 = p_center1.normalized();
	Vector3 center2 = p_center2.normalized();

	// Compute tangent circle radius. Cone radii are authored in [1deg, 180deg], so two large
	// adjacent cones with r1 + r2 > pi would give a NEGATIVE radius (a bogus keep-out bridge). When
	// the radii sum past pi the cones already overlap, so no real bridge is needed -- clamp to a
	// point. (Breakage shown in ../swing-twist-kusudama/KusGuards.lean.)
	r_tangent_radius = (Math::PI - (p_radius1 + p_radius2)) / 2.0;
	if (r_tangent_radius < (real_t)0.0) {
		r_tangent_radius = (real_t)0.0;
	}

	// Find arc normal (axis perpendicular to both cone centers)
	Vector3 arc_normal = center1.cross(center2);
	real_t arc_normal_len = arc_normal.length();

	if (Math::is_zero_approx(arc_normal_len)) {
		// Cones are parallel or opposite - handle specially
		arc_normal = center1.get_any_perpendicular();
		if (arc_normal.is_zero_approx()) {
			arc_normal = Vector3::UP;
		}
		arc_normal.normalize();

		// For opposite cones, tangent circles are at 90 degrees from the cone centers
		Vector3 perp1 = center1.get_any_perpendicular().normalized();

		// Rotate around center1 by the tangent radius to get tangent centers
		Quaternion rot1 = Quaternion(center1, r_tangent_radius);
		Quaternion rot2 = Quaternion(center1, -r_tangent_radius);
		r_tangent1 = rot1.xform(perp1).normalized();
		r_tangent2 = rot2.xform(perp1).normalized();
		return;
	}
	arc_normal.normalize();

	// Use plane intersection method
	real_t boundary_plus_tangent_radius_a = p_radius1 + r_tangent_radius;
	real_t boundary_plus_tangent_radius_b = p_radius2 + r_tangent_radius;

	// The axis of this cone, scaled to minimize its distance to the tangent contact points
	Vector3 scaled_axis_a = center1 * Math::cos(boundary_plus_tangent_radius_a);
	// A point on the plane running through the tangent contact points
	Vector3 safe_arc_normal = arc_normal;
	if (Math::is_zero_approx(safe_arc_normal.length_squared())) {
		safe_arc_normal = Vector3::UP;
	}
	Quaternion temp_var = Quaternion(safe_arc_normal.normalized(), boundary_plus_tangent_radius_a);
	Vector3 plane_dir1_a = temp_var.xform(center1);
	// Another point on the same plane
	Vector3 safe_center1 = center1;
	if (Math::is_zero_approx(safe_center1.length_squared())) {
		safe_center1 = Vector3::BACK;
	}
	Quaternion temp_var2 = Quaternion(safe_center1.normalized(), Math::PI / 2);
	Vector3 plane_dir2_a = temp_var2.xform(plane_dir1_a);

	Vector3 scaled_axis_b = center2 * Math::cos(boundary_plus_tangent_radius_b);
	// A point on the plane running through the tangent contact points
	Quaternion temp_var3 = Quaternion(safe_arc_normal.normalized(), boundary_plus_tangent_radius_b);
	Vector3 plane_dir1_b = temp_var3.xform(center2);
	// Another point on the same plane
	Vector3 safe_center2 = center2;
	if (Math::is_zero_approx(safe_center2.length_squared())) {
		safe_center2 = Vector3::BACK;
	}
	Quaternion temp_var4 = Quaternion(safe_center2.normalized(), Math::PI / 2);
	Vector3 plane_dir2_b = temp_var4.xform(plane_dir1_b);

	// Ray from scaled center of next cone to half way point between the circumference of this cone and the next cone
	Vector3 ray1_b_start = plane_dir1_b;
	Vector3 ray1_b_end = scaled_axis_b;
	Vector3 ray2_b_start = plane_dir1_b;
	Vector3 ray2_b_end = plane_dir2_b;

	extend_ray(ray1_b_start, ray1_b_end, 99.0);
	extend_ray(ray2_b_start, ray2_b_end, 99.0);

	Plane plane_ta(scaled_axis_a, plane_dir1_a, plane_dir2_a);
	Vector3 intersection1;
	Vector3 intersection2;
	if (!plane_ta.intersects_ray(ray1_b_start, ray1_b_start.direction_to(ray1_b_end), &intersection1)) {
		intersection1 = Vector3(NAN, NAN, NAN);
	}
	if (!plane_ta.intersects_ray(ray2_b_start, ray2_b_start.direction_to(ray2_b_end), &intersection2)) {
		intersection2 = Vector3(NAN, NAN, NAN);
	}

	Vector3 intersection_ray_start = intersection1;
	Vector3 intersection_ray_end = intersection2;
	extend_ray(intersection_ray_start, intersection_ray_end, 99.0);

	Vector3 sphere_intersect1;
	Vector3 sphere_intersect2;
	ray_sphere_intersection_full(intersection_ray_start, intersection_ray_end, Vector3(), 1.0, &sphere_intersect1, &sphere_intersect2);

	r_tangent1 = sphere_intersect1.normalized();
	r_tangent2 = sphere_intersect2.normalized();

	// Handle degenerate tangent centers (NaN or zero)
	if (!r_tangent1.is_finite() || Math::is_zero_approx(r_tangent1.length_squared())) {
		r_tangent1 = center1.get_any_perpendicular();
		if (Math::is_zero_approx(r_tangent1.length_squared())) {
			r_tangent1 = Vector3::UP;
		}
		r_tangent1.normalize();
	}
	if (!r_tangent2.is_finite() || Math::is_zero_approx(r_tangent2.length_squared())) {
		Vector3 orthogonal_base = r_tangent1.is_finite() ? r_tangent1 : center1;
		r_tangent2 = orthogonal_base.get_any_perpendicular();
		if (Math::is_zero_approx(r_tangent2.length_squared())) {
			r_tangent2 = Vector3::RIGHT;
		}
		r_tangent2.normalize();
	}
}

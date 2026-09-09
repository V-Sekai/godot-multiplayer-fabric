/**************************************************************************/
/*  joint_limitation_kusudama_3d.h                                        */
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

#include "core/templates/local_vector.h"
#include "scene/resources/3d/joint_limitation_3d.h"
#include "scene/resources/mesh.h"

class JointLimitationKusudama3D : public JointLimitation3D {
	GDCLASS(JointLimitationKusudama3D, JointLimitation3D);

	// Cones data: each cone is stored as Vector4(center_x, center_y, center_z, radius)
	// Center is stored raw for serialization and direct input (like gravity_direction in SpringBoneSimulator).
	LocalVector<Vector4> cones;
	// Cached normalized centers for internal use; invalidated when cones change.
	mutable LocalVector<Vector3> _normalized_cone_centers_cache;
	// Polygon cache: built from cones in convex hull order to form a closed boundary.
	mutable LocalVector<uint32_t> _hull_order;
	mutable LocalVector<Vector3> _polygon_vertices;
	mutable LocalVector<Vector3> _polygon_normals;
	// Per-pair tangent data (hull-ordered, closed loop): tan1, tan2, tan_radius for each pair.
	mutable LocalVector<Vector3> _tangent_centers_1;
	mutable LocalVector<Vector3> _tangent_centers_2;
	mutable LocalVector<real_t> _tangent_radii;
	mutable bool _polygon_dirty = true;

	// Twist limit (about the bone forward axis). Active when twist_to > twist_from.
	// Verified in Lean+Plausible (swing-twist clamp: in-range identity, output in range,
	// swing preserved, idempotent). See /tmp/kusudama_lean/Kusudama.lean.
	real_t twist_from = 0.0;
	real_t twist_to = 0.0;

	// Cushion: the soft-limit margin (radians). Near a cone/twist boundary the projection
	// eases the direction back over this band instead of hard-clamping -- the EWBIK "cushion"
	// (a comfortable approach to the limit). Wider = the joint resists the limit earlier and
	// more gently. Kept above a small floor so the projection stays C1 (no boundary teleport).
	real_t cushion = 0.06;

	// Strength: how strongly the constraint is enforced. 1 fully clamps to the allowed region;
	// 0 lets the direction pass through unconstrained; in between blends the two. Lets an animator
	// soften or disable a limit in place. EWBIK Kusudama strength.
	real_t strength = 1.0;

	Vector3 _swing_center() const; // normalized mean of cone centers (the rest/forward axis)
	bool _point_in_cone_union(const Vector3 &p_point) const;
	real_t _swing_boundary(const Vector3 &p_center, const Vector3 &p_tangent) const;

	void _invalidate_normalized_cache() const;
	Vector3 _get_cone_center_normalized(int p_index) const;
	void _invalidate_polygon_cache() const;
	void _rebuild_polygon_cache() const;
	void _compute_hull_order() const;
	bool _is_in_tangent_path(const Vector3 &p_point, uint32_t p_pair_index) const;
	bool _polygon_contains(const Vector3 &p_point) const;
	Vector3 _closest_on_small_circle(const Vector3 &p_point, const Vector3 &p_center, real_t p_radius) const;
	Vector3 _continuous_project(const Vector3 &p_point) const;

#ifdef TOOLS_ENABLED
	typedef Pair<Vector3, Vector3> Segment;
#endif // TOOLS_ENABLED

protected:
	static void _bind_methods();
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;

	virtual Vector3 _solve(const Vector3 &p_direction) const override;

private:
	bool is_point_in_cone(const Vector3 &p_point, const Vector3 &p_cone_center, real_t p_cone_radius) const;
	void extend_ray(Vector3 &r_start, Vector3 &r_end, real_t p_amount) const;
	int ray_sphere_intersection_full(const Vector3 &p_ray_start, const Vector3 &p_ray_end, const Vector3 &p_sphere_center, real_t p_radius, Vector3 *r_intersection1, Vector3 *r_intersection2) const;
	void compute_tangent_circles(const Vector3 &p_center1, real_t p_radius1, const Vector3 &p_center2, real_t p_radius2, Vector3 &r_tangent1, Vector3 &r_tangent2, real_t &r_tangent_radius) const;

public:
	void set_cones(const Vector<Vector4> &p_cones);
	Vector<Vector4> get_cones() const;

	void set_cone_count(int p_count);
	int get_cone_count() const;

	void set_cone_center(int p_index, const Vector3 &p_center);
	Vector3 get_cone_center(int p_index) const;

	// A cone center as two basis axes in [-1,1]: the right (X) and forward (Z) components
	// of the unit center; the up (Y) component is implied (positive). Lets a cone center be
	// authored/encoded as two axes instead of a Vector3.
	void set_cone_axes(int p_index, const Vector2 &p_axes);
	Vector2 get_cone_axes(int p_index) const;

	void set_cone_radius(int p_index, real_t p_radius);
	real_t get_cone_radius(int p_index) const;

	// --- Twist limit (radians, about the bone forward axis). Verified in Lean+Plausible. ---
	void set_twist_from(real_t p_radians);
	real_t get_twist_from() const;
	void set_twist_to(real_t p_radians);
	real_t get_twist_to() const;
	bool has_twist_limit() const;
	real_t clamp_twist_angle(real_t p_angle) const;

	// --- Cushion: tunable soft-limit margin (radians). EWBIK's "cushion". ---
	void set_cushion(real_t p_radians);
	real_t get_cushion() const;

	// --- Strength: how strongly the constraint is enforced [0..1]. ---
	void set_strength(real_t p_strength);
	real_t get_strength() const;
	// Clamp the twist of p_rotation about p_axis into [twist_from, twist_to]; swing untouched.
	Quaternion limit_twist(const Quaternion &p_rotation, const Vector3 &p_axis) const;

	// --- normalization to [0,1] between the range limits (per-axis, 0 at center -> 1 at limit). ---
	// Swing -> unit-disk coord: 0 at the ROM center, magnitude 1 on the ROM boundary.
	Vector2 swing_to_normalized(const Vector3 &p_direction) const;
	Vector3 swing_from_normalized(const Vector2 &p_normalized) const;
	// Twist angle -> [0,1] between twist_from..twist_to (and back).
	real_t twist_to_normalized(real_t p_angle) const;
	real_t twist_from_normalized(real_t p_normalized) const;

	static const int MAX_KUSUDAMA_CONES = 30;

#ifdef TOOLS_ENABLED
	int get_cone_sequence_for_shader(PackedVector4Array &r_cone_sequence) const;
	// r_mesh_to_skeleton_rest: transform from mesh local to skeleton global rest space. Identity when skinned (p_bone_index >= 0); otherwise constraint pose with sphere scale.
	void get_kusudama_fill_mesh_and_material(const Transform3D &p_transform, float p_bone_length, const Color &p_color, int p_bone_index, Transform3D &r_mesh_to_skeleton_rest, Ref<ArrayMesh> &r_mesh, Ref<Material> &r_material) const;
	virtual void append_extra_gizmo_meshes(const Transform3D &p_transform, float p_bone_length, const Color &p_color, Vector<ExtraMeshEntry> &r_extra_meshes, int p_bone_index = -1) const override;

#endif // TOOLS_ENABLED
};

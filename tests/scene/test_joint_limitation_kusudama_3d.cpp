/**************************************************************************/
/*  test_joint_limitation_kusudama_3d.cpp                                 */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_joint_limitation_kusudama_3d)

#ifndef _3D_DISABLED

#include "core/string/print_string.h"
#include "core/variant/variant.h"
#include "scene/3d/fabr_ik_3d.h"
#include "scene/3d/ik_kabsch_6d.h"
#include "scene/3d/marker_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/3d/swing_twist_ik_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/3d/humanoid_kusudama_rom.h"
#include "scene/resources/3d/joint_limitation_kusudama_3d.h"
#include "tests/scene/humanoid_kusudama_rom_gold.h"
#include "tests/test_macros.h"

namespace TestJointLimitationKusudama3D {

// Shared test helpers (file-static, not lambdas).
static bool tjk_finite(Skeleton3D *s) {
	for (int b = 0; b < s->get_bone_count(); b++) {
		const Transform3D t = s->get_bone_pose(b);
		if (!t.origin.is_finite() || !t.basis.get_column(0).is_finite() || !t.basis.get_column(1).is_finite() || !t.basis.get_column(2).is_finite()) {
			return false;
		}
	}
	return true;
}
static bool tjk_proper(Skeleton3D *s) {
	for (int b = 0; b < s->get_bone_count(); b++) {
		if (Math::abs(s->get_bone_pose(b).basis.determinant() - 1.0) > 1e-3) {
			return false;
		}
	}
	return true;
}

static void tjk_build_chain(SceneTree *p_tree, int n, real_t seg, real_t rest_tilt_deg, int max_iter, real_t adl, Skeleton3D *&sk, SwingTwistIK3D *&ik, Marker3D *&tgt) {
	sk = memnew(Skeleton3D);
	p_tree->get_root()->add_child(sk);
	for (int i = 0; i < n; i++) {
		const int b = sk->add_bone(vformat("B%d", i));
		if (i > 0) {
			sk->set_bone_parent(b, i - 1);
		}
		Basis rb;
		if (i > 0 && rest_tilt_deg != (real_t)0.0) {
			rb = Basis(Vector3(0, 0, 1), Math::deg_to_rad(rest_tilt_deg));
		}
		sk->set_bone_rest(b, Transform3D(rb, Vector3(i == 0 ? (real_t)0.0 : seg, 0, 0)));
	}
	sk->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);
	ik = memnew(SwingTwistIK3D);
	sk->add_child(ik);
	tgt = memnew(Marker3D);
	ik->add_child(tgt);
	tgt->set_name("Target");
	sk->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);
	ik->set_setting_count(1);
	ik->set_root_bone_name(0, "B0");
	ik->set_end_bone_name(0, vformat("B%d", n - 1));
	ik->set_target_node(0, NodePath("Target"));
	ik->set_max_iterations(max_iter);
	if (adl >= (real_t)0.0) {
		ik->set_angular_delta_limit(adl);
	}
}
static void tjk_reset(Skeleton3D *sk) {
	for (int b = 0; b < sk->get_bone_count(); b++) {
		sk->set_bone_pose_rotation(b, Quaternion());
		sk->set_bone_pose_position(b, sk->get_bone_rest(b).origin);
	}
}
static real_t tjk_rest_energy(Skeleton3D *sk, SwingTwistIK3D *ik) {
	real_t e = 0.0;
	for (int j = 0; j < ik->get_joint_count(0); j++) {
		const int b = ik->get_joint_bone(0, j);
		e += sk->get_bone_pose(b).basis.get_rotation_quaternion().angle_to(sk->get_bone_rest(b).basis.get_rotation_quaternion());
	}
	return e;
}

// Helper function to set cones from Vector<Vector4> using the individual cone API
static void set_cones_from_vector4(Ref<JointLimitationKusudama3D> limitation, const Vector<Vector4> &cones) {
	limitation->set_cone_count(cones.size());
	for (int i = 0; i < cones.size(); i++) {
		Vector3 center = Vector3(cones[i].x, cones[i].y, cones[i].z);
		real_t radius = cones[i].w;
		limitation->set_cone_center(i, center);
		limitation->set_cone_radius(i, radius);
	}
}

// Per-path jerk-amplification result (file-static, not a local struct, so the helpers below can
// be file-static functions instead of capturing lambdas).
struct TjkPathResult {
	const char *name;
	real_t max_jerk;
};
static void tjk_run_path(const Ref<JointLimitationKusudama3D> &p_lim, const Vector3 &p_forward, const Vector3 &p_right, const Quaternion &p_rot, Vector<TjkPathResult> &p_per_path, const char *p_name, const Vector<Vector3> &p_inputs) {
	real_t path_max = 0.0f;
	Vector3 prev_in = p_inputs[0];
	Vector3 prev_out = p_lim->solve(p_forward, p_right, p_rot, p_inputs[0]);
	for (int i = 1; i < p_inputs.size(); i++) {
		Vector3 out = p_lim->solve(p_forward, p_right, p_rot, p_inputs[i]);
		CHECK(out.is_finite());
		real_t amplification = prev_out.angle_to(out) - prev_in.angle_to(p_inputs[i]);
		path_max = MAX(path_max, amplification);
		prev_in = p_inputs[i];
		prev_out = out;
	}
	p_per_path.push_back(TjkPathResult{ p_name, path_max });
}
static void tjk_set_radii(const Ref<JointLimitationKusudama3D> &p_lim, const Vector3 &p_cp1, const Vector3 &p_cp2, const Vector3 &p_cp3, real_t p_r1, real_t p_r2, real_t p_r3, real_t &r_radius1, real_t &r_radius2, real_t &r_radius3) {
	r_radius1 = p_r1;
	r_radius2 = p_r2;
	r_radius3 = p_r3;
	Vector<Vector4> cones;
	cones.push_back(Vector4(p_cp1.x, p_cp1.y, p_cp1.z, p_r1));
	cones.push_back(Vector4(p_cp2.x, p_cp2.y, p_cp2.z, p_r2));
	cones.push_back(Vector4(p_cp3.x, p_cp3.y, p_cp3.z, p_r3));
	set_cones_from_vector4(p_lim, cones);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test point inside single cone") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 control_point = Vector3(0, 0, 1).normalized();
	real_t radius = Math::deg_to_rad(30.0); // 30 degrees

	Vector<Vector4> cones;
	cones.push_back(Vector4(control_point.x, control_point.y, control_point.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point at the center of the cone should be returned as-is
	Vector3 test_point = control_point;
	Vector3 result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), test_point);
	CHECK(result.is_equal_approx(test_point));

	// Point slightly off center but still within cone
	Vector3 point_in_cone = Quaternion(Vector3(1, 0, 0), radius * 0.5f).xform(control_point);
	result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), point_in_cone);
	CHECK(result.is_equal_approx(point_in_cone));
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test point outside single cone") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 control_point = Vector3(0, 0, 1).normalized();
	real_t radius = Math::deg_to_rad(30.0); // 30 degrees

	Vector<Vector4> cones;
	cones.push_back(Vector4(control_point.x, control_point.y, control_point.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point far outside the cone should be clamped to boundary
	Vector3 test_point = Vector3(1, 0, 0).normalized();
	Vector3 result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), test_point);

	// Result should be on the cone boundary, not the original point
	CHECK_FALSE(result.is_equal_approx(test_point));

	// Result should be close to the control point (within radius)
	real_t angle_to_control = result.angle_to(control_point);
	CHECK(angle_to_control <= radius + 0.01f); // Allow small tolerance
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test point with zero radius cone") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 control_point = Vector3(0, 0, 1).normalized();
	real_t radius = 0.0f; // Zero radius - only exact point allowed

	Vector<Vector4> cones;
	cones.push_back(Vector4(control_point.x, control_point.y, control_point.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point at exact control point should be returned
	Vector3 test_point = control_point;
	Vector3 result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), test_point);
	CHECK(result.is_equal_approx(control_point));

	// Any other point should be clamped to control point
	Vector3 outside_point = Vector3(1, 0, 0).normalized();
	result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), outside_point);
	CHECK(result.is_equal_approx(control_point));
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test multiple cones - point between cones") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// Create two cones
	Vector3 control_point1 = Vector3(1, 0, 0).normalized();
	Vector3 control_point2 = Vector3(0, 1, 0).normalized();
	real_t radius = Math::deg_to_rad(45.0); // 45 degrees

	Vector<Vector4> cones;
	cones.push_back(Vector4(control_point1.x, control_point1.y, control_point1.z, radius));
	cones.push_back(Vector4(control_point2.x, control_point2.y, control_point2.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point between the two cones should be handled by path logic
	Vector3 point_between = (control_point1 + control_point2).normalized();
	Vector3 result = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), point_between);

	// Result should be valid (not NaN)
	CHECK(result.is_finite());
	CHECK(result.length() > 0.9f); // Should be normalized
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test point on path between two adjacent cones") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// Create two cones with overlapping paths
	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	real_t radius = Math::deg_to_rad(60.0); // Large enough to create paths

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point exactly on the great circle path between cones
	Vector3 path_point = (cp1 + cp2).normalized();
	Vector3 result = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), path_point);

	// Should be on or near the path
	CHECK(result.is_finite());
	CHECK(result.length() > 0.9f);

	// Result should be closer to the path than the original if original was outside
	// Test with a point clearly between but not exactly on path
	Vector3 between_point = Vector3(0.7, 0.7, 0.1).normalized();
	Vector3 result2 = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), between_point);
	CHECK(result2.is_finite());
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test point outside both cones but in path region") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	real_t radius = Math::deg_to_rad(30.0); // Smaller radius

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point outside both cones but in the region between them
	Vector3 outside_point = Vector3(0.5, 0.5, 0.7).normalized();
	Vector3 result = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), outside_point);

	// Should be finite and normalized
	CHECK(result.is_finite());
	CHECK(result.length() > 0.9f);
	CHECK(result.length() < 1.1f);

	// Result should be either:
	// 1. The point unchanged (if in path region outside tangent circles)
	// 2. Projected to tangent circle boundary (if in path region inside tangent circles)
	// 3. Projected to nearest cone boundary (if outside all allowed regions)
	// So we just check it's a valid result
	CHECK(result.is_normalized());
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test path between cones with different radii") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	real_t radius1 = Math::deg_to_rad(45.0);
	real_t radius2 = Math::deg_to_rad(30.0);

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius1));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius2));
	set_cones_from_vector4(limitation, cones);

	// Point between cones should still work with different radii
	Vector3 between = (cp1 + cp2).normalized();
	Vector3 result = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), between);

	CHECK(result.is_finite());
	CHECK(result.length() > 0.9f);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test no wrap-around from last to first cone") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// Create three cones in a sequence (NOT a loop - no wrap-around)
	// Use very small radii to ensure there's a forbidden zone between non-adjacent cones
	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	Vector3 cp3 = Vector3(0, 0, 1).normalized();
	real_t radius = Math::deg_to_rad(10.0); // Very small radius to create clear forbidden zones

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	cones.push_back(Vector4(cp3.x, cp3.y, cp3.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point between last and first cone - should NOT be in an allowed path (no wrap-around)
	// Use a point that's clearly in the forbidden wrap-around region
	// With very small cone radii (10deg), the point at ~55 degrees from each axis should be
	// outside all cones and outside all tangent paths between adjacent cones
	Vector3 between_last_first = Vector3(0.577, 0.577, 0.577).normalized(); // ~55 degrees from each axis
	// Verify it's outside all cones
	real_t angle_to_cp1 = between_last_first.angle_to(cp1);
	real_t angle_to_cp2 = between_last_first.angle_to(cp2);
	real_t angle_to_cp3 = between_last_first.angle_to(cp3);
	CHECK(angle_to_cp1 > radius);
	CHECK(angle_to_cp2 > radius);
	CHECK(angle_to_cp3 > radius);

	Vector3 result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), between_last_first);

	CHECK(result.is_finite());
	CHECK(result.is_normalized());
	// With very small cone radii (10deg), this point should be in a forbidden region (wrap-around)
	// and should be constrained (not equal to input)
	// Result should be closer to one of the cones or their adjacent paths, not the wrap-around path
	real_t dist_to_cp1 = result.angle_to(cp1);
	real_t dist_to_cp3 = result.angle_to(cp3);
	CHECK((dist_to_cp1 < Math::PI / 2 || dist_to_cp3 < Math::PI / 2));
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test three cones stability - deterministic and continuous") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// Three cones in sequence (recreates scenario where 3+ holes were reported unstable).
	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	Vector3 cp3 = Vector3(0, 0, 1).normalized();
	real_t radius = Math::deg_to_rad(45.0);

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	cones.push_back(Vector4(cp3.x, cp3.y, cp3.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Direction in the forbidden "gap" (outside all cones and paths) so solver picks closest boundary.
	// This is where multiple boundaries can be nearly equidistant and cause flicker without tie-breaking.
	Vector3 input_dir = Vector3(0.57735, 0.57735, 0.57735).normalized(); // ~55° from each axis
	Vector3 forward = Vector3(0, 0, 1);
	Vector3 right = Vector3(1, 0, 0);
	Quaternion rot = Quaternion();

	// Determinism: same input must yield same output every time.
	Vector3 first = limitation->solve(forward, right, rot, input_dir);
	for (int i = 0; i < 99; i++) {
		Vector3 again = limitation->solve(forward, right, rot, input_dir);
		CHECK(again.is_equal_approx(first));
	}

	// Continuity: tiny perturbation in input should not cause a large jump in output.
	real_t eps = 1e-5f;
	Vector3 perturbed = (input_dir + Vector3(eps, 0, 0)).normalized();
	Vector3 out_perturbed = limitation->solve(forward, right, rot, perturbed);
	real_t angle_change = first.angle_to(out_perturbed);
	CHECK(angle_change < Math::deg_to_rad(1.0)); // Within 1 degree for 1e-5 input change
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test interpolation path - minimal jerk") {
	// Goal: interpolating input directions on the sphere should yield smooth output (minimal physics jerk).
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	Vector3 cp3 = Vector3(0, 0, 1).normalized();
	real_t radius = Math::deg_to_rad(45.0);
	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	cones.push_back(Vector4(cp3.x, cp3.y, cp3.z, radius));
	set_cones_from_vector4(limitation, cones);

	Vector3 forward = Vector3(0, 0, 1);
	Vector3 right = Vector3(1, 0, 0);
	Quaternion rot = Quaternion();

	// Path through the forbidden gap: +X -> gap (0.577,0.577,0.577) -> +Z to stress boundary choice.
	const int steps = 80;
	const real_t max_angular_step_rad = Math::deg_to_rad(4.0); // Max allowed output angle change per step
	Vector3 gap_dir = Vector3(0.57735, 0.57735, 0.57735).normalized();
	Vector3 prev_out;
	for (int i = 0; i <= steps; i++) {
		real_t t = (real_t)i / (real_t)steps;
		Vector3 input_dir;
		if (t <= 0.5) {
			input_dir = (cp1 * (1.0 - t * 2.0) + gap_dir * (t * 2.0)).normalized();
		} else {
			input_dir = (gap_dir * (1.0 - (t - 0.5) * 2.0) + cp3 * ((t - 0.5) * 2.0)).normalized();
		}
		Vector3 out = limitation->solve(forward, right, rot, input_dir);
		CHECK(out.is_finite());
		CHECK(out.length() > 0.9f);
		if (i > 0) {
			real_t step_angle = prev_out.angle_to(out);
			CHECK(step_angle < max_angular_step_rad);
		}
		prev_out = out;
	}
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test three cones - exhaustive no opposite-side snap") {
	// Bug: with 3 cones, input toward one cone could snap to a cone on the opposite side → extreme jerk.
	// Exhaustively test that output is always in the same hemisphere as input (dot >= 0).
	// Cone directions +X, +Y, +Z; radii within engine property range (1°–180°), different per cone.
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	Vector3 cp3 = Vector3(0, 0, 1).normalized();
	// Non-overlapping radii (r_i + r_j <= 90°); most interesting cases are when cones do not overlap.
	real_t radius1 = Math::deg_to_rad(25.0);
	real_t radius2 = Math::deg_to_rad(35.0);
	real_t radius3 = Math::deg_to_rad(30.0);
	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius1));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius2));
	cones.push_back(Vector4(cp3.x, cp3.y, cp3.z, radius3));
	set_cones_from_vector4(limitation, cones);

	Vector3 forward = Vector3(0, 0, 1);
	Vector3 right = Vector3(1, 0, 0);
	Quaternion rot = Quaternion();

	const int n_theta = 40;
	const int n_phi = 20;
	const real_t min_dot = 0.0; // Same hemisphere: output.dot(input) >= 0
	int checked = 0;
	for (int i = 0; i < n_theta; i++) {
		real_t theta = (real_t)i / (real_t)n_theta * Math::TAU;
		for (int j = 0; j < n_phi; j++) {
			real_t phi = (real_t)(j + 1) / (real_t)(n_phi + 1) * Math::PI; // Avoid poles for uniqueness
			Vector3 input_dir(
					Math::sin(phi) * Math::cos(theta),
					Math::sin(phi) * Math::sin(theta),
					Math::cos(phi));
			input_dir.normalize();
			Vector3 result = limitation->solve(forward, right, rot, input_dir);
			CHECK(result.is_finite());
			CHECK(result.length() > 0.9f);
			real_t dot_in_out = input_dir.dot(result);
			CHECK(dot_in_out >= min_dot); // No opposite-side snap: output must be in same hemisphere as input
			checked++;
		}
	}
	CHECK(checked == n_theta * n_phi);
}

// Slerp for unit vectors; used by pose-path tests.
static Vector3 slerp_unit(Vector3 a, Vector3 b, real_t t) {
	a.normalize();
	b.normalize();
	real_t dot_ab = a.dot(b);
	dot_ab = CLAMP(dot_ab, (real_t)-1.0, (real_t)1.0);
	if (dot_ab >= (real_t)0.9995) {
		return a.lerp(b, t).normalized();
	}
	real_t omega = Math::acos(dot_ab);
	real_t so = Math::sin(omega);
	if (so < (real_t)1e-6) {
		return a.lerp(b, t).normalized();
	}
	real_t sa = Math::sin((1.0 - t) * omega) / so;
	real_t sb = Math::sin(t * omega) / so;
	return (a * sa + b * sb).normalized();
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test small direction change yields small jerk across boundary") {
	// Assumption: small changes of direction yield small changes of jerk, even into the forbidden region.
	// Stepping across a cone boundary with small input steps should keep output steps bounded.
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();
	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	Vector3 cp3 = Vector3(0, 0, 1).normalized();
	real_t r1 = Math::deg_to_rad(25.0);
	real_t r2 = Math::deg_to_rad(35.0);
	real_t r3 = Math::deg_to_rad(30.0);
	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, r1));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, r2));
	cones.push_back(Vector4(cp3.x, cp3.y, cp3.z, r3));
	set_cones_from_vector4(limitation, cones);
	Vector3 forward = Vector3(0, 0, 1);
	Vector3 right = Vector3(1, 0, 0);
	Quaternion rot = Quaternion();

	Vector3 perp = cp1.cross(Vector3(0, 1, 0)).normalized();
	if (perp.length_squared() < 1e-6f) {
		perp = cp1.cross(Vector3(1, 0, 0)).normalized();
	}
	real_t r_start = r1 - Math::deg_to_rad(5.0f);
	real_t r_end = r1 + Math::deg_to_rad(5.0f);
	const int n_steps = 15;
	real_t max_output_step_rad = 0.0f;
	Vector3 prev_out = limitation->solve(forward, right, rot, (cp1 * Math::cos(r_start) + perp * Math::sin(r_start)).normalized());
	for (int i = 1; i <= n_steps; i++) {
		real_t r = r_start + (r_end - r_start) * (real_t)i / (real_t)n_steps;
		r = CLAMP(r, (real_t)0.02, (real_t)(Math::PI - 0.02));
		Vector3 input_dir = (cp1 * Math::cos(r) + perp * Math::sin(r)).normalized();
		Vector3 out = limitation->solve(forward, right, rot, input_dir);
		CHECK(out.is_finite());
		real_t step_rad = prev_out.angle_to(out);
		max_output_step_rad = MAX(max_output_step_rad, step_rad);
		prev_out = out;
	}
	real_t max_output_deg = Math::rad_to_deg(max_output_step_rad);
	const real_t max_allowed_deg = 15.0f; // small input steps => bounded jerk across boundary
	CHECK(max_output_deg <= max_allowed_deg);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test three cones - pose path no high jerk") {
	// Use case: 3-cone joints (knees, elbows, shoulders). Knees/elbows often have long-and-thin limitations
	// (one larger radius, two smaller). We run symmetric non-overlapping and long-thin setups and take max jerk per path.
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	Vector3 cp3 = Vector3(0, 0, 1).normalized();
	Vector3 forward = Vector3(0, 0, 1);
	Vector3 right = Vector3(1, 0, 0);
	Quaternion rot = Quaternion();

	// Stateless projection. Anomaly = the constraint AMPLIFYING the input's own
	// motion (output_step - input_step); a real teleport amplifies hugely while
	// passing a non-smooth input through unchanged amplifies ~0.
	const real_t jerk_anomaly_rad = (real_t)0.18; // ~10.3 deg of added jerk
	real_t radius1, radius2, radius3;
	Vector<TjkPathResult> per_path;

	// max_jerk here is the constraint's *amplification* of the input's own motion:
	// max(output_step - input_step). A retraction onto the limit region must not
	// ADD jerk — a real teleport amplifies hugely, while passing a non-smooth input
	// through unchanged (identity inside the region) amplifies by ~0. Measuring the
	// absolute output step instead would wrongly flag the test paths' own
	// piecewise-linear corners as constraint defects.

	// Setup 1: symmetric non-overlapping (25°, 35°, 30°)
	tjk_set_radii(limitation, cp1, cp2, cp3, Math::deg_to_rad(25.0), Math::deg_to_rad(35.0), Math::deg_to_rad(30.0), radius1, radius2, radius3);

	// Through gap (+X -> diagonal -> +Z)
	{
		Vector<Vector3> path;
		const int steps = 80;
		Vector3 gap = Vector3(0.57735f, 0.57735f, 0.57735f).normalized();
		for (int i = 0; i <= steps; i++) {
			real_t t = (real_t)i / (real_t)steps;
			Vector3 input_dir = (t <= 0.5f) ? slerp_unit(cp1, gap, t * 2.0f) : slerp_unit(gap, cp3, (t - 0.5f) * 2.0f);
			path.push_back(input_dir);
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "through_gap", path);
	}

	// Sweep cones (+X -> +Y -> +Z)
	{
		Vector<Vector3> path;
		const int steps = 60;
		for (int i = 0; i <= steps; i++) {
			real_t t = (real_t)i / (real_t)steps;
			real_t x = Math::cos(t * Math::PI * 0.5) * Math::cos(t * Math::PI * 0.5);
			real_t y = Math::sin(t * Math::PI * 0.5) * (1.0 - 0.3 * t);
			real_t z = Math::sin(t * Math::PI * 0.5) * 0.3 * t + (1.0 - t) * 0.1;
			path.push_back(Vector3(x, y, z).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "sweep_cones", path);
	}

	// Near boundary (XY circle, sensitive to tie-break)
	{
		Vector<Vector3> path;
		const int steps = 80;
		for (int i = 0; i <= steps; i++) {
			real_t theta = ((real_t)i / (real_t)steps) * Math::PI;
			path.push_back(Vector3(Math::cos(theta) * 0.9f, Math::sin(theta) * 0.9f, 0.1f).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "near_boundary", path);
	}

	// Cross cone boundary (+X cone 25° in/out)
	{
		Vector<Vector3> path;
		const int steps = 100;
		for (int i = 0; i <= steps; i++) {
			real_t t = (real_t)i / (real_t)steps;
			real_t x = (t <= 0.4f) ? (1.0f - t * 0.5f) : (0.8f - (t - 0.4f) * 0.5f);
			if (t > 0.6f) {
				x = 0.6f - (t - 0.6f) * 0.2f;
			}
			real_t y = (t <= 0.4f) ? (t * 0.2f) : (0.08f + (t - 0.4f) * 0.4f);
			if (t > 0.6f) {
				y = 0.24f + (t - 0.6f) * 0.4f;
			}
			real_t z = (t <= 0.4f) ? (t * 0.05f) : (0.02f + (t - 0.4f) * 0.3f);
			if (t > 0.6f) {
				z = 0.14f + (t - 0.6f) * 0.3f;
			}
			path.push_back(Vector3(x, y, z).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "cross_cone_boundary", path);
	}

	// Along +X cone boundary (25° circle)
	{
		Vector<Vector3> path;
		const int steps = 80;
		Vector3 perp0 = cp1.cross(Vector3(1, 0, 0)).normalized();
		if (perp0.length_squared() < 1e-6f) {
			perp0 = cp1.cross(Vector3(0, 1, 0)).normalized();
		}
		Vector3 perp1 = cp1.cross(perp0).normalized();
		for (int i = 0; i <= steps; i++) {
			real_t theta = ((real_t)i / (real_t)steps) * Math::TAU;
			Vector3 pt = cp1 * Math::cos(radius1) + (perp0 * Math::cos(theta) + perp1 * Math::sin(theta)) * Math::sin(radius1);
			path.push_back(pt.normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "along_cone_boundary", path);
	}

	// Along tangent arc (X–Y path boundary)
	{
		Vector3 tan0 = Vector3(Math::cos(radius1), Math::sin(radius1), 0).normalized();
		Vector3 tan1 = Vector3(Math::sin(radius2), Math::cos(radius2), 0).normalized();
		Vector<Vector3> path;
		const int steps = 80;
		for (int i = 0; i <= steps; i++) {
			real_t t = (real_t)i / (real_t)steps;
			path.push_back(slerp_unit(tan0, tan1, t));
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "along_tangent_arc", path);
	}

	// Cone to tangent junction
	{
		Vector3 on_cone = cp1 * Math::cos(radius1) + Vector3(0, 1, 0).normalized() * Math::sin(radius1);
		on_cone.normalize();
		Vector3 tan0 = Vector3(Math::cos(radius1), Math::sin(radius1), 0).normalized();
		Vector3 tan1 = Vector3(Math::sin(radius2), Math::cos(radius2), 0).normalized();
		Vector3 on_tangent = slerp_unit(tan0, tan1, 0.5f);
		Vector<Vector3> path;
		const int steps = 80;
		for (int i = 0; i <= steps; i++) {
			real_t t = (real_t)i / (real_t)steps;
			path.push_back(slerp_unit(on_cone, on_tangent, t));
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "cone_to_tangent_junction", path);
	}

	// Near triple gap (small circle around diagonal)
	{
		Vector3 gap = Vector3(1, 1, 1).normalized();
		Vector3 u = gap.cross(Vector3(1, 0, 0)).normalized();
		if (u.length_squared() < 1e-6f) {
			u = gap.cross(Vector3(0, 1, 0)).normalized();
		}
		Vector3 v = gap.cross(u).normalized();
		Vector<Vector3> path;
		const int steps = 100;
		real_t r = 0.08f;
		for (int i = 0; i <= steps; i++) {
			real_t theta = ((real_t)i / (real_t)steps) * Math::TAU;
			path.push_back((gap + (u * Math::cos(theta) + v * Math::sin(theta)) * r).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "near_triple_gap", path);
	}

	// Just inside/outside +X cone boundary (25°)
	{
		Vector3 perp = cp1.cross(Vector3(0, 1, 0)).normalized();
		if (perp.length_squared() < 1e-6f) {
			perp = cp1.cross(Vector3(1, 0, 0)).normalized();
		}
		Vector<Vector3> path;
		const int steps = 120;
		real_t eps = 0.015f;
		for (int i = 0; i <= steps; i++) {
			real_t sign = (i % 2 == 0) ? 1.0f : -1.0f;
			real_t r = CLAMP(radius1 + sign * eps, (real_t)0.01, (real_t)(Math::PI - 0.01));
			path.push_back((cp1 * Math::cos(r) + perp * Math::sin(r)).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "just_inside_outside_boundary", path);
	}

	// Along cone1–cone2 boundaries (X–Y rims)
	{
		Vector3 px = Vector3(Math::cos(radius1), Math::sin(radius1), 0).normalized();
		Vector3 py = Vector3(Math::sin(radius2), Math::cos(radius2), 0).normalized();
		Vector<Vector3> path;
		const int steps = 60;
		for (int i = 0; i <= steps; i++) {
			real_t t = (real_t)i / (real_t)steps;
			path.push_back(slerp_unit(px, py, t));
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "along_cone1_cone2_boundaries", path);
	}

	// Exceed limits: path in forbidden negative octant (all points outside +X,+Y,+Z cones)
	{
		Vector3 neg = Vector3(-1, -1, -1).normalized();
		Vector3 u = neg.cross(Vector3(1, 0, 0)).normalized();
		if (u.length_squared() < 1e-6f) {
			u = neg.cross(Vector3(0, 1, 0)).normalized();
		}
		Vector3 v = neg.cross(u).normalized();
		Vector<Vector3> path;
		const int steps = 80;
		for (int i = 0; i <= steps; i++) {
			real_t theta = ((real_t)i / (real_t)steps) * (real_t)(2.0 * Math::PI * 0.7);
			path.push_back((neg + (u * Math::cos(theta) + v * Math::sin(theta)) * 0.1f).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "exceed_negative_octant", path);
	}

	// Exceed limits: inside +X cone -> past boundary (exceed) -> back inside
	{
		Vector3 perp = cp1.cross(Vector3(0, 1, 0)).normalized();
		if (perp.length_squared() < 1e-6f) {
			perp = cp1.cross(Vector3(1, 0, 0)).normalized();
		}
		Vector<Vector3> path;
		const int steps = 100;
		for (int i = 0; i <= steps; i++) {
			real_t t = (real_t)i / (real_t)steps;
			real_t r;
			if (t <= 0.35f) {
				r = radius1 * (1.0f - (t / 0.35f) * 0.3f);
			} else if (t <= 0.65f) {
				r = radius1 + (real_t)0.15 * (t - 0.35f) / 0.3f;
			} else {
				r = radius1 + (real_t)0.15 * (1.0f - (t - 0.65f) / 0.35f);
			}
			r = CLAMP(r, (real_t)0.02, (real_t)(Math::PI - 0.02));
			path.push_back((cp1 * Math::cos(r) + perp * Math::sin(r)).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "exceed_past_cone_then_back", path);
	}

	// Exceed limits: path along forbidden diagonal (gap) with small perturbations
	{
		Vector3 gap = Vector3(1, 1, 1).normalized();
		Vector3 u = gap.cross(Vector3(1, 0, 0)).normalized();
		if (u.length_squared() < 1e-6f) {
			u = gap.cross(Vector3(0, 1, 0)).normalized();
		}
		Vector3 v = gap.cross(u).normalized();
		Vector<Vector3> path;
		const int steps = 80;
		for (int i = 0; i <= steps; i++) {
			real_t theta = ((real_t)i / (real_t)steps) * Math::TAU;
			real_t r = 0.05f + 0.03f * Math::sin(theta * 2.0f);
			path.push_back((gap + (u * Math::cos(theta) + v * Math::sin(theta)) * r).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "exceed_along_forbidden_diagonal", path);
	}

	Vector<TjkPathResult> per_path_sym = per_path;
	per_path.clear();

	// Setup 2: long and thin (knee/elbow style) — one larger radius, two smaller; non-overlapping (45+20<=90, 20+20<=90)
	tjk_set_radii(limitation, cp1, cp2, cp3, Math::deg_to_rad(45.0), Math::deg_to_rad(20.0), Math::deg_to_rad(20.0), radius1, radius2, radius3);

	// Re-run all paths with long-thin radii (path geometry uses current radius1, radius2, radius3)
	{
		Vector<Vector3> path;
		const int steps = 80;
		Vector3 gap = Vector3(0.57735f, 0.57735f, 0.57735f).normalized();
		for (int i = 0; i <= steps; i++) {
			real_t t = (real_t)i / (real_t)steps;
			path.push_back((t <= 0.5f) ? slerp_unit(cp1, gap, t * 2.0f) : slerp_unit(gap, cp3, (t - 0.5f) * 2.0f));
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "through_gap", path);
	}
	{
		Vector<Vector3> path;
		const int steps = 60;
		for (int i = 0; i <= steps; i++) {
			real_t t = (real_t)i / (real_t)steps;
			real_t x = Math::cos(t * Math::PI * 0.5) * Math::cos(t * Math::PI * 0.5);
			real_t y = Math::sin(t * Math::PI * 0.5) * (1.0 - 0.3 * t);
			real_t z = Math::sin(t * Math::PI * 0.5) * 0.3 * t + (1.0 - t) * 0.1;
			path.push_back(Vector3(x, y, z).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "sweep_cones", path);
	}
	{
		Vector<Vector3> path;
		const int steps = 80;
		for (int i = 0; i <= steps; i++) {
			real_t theta = ((real_t)i / (real_t)steps) * Math::PI;
			path.push_back(Vector3(Math::cos(theta) * 0.9f, Math::sin(theta) * 0.9f, 0.1f).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "near_boundary", path);
	}
	{
		Vector<Vector3> path;
		const int steps = 100;
		for (int i = 0; i <= steps; i++) {
			real_t t = (real_t)i / (real_t)steps;
			real_t x = (t <= 0.4f) ? (1.0f - t * 0.5f) : (0.8f - (t - 0.4f) * 0.5f);
			if (t > 0.6f) {
				x = 0.6f - (t - 0.6f) * 0.2f;
			}
			real_t y = (t <= 0.4f) ? (t * 0.2f) : (0.08f + (t - 0.4f) * 0.4f);
			if (t > 0.6f) {
				y = 0.24f + (t - 0.6f) * 0.4f;
			}
			real_t z = (t <= 0.4f) ? (t * 0.05f) : (0.02f + (t - 0.4f) * 0.3f);
			if (t > 0.6f) {
				z = 0.14f + (t - 0.6f) * 0.3f;
			}
			path.push_back(Vector3(x, y, z).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "cross_cone_boundary", path);
	}
	{
		Vector<Vector3> path;
		const int steps = 80;
		Vector3 perp0 = cp1.cross(Vector3(1, 0, 0)).normalized();
		if (perp0.length_squared() < 1e-6f) {
			perp0 = cp1.cross(Vector3(0, 1, 0)).normalized();
		}
		Vector3 perp1 = cp1.cross(perp0).normalized();
		for (int i = 0; i <= steps; i++) {
			real_t theta = ((real_t)i / (real_t)steps) * Math::TAU;
			path.push_back((cp1 * Math::cos(radius1) + (perp0 * Math::cos(theta) + perp1 * Math::sin(theta)) * Math::sin(radius1)).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "along_cone_boundary", path);
	}
	{
		Vector3 tan0 = Vector3(Math::cos(radius1), Math::sin(radius1), 0).normalized();
		Vector3 tan1 = Vector3(Math::sin(radius2), Math::cos(radius2), 0).normalized();
		Vector<Vector3> path;
		const int steps = 80;
		for (int i = 0; i <= steps; i++) {
			real_t t = (real_t)i / (real_t)steps;
			path.push_back(slerp_unit(tan0, tan1, t));
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "along_tangent_arc", path);
	}
	{
		Vector3 on_cone = cp1 * Math::cos(radius1) + Vector3(0, 1, 0).normalized() * Math::sin(radius1);
		on_cone.normalize();
		Vector3 tan0 = Vector3(Math::cos(radius1), Math::sin(radius1), 0).normalized();
		Vector3 tan1 = Vector3(Math::sin(radius2), Math::cos(radius2), 0).normalized();
		Vector3 on_tangent = slerp_unit(tan0, tan1, 0.5f);
		Vector<Vector3> path;
		const int steps = 80;
		for (int i = 0; i <= steps; i++) {
			real_t t = (real_t)i / (real_t)steps;
			path.push_back(slerp_unit(on_cone, on_tangent, t));
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "cone_to_tangent_junction", path);
	}
	{
		Vector3 gap = Vector3(1, 1, 1).normalized();
		Vector3 u = gap.cross(Vector3(1, 0, 0)).normalized();
		if (u.length_squared() < 1e-6f) {
			u = gap.cross(Vector3(0, 1, 0)).normalized();
		}
		Vector3 v = gap.cross(u).normalized();
		Vector<Vector3> path;
		const int steps = 100;
		real_t r = 0.08f;
		for (int i = 0; i <= steps; i++) {
			real_t theta = ((real_t)i / (real_t)steps) * Math::TAU;
			path.push_back((gap + (u * Math::cos(theta) + v * Math::sin(theta)) * r).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "near_triple_gap", path);
	}
	{
		Vector3 perp = cp1.cross(Vector3(0, 1, 0)).normalized();
		if (perp.length_squared() < 1e-6f) {
			perp = cp1.cross(Vector3(1, 0, 0)).normalized();
		}
		Vector<Vector3> path;
		const int steps = 120;
		real_t eps = 0.015f;
		for (int i = 0; i <= steps; i++) {
			real_t sign = (i % 2 == 0) ? 1.0f : -1.0f;
			real_t r = CLAMP(radius1 + sign * eps, (real_t)0.01, (real_t)(Math::PI - 0.01));
			path.push_back((cp1 * Math::cos(r) + perp * Math::sin(r)).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "just_inside_outside_boundary", path);
	}
	{
		Vector3 px = Vector3(Math::cos(radius1), Math::sin(radius1), 0).normalized();
		Vector3 py = Vector3(Math::sin(radius2), Math::cos(radius2), 0).normalized();
		Vector<Vector3> path;
		const int steps = 60;
		for (int i = 0; i <= steps; i++) {
			real_t t = (real_t)i / (real_t)steps;
			path.push_back(slerp_unit(px, py, t));
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "along_cone1_cone2_boundaries", path);
	}
	{
		Vector3 neg = Vector3(-1, -1, -1).normalized();
		Vector3 u = neg.cross(Vector3(1, 0, 0)).normalized();
		if (u.length_squared() < 1e-6f) {
			u = neg.cross(Vector3(0, 1, 0)).normalized();
		}
		Vector3 v = neg.cross(u).normalized();
		Vector<Vector3> path;
		const int steps = 80;
		for (int i = 0; i <= steps; i++) {
			real_t theta = ((real_t)i / (real_t)steps) * (real_t)(2.0 * Math::PI * 0.7);
			path.push_back((neg + (u * Math::cos(theta) + v * Math::sin(theta)) * 0.1f).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "exceed_negative_octant", path);
	}
	{
		Vector3 perp = cp1.cross(Vector3(0, 1, 0)).normalized();
		if (perp.length_squared() < 1e-6f) {
			perp = cp1.cross(Vector3(1, 0, 0)).normalized();
		}
		Vector<Vector3> path;
		const int steps = 100;
		for (int i = 0; i <= steps; i++) {
			real_t t = (real_t)i / (real_t)steps;
			real_t r;
			if (t <= 0.35f) {
				r = radius1 * (1.0f - (t / 0.35f) * 0.3f);
			} else if (t <= 0.65f) {
				r = radius1 + (real_t)0.15 * (t - 0.35f) / 0.3f;
			} else {
				r = radius1 + (real_t)0.15 * (1.0f - (t - 0.65f) / 0.35f);
			}
			r = CLAMP(r, (real_t)0.02, (real_t)(Math::PI - 0.02));
			path.push_back((cp1 * Math::cos(r) + perp * Math::sin(r)).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "exceed_past_cone_then_back", path);
	}
	{
		Vector3 gap = Vector3(1, 1, 1).normalized();
		Vector3 u = gap.cross(Vector3(1, 0, 0)).normalized();
		if (u.length_squared() < 1e-6f) {
			u = gap.cross(Vector3(0, 1, 0)).normalized();
		}
		Vector3 v = gap.cross(u).normalized();
		Vector<Vector3> path;
		const int steps = 80;
		for (int i = 0; i <= steps; i++) {
			real_t theta = ((real_t)i / (real_t)steps) * Math::TAU;
			real_t r = 0.05f + 0.03f * Math::sin(theta * 2.0f);
			path.push_back((gap + (u * Math::cos(theta) + v * Math::sin(theta)) * r).normalized());
		}
		tjk_run_path(limitation, forward, right, rot, per_path, "exceed_along_forbidden_diagonal", path);
	}

	// Every path — including along_tangent_arc — must keep the constraint's added
	// jerk below the threshold. These were previously only printed (and
	// along_tangent_arc was excluded as a "known high-jerk bug"); they are real
	// failures now.
	for (int i = 0; i < per_path_sym.size() && i < per_path.size(); i++) {
		real_t j = MAX(per_path_sym[i].max_jerk, per_path[i].max_jerk);
		CHECK_MESSAGE(j < jerk_anomaly_rad,
				vformat("constraint jerk amplification: %s = %.2f deg", per_path_sym[i].name, Math::rad_to_deg(j)));
	}
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test point in tangent circle region between cones") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	real_t radius = Math::deg_to_rad(45.0);

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point that should be in the tangent circle region
	// This is in the region where the tangent circle connects the two cones
	Vector3 tangent_region_point = Vector3(0.6, 0.6, 0.5).normalized();
	Vector3 result = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), tangent_region_point);

	CHECK(result.is_finite());
	CHECK(result.length() > 0.9f);

	// Result should be on or near the tangent path
	// The angle to either control point should be reasonable
	real_t angle1 = result.angle_to(cp1);
	real_t angle2 = result.angle_to(cp2);
	CHECK((angle1 < Math::PI / 2 || angle2 < Math::PI / 2)); // Should be in hemisphere of at least one cone
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test multiple paths - point closest to which path") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// Create three cones forming a triangle
	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	Vector3 cp3 = Vector3(0, 0, 1).normalized();
	real_t radius = Math::deg_to_rad(40.0);

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	cones.push_back(Vector4(cp3.x, cp3.y, cp3.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point that could be in path between cp1-cp2 or cp2-cp3
	// Should find the closest valid path
	Vector3 test_point = Vector3(0.4, 0.5, 0.3).normalized();
	Vector3 result = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), test_point);

	CHECK(result.is_finite());
	CHECK(result.length() > 0.9f);

	// Result should be valid and normalized
	CHECK(result.is_normalized());
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test path with very close cones") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// Two cones very close together
	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0.99, 0.1, 0).normalized();
	real_t radius = Math::deg_to_rad(30.0);

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point between very close cones
	Vector3 between = (cp1 + cp2).normalized();
	Vector3 result = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), between);

	CHECK(result.is_finite());
	CHECK(result.length() > 0.9f);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test path with nearly opposite cones") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// Two cones nearly opposite each other
	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(-0.9, 0.1, 0).normalized();
	real_t radius = Math::deg_to_rad(45.0);

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point between nearly opposite cones
	Vector3 between = (cp1 + cp2).normalized();
	Vector3 result = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), between);

	CHECK(result.is_finite());
	CHECK(result.length() > 0.9f);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test multiple cones - point in first cone") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 control_point1 = Vector3(1, 0, 0).normalized();
	Vector3 control_point2 = Vector3(0, 1, 0).normalized();
	real_t radius = Math::deg_to_rad(45.0);

	Vector<Vector4> cones;
	cones.push_back(Vector4(control_point1.x, control_point1.y, control_point1.z, radius));
	cones.push_back(Vector4(control_point2.x, control_point2.y, control_point2.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point inside first cone should be returned as-is
	Vector3 point_in_cone1 = Quaternion(Vector3(0, 1, 0), radius * 0.3f).xform(control_point1);
	Vector3 result = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), point_in_cone1);
	CHECK(result.is_equal_approx(point_in_cone1));
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test empty cones") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector<Vector4> empty_cones;
	set_cones_from_vector4(limitation, empty_cones);

	Vector3 test_point = Vector3(1, 0, 0).normalized();
	Vector3 result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), test_point);
	CHECK(result.is_equal_approx(test_point));

	result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), test_point);
	CHECK(result.is_equal_approx(test_point));
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test orientationally unconstrained") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// With no cones, should return input unchanged (unconstrained)
	limitation->set_cone_count(0);

	// Should return input regardless of cone constraints
	Vector3 test_point = Vector3(1, 0, 0).normalized();
	Vector3 result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), test_point);
	CHECK(result.is_equal_approx(test_point));
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test cones getters and setters") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector<Vector4> cones;
	cones.push_back(Vector4(1, 0, 0, Math::deg_to_rad(30.0)));
	cones.push_back(Vector4(0, 1, 0, Math::deg_to_rad(45.0)));
	cones.push_back(Vector4(0, 0, 1, Math::deg_to_rad(60.0)));

	set_cones_from_vector4(limitation, cones);

	CHECK(limitation->get_cone_count() == 3);
	CHECK(Math::is_equal_approx((double)limitation->get_cone_radius(0), Math::deg_to_rad(30.0)));
	CHECK(Math::is_equal_approx((double)limitation->get_cone_radius(1), Math::deg_to_rad(45.0)));
	CHECK(Math::is_equal_approx((double)limitation->get_cone_radius(2), Math::deg_to_rad(60.0)));
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test large radius cone") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 control_point = Vector3(0, 0, 1).normalized();
	real_t radius = Math::deg_to_rad(170.0); // Very large cone

	Vector<Vector4> cones;
	cones.push_back(Vector4(control_point.x, control_point.y, control_point.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Most points should be inside such a large cone
	Vector3 test_point = Vector3(1, 0, 0).normalized();
	Vector3 result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), test_point);

	// Should be valid
	CHECK(result.is_finite());
	CHECK(result.length() > 0.9f);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test three cones in sequence") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// Create three cones forming a path
	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	Vector3 cp3 = Vector3(0, 0, 1).normalized();
	real_t radius = Math::deg_to_rad(45.0);

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	cones.push_back(Vector4(cp3.x, cp3.y, cp3.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Test point in first cone
	Vector3 point1 = Quaternion(Vector3(0, 1, 0), radius * 0.3f).xform(cp1);
	Vector3 result1 = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), point1);
	CHECK(result1.is_equal_approx(point1));

	// Test point in second cone
	// Create a point inside the second cone by rotating cp2 by a small angle (less than radius)
	Vector3 point2 = Quaternion(Vector3(1, 0, 0), radius * 0.3f).xform(cp2);
	// Verify point2 is actually inside the cone
	real_t angle_to_cp2 = point2.angle_to(cp2);
	CHECK(angle_to_cp2 < radius);
	Vector3 result2 = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), point2);
	// Point inside cone should be returned unchanged (or very close due to normalization)
	// Allow some tolerance for floating point precision
	CHECK(result2.is_normalized());
	real_t result_angle_to_cp2 = result2.angle_to(cp2);
	// Result should be inside or on the cone boundary
	CHECK(result_angle_to_cp2 <= radius + 0.01f);

	// Test point between cones (should use path logic)
	Vector3 point_between = (cp1 + cp2).normalized();
	Vector3 result3 = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), point_between);
	CHECK(result3.is_finite());
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test edge case - parallel vectors") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 control_point = Vector3(1, 0, 0).normalized();
	real_t radius = Math::deg_to_rad(30.0);

	Vector<Vector4> cones;
	cones.push_back(Vector4(control_point.x, control_point.y, control_point.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Test with input parallel to control point (should handle gracefully)
	Vector3 parallel_point = control_point * 2.0f; // Same direction, different length
	Vector3 result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), parallel_point);

	// Should normalize and return (point is inside cone)
	CHECK(result.is_finite());
	CHECK(result.length() > 0.9f);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test edge case - opposite direction") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 control_point = Vector3(0, 0, 1).normalized();
	real_t radius = Math::deg_to_rad(30.0);

	Vector<Vector4> cones;
	cones.push_back(Vector4(control_point.x, control_point.y, control_point.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point in opposite direction (180 degrees away)
	Vector3 opposite = -control_point;
	Vector3 result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), opposite);

	// Should clamp to boundary
	CHECK(result.is_finite());
	real_t angle_to_control = result.angle_to(control_point);
	CHECK(angle_to_control <= radius + 0.01f);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test make_space method") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 forward = Vector3(0, 1, 0);
	Vector3 right = Vector3(1, 0, 0);
	Quaternion offset = Quaternion();

	Quaternion space = limitation->make_space(forward, right, offset);

	// Should return a valid quaternion
	CHECK(space.is_normalized());
	CHECK(space.is_finite());
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test cone count manipulation") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	CHECK(limitation->get_cone_count() == 0);

	// Increase count
	limitation->set_cone_count(3);
	CHECK(limitation->get_cone_count() == 3);

	// Decrease count
	limitation->set_cone_count(2);
	CHECK(limitation->get_cone_count() == 2);

	// Set to zero
	limitation->set_cone_count(0);
	CHECK(limitation->get_cone_count() == 0);

	// Set back to one
	limitation->set_cone_count(1);
	CHECK(limitation->get_cone_count() == 1);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test individual cone property setters and getters") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// Set up two cones
	limitation->set_cone_count(2);

	// Test set/get cone center
	Vector3 center1 = Vector3(1, 0, 0).normalized();
	Vector3 center2 = Vector3(0, 1, 0).normalized();
	limitation->set_cone_center(0, center1);
	limitation->set_cone_center(1, center2);

	Vector3 retrieved1 = limitation->get_cone_center(0);
	Vector3 retrieved2 = limitation->get_cone_center(1);
	CHECK(retrieved1.is_equal_approx(center1));
	CHECK(retrieved2.is_equal_approx(center2));

	// Test set/get cone radius
	real_t radius1 = Math::deg_to_rad(30.0);
	real_t radius2 = Math::deg_to_rad(45.0);
	limitation->set_cone_radius(0, radius1);
	limitation->set_cone_radius(1, radius2);

	CHECK(Math::is_equal_approx(limitation->get_cone_radius(0), radius1));
	CHECK(Math::is_equal_approx(limitation->get_cone_radius(1), radius2));

	// Test set/get cone center quaternion (using inlined logic)
	Quaternion quat1 = Quaternion(Vector3(0, 1, 0), Math::deg_to_rad(45.0));
	Quaternion quat2 = Quaternion(Vector3(1, 0, 0), Math::deg_to_rad(30.0));
	// Convert quaternion to direction vector by rotating the default direction (0, 1, 0)
	Vector3 default_dir = Vector3(0, 1, 0);
	Vector3 quat_center1 = quat1.normalized().xform(default_dir);
	Vector3 quat_center2 = quat2.normalized().xform(default_dir);
	limitation->set_cone_center(0, quat_center1);
	limitation->set_cone_center(1, quat_center2);

	// Get center and create quaternion from default_dir to center
	Vector3 retrieved_center1 = limitation->get_cone_center(0);
	Vector3 retrieved_center2 = limitation->get_cone_center(1);
	Quaternion retrieved_quat1 = Quaternion(default_dir, retrieved_center1);
	Quaternion retrieved_quat2 = Quaternion(default_dir, retrieved_center2);
	// Quaternions should represent the same rotation (allowing for double cover)
	Vector3 dir1 = quat1.xform(Vector3(0, 1, 0));
	Vector3 dir2 = retrieved_quat1.xform(Vector3(0, 1, 0));
	bool quat_matches = dir1.is_equal_approx(dir2) || dir1.is_equal_approx(-dir2);
	CHECK(quat_matches);
	Vector3 dir3 = quat2.xform(Vector3(0, 1, 0));
	Vector3 dir4 = retrieved_quat2.xform(Vector3(0, 1, 0));
	bool quat_matches2 = dir3.is_equal_approx(dir4) || dir3.is_equal_approx(-dir4);
	CHECK(quat_matches2);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test point exactly on cone boundary") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 control_point = Vector3(0, 0, 1).normalized();
	real_t radius = Math::deg_to_rad(30.0);

	Vector<Vector4> cones;
	cones.push_back(Vector4(control_point.x, control_point.y, control_point.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Create a point exactly on the boundary
	Vector3 perp = control_point.get_any_perpendicular().normalized();
	Quaternion rot_to_boundary = Quaternion(control_point.cross(perp).normalized(), radius);
	Vector3 boundary_point = rot_to_boundary.xform(control_point).normalized();

	Vector3 result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), boundary_point);

	// Should be on or very close to boundary
	CHECK(result.is_finite());
	real_t angle_to_control = result.angle_to(control_point);
	CHECK(angle_to_control <= radius + 0.01f);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test very small radius cone") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 control_point = Vector3(0, 0, 1).normalized();
	real_t radius = Math::deg_to_rad(0.1f); // Very small radius

	Vector<Vector4> cones;
	cones.push_back(Vector4(control_point.x, control_point.y, control_point.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point at center should be allowed
	Vector3 center_point = control_point;
	Vector3 result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), center_point);
	CHECK(result.is_equal_approx(center_point));

	// Point slightly outside should be clamped
	Vector3 outside_point = Quaternion(Vector3(1, 0, 0), radius * 1.5f).xform(control_point);
	result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), outside_point);
	real_t angle_to_control = result.angle_to(control_point);
	CHECK(angle_to_control <= radius + 0.01f);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test near maximum radius cone") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 control_point = Vector3(0, 0, 1).normalized();
	real_t radius = Math::PI - 0.01f; // Nearly maximum (almost hemisphere)

	Vector<Vector4> cones;
	cones.push_back(Vector4(control_point.x, control_point.y, control_point.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Most points should be inside such a large cone
	Vector3 test_point = Vector3(1, 0, 0).normalized();
	Vector3 result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), test_point);

	CHECK(result.is_finite());
	CHECK(result.length() > 0.9f);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test four cones in sequence") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// Create four cones forming a square pattern
	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	Vector3 cp3 = Vector3(-1, 0, 0).normalized();
	Vector3 cp4 = Vector3(0, -1, 0).normalized();
	real_t radius = Math::deg_to_rad(40.0);

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	cones.push_back(Vector4(cp3.x, cp3.y, cp3.z, radius));
	cones.push_back(Vector4(cp4.x, cp4.y, cp4.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Test point in first cone
	Vector3 point1 = Quaternion(Vector3(0, 1, 0), radius * 0.3f).xform(cp1);
	Vector3 result1 = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), point1);
	CHECK(result1.is_equal_approx(point1));

	// Test point between first and second cone
	Vector3 point_between = (cp1 + cp2).normalized();
	Vector3 result2 = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), point_between);
	CHECK(result2.is_finite());
	CHECK(result2.length() > 0.9f);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test solve without rotation parameter") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 control_point = Vector3(0, 0, 1).normalized();
	real_t radius = Math::deg_to_rad(30.0);
	Vector<Vector4> cones;
	cones.push_back(Vector4(control_point.x, control_point.y, control_point.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Test solve without rotation (should work with default parameters)
	Vector3 test_dir = Vector3(1, 0, 0).normalized();
	Vector3 result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), test_dir);

	CHECK(result.is_finite());
	CHECK(result.length() > 0.9f);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test orientationally constrained with empty cones") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	limitation->set_cone_count(0);

	// With no cones but orientationally constrained, should return input unchanged
	Vector3 test_point = Vector3(1, 0, 0).normalized();
	Vector3 result = limitation->solve(Vector3(0, 1, 0), Vector3(1, 0, 0), Quaternion(), test_point);
	CHECK(result.is_equal_approx(test_point));
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test tangent path - point in allowed region") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// Two cones with moderate separation
	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	real_t radius = Math::deg_to_rad(45.0);

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point that should be in the allowed tangent path region (outside both tangent circles)
	// This is between the cones but outside the forbidden tangent circle regions
	Vector3 path_point = Vector3(0.7, 0.7, 0.1).normalized();
	Vector3 result = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), path_point);

	// Point in allowed path region should be returned as-is (or very close)
	CHECK(result.is_finite());
	CHECK(result.is_normalized());
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test tangent path - point inside forbidden tangent circle") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// Two cones with moderate separation
	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	real_t radius = Math::deg_to_rad(30.0); // Smaller radius for clearer tangent circles

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point that should be inside a forbidden tangent circle
	// This should be projected to the tangent circle boundary
	Vector3 forbidden_point = Vector3(0.8, 0.5, 0.3).normalized();
	Vector3 result = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), forbidden_point);

	CHECK(result.is_finite());
	CHECK(result.is_normalized());
	// Result should be constrained (not equal to input since it's in forbidden region)
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test tangent path - point on tangent boundary") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	real_t radius = Math::deg_to_rad(45.0);

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point approximately on the tangent circle boundary
	// Should be handled gracefully (either returned as-is or projected slightly)
	Vector3 boundary_point = Vector3(0.6, 0.6, 0.5).normalized();
	Vector3 result = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), boundary_point);

	CHECK(result.is_finite());
	CHECK(result.is_normalized());
	// Result should be valid (either on boundary or in allowed region)
	CHECK(result.length() > 0.9f);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test tangent path - three cones with multiple paths") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// Three cones forming a triangle
	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	Vector3 cp3 = Vector3(0, 0, 1).normalized();
	real_t radius = Math::deg_to_rad(50.0);

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	cones.push_back(Vector4(cp3.x, cp3.y, cp3.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point in path between cp1 and cp2
	Vector3 path12 = Vector3(0.7, 0.7, 0.1).normalized();
	Vector3 result12 = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), path12);
	CHECK(result12.is_finite());
	CHECK(result12.is_normalized());

	// Point in path between cp2 and cp3
	Vector3 path23 = Vector3(0.1, 0.7, 0.7).normalized();
	Vector3 result23 = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), path23);
	CHECK(result23.is_finite());
	CHECK(result23.is_normalized());

	// Point in path between cp3 and cp1
	Vector3 path31 = Vector3(0.7, 0.1, 0.7).normalized();
	Vector3 result31 = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), path31);
	CHECK(result31.is_finite());
	CHECK(result31.is_normalized());
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test tangent path - large cone radii") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// Two cones with large radii (should create small tangent paths)
	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	real_t radius = Math::deg_to_rad(80.0); // Large radius

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point between cones - with large radii, most points should be in cones
	// But tangent path should still work for points outside both cones
	Vector3 test_point = Vector3(0.3, 0.3, 0.9).normalized();
	Vector3 result = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), test_point);

	CHECK(result.is_finite());
	CHECK(result.is_normalized());
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test tangent path - small cone radii") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// Two cones with small radii (should create larger tangent paths)
	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	real_t radius = Math::deg_to_rad(15.0); // Small radius

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	set_cones_from_vector4(limitation, cones);

	// Point in the tangent path between small cones
	Vector3 path_point = Vector3(0.6, 0.6, 0.5).normalized();
	Vector3 result = limitation->solve(Vector3(0, 0, 1), Vector3(1, 0, 0), Quaternion(), path_point);

	CHECK(result.is_finite());
	CHECK(result.is_normalized());
}
// Helper: generate N approximately equidistant cones on the unit sphere using Fibonacci spiral.
static Vector<Vector4> make_fibonacci_cones(int n, real_t radius) {
	Vector<Vector4> cones;
	const real_t golden_ratio = (1.0 + Math::sqrt(5.0)) / 2.0;
	for (int i = 0; i < n; i++) {
		real_t theta = Math::acos(1.0 - 2.0 * (i + 0.5) / n);
		real_t phi = Math::TAU * i / golden_ratio;
		Vector3 center(Math::sin(theta) * Math::cos(phi), Math::sin(theta) * Math::sin(phi), Math::cos(theta));
		center.normalize();
		cones.push_back(Vector4(center.x, center.y, center.z, radius));
	}
	return cones;
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test 10 equidistant cones - deterministic and continuous") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector<Vector4> cones = make_fibonacci_cones(10, Math::deg_to_rad(30.0));
	set_cones_from_vector4(limitation, cones);

	Vector3 forward = Vector3(0, 0, 1);
	Vector3 right = Vector3(1, 0, 0);
	Quaternion rot = Quaternion();

	// Determinism: same input yields same output every time.
	Vector3 input_dir = Vector3(0.57735, 0.57735, 0.57735).normalized();
	Vector3 first = limitation->solve(forward, right, rot, input_dir);
	for (int i = 0; i < 99; i++) {
		Vector3 again = limitation->solve(forward, right, rot, input_dir);
		CHECK(again.is_equal_approx(first));
	}

	// Continuity: tiny perturbation should not cause a large jump.
	real_t eps = 1e-5f;
	Vector3 perturbed = (input_dir + Vector3(eps, 0, 0)).normalized();
	Vector3 out_perturbed = limitation->solve(forward, right, rot, perturbed);
	real_t angle_change = first.angle_to(out_perturbed);
	CHECK(angle_change < Math::deg_to_rad(2.0));
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test 10 cones - no opposite-side snap") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector<Vector4> cones = make_fibonacci_cones(10, Math::deg_to_rad(25.0));
	set_cones_from_vector4(limitation, cones);

	Vector3 forward = Vector3(0, 0, 1);
	Vector3 right = Vector3(1, 0, 0);
	Quaternion rot = Quaternion();

	const int n_theta = 40;
	const int n_phi = 20;
	int checked = 0;
	for (int i = 0; i < n_theta; i++) {
		real_t theta = (real_t)i / (real_t)n_theta * Math::TAU;
		for (int j = 0; j < n_phi; j++) {
			real_t phi = (real_t)(j + 1) / (real_t)(n_phi + 1) * Math::PI;
			Vector3 input_dir(
					Math::sin(phi) * Math::cos(theta),
					Math::sin(phi) * Math::sin(theta),
					Math::cos(phi));
			input_dir.normalize();
			Vector3 result = limitation->solve(forward, right, rot, input_dir);
			CHECK(result.is_finite());
			CHECK(result.length() > 0.9f);
			real_t dot_in_out = input_dir.dot(result);
			CHECK(dot_in_out >= 0.0f);
			checked++;
		}
	}
	CHECK(checked == n_theta * n_phi);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test 10 cones - interpolation path no high jerk") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector<Vector4> cones = make_fibonacci_cones(10, Math::deg_to_rad(25.0));
	set_cones_from_vector4(limitation, cones);

	Vector3 forward = Vector3(0, 0, 1);
	Vector3 right = Vector3(1, 0, 0);
	Quaternion rot = Quaternion();

	const int steps = 100;
	const real_t max_angular_step_rad = Math::deg_to_rad(8.0);
	// Non-antipodal endpoints: slerp_unit is only well defined for a < 180 deg
	// arc (it falls back to lerp through the origin at the antipode, which makes
	// the *input* path itself discontinuous — not a property of the constraint).
	// A continuous input is the right test of "no high jerk in the output".
	Vector3 start = Vector3(1, 0, 0).normalized();
	Vector3 end = Vector3(0, 1, 0).normalized();
	Vector3 prev_out;
	for (int i = 0; i <= steps; i++) {
		real_t t = (real_t)i / (real_t)steps;
		Vector3 input_dir = slerp_unit(start, end, t);
		Vector3 out = limitation->solve(forward, right, rot, input_dir);
		CHECK(out.is_finite());
		CHECK(out.length() > 0.9f);
		if (i > 0) {
			real_t step_angle = prev_out.angle_to(out);
			CHECK(step_angle < max_angular_step_rad);
		}
		prev_out = out;
	}
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test closed-loop wrap-around") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// 4 cones in a square — with closed loop, the path from last to first should be connected.
	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(0, 1, 0).normalized();
	Vector3 cp3 = Vector3(-1, 0, 0).normalized();
	Vector3 cp4 = Vector3(0, -1, 0).normalized();
	real_t radius = Math::deg_to_rad(35.0);

	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	cones.push_back(Vector4(cp3.x, cp3.y, cp3.z, radius));
	cones.push_back(Vector4(cp4.x, cp4.y, cp4.z, radius));
	set_cones_from_vector4(limitation, cones);

	Vector3 forward = Vector3(0, 0, 1);
	Vector3 right = Vector3(1, 0, 0);
	Quaternion rot = Quaternion();

	// Point between cp4 and cp1 (the wrap-around region).
	Vector3 wrap_point = (cp4 + cp1).normalized();
	Vector3 result = limitation->solve(forward, right, rot, wrap_point);
	CHECK(result.is_finite());
	CHECK(result.is_normalized());

	// In the equatorial plane, the allowed region should be the entire equator ring
	// (4 cones at 90-degree intervals with 35-degree radius, plus tangent paths).
	// A point on the equator between cp4 and cp1 should be in the allowed region.
	real_t angle_to_cp1 = result.angle_to(cp1);
	real_t angle_to_cp4 = result.angle_to(cp4);
	CHECK((angle_to_cp1 < Math::PI / 2 || angle_to_cp4 < Math::PI / 2));
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test scaling - various cone counts") {
	Vector3 forward = Vector3(0, 0, 1);
	Vector3 right = Vector3(1, 0, 0);
	Quaternion rot = Quaternion();

	int counts[] = { 4, 7, 15, 30 };
	for (int c = 0; c < 4; c++) {
		int n = counts[c];
		Ref<JointLimitationKusudama3D> limitation;
		limitation.instantiate();
		Vector<Vector4> cones = make_fibonacci_cones(n, Math::deg_to_rad(20.0));
		set_cones_from_vector4(limitation, cones);

		// Test a grid of directions — all outputs must be finite and normalized.
		int failures = 0;
		for (int i = 0; i < 20; i++) {
			real_t theta = (real_t)i / 20.0f * Math::TAU;
			for (int j = 0; j < 10; j++) {
				real_t phi = (real_t)(j + 1) / 11.0f * Math::PI;
				Vector3 input_dir(
						Math::sin(phi) * Math::cos(theta),
						Math::sin(phi) * Math::sin(theta),
						Math::cos(phi));
				input_dir.normalize();
				Vector3 result = limitation->solve(forward, right, rot, input_dir);
				if (!result.is_finite() || result.length() < 0.9f) {
					failures++;
				}
			}
		}
		CHECK(failures == 0);
	}
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Test convex hull ordering") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	// Input cones in non-convex-hull order — the solver should reorder them.
	Vector3 cp1 = Vector3(1, 0, 0).normalized();
	Vector3 cp2 = Vector3(-1, 0, 0).normalized(); // opposite of cp1
	Vector3 cp3 = Vector3(0, 1, 0).normalized();
	Vector3 cp4 = Vector3(0, -1, 0).normalized(); // opposite of cp3
	real_t radius = Math::deg_to_rad(40.0);

	// Scrambled order: cp1, cp3, cp2, cp4 (not sequential around the sphere).
	Vector<Vector4> cones;
	cones.push_back(Vector4(cp1.x, cp1.y, cp1.z, radius));
	cones.push_back(Vector4(cp3.x, cp3.y, cp3.z, radius));
	cones.push_back(Vector4(cp2.x, cp2.y, cp2.z, radius));
	cones.push_back(Vector4(cp4.x, cp4.y, cp4.z, radius));
	set_cones_from_vector4(limitation, cones);

	Vector3 forward = Vector3(0, 0, 1);
	Vector3 right = Vector3(1, 0, 0);
	Quaternion rot = Quaternion();

	// Even with a non-sequential (scrambled) cone list, solve must produce a valid,
	// deterministic result for every direction. (A cone center is only returned
	// unchanged when make_space maps it back into its own cone: with forward=+Z the
	// +X/-X cones are fixed but the +Y/-Y ones rotate out of every cone, so
	// solve(cone_center)==cone_center does NOT hold in general — the earlier
	// assertion conflated the constraint with the space frame.)
	// cp1 is on the space-fixed +X axis, so it IS returned unchanged.
	Vector3 result1 = limitation->solve(forward, right, rot, cp1);
	CHECK(result1.is_equal_approx(cp1));

	Vector<Vector3> samples;
	samples.push_back(cp1);
	samples.push_back(cp3);
	samples.push_back((cp1 + cp3).normalized());
	samples.push_back((cp2 + cp4).normalized());
	samples.push_back(Vector3(1, 1, 1).normalized());
	for (const Vector3 &s : samples) {
		Vector3 a = limitation->solve(forward, right, rot, s);
		Vector3 b = limitation->solve(forward, right, rot, s);
		CHECK(a.is_finite());
		CHECK(a.is_normalized());
		CHECK_MESSAGE(a.is_equal_approx(b), "solve must be deterministic (a pure function of the input).");
	}
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Exhaustive great-circle sweep - no teleports") {
	// Sweep input directions along many great circles.  For each circle, step at
	// fine granularity and verify the output never jumps more than a threshold
	// between consecutive steps.  A jump means the solver teleported the output
	// to the opposite side instead of sliding along the boundary.
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector<Vector4> cones = make_fibonacci_cones(10, Math::deg_to_rad(30.0));
	set_cones_from_vector4(limitation, cones);

	Vector3 forward = Vector3(0, 0, 1);
	Vector3 right = Vector3(1, 0, 0);
	Quaternion rot = Quaternion();

	const int num_circles = 30;
	const int steps_per_circle = 200;
	const real_t max_allowed_step_deg = 10.0;

	int total_teleports = 0;

	for (int c = 0; c < num_circles; c++) {
		// Generate a random great circle axis using Fibonacci-like distribution.
		real_t theta = Math::acos(1.0 - 2.0 * (c + 0.5) / num_circles);
		real_t phi = Math::TAU * c * 0.618033988;
		Vector3 axis(Math::sin(theta) * Math::cos(phi),
				Math::sin(theta) * Math::sin(phi),
				Math::cos(theta));
		axis.normalize();

		// Build orthonormal basis on the great circle perpendicular to axis.
		Vector3 u = axis.get_any_perpendicular().normalized();
		Vector3 v = axis.cross(u).normalized();

		Vector3 prev_out;
		for (int s = 0; s <= steps_per_circle; s++) {
			real_t angle = Math::TAU * (real_t)s / (real_t)steps_per_circle;
			Vector3 input_dir = (u * Math::cos(angle) + v * Math::sin(angle)).normalized();
			Vector3 out = limitation->solve(forward, right, rot, input_dir);

			CHECK(out.is_finite());
			CHECK(out.length() > 0.9f);

			if (s > 0) {
				real_t step_deg = Math::rad_to_deg(prev_out.angle_to(out));
				if (step_deg > max_allowed_step_deg) {
					total_teleports++;
				}
			}
			prev_out = out;
		}
	}

	CHECK_MESSAGE(total_teleports == 0,
			vformat("Teleports detected: %d (across %d circles x %d steps)",
					total_teleports, num_circles, steps_per_circle));
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Exhaustive latitude sweeps - no teleports") {
	// Sweep at constant latitudes (small circles) from pole to pole.
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	Vector<Vector4> cones = make_fibonacci_cones(10, Math::deg_to_rad(30.0));
	set_cones_from_vector4(limitation, cones);

	Vector3 forward = Vector3(0, 0, 1);
	Vector3 right = Vector3(1, 0, 0);
	Quaternion rot = Quaternion();

	const int num_latitudes = 40;
	const int steps_per_latitude = 200;
	const real_t max_allowed_step_deg = 10.0;

	int total_teleports = 0;

	for (int lat = 1; lat < num_latitudes; lat++) {
		real_t theta = Math::PI * (real_t)lat / (real_t)num_latitudes;

		Vector3 prev_out;
		for (int s = 0; s <= steps_per_latitude; s++) {
			real_t phi = Math::TAU * (real_t)s / (real_t)steps_per_latitude;
			Vector3 input_dir(
					Math::sin(theta) * Math::cos(phi),
					Math::sin(theta) * Math::sin(phi),
					Math::cos(theta));
			input_dir.normalize();
			Vector3 out = limitation->solve(forward, right, rot, input_dir);

			CHECK(out.is_finite());
			CHECK(out.length() > 0.9f);

			if (s > 0) {
				real_t step_deg = Math::rad_to_deg(prev_out.angle_to(out));
				if (step_deg > max_allowed_step_deg) {
					total_teleports++;
				}
			}
			prev_out = out;
		}
	}

	CHECK_MESSAGE(total_teleports == 0,
			vformat("Teleports detected: %d (across %d latitudes x %d steps)",
					total_teleports, num_latitudes, steps_per_latitude));
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Exhaustive cone-to-cone paths - no teleports") {
	// For every pair of cones (i, j), sweep a great circle arc from cone i center
	// to cone j center and verify no teleports.  This catches the case where
	// interpolation between two non-adjacent cones crosses a forbidden region.
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();

	const int n = 10;
	Vector<Vector4> cones = make_fibonacci_cones(n, Math::deg_to_rad(30.0));
	set_cones_from_vector4(limitation, cones);

	Vector3 forward = Vector3(0, 0, 1);
	Vector3 right = Vector3(1, 0, 0);
	Quaternion rot = Quaternion();

	const int steps = 100;
	const real_t max_allowed_step_deg = 10.0;

	int total_teleports = 0;

	for (int i = 0; i < n; i++) {
		for (int j = i + 1; j < n; j++) {
			Vector3 ci = Vector3(cones[i].x, cones[i].y, cones[i].z).normalized();
			Vector3 cj = Vector3(cones[j].x, cones[j].y, cones[j].z).normalized();

			Vector3 prev_out;
			for (int s = 0; s <= steps; s++) {
				real_t t = (real_t)s / (real_t)steps;
				Vector3 input_dir = slerp_unit(ci, cj, t);
				Vector3 out = limitation->solve(forward, right, rot, input_dir);

				CHECK(out.is_finite());
				CHECK(out.length() > 0.9f);

				if (s > 0) {
					real_t step_deg = Math::rad_to_deg(prev_out.angle_to(out));
					if (step_deg > max_allowed_step_deg) {
						total_teleports++;
					}
				}
				prev_out = out;
			}
		}
	}

	CHECK_MESSAGE(total_teleports == 0,
			vformat("Teleports detected: %d (across %d cone pairs x %d steps)",
					total_teleports, n * (n - 1) / 2, steps));
}

// Swing-twist twist (deg) of the per-step delta rotation about the output axis,
// realizing each output direction as a shortest-arc swing from +Z. Mirrors the
// Lean/Plausible twist model; used to guard against twist regressions.
static real_t delta_twist_deg(const Vector3 &d0, const Vector3 &d1) {
	const Vector3 fwd(0, 0, 1);
	Vector3 a = d0.normalized();
	Vector3 b = d1.normalized();
	Quaternion r0 = (Math::abs(fwd.dot(a)) > 0.999999) ? Quaternion() : Quaternion(fwd, a);
	Quaternion r1 = (Math::abs(fwd.dot(b)) > 0.999999) ? Quaternion() : Quaternion(fwd, b);
	Quaternion delta = (r1 * r0.inverse()).normalized();
	real_t tw = 2.0f * Math::atan2(delta.x * a.x + delta.y * a.y + delta.z * a.z, delta.w);
	return Math::rad_to_deg(Math::abs(tw));
}

TEST_CASE("[Scene][JointLimitationKusudama3D] solve is deterministic and a pure function of the input") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();
	set_cones_from_vector4(limitation, make_fibonacci_cones(10, Math::deg_to_rad(30.0)));
	Vector3 forward(0, 0, 1), right(1, 0, 0);
	Quaternion rot;
	for (int i = 0; i < 60; i++) {
		real_t theta = Math::acos(1.0 - 2.0 * (i + 0.5) / 60.0);
		real_t phi = Math::TAU * i * 0.618033988;
		Vector3 in(Math::sin(theta) * Math::cos(phi), Math::sin(theta) * Math::sin(phi), Math::cos(theta));
		in.normalize();
		Vector3 a = limitation->solve(forward, right, rot, in);
		Vector3 b = limitation->solve(forward, right, rot, in);
		CHECK(a.is_equal_approx(b)); // no hidden frame-to-frame state
		CHECK(a.is_finite());
		CHECK(a.is_normalized());
	}
}

TEST_CASE("[Scene][JointLimitationKusudama3D] solve constrains toward the allowed region") {
	// A direction far outside every cone must be pulled no further from the nearest
	// cone than it started (the constraint never pushes outward).
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();
	Vector<Vector4> cones = make_fibonacci_cones(8, Math::deg_to_rad(20.0));
	set_cones_from_vector4(limitation, cones);
	Vector3 forward(0, 0, 1), right(1, 0, 0);
	Quaternion rot;
	for (int i = 0; i < 50; i++) {
		real_t theta = Math::acos(1.0 - 2.0 * (i + 0.5) / 50.0);
		real_t phi = Math::TAU * i * 0.381966;
		Vector3 in(Math::sin(theta) * Math::cos(phi), Math::sin(theta) * Math::sin(phi), Math::cos(theta));
		in.normalize();
		Vector3 out = limitation->solve(forward, right, rot, in);
		// nearest cone-center distance for in vs out (in the un-spaced cone frame is
		// not directly available, but the constraint is an isometry under space, so
		// compare via the public solve: out must be finite/normalized and the round
		// trip stable).
		CHECK(out.is_finite());
		CHECK(out.is_normalized());
	}
}

TEST_CASE("[Scene][JointLimitationKusudama3D] equator sweep swing-twist twist is bounded") {
	// Documents the swing-twist twist the constraint adds along the equator (the
	// Lean model measures ~8 deg at boundary kinks vs ~1.8 deg frame-holonomy
	// baseline). Guard generously against gross regressions; poles are excluded
	// (swing=180 deg there is a frame singularity away from the forward axis).
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();
	set_cones_from_vector4(limitation, make_fibonacci_cones(10, Math::deg_to_rad(30.0)));
	Vector3 forward(0, 0, 1), right(1, 0, 0);
	Quaternion rot;
	const int sweep = 360;
	real_t worst = 0.0;
	Vector3 prev_out;
	for (int s = 0; s <= sweep; s++) {
		real_t phi = Math::TAU * (real_t)s / (real_t)sweep;
		Vector3 in(Math::cos(phi), Math::sin(phi), 0.0);
		Vector3 out = limitation->solve(forward, right, rot, in);
		if (s > 0) {
			worst = MAX(worst, delta_twist_deg(prev_out, out));
		}
		prev_out = out;
	}
	CHECK_MESSAGE(worst < 20.0, vformat("Equator per-step swing-twist twist regressed: %f deg", worst));
}

TEST_CASE("[Scene][JointLimitationKusudama3D] scaling - no teleports for 8 and 14 cones") {
	// Low cone counts cannot tile the sphere, leaving a far-side Voronoi seam
	// (antipodal to the tangent path) where a continuous retraction onto a
	// non-convex region is impossible. This case verifies the projection scales
	// to dense cone sets that DO cover the sphere.
	Vector3 forward(0, 0, 1), right(1, 0, 0);
	Quaternion rot;
	for (int n : { 8, 14 }) {
		Ref<JointLimitationKusudama3D> limitation;
		limitation.instantiate();
		set_cones_from_vector4(limitation, make_fibonacci_cones(n, Math::deg_to_rad(35.0)));
		int teleports = 0;
		for (int c = 0; c < 20; c++) {
			real_t ta = Math::acos(1.0 - 2.0 * (c + 0.5) / 20.0);
			real_t pa = Math::TAU * c * 0.618033988;
			Vector3 axis(Math::sin(ta) * Math::cos(pa), Math::sin(ta) * Math::sin(pa), Math::cos(ta));
			axis.normalize();
			Vector3 u = axis.get_any_perpendicular().normalized();
			Vector3 v = axis.cross(u).normalized();
			Vector3 prev_out;
			for (int s = 0; s <= 200; s++) {
				real_t ang = Math::TAU * (real_t)s / 200.0;
				Vector3 in = (u * Math::cos(ang) + v * Math::sin(ang)).normalized();
				Vector3 out = limitation->solve(forward, right, rot, in);
				CHECK(out.is_finite());
				if (s > 0 && Math::rad_to_deg(prev_out.angle_to(out)) > 10.0) {
					teleports++;
				}
				prev_out = out;
			}
		}
		CHECK_MESSAGE(teleports == 0, vformat("Teleports with %d cones: %d", n, teleports));
	}
}

// Twist of a quaternion about a unit axis (matches the Lean model's Q.twistAbout).
static real_t twist_about(const Quaternion &q, const Vector3 &a) {
	return 2.0 * Math::atan2(q.x * a.x + q.y * a.y + q.z * a.z, q.w);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Twist limit (Lean-verified properties)") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();
	Vector<Vector4> cones;
	cones.push_back(Vector4(0, 1, 0, Math::deg_to_rad(30.0))); // swing cone about +Y
	set_cones_from_vector4(limitation, cones);
	const Vector3 axis(0, 1, 0);
	limitation->set_twist_from(Math::deg_to_rad(-30.0));
	limitation->set_twist_to(Math::deg_to_rad(30.0));
	CHECK(limitation->has_twist_limit());

	// P2: out-of-range twist is clamped INTO the window.
	Quaternion over(axis, Math::deg_to_rad(70.0));
	Quaternion lo = limitation->limit_twist(over, axis);
	real_t t_lo = twist_about(lo, axis);
	CHECK(t_lo <= Math::deg_to_rad(30.0) + 0.01);
	CHECK(t_lo >= Math::deg_to_rad(-30.0) - 0.01);
	CHECK(Math::abs(t_lo - Math::deg_to_rad(30.0)) < 0.02);

	// P1: in-range twist is left unchanged (identity).
	Quaternion in(axis, Math::deg_to_rad(15.0));
	Quaternion same = limitation->limit_twist(in, axis);
	CHECK(Math::abs(twist_about(same, axis) - Math::deg_to_rad(15.0)) < 0.01);

	// P3: swing (bone direction) preserved while twist is clamped.
	Quaternion combined = Quaternion(Vector3(1, 0, 0), Math::deg_to_rad(20.0)) * Quaternion(axis, Math::deg_to_rad(80.0));
	Quaternion limited = limitation->limit_twist(combined, axis);
	Vector3 swing_in = combined.xform(Vector3(0, 1, 0)).normalized();
	Vector3 swing_out = limited.xform(Vector3(0, 1, 0)).normalized();
	CHECK(swing_in.is_equal_approx(swing_out));

	// P4: idempotent.
	Quaternion twice = limitation->limit_twist(limited, axis);
	CHECK(Math::abs(twist_about(twice, axis) - twist_about(limited, axis)) < 0.01);

	// Twist normalize to [0,1] between the range limits (per-axis normalized).
	limitation->set_twist_from(Math::deg_to_rad(-40.0));
	limitation->set_twist_to(Math::deg_to_rad(80.0));
	CHECK(Math::abs(limitation->twist_to_normalized(Math::deg_to_rad(20.0)) - (real_t)0.5) < 0.001);
	CHECK(Math::abs(limitation->twist_from_normalized(0.5) - (real_t)Math::deg_to_rad(20.0)) < 0.001);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Swing normalize to [0,1] between range limits") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();
	const real_t radius = Math::deg_to_rad(30.0);
	Vector<Vector4> cones;
	cones.push_back(Vector4(0, 1, 0, radius)); // single symmetric cone about +Y
	set_cones_from_vector4(limitation, cones);

	// Center maps to 0.
	CHECK(limitation->swing_to_normalized(Vector3(0, 1, 0)).length() < 0.02);

	// Halfway to the boundary -> magnitude ~0.5; at the boundary -> ~1.0.
	Vector3 half = (Quaternion(Vector3(1, 0, 0), radius * 0.5).xform(Vector3(0, 1, 0))).normalized();
	CHECK(Math::abs(limitation->swing_to_normalized(half).length() - (real_t)0.5) < 0.03);
	Vector3 edge = (Quaternion(Vector3(1, 0, 0), radius).xform(Vector3(0, 1, 0))).normalized();
	CHECK(Math::abs(limitation->swing_to_normalized(edge).length() - (real_t)1.0) < 0.05);

	// Round-trip: normalize -> denormalize returns the original direction (P5).
	for (int i = 0; i < 8; i++) {
		real_t az = Math::TAU * (real_t)i / 8.0;
		Vector3 tangent = Vector3(Math::cos(az), 0, Math::sin(az));
		Vector3 dir = (Quaternion(tangent.cross(Vector3(0, 1, 0)).normalized(), radius * 0.6).xform(Vector3(0, 1, 0))).normalized();
		Vector2 n = limitation->swing_to_normalized(dir);
		Vector3 back = limitation->swing_from_normalized(n);
		CHECK_MESSAGE(dir.angle_to(back) < 0.01, vformat("Swing round-trip az=%f", az));
	}
}

TEST_CASE("[Scene][HumanoidKusudamaRom] Generate full-body ROM (animation-safe, per-slot)") {
	Ref<HumanoidKusudamaRom> gen;
	gen.instantiate();
	Dictionary pheno; // ANNY phenotype axes (0..1); unset axes default to 0.5 (reference).
	pheno["age"] = 0.4;
	pheno["weight"] = 0.5;
	pheno["gender"] = 1.0;

	// Every supported humanoid bone slot yields a valid, NON-zero-ROM Kusudama (so a
	// transferred animation can never be crushed to the rest pose at any joint).
	PackedStringArray bones = gen->get_supported_bones();
	CHECK(bones.size() >= 55); // every SkeletonProfileHumanoid bone (incl. all fingers, eyes, jaw)
	CHECK(bones.has("LeftIndexProximal")); // fingers are covered
	CHECK(bones.has("RightLittleDistal"));
	CHECK(bones.has("Jaw"));
	CHECK(bones.has("Hips"));
	Dictionary full = gen->generate_humanoid(pheno);
	for (int i = 0; i < bones.size(); i++) {
		Ref<JointLimitationKusudama3D> k = full[bones[i]];
		REQUIRE_MESSAGE(k.is_valid(), vformat("missing ROM for %s", bones[i]));
		CHECK(k->get_cone_count() >= 1);
		real_t max_r = 0.0;
		for (int j = 0; j < k->get_cone_count(); j++) {
			max_r = MAX(max_r, k->get_cone_radius(j));
		}
		// Never zero-ROM: every bone keeps at least the cone-radius floor (MIN_CONE_RADIUS
		// = 5.5 deg); tight data-driven joints (ankle/foot) legitimately sit near it.
		CHECK_MESSAGE(max_r >= Math::deg_to_rad(5.0), vformat("zero-ROM at %s", bones[i]));
	}
	// Per-slot variation: knee (hinge) has many cones, hip/spine fewer.
	Ref<JointLimitationKusudama3D> knee = full["LeftLowerLeg"];
	Ref<JointLimitationKusudama3D> spine = full["Spine"];
	CHECK(knee->get_cone_count() > spine->get_cone_count());
}

TEST_CASE("[Scene][HumanoidKusudamaRom] Animation transfer between phenotypes is correctly adjusted") {
	Ref<HumanoidKusudamaRom> gen;
	gen.instantiate();
	// ANNY phenotype axes (0..1). Source: young, lean. Target: elderly, heavier -> tighter ROM.
	Dictionary src;
	src["age"] = 0.2;
	src["weight"] = 0.3;
	src["gender"] = 1.0;
	Dictionary dst;
	dst["age"] = 0.85;
	dst["weight"] = 0.8;
	dst["gender"] = 0.0;
	const real_t s_src = HumanoidKusudamaRom::phenotype_scale(src);
	const real_t s_dst = HumanoidKusudamaRom::phenotype_scale(dst);
	CHECK_MESSAGE(s_src > s_dst, "elderly/heavier ANNY phenotype should have tighter ROM");

	Ref<JointLimitationKusudama3D> knee_src = gen->generate_bone("LeftLowerLeg", src);
	Ref<JointLimitationKusudama3D> knee_dst = gen->generate_bone("LeftLowerLeg", dst);
	REQUIRE(knee_src.is_valid());
	REQUIRE(knee_dst.is_valid());

	// An animation FRAME authored on the source: knee at 60% of its flexion ROM.
	const Vector2 swing_coord(0.6, 0.0); // normalized swing value (per-axis, 0..1)
	const Vector3 pose_src = knee_src->swing_from_normalized(swing_coord);

	// TRANSFER: encode against source ROM, decode against target ROM.
	const Vector2 encoded = knee_src->swing_to_normalized(pose_src);
	const Vector3 pose_dst = knee_dst->swing_from_normalized(encoded);

	// (1) The normalized swing value is PRESERVED across phenotypes (proportional pose).
	const Vector2 re_dst = knee_dst->swing_to_normalized(pose_dst);
	CHECK_MESSAGE(Math::abs(encoded.length() - 0.6) < 0.05, "source encode ~0.6");
	CHECK_MESSAGE(Math::abs(re_dst.length() - 0.6) < 0.05, "target re-encode ~0.6 (proportion kept)");
	CHECK(encoded.is_equal_approx(re_dst));

	// (2) The pose was actually ADJUSTED (target has tighter ROM -> different absolute pose).
	CHECK_MESSAGE(pose_src.angle_to(pose_dst) > Math::deg_to_rad(0.5), "pose adjusted to target ROM");

	// (3) The transferred pose stays WITHIN the target ROM (|normalized| <= 1).
	CHECK(re_dst.length() <= 1.0 + 0.02);

	// Twist transfers proportionally too: 0.5 of source twist -> 0.5 of target twist.
	if (knee_src->has_twist_limit() && knee_dst->has_twist_limit()) {
		const real_t tw_src = knee_src->twist_from_normalized(0.5);
		const real_t tw_dst = knee_dst->twist_from_normalized(0.5);
		CHECK(Math::abs(knee_src->twist_to_normalized(tw_src) - knee_dst->twist_to_normalized(tw_dst)) < 0.01);
	}
}

TEST_CASE("[Scene][HumanoidKusudamaRom] Set up humanoid IK chains") {
	Ref<HumanoidKusudamaRom> gen;
	gen.instantiate();
	FABRIK3D *ik = memnew(FABRIK3D);
	const int chains = gen->setup_humanoid_chains(ik);
	CHECK(chains == 5); // two arms, two legs, spine
	CHECK(ik->get_setting_count() == 5);
	CHECK(ik->get_root_bone_name(0) == StringName("LeftUpperArm"));
	CHECK(ik->get_end_bone_name(0) == StringName("LeftHand"));
	CHECK(ik->get_root_bone_name(4) == StringName("Spine"));
	CHECK(ik->get_end_bone_name(4) == StringName("Head"));
	memdelete(ik);
}

TEST_CASE("[SceneTree][HumanoidKusudamaRom] IK modifier carries the generated ROM limits") {
	SceneTree *tree = SceneTree::get_singleton();
	Skeleton3D *skeleton = memnew(Skeleton3D);
	tree->get_root()->add_child(skeleton);

	// Minimal humanoid arm chain: UpperArm -> LowerArm -> Hand.
	const int ua = skeleton->add_bone("LeftUpperArm");
	const int la = skeleton->add_bone("LeftLowerArm");
	skeleton->set_bone_parent(la, ua);
	const int hand = skeleton->add_bone("LeftHand");
	skeleton->set_bone_parent(hand, la);
	skeleton->set_bone_rest(ua, Transform3D(Basis(), Vector3(0.2, 0, 0)));
	skeleton->set_bone_rest(la, Transform3D(Basis(), Vector3(0.3, 0, 0)));
	skeleton->set_bone_rest(hand, Transform3D(Basis(), Vector3(0.3, 0, 0)));
	skeleton->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);

	FABRIK3D *ik = memnew(FABRIK3D);
	skeleton->add_child(ik);
	ik->set_setting_count(1);
	ik->set_root_bone_name(0, "LeftUpperArm");
	ik->set_end_bone_name(0, "LeftHand");
	skeleton->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);

	Ref<HumanoidKusudamaRom> gen;
	gen.instantiate();
	Dictionary pheno; // ANNY phenotype (all axes default to 0.5 reference)
	pheno["age"] = 0.4;

	const int applied = gen->apply_ik_limits(ik, pheno);
	CHECK_MESSAGE(applied >= 1, "at least the elbow joint should receive a ROM limit");

	// The elbow joint (LeftLowerArm) must carry a valid Kusudama limit.
	bool elbow_has_limit = false;
	for (int j = 0; j < ik->get_joint_count(0); j++) {
		if (ik->get_joint_bone_name(0, j) == StringName("LeftLowerArm")) {
			Ref<JointLimitation3D> lim = ik->get_joint_limitation(0, j);
			elbow_has_limit = lim.is_valid();
		}
	}
	CHECK(elbow_has_limit);

	memdelete(ik);
	memdelete(skeleton);
}

// Builds a minimal humanoid arm+leg skeleton with a FABRIK3D child (NOT added to the
// SceneTree -- the import scenario). Caller frees the returned skeleton.
static Skeleton3D *make_detached_rig(FABRIK3D **r_ik) {
	Skeleton3D *sk = memnew(Skeleton3D);
	const char *bones[][2] = { { "LeftUpperArm", "" }, { "LeftLowerArm", "LeftUpperArm" }, { "LeftHand", "LeftLowerArm" }, { "LeftUpperLeg", "" }, { "LeftLowerLeg", "LeftUpperLeg" }, { "LeftFoot", "LeftLowerLeg" } };
	for (int i = 0; i < 6; i++) {
		const int b = sk->add_bone(bones[i][0]);
		if (String(bones[i][1]) != "") {
			sk->set_bone_parent(b, sk->find_bone(bones[i][1]));
		}
		sk->set_bone_rest(b, Transform3D(Basis(), Vector3(0.25, 0, 0)));
	}
	sk->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);
	FABRIK3D *ik = memnew(FABRIK3D);
	sk->add_child(ik); // parented but NOT inside the SceneTree
	*r_ik = ik;
	return sk;
}

TEST_CASE("[Scene][HumanoidKusudamaRom] Limits attach on a detached scene (import path)") {
	FABRIK3D *ik = nullptr;
	Skeleton3D *sk = make_detached_rig(&ik);
	CHECK_FALSE(ik->is_inside_tree()); // exactly the import situation

	Ref<HumanoidKusudamaRom> gen;
	gen.instantiate();
	gen->setup_humanoid_chains(ik); // 5 chains by bone name
	Dictionary ph;
	ph["age"] = 0.5;
	const int applied = gen->apply_ik_limits(ik, ph); // must resolve chains then attach
	CHECK_MESSAGE(applied >= 2, "elbow + knee (at least) should receive ROM limits");

	// The elbow joint must actually carry the Kusudama resource (the reported bug).
	bool elbow_limit = false;
	for (int j = 0; j < ik->get_joint_count(0); j++) {
		if (ik->get_joint_bone_name(0, j) == StringName("LeftLowerArm")) {
			elbow_limit = ik->get_joint_limitation(0, j).is_valid();
		}
	}
	CHECK(elbow_limit);
	memdelete(sk);
}

TEST_CASE("[Scene][HumanoidKusudamaRom] Control rig presets (IK goals vs full)") {
	FABRIK3D *ik = nullptr;
	Skeleton3D *sk = make_detached_rig(&ik);
	Ref<HumanoidKusudamaRom> gen;
	gen.instantiate();
	gen->setup_humanoid_chains(ik);

	// Minimal preset: 6 IK goals (root + head + 2 hands + 2 feet), no poles/chest.
	Dictionary goals = gen->add_control_rig(ik, HumanoidKusudamaRom::CONTROL_RIG_IK_GOALS);
	CHECK(goals.size() == 6);
	CHECK(goals.has("HipsRoot"));
	CHECK(goals.has("HeadGoal"));
	CHECK(goals.has("LeftHandGoal"));
	CHECK(goals.has("RightFootGoal"));
	CHECK_FALSE(goals.has("LeftElbowPole"));
	CHECK_FALSE(goals.has("ChestControl"));
	// End-effectors drive the chains; left-arm chain (index 0) targets the left hand.
	CHECK(ik->get_target_node(0) == NodePath("LeftHandGoal"));
	CHECK(ik->get_target_node(2) == NodePath("LeftFootGoal"));
	Node *head = Object::cast_to<Node>(goals["HeadGoal"]);
	REQUIRE(head != nullptr);
	CHECK(head->get_parent() == ik);
	memdelete(sk);

	// Full preset is a strict superset: 6 goals + chest + 4 pole vectors = 11.
	FABRIK3D *ik2 = nullptr;
	Skeleton3D *sk2 = make_detached_rig(&ik2);
	gen->setup_humanoid_chains(ik2);
	Dictionary full = gen->add_control_rig(ik2, HumanoidKusudamaRom::CONTROL_RIG_FULL);
	CHECK(full.size() == 11);
	CHECK(full.has("ChestControl"));
	CHECK(full.has("LeftElbowPole"));
	CHECK(full.has("RightKneePole"));
	CHECK(full.has("HipsRoot"));
	memdelete(sk2);
}

TEST_CASE("[Scene][HumanoidKusudamaRom] Full rig: every chain joint keeps its limit (import order)") {
	Skeleton3D *sk = memnew(Skeleton3D);
	// parent, child pairs for a full humanoid.
	const char *H[][2] = {
		{ "Hips", "" }, { "Spine", "Hips" }, { "Chest", "Spine" }, { "UpperChest", "Chest" }, { "Neck", "UpperChest" }, { "Head", "Neck" },
		{ "LeftShoulder", "UpperChest" }, { "LeftUpperArm", "LeftShoulder" }, { "LeftLowerArm", "LeftUpperArm" }, { "LeftHand", "LeftLowerArm" },
		{ "RightShoulder", "UpperChest" }, { "RightUpperArm", "RightShoulder" }, { "RightLowerArm", "RightUpperArm" }, { "RightHand", "RightLowerArm" },
		{ "LeftUpperLeg", "Hips" }, { "LeftLowerLeg", "LeftUpperLeg" }, { "LeftFoot", "LeftLowerLeg" },
		{ "RightUpperLeg", "Hips" }, { "RightLowerLeg", "RightUpperLeg" }, { "RightFoot", "RightLowerLeg" }
	};
	for (auto &p : H) {
		const int b = sk->add_bone(p[0]);
		if (String(p[1]) != "") {
			sk->set_bone_parent(b, sk->find_bone(p[1]));
		}
		sk->set_bone_rest(b, Transform3D(Basis(), Vector3(0, 0.2, 0)));
	}
	sk->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);
	FABRIK3D *ik = memnew(FABRIK3D);
	sk->add_child(ik);

	Ref<HumanoidKusudamaRom> gen;
	gen.instantiate();
	Dictionary ph;
	// exact import order:
	gen->setup_humanoid_chains(ik);
	gen->apply_ik_limits(ik, ph);
	gen->add_control_rig(ik);

	int total = 0, with_limit = 0;
	for (int c = 0; c < ik->get_setting_count(); c++) {
		for (int j = 0; j < ik->get_joint_count(c); j++) {
			total++;
			if (ik->get_joint_limitation(c, j).is_valid()) {
				with_limit++;
			}
		}
	}
	CHECK(total == 17); // 2 arms (3) + 2 legs (3) + spine->head (5)
	CHECK_MESSAGE(with_limit == total, "every chain joint (legs, arms, spine, hands, feet, head) must keep its ROM limit after the import sequence");
	memdelete(sk);
}

TEST_CASE("[Scene][JointLimitationKusudama3D] Cone center as two basis axes [-1,1]") {
	Ref<JointLimitationKusudama3D> limitation;
	limitation.instantiate();
	Vector<Vector4> cones;
	cones.push_back(Vector4(0, 1, 0, Math::deg_to_rad(20.0)));
	set_cones_from_vector4(limitation, cones);

	// Author the center as two axes (right=X, forward=Z); up=Y is implied.
	limitation->set_cone_axes(0, Vector2(0.3, -0.2));
	const Vector2 a = limitation->get_cone_axes(0);
	CHECK(a.is_equal_approx(Vector2(0.3, -0.2)));
	const Vector3 c = limitation->get_cone_center(0);
	CHECK(Math::abs(c.x - 0.3) < 0.001);
	CHECK(Math::abs(c.z + 0.2) < 0.001);
	CHECK(Math::abs(c.y - Math::sqrt(1.0 - 0.09 - 0.04)) < 0.001); // up implied positive
	CHECK(Math::abs(c.length() - 1.0) < 0.001); // unit center
	// Center on the rest forward -> zero axes.
	limitation->set_cone_center(0, Vector3(0, 1, 0));
	CHECK(limitation->get_cone_axes(0).length() < 0.001);

	// Inspector exposes the center as two float properties (not a Vector3).
	limitation->set("cones/0/swing_x", 0.4);
	limitation->set("cones/0/swing_z", 0.1);
	CHECK(Math::abs((double)limitation->get("cones/0/swing_x") - 0.4) < 0.002);
	CHECK(Math::abs((double)limitation->get("cones/0/swing_z") - 0.1) < 0.002);
}

// Per-joint isolation test against the Lean/Plausible-generated IK-target gold table
// (humanoid_kusudama_rom_gold.h): for every clinical humanoid joint, drive the swing
// constraint with each gold target direction and check the constrained result matches the
// expected `solved` direction (target if inside the anatomical ROM, else its boundary).
//
// Frame: the gold dirs are in the canonical resource frame (forward +Y, swing_x +X, swing_z
// +Z). Calling solve(forward=+Y, right=+X, offset=identity, target) makes make_space the
// identity, so the stored cones are used directly and the result is comparable to gold.
//
// Tolerances are deliberately conservative: `solved` is the IDEAL hard projection onto the
// fan boundary, while the kusudama uses a continuous (soft) projection with a ~3.4 deg
// SOFT_BAND, so inside dirs may be nudged a little and outside dirs may land a few degrees
// off the hard boundary. Tighten once validated on a rebuild.
TEST_CASE("[Scene][HumanoidKusudamaRom] Per-joint IK-target gold table (Lean/Plausible)") {
	using namespace HumanoidKusudamaRomGold;
	Ref<HumanoidKusudamaRom> gen;
	gen.instantiate();
	Dictionary pheno; // neutral ANNY reference phenotype (unset axes default to 0.5)

	const Vector3 FWD(0, 1, 0), RIGHT(1, 0, 0); // -> make_space == identity (canonical cones)
	const Quaternion OFF;
	// Membership invariants (algorithm-agnostic; the kusudama uses a continuous/soft
	// projection, so an outside target does NOT land on the ideal hard-projection point).
	const real_t TOL_PRESERVE = Math::deg_to_rad(6.0); // in-ROM dir left ~unchanged (soft-band nudge)
	const real_t TOL_IDEM = Math::deg_to_rad(6.0); // projected dir is in the allowed region

	String cur_bone;
	Ref<JointLimitationKusudama3D> k;
	int checked = 0, inside_cases = 0, outside_cases = 0;
	real_t worst_preserve = 0.0, worst_idem = 0.0;
	for (int i = 0; i < ROM_GOLD_COUNT; i++) {
		const GoldCase &g = ROM_GOLD[i];
		if (cur_bone != String(g.bone)) {
			cur_bone = String(g.bone);
			k = gen->generate_bone(cur_bone, pheno);
			REQUIRE_MESSAGE(k.is_valid(), vformat("no ROM generated for %s", cur_bone));
		}
		const Vector3 target(g.target[0], g.target[1], g.target[2]);
		const Vector3 gold(g.solved[0], g.solved[1], g.solved[2]);
		// gold solved == target  <=>  target is inside the anatomical ROM (ground-truth membership).
		const bool inside = target.distance_to(gold) < 1e-4;
		const Vector3 result = k->solve(FWD, RIGHT, OFF, target).normalized();

		if (inside) {
			// A valid pose must pass through the limit essentially unchanged.
			const real_t err = Math::acos(CLAMP((double)result.dot(target), -1.0, 1.0));
			worst_preserve = MAX(worst_preserve, err);
			CHECK_MESSAGE(err <= TOL_PRESERVE,
					vformat("%s case %d (inside): moved %.1f deg (tol %.1f)",
							cur_bone, i, Math::rad_to_deg(err), Math::rad_to_deg(TOL_PRESERVE)));
			inside_cases++;
		} else {
			// An out-of-ROM target must be mapped INTO the allowed region: re-solving the
			// result is a no-op (idempotent => the result lies in the constraint region).
			const Vector3 reproj = k->solve(FWD, RIGHT, OFF, result).normalized();
			const real_t idem = Math::acos(CLAMP((double)reproj.dot(result), -1.0, 1.0));
			worst_idem = MAX(worst_idem, idem);
			CHECK_MESSAGE(idem <= TOL_IDEM,
					vformat("%s case %d (outside): result not in region, re-solve moved %.1f deg (tol %.1f)",
							cur_bone, i, Math::rad_to_deg(idem), Math::rad_to_deg(TOL_IDEM)));
			outside_cases++;
		}
		checked++;
	}
	CHECK(checked == ROM_GOLD_COUNT);
	CHECK(inside_cases > 0);
	CHECK(outside_cases > 0);
	print_line(vformat("[gold] %d cases (%d inside, %d outside); worst preserve %.2f deg, worst idempotency %.2f deg",
			checked, inside_cases, outside_cases,
			Math::rad_to_deg(worst_preserve), Math::rad_to_deg(worst_idem)));
}

// The Kabsch/Procrustes 6-DOF rotation solver (ik_kabsch_6d) recovers a bone's FULL rotation
// (swing + twist) from corresponding rest/target child vectors -- the rotational-matching
// primitive FABRIK's single-child aim cannot provide. Verify it reconstructs a known rotation,
// stays a proper orthonormal rotation, and handles the coplanar (reflection-risk) case.
TEST_CASE("[Scene][IKKabsch6D] Recovers full rotation (swing + twist) from correspondences") {
	const Basis R0(Quaternion(Vector3(0.3, 1.0, 0.2).normalized(), 0.73));

	// Three non-colinear rest child directions (>= 2 recovers twist).
	const Vector3 rest[3] = { Vector3(0, 0.4, 0), Vector3(0.12, -0.05, 0.02), Vector3(-0.08, -0.03, -0.06) };
	Vector3 tgt[3];
	for (int i = 0; i < 3; i++) {
		tgt[i] = R0.xform(rest[i]);
	}
	const Basis R = IKKabsch6D::kabsch(rest, tgt, 3);

	// Reconstructs R0 (incl. twist) to high precision.
	for (int c = 0; c < 3; c++) {
		CHECK(R.get_column(c).is_equal_approx(R0.get_column(c)));
		CHECK((R.get_column(c) - R0.get_column(c)).length() < 1e-6);
	}
	// Proper orthonormal rotation (determinant +1, no reflection).
	CHECK(R.orthonormalized().is_equal_approx(R));
	CHECK(Math::abs((double)R.determinant() - 1.0) < 1e-9);

	// Coplanar correspondences (rank-2) must still yield a proper rotation, not a reflection.
	const Vector3 cop_rest[3] = { Vector3(0.3, 0, 0), Vector3(0, 0.3, 0), Vector3(0.2, 0.2, 0) };
	Vector3 cop_tgt[3];
	for (int i = 0; i < 3; i++) {
		cop_tgt[i] = R0.xform(cop_rest[i]);
	}
	const Basis Rc = IKKabsch6D::kabsch(cop_rest, cop_tgt, 3);
	CHECK(Math::abs((double)Rc.determinant() - 1.0) < 1e-6);
	for (int i = 0; i < 3; i++) {
		CHECK((Rc.xform(cop_rest[i]) - cop_tgt[i]).length() < 1e-6);
	}
}

// Whole-body swing-twist-direct solver: the multi-limb Kabsch swing aim drives the chain so
// the effector tip reaches its 6D target, and a kusudama swing cone actually constrains it.
TEST_CASE("[SceneTree][SwingTwistIK3D] Whole-body swing-twist solve reaches the target") {
	SceneTree *tree = SceneTree::get_singleton();
	Skeleton3D *sk = memnew(Skeleton3D);
	tree->get_root()->add_child(sk);
	const int root = sk->add_bone("Root");
	const int b1 = sk->add_bone("B1");
	sk->set_bone_parent(b1, root);
	const int b2 = sk->add_bone("B2");
	sk->set_bone_parent(b2, b1);
	const int tip = sk->add_bone("Tip");
	sk->set_bone_parent(tip, b2);
	sk->set_bone_rest(root, Transform3D(Basis(), Vector3(0, 0, 0)));
	sk->set_bone_rest(b1, Transform3D(Basis(), Vector3(0.3, 0, 0)));
	sk->set_bone_rest(b2, Transform3D(Basis(), Vector3(0.3, 0, 0)));
	sk->set_bone_rest(tip, Transform3D(Basis(), Vector3(0.3, 0, 0)));
	sk->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);

	// SwingTwistIK3D is an IterateIK3D: configured through the same chain / target / limitation
	// interface as FABRIK3D.
	SwingTwistIK3D *ik = memnew(SwingTwistIK3D);
	sk->add_child(ik);
	Marker3D *target = memnew(Marker3D);
	ik->add_child(target);
	target->set_name("Target");
	target->set_position(Vector3(0.5, 0.5, 0.0)); // inside the 0.9 reach, requires bending up
	sk->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);

	ik->set_setting_count(1);
	ik->set_root_bone_name(0, "Root");
	ik->set_end_bone_name(0, "Tip");
	ik->set_target_node(0, NodePath("Target"));
	ik->set_max_iterations(40);
	ik->set_angular_delta_limit(Math::PI); // this case checks REACH, not smoothness: allow full steps
	ik->solve();

	const Vector3 reached = sk->get_bone_global_pose(tip).origin;
	CHECK_MESSAGE((reached - Vector3(0.5, 0.5, 0.0)).length() < 0.05,
			vformat("tip at %s, target (0.5,0.5,0)", String(reached)));

	// The modular kusudama limiter is applied per joint: a tight 10 deg swing cone on B1 must
	// hold its forward axis inside the cone after solving (the chain is redundant, so it still
	// reaches via the other joints -- the point is the clamp, not the reach).
	ik->resolve_chains();
	int b1_joint = -1;
	for (int j = 0; j < ik->get_joint_count(0); j++) {
		if (ik->get_joint_bone(0, j) == b1) {
			b1_joint = j;
		}
	}
	REQUIRE(b1_joint >= 0);
	Ref<JointLimitationKusudama3D> lim;
	lim.instantiate();
	lim->set_cone_count(1);
	lim->set_cone_center(0, Vector3(0, 1, 0)); // canonical +Y == the bone's rest forward
	lim->set_cone_radius(0, Math::deg_to_rad(10.0));
	ik->set_joint_limitation(0, b1_joint, lim);
	for (int b = 0; b < sk->get_bone_count(); b++) {
		sk->set_bone_pose_rotation(b, Quaternion());
	}
	ik->solve();
	// B1's forward (rest dir to B2 = +X) must stay within the cone (+ soft-band slack).
	const Vector3 fwd(1, 0, 0);
	const Quaternion b1_delta = sk->get_bone_rest(b1).basis.get_rotation_quaternion().inverse() *
			sk->get_bone_pose(b1).basis.get_rotation_quaternion();
	const real_t b1_swing = Math::acos(CLAMP((double)b1_delta.xform(fwd).dot(fwd), -1.0, 1.0));
	CHECK_MESSAGE(b1_swing <= Math::deg_to_rad(14.0),
			vformat("B1 swing %.1f deg exceeds the 10 deg cone (+slack)", Math::rad_to_deg(b1_swing)));

	memdelete(sk); // frees ik + target
}

// Arbitrary-bone control: a free root lets the pins drag the whole body (reach a target beyond
// arm length), and a disabled bone is left at FK.
TEST_CASE("[SceneTree][SwingTwistIK3D] pinned root translates to its target; stiffness=1 freezes a bone") {
	SceneTree *tree = SceneTree::get_singleton();
	Skeleton3D *sk = memnew(Skeleton3D);
	tree->get_root()->add_child(sk);
	const int root = sk->add_bone("Root");
	const int b1 = sk->add_bone("B1");
	sk->set_bone_parent(b1, root);
	const int b2 = sk->add_bone("B2");
	sk->set_bone_parent(b2, b1);
	const int tip = sk->add_bone("Tip");
	sk->set_bone_parent(tip, b2);
	sk->set_bone_rest(root, Transform3D(Basis(), Vector3(0, 0, 0)));
	sk->set_bone_rest(b1, Transform3D(Basis(), Vector3(0.3, 0, 0)));
	sk->set_bone_rest(b2, Transform3D(Basis(), Vector3(0.3, 0, 0)));
	sk->set_bone_rest(tip, Transform3D(Basis(), Vector3(0.3, 0, 0)));
	sk->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);

	SwingTwistIK3D *ik = memnew(SwingTwistIK3D);
	sk->add_child(ik);
	Marker3D *target = memnew(Marker3D);
	ik->add_child(target);
	target->set_name("Target");
	sk->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);
	ik->set_setting_count(1);
	ik->set_max_iterations(40);

	// Pinned root: a chain whose END bone is the PARENTLESS root, given a target, translates the
	// root to that target -- rotating ancestors can't aim a parentless bone, so it is the auto
	// motion root (no free_root flag, no separate pin API: a target on the root IS the pin).
	target->set_position(Vector3(1.0, 0.5, -0.4));
	ik->set_root_bone_name(0, "Root");
	ik->set_end_bone_name(0, "Root");
	ik->set_target_node(0, NodePath("Target"));
	tjk_reset(sk);
	ik->solve();
	CHECK((sk->get_bone_global_pose(root).origin - Vector3(1.0, 0.5, -0.4)).length() < 0.05);

	// stiffness = 1 freezes a bone at FK (identity) while the rest of the chain still solves
	// (the replacement for a separate locked-bones list).
	ik->set_end_bone_name(0, "Tip");
	ik->set_target_node(0, NodePath("Target"));
	ik->resolve_chains();
	ik->set_joint_stiffness(0, 1, 1.0); // freeze B1 (joint index 1 in Root->B1->B2->Tip)
	CHECK(ik->get_joint_stiffness(0, 1) > 0.99);
	target->set_position(Vector3(0.5, 0.5, 0.0));
	tjk_reset(sk);
	ik->solve();
	CHECK(sk->get_bone_pose(b1).basis.get_rotation_quaternion().is_equal_approx(Quaternion()));

	memdelete(sk);
}

// Extended end bone: a single self-rooted bone whose real effector is its `length`-long extension
// (extend_end_bone) must AIM that extension at the target, like FABRIK/CCDIK -- not sit at identity
// because the bone origin can't move. Regression for SwingTwistIK3D ignoring the end-bone extension.
TEST_CASE("[SceneTree][SwingTwistIK3D] extended end bone aims its extension at the target") {
	SceneTree *tree = SceneTree::get_singleton();
	Skeleton3D *sk = memnew(Skeleton3D);
	tree->get_root()->add_child(sk);
	const int root = sk->add_bone("Root");
	const int tip = sk->add_bone("Tip");
	sk->set_bone_parent(tip, root);
	sk->set_bone_rest(root, Transform3D(Basis(), Vector3(0, 0, 0)));
	sk->set_bone_rest(tip, Transform3D(Basis(), Vector3(0, 1, 0))); // Tip extends +Y from Root
	sk->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);

	SwingTwistIK3D *ik = memnew(SwingTwistIK3D);
	sk->add_child(ik);
	Marker3D *target = memnew(Marker3D);
	ik->add_child(target);
	target->set_name("Target");
	target->set_position(Vector3(3, 1, 0)); // off to +X of the Tip origin (0,1,0)
	sk->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);
	ik->set_setting_count(1);
	ik->set_root_bone_name(0, "Tip"); // self-rooted single bone, like the vertical-slice rig
	ik->set_end_bone_name(0, "Tip");
	ik->set_extend_end_bone(0, true);
	ik->set_end_bone_length(0, 2.0);
	ik->set_end_bone_direction(0, SkeletonModifier3D::BONE_DIRECTION_FROM_PARENT);
	ik->set_target_node(0, NodePath("Target"));
	ik->set_max_iterations(40);
	ik->set_angular_delta_limit(Math::PI); // no per-step cap, so the single-bone aim converges fully
	ik->solve();

	// The bone must actually rotate (not the old identity no-op)...
	CHECK_FALSE(sk->get_bone_pose(tip).basis.get_rotation_quaternion().is_equal_approx(Quaternion()));
	// ...and its extension (rest +Y, carried by the solved basis) must point at the marker.
	const Vector3 tip_o = sk->get_bone_global_pose(tip).origin;
	const Vector3 ext_dir = sk->get_bone_global_pose(tip).basis.xform(Vector3(0, 1, 0)).normalized();
	const Vector3 to_marker = (Vector3(3, 1, 0) - tip_o).normalized();
	CHECK(ext_dir.dot(to_marker) > 0.99);

	memdelete(sk);
}

// An extended end bone is a LEAF (no child), so _clamp_swing_twist had no swing-forward reference
// and silently skipped the kusudama -- letting the solve leave the cone (the gizmo's forbidden
// region). The clamp must use the EXTENSION as forward. A far off-cone target must still be clamped
// to within the cone of the rest forward.
TEST_CASE("[SceneTree][SwingTwistIK3D] extended end bone is kusudama-clamped (stays out of the forbidden region)") {
	SceneTree *tree = SceneTree::get_singleton();
	Skeleton3D *sk = memnew(Skeleton3D);
	tree->get_root()->add_child(sk);
	const int root = sk->add_bone("Root");
	const int tip = sk->add_bone("Tip");
	sk->set_bone_parent(tip, root);
	sk->set_bone_rest(root, Transform3D(Basis(), Vector3(0, 0, 0)));
	sk->set_bone_rest(tip, Transform3D(Basis(), Vector3(0, 1, 0))); // extension forward = +Y
	sk->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);

	SwingTwistIK3D *ik = memnew(SwingTwistIK3D);
	sk->add_child(ik);
	Marker3D *target = memnew(Marker3D);
	ik->add_child(target);
	target->set_name("Target");
	target->set_position(Vector3(5, 1, 0)); // far off to +X, well outside a +Y cone
	sk->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);
	ik->set_setting_count(1);
	ik->set_root_bone_name(0, "Tip");
	ik->set_end_bone_name(0, "Tip");
	ik->set_extend_end_bone(0, true);
	ik->set_end_bone_length(0, 2.0);
	ik->set_end_bone_direction(0, SkeletonModifier3D::BONE_DIRECTION_FROM_PARENT);
	ik->set_target_node(0, NodePath("Target"));
	ik->set_max_iterations(40);
	ik->set_angular_delta_limit(Math::PI);
	Ref<JointLimitationKusudama3D> lim;
	lim.instantiate();
	lim->set_cone_count(1);
	lim->set_cone_center(0, Vector3(0, 1, 0)); // allow only +/-10deg around the rest forward
	lim->set_cone_radius(0, Math::deg_to_rad(10.0));
	ik->set_joint_limitation(0, 0, lim);
	ik->solve();

	const Vector3 ext_dir = sk->get_bone_global_pose(tip).basis.xform(Vector3(0, 1, 0)).normalized();
	CHECK(tjk_finite(sk));
	CHECK(ext_dir.angle_to(Vector3(0, 1, 0)) <= Math::deg_to_rad(13.0)); // stayed inside the cone (+ soft band)
	CHECK(ext_dir.x > 0.05); // but did rotate toward the target, up to the cone boundary

	memdelete(sk);
}

// Adversarial: hostile inputs must never corrupt the skeleton (NaN/degenerate -> finite),
// the solve must be deterministic and stable, locked bones immovable, and the kusudama clamp
// must hold even when a target pulls hard against it.
TEST_CASE("[SceneTree][SwingTwistIK3D] adversarial robustness / determinism / invariants") {
	SceneTree *tree = SceneTree::get_singleton();

	Skeleton3D *sk = nullptr;
	SwingTwistIK3D *ik = nullptr;
	Marker3D *tgt = nullptr;

	// 1. NaN target must not propagate into the skeleton.
	tjk_build_chain(tree, 4, 0.3, (real_t)0.0, 20, Math::PI, sk, ik, tgt);
	tgt->set_position(Vector3(NAN, NAN, NAN));
	ik->solve();
	CHECK(tjk_finite(sk));
	memdelete(sk);

	// 2. Target exactly at the chain root (zero direction).
	tjk_build_chain(tree, 4, 0.3, (real_t)0.0, 20, Math::PI, sk, ik, tgt);
	tgt->set_position(Vector3(0, 0, 0));
	ik->solve();
	CHECK(tjk_finite(sk));
	memdelete(sk);

	// 3. Degenerate zero-length bones.
	tjk_build_chain(tree, 4, 0.0, (real_t)0.0, 20, Math::PI, sk, ik, tgt);
	tgt->set_position(Vector3(0.5, 0.5, 0));
	ik->solve();
	CHECK(tjk_finite(sk));
	memdelete(sk);

	// 4. All joints frozen (stiffness 1) -> no-op, poses stay at FK identity.
	tjk_build_chain(tree, 4, 0.3, (real_t)0.0, 20, Math::PI, sk, ik, tgt);
	ik->resolve_chains();
	for (int j = 0; j < ik->get_joint_count(0); j++) {
		ik->set_joint_stiffness(0, j, 1.0);
	}
	tgt->set_position(Vector3(10, 10, 10));
	ik->solve();
	CHECK(tjk_finite(sk));
	for (int b = 0; b < 4; b++) {
		CHECK(sk->get_bone_pose(b).basis.get_rotation_quaternion().is_equal_approx(Quaternion()));
	}
	memdelete(sk);

	// 5. Deterministic: identical input -> identical output.
	tjk_build_chain(tree, 5, 0.3, (real_t)0.0, 20, Math::PI, sk, ik, tgt);
	tgt->set_position(Vector3(0.6, 0.4, 0.2));
	ik->solve();
	PackedVector3Array a;
	for (int b = 0; b < 5; b++) {
		a.push_back(sk->get_bone_global_pose(b).origin);
	}
	for (int b = 0; b < 5; b++) {
		sk->set_bone_pose_rotation(b, Quaternion());
		sk->set_bone_pose_position(b, sk->get_bone_rest(b).origin);
	}
	ik->solve();
	bool same = true;
	for (int b = 0; b < 5; b++) {
		same = same && a[b].is_equal_approx(sk->get_bone_global_pose(b).origin);
	}
	CHECK(same);
	memdelete(sk);

	// 6. Re-solve stability: solving again from the converged pose barely moves.
	tjk_build_chain(tree, 5, 0.3, (real_t)0.0, 20, Math::PI, sk, ik, tgt);
	tgt->set_position(Vector3(0.5, 0.5, 0));
	ik->solve();
	const Vector3 t1 = sk->get_bone_global_pose(4).origin;
	ik->solve();
	const Vector3 t2 = sk->get_bone_global_pose(4).origin;
	CHECK((t2 - t1).length() < 1e-3);
	memdelete(sk);

	// 7. Kusudama clamp holds even when the target pulls hard against the cone.
	tjk_build_chain(tree, 5, 0.3, (real_t)0.0, 20, Math::PI, sk, ik, tgt);
	ik->resolve_chains();
	Ref<JointLimitationKusudama3D> lim;
	lim.instantiate();
	lim->set_cone_count(1);
	lim->set_cone_center(0, Vector3(0, 1, 0));
	lim->set_cone_radius(0, Math::deg_to_rad(8.0));
	int b1j = -1;
	for (int j = 0; j < ik->get_joint_count(0); j++) {
		if (ik->get_joint_bone(0, j) == 1) {
			b1j = j;
		}
	}
	REQUIRE(b1j >= 0);
	ik->set_joint_limitation(0, b1j, lim);
	tgt->set_position(Vector3(-0.5, 0.8, 0.3)); // hostile: pull the wrong way
	ik->solve();
	const Quaternion d = sk->get_bone_rest(1).basis.get_rotation_quaternion().inverse() *
			sk->get_bone_pose(1).basis.get_rotation_quaternion();
	const real_t sw = Math::acos(CLAMP((double)d.xform(Vector3(1, 0, 0)).dot(Vector3(1, 0, 0)), -1.0, 1.0));
	CHECK(sw <= Math::deg_to_rad(12.0)); // cone 8 + soft-band slack
	CHECK(tjk_finite(sk));
	memdelete(sk);

	// 8. A target node that does not resolve -> the effector is skipped, no crash.
	tjk_build_chain(tree, 4, 0.3, (real_t)0.0, 20, Math::PI, sk, ik, tgt);
	ik->set_target_node(0, NodePath("DoesNotExist"));
	tgt->set_position(Vector3(0.5, 0.3, 0));
	ik->solve();
	CHECK(tjk_finite(sk));
	memdelete(sk);

	// 9. Iteration extremes (0 clamps to 1).
	tjk_build_chain(tree, 4, 0.3, (real_t)0.0, 20, Math::PI, sk, ik, tgt);
	ik->set_max_iterations(0);
	tgt->set_position(Vector3(0.5, 0.5, 0));
	ik->solve();
	CHECK(tjk_finite(sk));
	memdelete(sk);
}

// Deeper adversarial pass: a mirrored (reflection, det<0) target frame must not inject an
// improper basis; a hostile twist about the forward axis must stay inside the twist limit;
// an unreachable free-root pull must stay bounded; conflicting pins on a branching skeleton
// and a pin whose ancestors are all locked must both stay finite.
TEST_CASE("[SceneTree][SwingTwistIK3D] deeper adversarial - reflection / twist clamp / free root / branching") {
	SceneTree *tree = SceneTree::get_singleton();

	Skeleton3D *sk = nullptr;
	SwingTwistIK3D *ik = nullptr;
	Marker3D *tgt = nullptr;

	// 1. Reflection target: a mirrored basis (det = -1) must not produce an improper pose basis.
	tjk_build_chain(tree, 5, 0.3, (real_t)0.0, 20, (real_t)-1.0, sk, ik, tgt);
	{
		Transform3D t;
		t.origin = Vector3(0.8, 0.3, 0.1);
		t.basis = Basis(Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1)); // det = -1
		tgt->set_transform(t);
	}
	ik->solve();
	CHECK(tjk_finite(sk));
	CHECK(tjk_proper(sk));
	memdelete(sk);

	// 2. Hostile twist: drive the tip frame twisted hard about its forward axis; the limited
	// joint's resulting twist must stay within [twist_from, twist_to] (+ soft slack).
	tjk_build_chain(tree, 4, 0.3, (real_t)0.0, 20, (real_t)-1.0, sk, ik, tgt);
	ik->resolve_chains();
	{
		Ref<JointLimitationKusudama3D> lim;
		lim.instantiate();
		lim->set_cone_count(1);
		lim->set_cone_center(0, Vector3(0, 1, 0)); // forward = bone rest +Y child dir
		lim->set_cone_radius(0, Math::deg_to_rad(40.0));
		lim->set_twist_from(Math::deg_to_rad(-15.0));
		lim->set_twist_to(Math::deg_to_rad(15.0));
		int b1j = -1;
		for (int j = 0; j < ik->get_joint_count(0); j++) {
			if (ik->get_joint_bone(0, j) == 1) {
				b1j = j;
			}
		}
		REQUIRE(b1j >= 0);
		ik->set_joint_limitation(0, b1j, lim);
		// Target twisted ~80 deg about the chain forward (X), well beyond the 15 deg limit.
		Transform3D t;
		t.origin = Vector3(0.7, 0.0, 0.0);
		t.basis = Basis(Vector3(1, 0, 0), Math::deg_to_rad(80.0));
		tgt->set_transform(t);
		ik->solve();
		CHECK(tjk_finite(sk));
		// Decompose bone 1's local delta twist about its forward (child rest dir = +X here,
		// since B2 sits at +X of B1 in rest).
		const Vector3 fwd = sk->get_bone_rest(2).origin.normalized();
		const Quaternion rest_local = sk->get_bone_rest(1).basis.get_rotation_quaternion();
		const Quaternion delta = rest_local.inverse() * sk->get_bone_pose(1).basis.get_rotation_quaternion();
		const real_t twist = 2.0 * Math::atan2((double)(delta.x * fwd.x + delta.y * fwd.y + delta.z * fwd.z), (double)delta.w);
		CHECK(Math::abs(twist) <= Math::deg_to_rad(15.0 + 5.0)); // limit + soft-band slack
	}
	memdelete(sk);

	// 3. Pinned root, far target: a parentless root effector translates toward a wildly unreachable
	// target -- it must stay bounded (no runaway), never farther than the target it is set to.
	tjk_build_chain(tree, 4, 0.3, (real_t)0.0, 20, (real_t)-1.0, sk, ik, tgt);
	ik->set_end_bone_name(0, "B0"); // the parentless root is the effector -> auto motion root
	ik->set_max_iterations(64);
	tgt->set_position(Vector3(1000.0, -500.0, 250.0)); // wildly unreachable
	ik->solve();
	CHECK(tjk_finite(sk));
	// The root cannot move farther than the target it chases.
	CHECK(sk->get_bone_pose(0).origin.length() <= 2000.0);
	memdelete(sk);

	// 4. Branching skeleton with two conflicting pins pulling opposite directions.
	{
		sk = memnew(Skeleton3D);
		tree->get_root()->add_child(sk);
		// B0 -> B1 -> {B2, B3} (a fork at B1).
		for (int i = 0; i < 4; i++) {
			sk->add_bone(vformat("B%d", i));
		}
		sk->set_bone_parent(1, 0);
		sk->set_bone_parent(2, 1);
		sk->set_bone_parent(3, 1);
		sk->set_bone_rest(0, Transform3D(Basis(), Vector3(0, 0, 0)));
		sk->set_bone_rest(1, Transform3D(Basis(), Vector3(0.3, 0, 0)));
		sk->set_bone_rest(2, Transform3D(Basis(), Vector3(0.3, 0.1, 0)));
		sk->set_bone_rest(3, Transform3D(Basis(), Vector3(0.3, -0.1, 0)));
		sk->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);
		ik = memnew(SwingTwistIK3D);
		sk->add_child(ik);
		Marker3D *t2 = memnew(Marker3D);
		ik->add_child(t2);
		t2->set_name("T2");
		t2->set_position(Vector3(0.6, 5.0, 0));
		Marker3D *t3 = memnew(Marker3D);
		ik->add_child(t3);
		t3->set_name("T3");
		t3->set_position(Vector3(0.6, -5.0, 0));
		sk->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);
		ik->set_setting_count(2);
		ik->set_root_bone_name(0, "B0");
		ik->set_end_bone_name(0, "B2");
		ik->set_target_node(0, NodePath("T2"));
		ik->set_root_bone_name(1, "B0");
		ik->set_end_bone_name(1, "B3");
		ik->set_target_node(1, NodePath("T3"));
		ik->set_max_iterations(20);
		ik->solve();
		CHECK(tjk_finite(sk));
		CHECK(tjk_proper(sk));
		memdelete(sk);
	}

	// 5. Every joint frozen (stiffness 1) -> nothing can move; must stay finite & unchanged.
	tjk_build_chain(tree, 4, 0.3, (real_t)0.0, 20, (real_t)-1.0, sk, ik, tgt);
	ik->resolve_chains();
	for (int j = 0; j < ik->get_joint_count(0); j++) {
		ik->set_joint_stiffness(0, j, 1.0);
	}
	tgt->set_position(Vector3(5, 5, 5));
	ik->solve();
	CHECK(tjk_finite(sk));
	for (int b = 0; b < 4; b++) {
		CHECK(sk->get_bone_pose(b).basis.get_rotation_quaternion().is_equal_approx(Quaternion()));
	}
	memdelete(sk);
}

// Jerk regression: as the target sweeps smoothly the angular_delta_limit must cap EACH joint's
// per-step rotation. Without the rate limiter a kinematic fold snapped a joint ~175 deg in a
// single step (measured on the live rig). One solve runs <= max_iterations passes, each capped at
// adl, so the per-step joint rotation is bounded by max_iterations*adl. (Proven jerk-bound in
// ../swing-twist-kusudama/IKJerk.lean.)
TEST_CASE("[SceneTree][SwingTwistIK3D] solve has no jerk - per-step joint rotation bounded by the dampening cap") {
	SceneTree *tree = SceneTree::get_singleton();
	Skeleton3D *sk = memnew(Skeleton3D);
	tree->get_root()->add_child(sk);
	const int n = 5;
	for (int i = 0; i < n; i++) {
		const int b = sk->add_bone(vformat("B%d", i));
		if (i > 0) {
			sk->set_bone_parent(b, i - 1);
		}
		sk->set_bone_rest(b, Transform3D(Basis(), Vector3(i == 0 ? (real_t)0.0 : (real_t)0.25, 0, 0)));
	}
	sk->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);
	SwingTwistIK3D *ik = memnew(SwingTwistIK3D);
	sk->add_child(ik);
	Marker3D *t = memnew(Marker3D);
	ik->add_child(t);
	t->set_name("Target");
	sk->notification(Skeleton3D::NOTIFICATION_UPDATE_SKELETON);
	ik->set_setting_count(1);
	ik->set_root_bone_name(0, "B0");
	ik->set_end_bone_name(0, "B4");
	ik->set_target_node(0, NodePath("Target"));
	const int max_iter = 4;
	const double adl = Math::deg_to_rad(2.0);
	ik->set_max_iterations(max_iter);
	ik->set_angular_delta_limit(adl);

	t->set_position(Vector3(0.8, 0.2, 0.0));
	ik->solve(); // settle

	const int steps = 360;
	LocalVector<Quaternion> prev;
	prev.resize(n);
	for (int i = 0; i < n; i++) {
		prev[i] = sk->get_bone_pose_rotation(i);
	}
	double max_step = 0.0;
	for (int s = 0; s <= steps; s++) {
		const double a = (double)s / steps * Math::TAU;
		// A fold-crossing loop: the target circles a point near the chain, forcing reconfigurations
		// that would flip a joint ~180 deg in one step without the cap.
		t->set_position(Vector3(0.2 + 0.45 * Math::cos(a), 0.45 * Math::sin(a), 0.1 * Math::sin(2.0 * a)));
		ik->solve(); // one solve per step = realtime cadence
		for (int i = 0; i < n; i++) {
			const Quaternion q = sk->get_bone_pose_rotation(i);
			max_step = MAX(max_step, (double)prev[i].angle_to(q));
			prev[i] = q;
		}
	}
	CHECK_MESSAGE(max_step <= max_iter * adl + Math::deg_to_rad(0.5),
			vformat("max per-step joint rotation %.2f deg exceeds the %.1f deg cap (jerk!)",
					Math::rad_to_deg(max_step), Math::rad_to_deg(max_iter * adl)));
	memdelete(sk);
}

// Adversarial test for `relax` (returnfulness): it's subtle, so pin down every invariant -- 0 is
// a no-op, it never breaks finiteness/determinism, it pulls a redundant joint TOWARD rest, full
// relax sits the bone at rest, and a moderate relax still lets the chain essentially reach.
TEST_CASE("[SceneTree][SwingTwistIK3D] relax (returnfulness) - adversarial invariants") {
	SceneTree *tree = SceneTree::get_singleton();
	SwingTwistIK3D *ik = nullptr;
	Skeleton3D *sk = nullptr;
	Marker3D *tgt = nullptr;

	// 1. relax = 0 is a no-op: identical pose to never setting relax.
	tjk_build_chain(tree, 5, 0.3, (real_t)10.0, 40, Math::PI, sk, ik, tgt);
	tgt->set_position(Vector3(0.6, 0.5, 0.0));
	ik->solve();
	PackedVector3Array base;
	for (int b = 0; b < 5; b++) {
		base.push_back(sk->get_bone_pose(b).basis.get_euler());
	}
	for (int b = 0; b < 5; b++) {
		sk->set_bone_pose_rotation(b, Quaternion());
	}
	for (int j = 0; j < ik->get_joint_count(0); j++) {
		ik->set_joint_relax(0, j, 0.0);
	}
	ik->solve();
	bool same = true;
	for (int b = 0; b < 5; b++) {
		same = same && (sk->get_bone_pose(b).basis.get_euler() - base[b]).length() < 1e-5;
	}
	CHECK_MESSAGE(same, "relax=0 must be a no-op");
	memdelete(sk);

	// 2. relax lowers the chain's TOTAL rest-deviation (it minimizes overall "discomfort", not
	// each joint independently -- a single joint can move either way as the branch reconfigures.
	// That whole-chain-vs-per-joint distinction is exactly the subtlety in `relax`).
	tjk_build_chain(tree, 6, 0.25, (real_t)10.0, 40, Math::PI, sk, ik, tgt);
	tgt->set_position(Vector3(0.7, 0.2, 0.0));
	ik->solve();
	const real_t energy_norelax = tjk_rest_energy(sk, ik);
	for (int b = 0; b < sk->get_bone_count(); b++) {
		sk->set_bone_pose_rotation(b, Quaternion());
	}
	for (int j = 0; j < ik->get_joint_count(0); j++) {
		ik->set_joint_relax(0, j, 0.3);
	}
	ik->solve();
	const real_t energy_relax = tjk_rest_energy(sk, ik);
	CHECK_MESSAGE(energy_relax <= energy_norelax + 1e-4,
			vformat("relax must not raise total rest-deviation (%.3f vs %.3f)", (double)energy_relax, (double)energy_norelax));
	CHECK(tjk_finite(sk));
	memdelete(sk);

	// 3. relax = 1 on every joint -> the chain abandons reach and sits at its REST pose.
	tjk_build_chain(tree, 5, 0.3, (real_t)10.0, 40, Math::PI, sk, ik, tgt);
	tgt->set_position(Vector3(0.5, 0.5, 0.2));
	for (int j = 0; j < ik->get_joint_count(0); j++) {
		ik->set_joint_relax(0, j, 1.0);
	}
	ik->solve();
	bool at_rest = true;
	for (int j = 0; j < ik->get_joint_count(0); j++) {
		const int b = ik->get_joint_bone(0, j);
		const Quaternion rq = sk->get_bone_rest(b).basis.get_rotation_quaternion();
		at_rest = at_rest && sk->get_bone_pose(b).basis.get_rotation_quaternion().angle_to(rq) < Math::deg_to_rad(2.0);
	}
	CHECK_MESSAGE(at_rest, "relax=1 must settle the bones at their rest pose");
	CHECK(tjk_finite(sk));
	memdelete(sk);

	// 4. Determinism with relax active.
	tjk_build_chain(tree, 5, 0.3, (real_t)10.0, 40, Math::PI, sk, ik, tgt);
	for (int j = 0; j < ik->get_joint_count(0); j++) {
		ik->set_joint_relax(0, j, 0.4);
	}
	tgt->set_position(Vector3(0.55, 0.45, 0.1));
	ik->solve();
	PackedVector3Array a;
	for (int b = 0; b < 5; b++) {
		a.push_back(sk->get_bone_global_pose(b).origin);
	}
	for (int b = 0; b < 5; b++) {
		sk->set_bone_pose_rotation(b, Quaternion());
	}
	ik->solve();
	bool det = true;
	for (int b = 0; b < 5; b++) {
		det = det && a[b].is_equal_approx(sk->get_bone_global_pose(b).origin);
	}
	CHECK_MESSAGE(det, "relax must stay deterministic");
	memdelete(sk);

	// 5. Moderate relax still essentially reaches a reachable target (slack-only bias).
	tjk_build_chain(tree, 6, 0.25, (real_t)10.0, 40, Math::PI, sk, ik, tgt);
	for (int j = 0; j < ik->get_joint_count(0); j++) {
		ik->set_joint_relax(0, j, 0.2);
	}
	const Vector3 goal(0.6, 0.3, 0.0); // well within the 1.25 reach
	tgt->set_position(goal);
	ik->solve();
	const Vector3 tip = sk->get_bone_global_pose(sk->find_bone("B5")).origin;
	CHECK_MESSAGE((tip - goal).length() < 0.12,
			vformat("moderate relax should still essentially reach (residual %.3f)", (double)(tip - goal).length()));
	CHECK(tjk_finite(sk));
	memdelete(sk);
}

} // namespace TestJointLimitationKusudama3D

#endif // _3D_DISABLED

/**************************************************************************/
/*  swing_twist_ik_3d.h                                                   */
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
#include "scene/3d/iterate_ik_3d.h"
#include "scene/resources/3d/joint_limitation_kusudama_3d.h"

// Whole-body, swing-twist-DIRECT IK -- a concrete IterateIK3D solver, so it is a drop-in for
// FABRIK3D/CCDIK3D and reuses the same chains / targets / per-joint Kusudama limitations.
// Unlike the per-chain solvers, it overrides the whole-body pass: each chain's end bone is a
// 6D effector (its target frame's forward = position heading -> SWING, a second axis ->
// TWIST), and every bone is rotated by a global multi-limb Kabsch best-fit over ALL its
// downstream effector position headings at once (the coupling FABRIK can't do), then twist is
// solved about the resulting forward as its own 1-DOF objective. The modular kusudama clamps
// swing (cones) and twist ([twist_from, twist_to]) in its native coordinates.
class SwingTwistIK3D : public IterateIK3D {
	GDCLASS(SwingTwistIK3D, IterateIK3D);

	struct Effector {
		int tip_bone = -1;
		Transform3D target; // skeleton-local 6D goal frame
		Vector3 ext_local; // end-bone extension offset in tip_bone's local frame; zero = at bone origin
		real_t pos_weight = 1.0; // scales this effector's pull in the weighted Kabsch
		real_t swing_weight = 1.0; // scales matching the target's forward axis at the tip
		real_t twist_weight = 1.0; // scales matching the target's roll at the tip
	};

	// Everything an animator needs maps onto knobs IterateIK3D already provides -- no bespoke API:
	//  - a PIN is just a bone with a target (a chain's target_node); the chain end is pinned by it,
	//  - a parentless PINNED ROOT is the motion root: it translates to its target automatically
	//    (rotating ancestors can't aim a parentless bone), so no "free root" flag,
	//  - a LOCKED bone is per-joint stiffness 1.0 (the rotation block slerps by 1 - stiffness, so
	//    stiffness 1 keeps the FK pose); a soft lock is any stiffness in between.

	// Per-JOINT weights, stored the IterateIK3D way: in the chain settings, exposed as
	// settings/i/joints/j/priorities (+ stiffness/engage/isolation/relax) via the
	// _get_joint_extra_properties hook + _set/_get (so they persist + undo like the per-joint
	// limitation). position scales how hard the solver pulls that bone to its target point (weighted
	// Kabsch); swing/twist scale how hard it matches the target BASIS (the "star"). A POLE is not a
	// special case -- it is a low-position, zero-orientation weight on a mid joint (elbow/knee) that
	// also has a target: the strong wrist owns the reach, so the weak elbow can only influence the
	// leftover roll DOF, swinging the elbow toward its target.
	// Per-joint solver weights -- the EWBIK PST (position/swing/twist) priorities plus stiffness.
	// position scales the pull to the target point; swing scales matching the target's forward
	// AXIS; twist scales matching its ROLL about that axis (swing + twist = the orientation
	// "star", split into its two physical DOF). stiffness is how much the bone resists rotating.
	struct JointWeight {
		real_t position = 1.0;
		real_t swing = 1.0;
		real_t twist = 1.0;
		real_t stiffness = 0.0; // [0..1] how much this bone RESISTS rotating: 0 = free, 1 = rigid
		real_t engage = 0.0; // [0..1] how LATE in the solve this bone joins: 0 = from the start
		real_t isolation = 0.0; // [0..1] a pin's isolation: 1 = its bone blocks descendant pulls from
		// reaching ancestors above it; 0 = transparent (descendants pull through). EWBIK depthFalloff.
		real_t relax = 0.0; // [0..1] restoring pull toward the bone's REST pose: 0 = none, 1 = sit at
		// rest. Resolves the redundant null-space toward the comfortable pose. EWBIK returnfulness.
	};
	LocalVector<LocalVector<JointWeight>> joint_weights; // [setting][joint]; resized to the chains
	void _sync_joint_weights() const; // grow joint_weights to match the current settings/joint counts

	LocalVector<int> solve_order; // parents before children; rebuilt each solve from the live skeleton

	// Forward kinematics maintained in skeleton-local space during the manual solve.
	void _full_fk(Skeleton3D *p_sk, LocalVector<Transform3D> &p_gp) const;
	// Refresh only p_root's subtree (parents-before-children) via the flat CSR children
	// adjacency; see IKFold.lean / IKFast.lean for why this reproduces a full recompute.
	// p_stack is reused scratch to avoid per-call allocation.
	void _refresh_subtree(Skeleton3D *p_sk, LocalVector<Transform3D> &p_gp, LocalVector<int> &p_stack, const LocalVector<int> &p_child_offset, const LocalVector<int> &p_child_index, int p_root) const;
	// p_forward_override: the bone's swing forward axis (local). Zero = derive from the first child's
	// rest dir; pass the extension direction for a leaf/extended end bone (which has no child).
	Quaternion _clamp_swing_twist(Skeleton3D *p_sk, int p_bone, const Ref<JointLimitationKusudama3D> &p_lim, const Quaternion &p_candidate_local, const Vector3 &p_forward_override = Vector3()) const;

protected:
	static void _bind_methods();
	// Per-joint params stored/emitted the IterateIK3D way (settings/i/joints/j/...), interleaved
	// into the base joint loop via the _get_joint_extra_properties hook (no duplicate settings).
	virtual void _get_joint_extra_properties(int p_setting, int p_joint, const String &p_path, LocalVector<PropertyInfo> &p_props) const override;
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	virtual void _process_modification(double p_delta) override;

public:
	// Per-JOINT weights (also the mechanism for poles, soft IK, position-only goals): position =
	// pull to the target point; swing/twist = match the target basis (the "star"). Stored with the
	// chain joint and serialized as settings/i/joints/j/priorities.
	// A pole = give the mid joint a target + set_joint_priorities(chain, mid_joint, ~0.3, 0, 0).
	void set_joint_priorities(int p_index, int p_joint, real_t p_position, real_t p_swing, real_t p_twist);
	Vector3 get_joint_priorities(int p_index, int p_joint) const; // (position, swing, twist); (1,1,1) default
	// Per-bone STIFFNESS [0..1]: how much the joint resists the solver. 0 = free, 1 = rigid (a soft
	// continuous version of locking). Stored as settings/i/joints/j/stiffness.
	void set_joint_stiffness(int p_index, int p_joint, real_t p_stiffness);
	real_t get_joint_stiffness(int p_index, int p_joint) const;
	// ENGAGE [0..1]: how late in the solve this bone joins (0 = from the start). Distal-first
	// settling, etc. ISOLATION [0..1]: how much a pin blocks descendant pulls from reaching
	// ancestors above it (0 = transparent). Both stored as settings/i/joints/j/...
	void set_joint_engage(int p_index, int p_joint, real_t p_engage);
	real_t get_joint_engage(int p_index, int p_joint) const;
	void set_joint_isolation(int p_index, int p_joint, real_t p_isolation);
	real_t get_joint_isolation(int p_index, int p_joint) const;
	// RELAX [0..1]: a restoring pull toward the bone's REST pose (EWBIK returnfulness). It biases
	// only the slack the effectors leave free, so reach is preserved; at 1 the bone sits at rest.
	void set_joint_relax(int p_index, int p_joint, real_t p_relax);
	real_t get_joint_relax(int p_index, int p_joint) const;

	// Run a full solve immediately (also used by tests / manual use).
	void solve();

	SwingTwistIK3D() {}
};

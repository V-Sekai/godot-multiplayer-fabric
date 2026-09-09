/**************************************************************************/
/*  humanoid_kusudama_rom.cpp                                             */
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

#include "humanoid_kusudama_rom.h"

#include "core/object/class_db.h"
#include "scene/3d/marker_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/3d/swing_twist_ik_3d.h"
#include "scene/resources/3d/humanoid_kusudama_rom_data.h"

// An ANNY phenotype axis in [0,1], neutral 0.5 when unset.
static real_t anny_axis(const Dictionary &p_phenotype, const char *p_key) {
	return p_phenotype.has(p_key) ? CLAMP((real_t)p_phenotype[p_key], (real_t)0.0, (real_t)1.0) : (real_t)0.5;
}

real_t HumanoidKusudamaRom::phenotype_scale(const Dictionary &p_phenotype) {
	using namespace HumanoidKusudamaRomData;
	// ROM is interpolated over the ANNY phenotype axes (gender, age, muscle, weight,
	// height, proportions), each in [0,1] with 0.5 the reference. The baked template is
	// the ROM at that reference; this scale smoothly moves it across the phenotype space.
	const real_t s = 1.0 +
			ANNY_AGE_COEF * (anny_axis(p_phenotype, "age") - 0.5) +
			ANNY_WEIGHT_COEF * (anny_axis(p_phenotype, "weight") - 0.5) +
			ANNY_MUSCLE_COEF * (anny_axis(p_phenotype, "muscle") - 0.5) +
			ANNY_GENDER_COEF * (anny_axis(p_phenotype, "gender") - 0.5) +
			ANNY_HEIGHT_COEF * (anny_axis(p_phenotype, "height") - 0.5) +
			ANNY_PROPORTIONS_COEF * (anny_axis(p_phenotype, "proportions") - 0.5);
	return CLAMP(s, (real_t)SCALE_MIN, (real_t)SCALE_MAX);
}

Ref<JointLimitationKusudama3D> HumanoidKusudamaRom::generate_bone(const String &p_bone, const Dictionary &p_phenotype) const {
	using namespace HumanoidKusudamaRomData;
	const real_t scale = phenotype_scale(p_phenotype);
	for (int i = 0; i < TEMPLATE_COUNT; i++) {
		if (p_bone != String(TEMPLATE[i].bone)) {
			continue;
		}
		Ref<JointLimitationKusudama3D> k;
		k.instantiate();
		// Scale the WHOLE swing region by phenotype: both each cone's radius AND its
		// center's angle from the rest axis (baked to +Y). Scaling radius alone would
		// barely move a hinge fan (its extent is set by the spread of cone centers), so
		// transferred poses wouldn't adjust. Scaling the region makes ROM grow/shrink
		// coherently -> animation transfer is proportional and correctly adjusted.
		const Vector3 rest_axis(0, 1, 0);
		Vector<Vector4> cs;
		for (int j = 0; j < TEMPLATE[i].cone_count; j++) {
			const ConeF &c = TEMPLATE[i].cones[j];
			Vector3 center = Vector3(c.x, c.y, c.z).normalized();
			const real_t theta = rest_axis.angle_to(center);
			if (theta > (real_t)1e-5) {
				center = rest_axis.slerp(center, scale).normalized(); // angle scales by `scale`
			}
			const real_t r = MAX(c.radius * scale, (real_t)MIN_CONE_RADIUS); // floor: never zero-ROM
			cs.push_back(Vector4(center.x, center.y, center.z, r));
		}
		k->set_cones(cs);
		const real_t tw = TEMPLATE[i].twist * scale; // symmetric twist window about rest
		if (tw > (real_t)1e-4) {
			k->set_twist_from(-tw * 0.5);
			k->set_twist_to(tw * 0.5);
		}
		return k;
	}
	return Ref<JointLimitationKusudama3D>();
}

Dictionary HumanoidKusudamaRom::generate(const PackedStringArray &p_bones, const Dictionary &p_phenotype) const {
	Dictionary out;
	for (int i = 0; i < p_bones.size(); i++) {
		Ref<JointLimitationKusudama3D> k = generate_bone(p_bones[i], p_phenotype);
		if (k.is_valid()) {
			out[p_bones[i]] = k;
		}
	}
	return out;
}

Dictionary HumanoidKusudamaRom::generate_humanoid(const Dictionary &p_phenotype) const {
	using namespace HumanoidKusudamaRomData;
	Dictionary out;
	for (int i = 0; i < TEMPLATE_COUNT; i++) {
		const String bone = String(TEMPLATE[i].bone);
		out[bone] = generate_bone(bone, p_phenotype);
	}
	return out;
}

PackedStringArray HumanoidKusudamaRom::get_supported_bones() const {
	using namespace HumanoidKusudamaRomData;
	PackedStringArray out;
	for (int i = 0; i < TEMPLATE_COUNT; i++) {
		out.push_back(String(TEMPLATE[i].bone));
	}
	return out;
}

int HumanoidKusudamaRom::setup_humanoid_chains(IterateIK3D *p_ik) const {
	ERR_FAIL_NULL_V(p_ik, 0);
	// root_bone -> end_bone per standard humanoid limb + spine.
	struct Chain {
		const char *root;
		const char *end;
	};
	static const Chain CHAINS[] = {
		{ "LeftUpperArm", "LeftHand" }, { "RightUpperArm", "RightHand" },
		{ "LeftUpperLeg", "LeftFoot" }, { "RightUpperLeg", "RightFoot" },
		{ "Spine", "Head" }
	};
	const int n = (int)(sizeof(CHAINS) / sizeof(CHAINS[0]));
	p_ik->set_setting_count(n);
	for (int i = 0; i < n; i++) {
		p_ik->set_root_bone_name(i, CHAINS[i].root);
		p_ik->set_end_bone_name(i, CHAINS[i].end);
		// Extend past the end bone so the END bone (hand/foot/head) is a SOLVED joint and
		// its swing/twist limit is enforced (otherwise the end-effector follows the target
		// unconstrained). The target then drives a virtual tip a short distance past it.
		// The length is a fallback; apply_ik_limits() derives it from the actual bone.
		p_ik->set_extend_end_bone(i, true);
		p_ik->set_end_bone_length(i, 0.1);
	}
	return n;
}

// End-bone extension length, derived from the skeleton: distance to the bone's first
// child, or a fraction of its incoming segment for a leaf bone (hand/foot/head). Keeps
// the extend-end-bone tip rig-scale appropriate.
static float hk_end_bone_length(Skeleton3D *p_sk, int p_bone) {
	for (int b = 0; b < p_sk->get_bone_count(); b++) {
		if (p_sk->get_bone_parent(b) == p_bone) {
			return MAX((float)p_sk->get_bone_rest(b).origin.length(), 0.02f);
		}
	}
	return MAX(0.4f * (float)p_sk->get_bone_rest(p_bone).origin.length(), 0.02f);
}

// Sensible per-joint damp-family defaults so the generated humanoid rig feels natural out of the
// box (the animator can still override each). priorities = (position, swing, twist); stiffness and
// relax in [0..1]; cushion in radians on the kusudama. Conservative values keep reach intact.
static void hk_joint_damp(const String &p_bone, Vector3 &r_priorities, real_t &r_stiffness, real_t &r_relax, real_t &r_cushion) {
	r_priorities = Vector3(1, 1, 1);
	r_stiffness = 0.0;
	r_relax = 0.0;
	r_cushion = 0.06;
	const String b = p_bone.to_lower();
	if (b.contains("hips") || b.contains("spine") || b.contains("chest")) {
		// Torso resists a little so the limbs move first, and carries less free twist.
		r_stiffness = 0.12;
		r_relax = 0.08;
		r_priorities = Vector3(1, 1, 0.6);
	} else if (b.contains("neck")) {
		r_stiffness = 0.06;
		r_relax = 0.05;
	} else if (b.contains("shoulder")) {
		r_relax = 0.05;
		r_cushion = 0.10; // shoulders approach their wide limit more gently
	} else if (b.contains("thumb") || b.contains("index") || b.contains("middle") || b.contains("ring") || b.contains("little")) {
		// Fingers curl back toward rest and barely care about roll.
		r_relax = 0.25;
		r_priorities = Vector3(1, 1, 0.3);
	} else if (b.contains("lowerarm") || b.contains("lowerleg")) {
		r_relax = 0.10; // keep a natural elbow/knee bend
	} else if (b.contains("upperarm") || b.contains("upperleg")) {
		r_relax = 0.05;
	}
	// Hand / Foot / Head fall through to full 6D, no damp (they are the goals).
}

int HumanoidKusudamaRom::apply_ik_limits(IterateIK3D *p_ik, const Dictionary &p_phenotype) const {
	ERR_FAIL_NULL_V(p_ik, 0);
	// Resolve the joint chains now (works even on a detached import scene) so the joints
	// exist before we attach limitations.
	p_ik->resolve_chains();
	Skeleton3D *sk = p_ik->get_skeleton();
	// Derive each chain's end-bone extension from the actual bone length (rig-scale aware).
	if (sk) {
		for (int i = 0; i < p_ik->get_setting_count(); i++) {
			const int eb = p_ik->get_end_bone(i);
			if (eb >= 0) {
				p_ik->set_end_bone_length(i, hk_end_bone_length(sk, eb));
			}
		}
	}
	SwingTwistIK3D *st = Object::cast_to<SwingTwistIK3D>(p_ik); // per-joint damp knobs are its own
	int applied = 0;
	for (int i = 0; i < p_ik->get_setting_count(); i++) {
		const int joints = p_ik->get_joint_count(i);
		for (int j = 0; j < joints; j++) {
			const String bone = p_ik->get_joint_bone_name(i, j);
			Ref<JointLimitationKusudama3D> k = generate_bone(bone, p_phenotype);
			if (!k.is_valid()) {
				continue;
			}
			// Per-joint damp-family defaults for a natural rig.
			Vector3 priorities;
			real_t stiffness, relax, cushion;
			hk_joint_damp(bone, priorities, stiffness, relax, cushion);
			k->set_cushion(cushion);
			if (st) {
				st->set_joint_priorities(i, j, priorities.x, priorities.y, priorities.z);
				st->set_joint_stiffness(i, j, stiffness);
				st->set_joint_relax(i, j, relax);
			}
			p_ik->set_joint_limitation(i, j, k);
			applied++;
			// Orient the swing cones in the bone's REST frame so make_space builds a full
			// basis (defined roll) instead of the roll-free shortest-arc fallback that
			// SECONDARY_DIRECTION_NONE's zero vector triggers. Godot's humanoid retarget
			// normalizes the bone roll, so the bone-local +Z is the sagittal/forward axis
			// (the plane hinge joints flex in); if it doesn't, the natural rest roll is used.
			p_ik->set_joint_limitation_right_axis(i, j, SkeletonModifier3D::SECONDARY_DIRECTION_PLUS_Z);
		}
	}
	return applied;
}

namespace {
// A tracking target: marker/bone name, and which IK chain it drives (-1 = no chain, a
// passive segment tracker; chain ordering matches setup_humanoid_chains).
struct HKTarget {
	const char *name;
	const char *bone;
	int chain;
};
Dictionary hk_build_targets(IterateIK3D *p_ik, const HKTarget *p_defs, int p_count) {
	p_ik->resolve_chains();
	Skeleton3D *sk = p_ik->get_skeleton();
	Node *owner = p_ik->get_owner() ? p_ik->get_owner() : static_cast<Node *>(p_ik);
	Dictionary out;
	for (int i = 0; i < p_count; i++) {
		Marker3D *m = memnew(Marker3D);
		m->set_name(p_defs[i].name);
		p_ik->add_child(m);
		m->set_owner(owner);
		if (sk) {
			const int b = sk->find_bone(p_defs[i].bone);
			if (b >= 0) {
				m->set_transform(sk->get_bone_global_rest(b)); // seat at the rest pose
			}
		}
		if (p_defs[i].chain >= 0 && p_defs[i].chain < p_ik->get_setting_count()) {
			p_ik->set_target_node(p_defs[i].chain, NodePath(String(p_defs[i].name)));
		}
		out[p_defs[i].name] = m;
	}
	return out;
}
} // namespace

Dictionary HumanoidKusudamaRom::add_control_rig(IterateIK3D *p_ik, ControlRig p_rig) const {
	ERR_FAIL_NULL_V(p_ik, Dictionary());
	// Standard humanoid control rig. Head/hands/feet are chain end-effectors (IK goals,
	// chain >= 0); hips is the root reference; elbows/knees are pole vectors; chest is an
	// upper-body control. `full_only` controls (poles + chest) are added only for the FULL
	// preset, which is a strict superset of IK_GOALS.
	struct Def {
		const char *name;
		const char *bone;
		int chain;
		bool full_only;
	};
	static const Def ALL[] = {
		{ "HeadGoal", "Head", 4, false },
		{ "LeftHandGoal", "LeftHand", 0, false },
		{ "RightHandGoal", "RightHand", 1, false },
		{ "HipsRoot", "Hips", -1, false },
		{ "LeftFootGoal", "LeftFoot", 2, false },
		{ "RightFootGoal", "RightFoot", 3, false },
		{ "ChestControl", "Chest", -1, true },
		{ "LeftElbowPole", "LeftLowerArm", -1, true },
		{ "RightElbowPole", "RightLowerArm", -1, true },
		{ "LeftKneePole", "LeftLowerLeg", -1, true },
		{ "RightKneePole", "RightLowerLeg", -1, true },
	};
	const bool full = (p_rig == CONTROL_RIG_FULL);
	HKTarget defs[sizeof(ALL) / sizeof(ALL[0])];
	int n = 0;
	for (const Def &d : ALL) {
		if (full || !d.full_only) {
			defs[n].name = d.name;
			defs[n].bone = d.bone;
			defs[n].chain = d.chain;
			n++;
		}
	}
	return hk_build_targets(p_ik, defs, n);
}

void HumanoidKusudamaRom::_bind_methods() {
	ClassDB::bind_static_method("HumanoidKusudamaRom", D_METHOD("phenotype_scale", "phenotype"), &HumanoidKusudamaRom::phenotype_scale);
	ClassDB::bind_method(D_METHOD("generate_bone", "bone", "phenotype"), &HumanoidKusudamaRom::generate_bone);
	ClassDB::bind_method(D_METHOD("generate", "bones", "phenotype"), &HumanoidKusudamaRom::generate);
	ClassDB::bind_method(D_METHOD("generate_humanoid", "phenotype"), &HumanoidKusudamaRom::generate_humanoid);
	ClassDB::bind_method(D_METHOD("get_supported_bones"), &HumanoidKusudamaRom::get_supported_bones);
	ClassDB::bind_method(D_METHOD("setup_humanoid_chains", "ik"), &HumanoidKusudamaRom::setup_humanoid_chains);
	ClassDB::bind_method(D_METHOD("apply_ik_limits", "ik", "phenotype"), &HumanoidKusudamaRom::apply_ik_limits);
	ClassDB::bind_method(D_METHOD("add_control_rig", "ik", "rig"), &HumanoidKusudamaRom::add_control_rig, DEFVAL(CONTROL_RIG_FULL));

	BIND_ENUM_CONSTANT(CONTROL_RIG_IK_GOALS);
	BIND_ENUM_CONSTANT(CONTROL_RIG_FULL);
}

/**************************************************************************/
/*  humanoid_kusudama_rom.h                                               */
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

#include "core/object/ref_counted.h"
#include "scene/3d/iterate_ik_3d.h"
#include "scene/resources/3d/joint_limitation_kusudama_3d.h"

// Generates per-bone JointLimitationKusudama3D range-of-motion limits for a Godot
// humanoid skeleton, from anatomical NORMAL-range data baked into the binary
// (humanoid_kusudama_rom_data.gen.h, derived from AddBiomechanics) plus clinical
// gap-fills, scaled smoothly by ANNY phenotype. No external files -> works inside the
// monolithic Godot executable. ROM is keyed by SkeletonProfileHumanoid bone slots, so
// it follows Godot's humanoid auto-classification (varies per slot, rig-agnostic).
class HumanoidKusudamaRom : public RefCounted {
	GDCLASS(HumanoidKusudamaRom, RefCounted);

protected:
	static void _bind_methods();

public:
	// phenotype keys: "age" (years), "height_m", "mass_kg", "sex" (0=female..1=male).
	static real_t phenotype_scale(const Dictionary &p_phenotype);

	// Build the Kusudama limit for one humanoid bone slot; null if the slot has no ROM data.
	Ref<JointLimitationKusudama3D> generate_bone(const String &p_bone, const Dictionary &p_phenotype) const;
	// bone_name(String) -> Ref<JointLimitationKusudama3D> for the requested bones.
	Dictionary generate(const PackedStringArray &p_bones, const Dictionary &p_phenotype) const;
	// Same, for every bone slot the data covers.
	Dictionary generate_humanoid(const Dictionary &p_phenotype) const;
	// All humanoid bone slots with baked ROM (data + clinical).
	PackedStringArray get_supported_bones() const;

	// --- IterateIK3D integration ---
	// Configure the standard humanoid IK chains (two arms, two legs, spine) on p_ik by
	// SkeletonProfileHumanoid bone name. Names are stored; bones resolve once p_ik is
	// parented to a Skeleton3D. Returns the number of chains created.
	int setup_humanoid_chains(IterateIK3D *p_ik) const;
	// Attach the phenotype-scaled Kusudama ROM to every joint of p_ik whose bone maps to
	// a humanoid slot. p_ik must already be parented to a Skeleton3D. Returns the number
	// of limits applied.
	int apply_ik_limits(IterateIK3D *p_ik, const Dictionary &p_phenotype) const;

	// Standard film/animation humanoid control-rig presets. IK_GOALS is the minimal effector
	// set; FULL adds pole vectors and a chest control. FULL is a strict superset of IK_GOALS.
	enum ControlRig {
		CONTROL_RIG_IK_GOALS, // 6 effectors: root(hips), head, 2 hand-IK, 2 foot-IK
		CONTROL_RIG_FULL, // + chest control + elbow/knee pole vectors (11 controls)
	};

	// Build a control rig on p_ik: head/hands/feet are chain end-effectors (IK goals), hips
	// is the root reference, elbows/knees are pole vectors, chest is an upper-body control.
	// Returns { control_name: Marker3D }.
	Dictionary add_control_rig(IterateIK3D *p_ik, ControlRig p_rig = CONTROL_RIG_FULL) const;
};

VARIANT_ENUM_CAST(HumanoidKusudamaRom::ControlRig);

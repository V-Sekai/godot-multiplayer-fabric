/**************************************************************************/
/*  post_import_plugin_humanoid_rom.cpp                                   */
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

#include "post_import_plugin_humanoid_rom.h"

#include "scene/3d/skeleton_3d.h"
#include "scene/3d/swing_twist_ik_3d.h"
#include "scene/resources/3d/humanoid_kusudama_rom.h"

void PostImportPluginHumanoidRom::get_internal_import_options(InternalImportCategory p_category, List<ResourceImporter::ImportOption> *r_options) {
	if (p_category == INTERNAL_IMPORT_CATEGORY_SKELETON_3D_NODE) {
		// Default OFF: opt-in generation of full-body Kusudama ROM limits as an IK modifier.
		r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::BOOL, "humanoid_rom/generate_limits", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), false));
		// ANNY phenotype axes (0..1, 0.5 = reference); ROM is interpolated over them.
		r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::FLOAT, "humanoid_rom/gender", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.5));
		r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::FLOAT, "humanoid_rom/age", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.5));
		r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::FLOAT, "humanoid_rom/muscle", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.5));
		r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::FLOAT, "humanoid_rom/weight", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.5));
	}
}

Variant PostImportPluginHumanoidRom::get_internal_option_visibility(InternalImportCategory p_category, const String &p_scene_import_type, const String &p_option, const HashMap<StringName, Variant> &p_options) const {
	if (p_category == INTERNAL_IMPORT_CATEGORY_SKELETON_3D_NODE) {
		if (p_option != "humanoid_rom/generate_limits" && p_option.begins_with("humanoid_rom/")) {
			// Only show the ANNY phenotype axes when generation is enabled.
			return p_options.has("humanoid_rom/generate_limits") && bool(p_options["humanoid_rom/generate_limits"]);
		}
	}
	return Variant();
}

void PostImportPluginHumanoidRom::internal_process(InternalImportCategory p_category, Node *p_base_scene, Node *p_node, Ref<Resource> p_resource, const Dictionary &p_options) {
	if (p_category != INTERNAL_IMPORT_CATEGORY_SKELETON_3D_NODE) {
		return;
	}
	if (!p_options.has("humanoid_rom/generate_limits") || !bool(p_options["humanoid_rom/generate_limits"])) {
		return; // default OFF
	}
	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(p_node);
	if (!skeleton) {
		return;
	}

	Dictionary phenotype; // ANNY phenotype axes (0..1)
	const char *axes[] = { "gender", "age", "muscle", "weight" };
	for (const char *a : axes) {
		const String key = String("humanoid_rom/") + a;
		phenotype[a] = p_options.has(key) ? (double)p_options[key] : 0.5;
	}

	Ref<HumanoidKusudamaRom> gen;
	gen.instantiate();

	// SwingTwistIK3D: a concrete IterateIK3D solver (drop-in for FABRIK3D) that does a
	// whole-body multi-limb swing-twist-direct solve with the modular per-bone Kusudama
	// limiter -- jointly optimizes coupled limbs (lower residual than per-chain FABRIK).
	SwingTwistIK3D *ik = memnew(SwingTwistIK3D);
	ik->set_name("HumanoidRomIK");
	skeleton->add_child(ik);
	ik->set_owner(p_base_scene ? p_base_scene : skeleton);
	gen->setup_humanoid_chains(ik);
	gen->add_control_rig(ik); // IK-goal + pole effectors as Marker3D targets on the chains
	gen->apply_ik_limits(ik, phenotype); // apply LAST so nothing rebuilds the joints afterward
}

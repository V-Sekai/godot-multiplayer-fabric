/**************************************************************************/
/*  resonance_audio_material_map.cpp                                      */
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

#include "resonance_audio_material_map.h"

#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"

void ResonanceAudioMaterialMap::set_material_mapping(const String &p_material_path, ReverbProbeGI::WallMaterial p_acoustic) {
	material_mappings[p_material_path] = (int)p_acoustic;
	emit_changed();
}

ReverbProbeGI::WallMaterial ResonanceAudioMaterialMap::get_material_mapping(const String &p_material_path) const {
	if (material_mappings.has(p_material_path)) {
		return (ReverbProbeGI::WallMaterial)(int)material_mappings[p_material_path];
	}
	return default_material;
}

bool ResonanceAudioMaterialMap::has_material_mapping(const String &p_material_path) const {
	return material_mappings.has(p_material_path);
}

void ResonanceAudioMaterialMap::remove_material_mapping(const String &p_material_path) {
	material_mappings.erase(p_material_path);
	emit_changed();
}

void ResonanceAudioMaterialMap::set_default_material(ReverbProbeGI::WallMaterial p_material) {
	default_material = p_material;
	emit_changed();
}

void ResonanceAudioMaterialMap::set_material_mappings(const Dictionary &p_mappings) {
	material_mappings = p_mappings;
	emit_changed();
}

static void _scan_node_materials(Node *p_node, Dictionary &r_mappings, ReverbProbeGI::WallMaterial p_default) {
	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(p_node);
	if (mi && mi->get_mesh().is_valid()) {
		Ref<Mesh> mesh = mi->get_mesh();
		for (int s = 0; s < mesh->get_surface_count(); s++) {
			Ref<Material> mat = mi->get_active_material(s);
			if (mat.is_valid()) {
				String path = mat->get_path();
				if (path.is_empty()) {
					path = mat->get_name();
				}
				if (path.is_empty()) {
					path = vformat("unnamed_%d_%d", mi->get_instance_id(), s);
				}
				if (!r_mappings.has(path)) {
					r_mappings[path] = (int)p_default;
				}
			}
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_scan_node_materials(p_node->get_child(i), r_mappings, p_default);
	}
}

void ResonanceAudioMaterialMap::scan_scene(Node *p_root) {
	_scan_node_materials(p_root, material_mappings, default_material);
	emit_changed();
	notify_property_list_changed();
}

void ResonanceAudioMaterialMap::_scan_from_editor() {
	SceneTree *tree = Object::cast_to<SceneTree>(OS::get_singleton()->get_main_loop());
	if (!tree) {
		return;
	}
	Node *root = tree->get_edited_scene_root();
	if (!root) {
		root = tree->get_root();
	}
	if (root) {
		scan_scene(root);
	}
}

void ResonanceAudioMaterialMap::clear_mappings() {
	material_mappings.clear();
	emit_changed();
	notify_property_list_changed();
}

static const char *WALL_MATERIAL_ENUM_HINT = "Transparent,Acoustic Ceiling Tiles,Brick Bare,Brick Painted,Concrete Block Coarse,Concrete Block Painted,Curtain Heavy,Fiber Glass Insulation,Glass Thin,Glass Thick,Grass,Linoleum On Concrete,Marble,Metal,Parquet On Concrete,Plaster Rough,Plaster Smooth,Plywood Panel,Polished Concrete Or Tile,Sheetrock,Water Or Ice Surface,Wood Ceiling,Wood Panel,Uniform";

// Find full path in dictionary by matching filename.
static String _find_path_by_filename(const Dictionary &p_mappings, const String &p_filename) {
	LocalVector<Variant> keys = p_mappings.get_key_list();
	for (const Variant &key : keys) {
		String path = key;
		if (path.get_file() == p_filename) {
			return path;
		}
	}
	return String();
}

bool ResonanceAudioMaterialMap::_set(const StringName &p_name, const Variant &p_value) {
	String n = p_name;
	if (n.begins_with("Mappings/")) {
		String filename = n.substr(9);
		String full_path = _find_path_by_filename(material_mappings, filename);
		if (!full_path.is_empty()) {
			material_mappings[full_path] = (int)p_value;
			emit_changed();
			return true;
		}
	}
	return false;
}

bool ResonanceAudioMaterialMap::_get(const StringName &p_name, Variant &r_ret) const {
	String n = p_name;
	if (n == "Scan Scene Materials") {
		r_ret = Callable(const_cast<ResonanceAudioMaterialMap *>(this), "_scan_from_editor");
		return true;
	}
	if (n.begins_with("Mappings/")) {
		String filename = n.substr(9);
		String full_path = _find_path_by_filename(material_mappings, filename);
		if (!full_path.is_empty()) {
			r_ret = material_mappings[full_path];
			return true;
		}
	}
	return false;
}

void ResonanceAudioMaterialMap::_get_property_list(List<PropertyInfo> *p_list) const {
	p_list->push_back(PropertyInfo(Variant::CALLABLE, "Scan Scene Materials", PROPERTY_HINT_TOOL_BUTTON, "Scan Scene,Search", PROPERTY_USAGE_EDITOR));

	LocalVector<Variant> keys = material_mappings.get_key_list();
	for (const Variant &key : keys) {
		String mat_path = key;
		String display = mat_path.get_file();
		if (display.is_empty()) {
			display = mat_path;
		}
		p_list->push_back(PropertyInfo(Variant::INT, "Mappings/" + display, PROPERTY_HINT_ENUM, WALL_MATERIAL_ENUM_HINT, PROPERTY_USAGE_EDITOR));
	}
}

void ResonanceAudioMaterialMap::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_material_mapping", "material_path", "acoustic_material"), &ResonanceAudioMaterialMap::set_material_mapping);
	ClassDB::bind_method(D_METHOD("get_material_mapping", "material_path"), &ResonanceAudioMaterialMap::get_material_mapping);
	ClassDB::bind_method(D_METHOD("has_material_mapping", "material_path"), &ResonanceAudioMaterialMap::has_material_mapping);
	ClassDB::bind_method(D_METHOD("remove_material_mapping", "material_path"), &ResonanceAudioMaterialMap::remove_material_mapping);

	ClassDB::bind_method(D_METHOD("set_default_material", "material"), &ResonanceAudioMaterialMap::set_default_material);
	ClassDB::bind_method(D_METHOD("get_default_material"), &ResonanceAudioMaterialMap::get_default_material);

	ClassDB::bind_method(D_METHOD("set_material_mappings", "mappings"), &ResonanceAudioMaterialMap::set_material_mappings);
	ClassDB::bind_method(D_METHOD("get_material_mappings"), &ResonanceAudioMaterialMap::get_material_mappings);

	ClassDB::bind_method(D_METHOD("scan_scene", "root"), &ResonanceAudioMaterialMap::scan_scene);
	ClassDB::bind_method(D_METHOD("clear_mappings"), &ResonanceAudioMaterialMap::clear_mappings);
	ClassDB::bind_method(D_METHOD("_scan_from_editor"), &ResonanceAudioMaterialMap::_scan_from_editor);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "default_material", PROPERTY_HINT_ENUM, "Transparent,Acoustic Ceiling Tiles,Brick Bare,Brick Painted,Concrete Block Coarse,Concrete Block Painted,Curtain Heavy,Fiber Glass Insulation,Glass Thin,Glass Thick,Grass,Linoleum On Concrete,Marble,Metal,Parquet On Concrete,Plaster Rough,Plaster Smooth,Plywood Panel,Polished Concrete Or Tile,Sheetrock,Water Or Ice Surface,Wood Ceiling,Wood Panel,Uniform"), "set_default_material", "get_default_material");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "material_mappings", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL), "set_material_mappings", "get_material_mappings");
}

/**************************************************************************/
/*  reverb_probe_gi_editor_plugin.cpp                                     */
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

#include "reverb_probe_gi_editor_plugin.h"

#include "core/math/probe_octree.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/main/scene_tree.h"
#include "servers/resonanceaudio/resonance_audio_material_map.h"
#include "servers/resonanceaudio/reverb_probe_gi.h"

static void _collect_vertices(Node *p_node, Vector<Vector3> &r_vertices, Vector<int> &r_indices, AABB &r_bounds) {
	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(p_node);
	if (mi && mi->get_mesh().is_valid()) {
		Ref<Mesh> mesh = mi->get_mesh();
		Transform3D xform = mi->get_global_transform();
		for (int s = 0; s < mesh->get_surface_count(); s++) {
			if (mesh->surface_get_primitive_type(s) != Mesh::PRIMITIVE_TRIANGLES) {
				continue;
			}
			Array arrays = mesh->surface_get_arrays(s);
			if (arrays.size() == 0) {
				continue;
			}
			PackedVector3Array verts = arrays[Mesh::ARRAY_VERTEX];
			PackedInt32Array idx = arrays[Mesh::ARRAY_INDEX];
			int base = r_vertices.size();
			for (int v = 0; v < verts.size(); v++) {
				Vector3 world_v = xform.xform(verts[v]);
				r_vertices.push_back(world_v);
				if (r_vertices.size() == 1) {
					r_bounds.position = world_v;
				} else {
					r_bounds.expand_to(world_v);
				}
			}
			if (idx.size() > 0) {
				for (int t = 0; t < idx.size(); t++) {
					r_indices.push_back(base + idx[t]);
				}
			} else {
				for (int v = 0; v < verts.size(); v++) {
					r_indices.push_back(base + v);
				}
			}
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_vertices(p_node->get_child(i), r_vertices, r_indices, r_bounds);
	}
}

void ReverbProbeGIEditorPlugin::_bake() {
	if (!probe_gi || !get_tree()->get_edited_scene_root()) {
		EditorNode::get_singleton()->show_warning(TTR("No scene root found."));
		return;
	}

	const uint64_t time_started = OS::get_singleton()->get_ticks_msec();

	Node *from_node = probe_gi->get_parent() ? probe_gi->get_parent() : get_tree()->get_edited_scene_root();

	// Auto-scan scene materials into the material map if it exists.
	Ref<ResonanceAudioMaterialMap> mat_map = probe_gi->get_material_map();
	if (mat_map.is_valid()) {
		mat_map->scan_scene(from_node);
		print_line(vformat("Scanned %d visual materials.", mat_map->get_material_mappings().size()));
	}

	// Generate probe positions using ProbeOctree.
	Vector<Vector3> vertices;
	Vector<int> indices;
	AABB bounds;
	_collect_vertices(from_node, vertices, indices, bounds);

	if (indices.size() < 3) {
		EditorNode::get_singleton()->show_warning(TTR("No meshes found to bake reverb probes."));
		return;
	}

	PackedVector3Array probes = ProbeOctree::generate_probes(vertices, indices, bounds, 8);
	if (probes.size() == 0) {
		EditorNode::get_singleton()->show_warning(TTR("Failed to generate probe positions."));
		return;
	}

	print_line(vformat("Baking %d reverb probes...", probes.size()));

	ReverbProbeGI::BakeError err = probe_gi->bake(from_node, probes);

	const int time_taken = OS::get_singleton()->get_ticks_msec() - time_started;
	print_line(vformat("Done baking reverb probes in %d.%02ds.", time_taken / 1000, (time_taken % 1000) / 10));

	switch (err) {
		case ReverbProbeGI::BAKE_ERROR_NO_MESHES: {
			EditorNode::get_singleton()->show_warning(TTR("No meshes found to bake reverb probes."));
		} break;
		case ReverbProbeGI::BAKE_ERROR_NO_PROBES: {
			EditorNode::get_singleton()->show_warning(TTR("No probe positions provided."));
		} break;
		default:
			break;
	}
}

void ReverbProbeGIEditorPlugin::edit(Object *p_object) {
	probe_gi = Object::cast_to<ReverbProbeGI>(p_object);
}

bool ReverbProbeGIEditorPlugin::handles(Object *p_object) const {
	return p_object->is_class("ReverbProbeGI");
}

void ReverbProbeGIEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		bake->show();
	} else {
		bake->hide();
	}
}

void ReverbProbeGIEditorPlugin::_bind_methods() {
	ClassDB::bind_method("_bake", &ReverbProbeGIEditorPlugin::_bake);
}

ReverbProbeGIEditorPlugin::ReverbProbeGIEditorPlugin() {
	bake = memnew(Button);
	bake->set_theme_type_variation(SceneStringName(FlatButton));
	bake->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("Bake"), EditorStringName(EditorIcons)));
	bake->set_text(TTR("Bake Reverb Probes"));
	bake->hide();
	bake->connect(SceneStringName(pressed), Callable(this, "_bake"));
	add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, bake);
}

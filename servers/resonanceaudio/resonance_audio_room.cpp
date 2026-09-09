/**************************************************************************/
/*  resonance_audio_room.cpp                                              */
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

#include "resonance_audio_room.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "servers/resonanceaudio/resonance_audio_wrapper.h"
#ifndef PHYSICS_3D_DISABLED
#include "scene/3d/physics/collision_shape_3d.h"
#endif // PHYSICS_3D_DISABLED
#include "scene/main/scene_tree.h"

#define _USE_MATH_DEFINES
#include "platforms/common/room_effects_utils.h"
#include "platforms/common/room_properties.h"

#include <cmath>

void ResonanceAudioRoom::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_room_effects_enabled", "enabled"), &ResonanceAudioRoom::set_room_effects_enabled);
	ClassDB::bind_method(D_METHOD("get_room_effects_enabled"), &ResonanceAudioRoom::get_room_effects_enabled);
	ClassDB::bind_method(D_METHOD("set_reflection_gain", "gain"), &ResonanceAudioRoom::set_reflection_gain);
	ClassDB::bind_method(D_METHOD("get_reflection_gain"), &ResonanceAudioRoom::get_reflection_gain);
	ClassDB::bind_method(D_METHOD("set_reverb_gain", "gain"), &ResonanceAudioRoom::set_reverb_gain);
	ClassDB::bind_method(D_METHOD("get_reverb_gain"), &ResonanceAudioRoom::get_reverb_gain);
	ClassDB::bind_method(D_METHOD("set_reverb_brightness", "brightness"), &ResonanceAudioRoom::set_reverb_brightness);
	ClassDB::bind_method(D_METHOD("get_reverb_brightness"), &ResonanceAudioRoom::get_reverb_brightness);
	ClassDB::bind_method(D_METHOD("set_reverb_time_scale", "scale"), &ResonanceAudioRoom::set_reverb_time_scale);
	ClassDB::bind_method(D_METHOD("get_reverb_time_scale"), &ResonanceAudioRoom::get_reverb_time_scale);
	ClassDB::bind_method(D_METHOD("set_wall_material", "material"), &ResonanceAudioRoom::set_wall_material);
	ClassDB::bind_method(D_METHOD("get_wall_material"), &ResonanceAudioRoom::get_wall_material);
	ClassDB::bind_method(D_METHOD("update_room_from_colliders"), &ResonanceAudioRoom::update_room_from_colliders);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "room_effects_enabled"), "set_room_effects_enabled", "get_room_effects_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reflection_gain", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_reflection_gain", "get_reflection_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reverb_gain", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_reverb_gain", "get_reverb_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reverb_brightness", PROPERTY_HINT_RANGE, "-1,1,0.01"), "set_reverb_brightness", "get_reverb_brightness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reverb_time_scale", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_reverb_time_scale", "get_reverb_time_scale");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "wall_material", PROPERTY_HINT_RANGE, "0,11,1"), "set_wall_material", "get_wall_material");
}

void ResonanceAudioRoom::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			update_room_from_colliders();
		} break;
	}
}

void ResonanceAudioRoom::update_room_from_colliders() {
	ResonanceAudioServer *server = ResonanceAudioServer::get_singleton();
	if (!server) {
		return;
	}

	server->enable_room_effects(room_effects_enabled);
	if (!room_effects_enabled) {
		return;
	}

	// Collect AABB from all StaticBody3D in the scene (unless in resonance_audio_exclude group).
	AABB room_aabb;
	bool first = true;
	Node *root = get_tree() ? get_tree()->get_current_scene() : nullptr;
	if (!root) {
		return;
	}

#ifndef PHYSICS_3D_DISABLED
	TypedArray<Node> bodies = root->find_children("*", "StaticBody3D", true, false);
	for (int i = 0; i < bodies.size(); i++) {
		Node *node = Object::cast_to<Node>(bodies[i]);
		if (!node || node->is_in_group("resonance_audio_exclude")) {
			continue;
		}
		Node3D *body = Object::cast_to<Node3D>(node);
		if (!body) {
			continue;
		}
		for (int j = 0; j < body->get_child_count(); j++) {
			CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(body->get_child(j));
			if (!cs || cs->get_shape().is_null()) {
				continue;
			}
			AABB shape_aabb;
			Ref<Shape3D> shape = cs->get_shape();
			if (shape.is_valid()) {
				// Use shape's enclosing AABB via its debug mesh vertices.
				Vector<Vector3> faces = shape->get_debug_mesh_lines();
				for (int f = 0; f < faces.size(); f++) {
					if (f == 0) {
						shape_aabb.position = faces[f];
					} else {
						shape_aabb.expand_to(faces[f]);
					}
				}
			}
			shape_aabb = body->get_global_transform().xform(shape_aabb);
			if (first) {
				room_aabb = shape_aabb;
				first = false;
			} else {
				room_aabb = room_aabb.merge(shape_aabb);
			}
		}
	}
#endif // PHYSICS_3D_DISABLED

	// Without 3D physics there are no colliders to derive a room from, so
	// `first` stays true and the room is left untouched.
	if (first) {
		return;
	}

	// Build RoomProperties from AABB.
	vraudio::RoomProperties props;
	Vector3 center = room_aabb.get_center();
	Vector3 size = room_aabb.get_size();
	props.position[0] = center.x;
	props.position[1] = center.y;
	props.position[2] = center.z;
	props.rotation[0] = 0.0f;
	props.rotation[1] = 0.0f;
	props.rotation[2] = 0.0f;
	props.rotation[3] = 1.0f;
	props.dimensions[0] = size.x;
	props.dimensions[1] = size.y;
	props.dimensions[2] = size.z;
	vraudio::MaterialName mat = static_cast<vraudio::MaterialName>(CLAMP(wall_material, 0, (int)vraudio::MaterialName::kUniform));
	for (int i = 0; i < 6; i++) {
		props.material_names[i] = mat;
	}
	props.reflection_scalar = reflection_gain;
	props.reverb_gain = reverb_gain;
	props.reverb_time = reverb_time_scale;
	props.reverb_brightness = reverb_brightness;

	// Compute and apply reflection + reverb properties.
	vraudio::ReflectionProperties refl = vraudio::ComputeReflectionProperties(props);
	vraudio::ReverbProperties reverb = vraudio::ComputeReverbProperties(props);

	server->set_reflection_properties(refl.gain);
	float rt60s[9];
	for (int i = 0; i < 9; i++) {
		rt60s[i] = reverb.rt60_values[i];
	}
	server->set_reverb_properties(rt60s, reverb.gain);
}

void ResonanceAudioRoom::set_room_effects_enabled(bool p_enabled) {
	room_effects_enabled = p_enabled;
}
bool ResonanceAudioRoom::get_room_effects_enabled() const {
	return room_effects_enabled;
}

void ResonanceAudioRoom::set_reflection_gain(float p_gain) {
	reflection_gain = p_gain;
}
float ResonanceAudioRoom::get_reflection_gain() const {
	return reflection_gain;
}

void ResonanceAudioRoom::set_reverb_gain(float p_gain) {
	reverb_gain = p_gain;
}
float ResonanceAudioRoom::get_reverb_gain() const {
	return reverb_gain;
}

void ResonanceAudioRoom::set_reverb_brightness(float p_brightness) {
	reverb_brightness = p_brightness;
}
float ResonanceAudioRoom::get_reverb_brightness() const {
	return reverb_brightness;
}

void ResonanceAudioRoom::set_reverb_time_scale(float p_scale) {
	reverb_time_scale = p_scale;
}
float ResonanceAudioRoom::get_reverb_time_scale() const {
	return reverb_time_scale;
}

void ResonanceAudioRoom::set_wall_material(int p_material) {
	wall_material = p_material;
}
int ResonanceAudioRoom::get_wall_material() const {
	return wall_material;
}

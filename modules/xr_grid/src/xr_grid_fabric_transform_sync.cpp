/**************************************************************************/
/*  xr_grid_fabric_transform_sync.cpp                                     */
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

#include "xr_grid_fabric_transform_sync.h"

#include "xr_grid_entity_packet.h"
#include "xr_grid_fabric_manager.h"

#include "core/object/class_db.h"

XRGridFabricManager *XRGridFabricTransformSync::_resolve_manager() const {
	if (!fabric_manager_path.is_empty()) {
		Node *n = get_node_or_null(fabric_manager_path);
		if (n) {
			return Object::cast_to<XRGridFabricManager>(n);
		}
	}
	Node *autoload = get_node_or_null(NodePath("/root/FabricManager"));
	return autoload ? Object::cast_to<XRGridFabricManager>(autoload) : nullptr;
}

void XRGridFabricTransformSync::apply_remote(const Dictionary &p_decoded) {
	if (target == nullptr) {
		return;
	}
	const Vector3 pos = p_decoded.get("position", Vector3());
	const Quaternion rot = p_decoded.get("rotation", Quaternion());
	target->set_global_position(pos);
	Transform3D xform = target->get_global_transform();
	xform.basis = Basis(rot);
	target->set_global_transform(xform);
}

void XRGridFabricTransformSync::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			target = Object::cast_to<Node3D>(get_parent());
			ERR_FAIL_NULL_MSG(target,
					"XRGridFabricTransformSync must be a child of Node3D.");
			set_process(true);
		} break;
		case NOTIFICATION_PROCESS: {
			if (!is_local) {
				return;
			}
			XRGridFabricManager *mgr = _resolve_manager();
			if (mgr == nullptr ||
					mgr->get_state() != XRGridFabricManager::STATE_CONNECTED) {
				return;
			}
			send_timer += get_process_delta_time();
			const double interval = 1.0 / send_rate_hz;
			if (send_timer < interval) {
				return;
			}
			send_timer -= interval;
			const int64_t pid_safe =
					mgr->get_local_player_id() % XRGridEntityPacket::MAX_PLAYER_ID;
			global_id = XRGridEntityPacket::PLAYER_ENTITY_BASE +
					pid_safe * 3 + sub_index;
			const Transform3D t = target ? target->get_global_transform() : Transform3D();
			const PackedByteArray pkt = XRGridEntityPacket::encode(
					global_id,
					t.origin,
					Vector3(),
					t.basis.get_rotation_quaternion(),
					entity_class,
					int(mgr->get_local_player_id()),
					mgr->get_frame_counter(),
					mgr->get_hlc_counter(),
					sub_index);
			mgr->increment_hlc_counter();
			mgr->send_entity(pkt);
			++send_count;
		} break;
		default:
			break;
	}
}

void XRGridFabricTransformSync::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_entity_class", "v"),
			&XRGridFabricTransformSync::set_entity_class);
	ClassDB::bind_method(D_METHOD("get_entity_class"),
			&XRGridFabricTransformSync::get_entity_class);
	ClassDB::bind_method(D_METHOD("set_sub_index", "v"),
			&XRGridFabricTransformSync::set_sub_index);
	ClassDB::bind_method(D_METHOD("get_sub_index"),
			&XRGridFabricTransformSync::get_sub_index);
	ClassDB::bind_method(D_METHOD("set_send_rate_hz", "v"),
			&XRGridFabricTransformSync::set_send_rate_hz);
	ClassDB::bind_method(D_METHOD("get_send_rate_hz"),
			&XRGridFabricTransformSync::get_send_rate_hz);
	ClassDB::bind_method(D_METHOD("set_is_local", "v"),
			&XRGridFabricTransformSync::set_is_local);
	ClassDB::bind_method(D_METHOD("get_is_local"),
			&XRGridFabricTransformSync::get_is_local);
	ClassDB::bind_method(D_METHOD("set_fabric_manager_path", "p"),
			&XRGridFabricTransformSync::set_fabric_manager_path);
	ClassDB::bind_method(D_METHOD("get_fabric_manager_path"),
			&XRGridFabricTransformSync::get_fabric_manager_path);
	ClassDB::bind_method(D_METHOD("apply_remote", "decoded"),
			&XRGridFabricTransformSync::apply_remote);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "entity_class"),
			"set_entity_class", "get_entity_class");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sub_index"),
			"set_sub_index", "get_sub_index");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "send_rate_hz"),
			"set_send_rate_hz", "get_send_rate_hz");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_local"),
			"set_is_local", "get_is_local");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "fabric_manager_path"),
			"set_fabric_manager_path", "get_fabric_manager_path");
}

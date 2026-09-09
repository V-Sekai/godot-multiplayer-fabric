/**************************************************************************/
/*  xr_grid_fabric_manager.cpp                                            */
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

#include "xr_grid_fabric_manager.h"

#include "core/object/class_db.h"

void XRGridFabricManager::set_peer(const Ref<MultiplayerPeer> &p_peer) {
	peer = p_peer;
	if (peer.is_valid()) {
		state = STATE_CONNECTING;
	} else {
		state = STATE_DISCONNECTED;
	}
	emit_signal("connection_state_changed", state);
}

Error XRGridFabricManager::connect_to_zone(const String &p_address, int p_port) {
	// WebTransportPeer is provided by modules/http3. Resolve it
	// dynamically via ClassDB so this module doesn't take a hard build
	// dependency on modules/http3 — if http3 isn't compiled in, the
	// instantiate call returns null and we surface a clear error.
	Object *raw = ClassDB::instantiate("WebTransportPeer");
	ERR_FAIL_NULL_V_MSG(raw, ERR_UNAVAILABLE,
			"XRGridFabricManager::connect_to_zone — WebTransportPeer "
			"is not registered. Enable modules/http3 in the build or "
			"call set_peer with another MultiplayerPeer subclass.");
	MultiplayerPeer *new_peer = Object::cast_to<MultiplayerPeer>(raw);
	if (new_peer == nullptr) {
		memdelete(raw);
		ERR_FAIL_V_MSG(ERR_INVALID_DATA,
				"WebTransportPeer registered but does not inherit "
				"MultiplayerPeer — cannot use it as the fabric peer.");
	}
	Ref<MultiplayerPeer> peer_ref;
	peer_ref.reference_ptr(new_peer);
	// upstream xr-grid calls create_client(address, port, "/wt"). The
	// signature is part of the http3 module; route through Object::call
	// so we don't need to include its header.
	Array args;
	args.push_back(p_address);
	args.push_back(p_port);
	args.push_back("/wt");
	const Variant rv = new_peer->callv("create_client", args);
	const int err_code = int(rv);
	if (err_code != OK) {
		ERR_FAIL_V_MSG(Error(err_code),
				vformat("WebTransportPeer::create_client failed (%d) for %s:%d",
						err_code, p_address, p_port));
	}
	set_peer(peer_ref);
	return OK;
}

void XRGridFabricManager::send_entity(const PackedByteArray &p_packet) {
	if (peer.is_null() || state != STATE_CONNECTED) {
		return;
	}
	peer->set_target_peer(0);
	peer->set_transfer_mode(MultiplayerPeer::TRANSFER_MODE_UNRELIABLE);
	peer->put_packet(p_packet.ptr(), p_packet.size());
}

void XRGridFabricManager::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PROCESS: {
			if (peer.is_null()) {
				return;
			}
			peer->poll();
			if (state == STATE_CONNECTING) {
				if (peer->get_connection_status() ==
						MultiplayerPeer::CONNECTION_CONNECTED) {
					local_player_id = peer->get_unique_id();
					state = STATE_CONNECTED;
					emit_signal("connection_state_changed", state);
				}
			}
			if (state != STATE_CONNECTED) {
				return;
			}
			++frame_counter;
			hlc_counter = 0;
			while (peer->get_available_packet_count() > 0) {
				const uint8_t *packet_data = nullptr;
				int len = 0;
				if (peer->get_packet(&packet_data, len) != OK || len <= 0) {
					break;
				}
				if (len == 100) {
					PackedByteArray pkt;
					pkt.resize(len);
					memcpy(pkt.ptrw(), packet_data, len);
					emit_signal("entity_received", pkt);
				}
			}
		} break;
		case NOTIFICATION_READY:
			set_process(true);
			break;
		default:
			break;
	}
}

void XRGridFabricManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_peer", "peer"), &XRGridFabricManager::set_peer);
	ClassDB::bind_method(D_METHOD("get_peer"), &XRGridFabricManager::get_peer);
	ClassDB::bind_method(D_METHOD("connect_to_zone", "address", "port"),
			&XRGridFabricManager::connect_to_zone);
	ClassDB::bind_method(D_METHOD("send_entity", "packet"), &XRGridFabricManager::send_entity);
	ClassDB::bind_method(D_METHOD("get_state"), &XRGridFabricManager::get_state);
	ClassDB::bind_method(D_METHOD("get_local_player_id"), &XRGridFabricManager::get_local_player_id);
	ClassDB::bind_method(D_METHOD("get_frame_counter"), &XRGridFabricManager::get_frame_counter);
	ClassDB::bind_method(D_METHOD("get_hlc_counter"), &XRGridFabricManager::get_hlc_counter);
	ClassDB::bind_method(D_METHOD("increment_hlc_counter"),
			&XRGridFabricManager::increment_hlc_counter);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "peer",
						 PROPERTY_HINT_RESOURCE_TYPE, "MultiplayerPeer"),
			"set_peer", "get_peer");

	ADD_SIGNAL(MethodInfo("entity_received",
			PropertyInfo(Variant::PACKED_BYTE_ARRAY, "packet")));
	ADD_SIGNAL(MethodInfo("connection_state_changed",
			PropertyInfo(Variant::INT, "state")));

	BIND_ENUM_CONSTANT(STATE_DISCONNECTED);
	BIND_ENUM_CONSTANT(STATE_CONNECTING);
	BIND_ENUM_CONSTANT(STATE_CONNECTED);
}

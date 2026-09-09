/**************************************************************************/
/*  xr_grid_fabric_manager.h                                              */
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
#include "core/variant/typed_array.h"
#include "scene/main/multiplayer_peer.h"
#include "scene/main/node.h"

class XRGridFabricManager : public Node {
	GDCLASS(XRGridFabricManager, Node);

public:
	enum State {
		STATE_DISCONNECTED = 0,
		STATE_CONNECTING = 1,
		STATE_CONNECTED = 2,
	};

private:
	State state = STATE_DISCONNECTED;
	int64_t local_player_id = 0;
	int frame_counter = 0;
	int hlc_counter = 0;
	Ref<MultiplayerPeer> peer;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	XRGridFabricManager() = default;

	// Engine-agnostic peer attachment. Pass any MultiplayerPeer
	// (ENetMultiplayerPeer, future WebTransportPeer, etc.).
	void set_peer(const Ref<MultiplayerPeer> &p_peer);
	Ref<MultiplayerPeer> get_peer() const { return peer; }

	// Upstream-compatible entry. Currently errors out because the engine
	// fork has no WebTransportPeer class. Kept so existing call sites
	// surface a clear error instead of silently disconnecting.
	Error connect_to_zone(const String &p_address, int p_port);

	void send_entity(const PackedByteArray &p_packet);

	State get_state() const { return state; }
	int64_t get_local_player_id() const { return local_player_id; }
	int get_frame_counter() const { return frame_counter; }
	int get_hlc_counter() const { return hlc_counter; }

	// FabricTransformSync calls this after stamping a packet.
	void increment_hlc_counter() { ++hlc_counter; }
};

VARIANT_ENUM_CAST(XRGridFabricManager::State);

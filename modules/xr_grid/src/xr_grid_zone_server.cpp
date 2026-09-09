/**************************************************************************/
/*  xr_grid_zone_server.cpp                                               */
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

#include "xr_grid_zone_server.h"

#include "core/object/class_db.h"

void XRGridZoneServer::start(const Ref<MultiplayerPeer> &p_server_peer) {
	ERR_FAIL_COND_MSG(p_server_peer.is_null(),
			"XRGridZoneServer::start — peer is null.");
	server_peer = p_server_peer;
	running = true;
	set_process(true);
}

void XRGridZoneServer::stop() {
	running = false;
	set_process(false);
	server_peer.unref();
}

void XRGridZoneServer::_notification(int p_what) {
	if (p_what != NOTIFICATION_PROCESS) {
		return;
	}
	if (!running || server_peer.is_null()) {
		return;
	}
	server_peer->poll();
	while (server_peer->get_available_packet_count() > 0) {
		const uint8_t *packet_data = nullptr;
		int len = 0;
		if (server_peer->get_packet(&packet_data, len) != OK || len <= 0) {
			break;
		}
		if (len == 100) {
			server_peer->set_target_peer(0);
			server_peer->set_transfer_mode(MultiplayerPeer::TRANSFER_MODE_UNRELIABLE);
			server_peer->put_packet(packet_data, len);
			++relay_count;
		}
	}
}

void XRGridZoneServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start", "server_peer"),
			&XRGridZoneServer::start);
	ClassDB::bind_method(D_METHOD("stop"), &XRGridZoneServer::stop);
	ClassDB::bind_method(D_METHOD("is_running"), &XRGridZoneServer::is_running);
	ClassDB::bind_method(D_METHOD("get_relay_count"),
			&XRGridZoneServer::get_relay_count);
}

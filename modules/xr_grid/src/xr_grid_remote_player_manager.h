/**************************************************************************/
/*  xr_grid_remote_player_manager.h                                       */
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
#include "core/templates/hash_map.h"
#include "scene/main/node.h"

class XRGridRemotePlayer;

class XRGridRemotePlayerManager : public Node {
	GDCLASS(XRGridRemotePlayerManager, Node);

	HashMap<int64_t, XRGridRemotePlayer *> players;
	HashMap<int64_t, double> last_seen;
	NodePath fabric_manager_path;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	XRGridRemotePlayerManager() = default;

	static constexpr double TIMEOUT_SEC = 10.0;

	void set_fabric_manager_path(const NodePath &p_path) { fabric_manager_path = p_path; }
	NodePath get_fabric_manager_path() const { return fabric_manager_path; }

	int get_remote_player_count() const { return int(players.size()); }

	// Signal target — also publicly callable.
	void on_entity_received(const PackedByteArray &p_packet);
};

/**************************************************************************/
/*  xr_grid_zone_scene_tree.h                                             */
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

#include "scene/main/multiplayer_peer.h"
#include "scene/main/scene_tree.h"

class XRGridZoneSceneTree : public SceneTree {
	GDCLASS(XRGridZoneSceneTree, SceneTree);

	Ref<MultiplayerPeer> server_peer;
	int zone_port = 9000;
	bool started = false;
	bool aborted = false;
	int64_t relay_count = 0;

	void _parse_cmdline();
	bool _try_start_server();

protected:
	static void _bind_methods();

public:
	XRGridZoneSceneTree() = default;

	// SceneTree overrides — these run the main loop directly when this
	// class is selected as the main loop on engine boot.
	virtual void initialize() override;
	virtual bool process(double p_time) override;
	virtual void finalize() override;

	int get_zone_port() const { return zone_port; }
	int64_t get_relay_count() const { return relay_count; }
	bool is_running() const { return started && !aborted; }
};

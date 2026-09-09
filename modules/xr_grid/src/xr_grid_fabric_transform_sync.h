/**************************************************************************/
/*  xr_grid_fabric_transform_sync.h                                       */
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
#include "core/variant/dictionary.h"
#include "scene/3d/node_3d.h"

class XRGridFabricManager;

class XRGridFabricTransformSync : public Node {
	GDCLASS(XRGridFabricTransformSync, Node);

	int entity_class = 1;
	int sub_index = 0;
	double send_rate_hz = 30.0;
	bool is_local = true;
	NodePath fabric_manager_path;

	Node3D *target = nullptr;
	int64_t global_id = 0;
	double send_timer = 0.0;
	int send_count = 0;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	XRGridFabricTransformSync() = default;

	void set_entity_class(int p_v) { entity_class = p_v; }
	int get_entity_class() const { return entity_class; }
	void set_sub_index(int p_v) { sub_index = p_v; }
	int get_sub_index() const { return sub_index; }
	void set_send_rate_hz(double p_v) { send_rate_hz = MAX(p_v, 0.001); }
	double get_send_rate_hz() const { return send_rate_hz; }
	void set_is_local(bool p_v) { is_local = p_v; }
	bool get_is_local() const { return is_local; }
	void set_fabric_manager_path(const NodePath &p_path) { fabric_manager_path = p_path; }
	NodePath get_fabric_manager_path() const { return fabric_manager_path; }

	// Replay a decoded packet onto the parent Node3D's transform.
	void apply_remote(const Dictionary &p_decoded);

private:
	XRGridFabricManager *_resolve_manager() const;
};

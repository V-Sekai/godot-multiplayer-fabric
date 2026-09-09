/**************************************************************************/
/*  xr_grid_xr_origin.cpp                                                 */
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

#include "xr_grid_xr_origin.h"

#include "xr_grid_fabric_manager.h"

#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/main/viewport.h"
#include "servers/xr/xr_server.h"

void XRGridXROrigin::_notification(int p_what) {
	if (p_what != NOTIFICATION_READY) {
		return;
	}
	interface = XRServer::get_singleton()->find_interface("OpenXR");
	if (interface.is_valid() && interface->is_initialized()) {
		vr_supported = true;
		Viewport *vp = get_viewport();
		if (vp) {
			vp->set_use_xr(true);
		}
	}
	// CLI override: --fabric-server=host:port
	const List<String> args = OS::get_singleton()->get_cmdline_args();
	for (const String &arg : args) {
		if (arg.begins_with("--fabric-server=")) {
			const String spec = arg.split("=", true, 1)[1];
			const Vector<String> parts = spec.split(":");
			if (parts.size() > 0) {
				fabric_address = parts[0];
			}
			if (parts.size() > 1) {
				fabric_port = parts[1].to_int();
			}
		}
	}
	// Connect to the zone via FabricManager if findable.
	Node *fm_node = fabric_manager_path.is_empty()
			? get_node_or_null(NodePath("/root/FabricManager"))
			: get_node_or_null(fabric_manager_path);
	XRGridFabricManager *fm = fm_node ? Object::cast_to<XRGridFabricManager>(fm_node) : nullptr;
	if (fm) {
		fm->connect_to_zone(fabric_address, fabric_port);
	}
}

void XRGridXROrigin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_fabric_address", "address"),
			&XRGridXROrigin::set_fabric_address);
	ClassDB::bind_method(D_METHOD("get_fabric_address"),
			&XRGridXROrigin::get_fabric_address);
	ClassDB::bind_method(D_METHOD("set_fabric_port", "port"),
			&XRGridXROrigin::set_fabric_port);
	ClassDB::bind_method(D_METHOD("get_fabric_port"),
			&XRGridXROrigin::get_fabric_port);
	ClassDB::bind_method(D_METHOD("set_fabric_manager_path", "path"),
			&XRGridXROrigin::set_fabric_manager_path);
	ClassDB::bind_method(D_METHOD("get_fabric_manager_path"),
			&XRGridXROrigin::get_fabric_manager_path);
	ClassDB::bind_method(D_METHOD("is_vr_supported"),
			&XRGridXROrigin::is_vr_supported);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "fabric_address"),
			"set_fabric_address", "get_fabric_address");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "fabric_port"),
			"set_fabric_port", "get_fabric_port");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "fabric_manager_path"),
			"set_fabric_manager_path", "get_fabric_manager_path");
}

/**************************************************************************/
/*  xr_grid_vsk_version.cpp                                               */
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

#include "xr_grid_vsk_version.h"

#include "core/object/class_db.h"

String XRGridVskVersion::get_build_label() const {
	return build_date_str + "\n" + build_label;
}

void XRGridVskVersion::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_build_label", "label"),
			&XRGridVskVersion::set_build_label);
	ClassDB::bind_method(D_METHOD("get_build_label_raw"),
			&XRGridVskVersion::get_build_label_raw);
	ClassDB::bind_method(D_METHOD("set_build_date_str", "date"),
			&XRGridVskVersion::set_build_date_str);
	ClassDB::bind_method(D_METHOD("get_build_date_str"),
			&XRGridVskVersion::get_build_date_str);
	ClassDB::bind_method(D_METHOD("set_build_unix_time", "t"),
			&XRGridVskVersion::set_build_unix_time);
	ClassDB::bind_method(D_METHOD("get_build_unix_time"),
			&XRGridVskVersion::get_build_unix_time);
	ClassDB::bind_method(D_METHOD("get_build_label"),
			&XRGridVskVersion::get_build_label);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "build_label"),
			"set_build_label", "get_build_label_raw");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "build_date_str"),
			"set_build_date_str", "get_build_date_str");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "build_unix_time"),
			"set_build_unix_time", "get_build_unix_time");
}

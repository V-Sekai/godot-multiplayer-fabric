/**************************************************************************/
/*  xr_grid_vsk_version.h                                                 */
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
#include "core/string/ustring.h"

class XRGridVskVersion : public RefCounted {
	GDCLASS(XRGridVskVersion, RefCounted);

	String build_label = "DEVELOPER_BUILD";
	String build_date_str = "Build Date";
	int64_t build_unix_time = -1;

protected:
	static void _bind_methods();

public:
	XRGridVskVersion() = default;

	void set_build_label(const String &p_label) { build_label = p_label; }
	String get_build_label_raw() const { return build_label; }

	void set_build_date_str(const String &p_date) { build_date_str = p_date; }
	String get_build_date_str() const { return build_date_str; }

	void set_build_unix_time(int64_t p_t) { build_unix_time = p_t; }
	int64_t get_build_unix_time() const { return build_unix_time; }

	// Concatenated "<build_date_str>\n<build_label>" — matches the
	// upstream get_build_label() return shape.
	String get_build_label() const;
};

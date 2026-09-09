/**************************************************************************/
/*  cassie_surface_constraint.h                                           */
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

#include "core/io/resource.h"
#include "core/math/vector3.h"

// CassieSurfaceConstraint records that the user's stroke was drawing on a
// surface patch — a dual-phase constraint with an entry (start) position
// and an optional exit (end) position when the user left mid-stroke.
// Port of E:\cassie\Assets\Scripts\Data\Constraints\SurfaceConstraint.cs.
//
// SurfaceConstraint is NOT a Constraint subclass in Unity, so the C++ port
// keeps it as a standalone Resource.

class CassieSurfaceConstraint : public Resource {
	GDCLASS(CassieSurfaceConstraint, Resource);

	int patch_id = -1;
	Vector3 start_position;
	bool left_mid_stroke = false;
	Vector3 end_position;

protected:
	static void _bind_methods();

public:
	CassieSurfaceConstraint() = default;

	void set_patch_id(int p_id) { patch_id = p_id; }
	int get_patch_id() const { return patch_id; }

	void set_start_position(const Vector3 &p_pos) { start_position = p_pos; }
	Vector3 get_start_position() const { return start_position; }

	bool has_left_mid_stroke() const { return left_mid_stroke; }

	void set_end_position(const Vector3 &p_pos) { end_position = p_pos; }
	Vector3 get_end_position() const { return end_position; }

	// Marks the stroke as having left the surface and records the exit point.
	void leave(const Vector3 &p_position);
};

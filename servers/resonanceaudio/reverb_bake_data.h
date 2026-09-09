/**************************************************************************/
/*  reverb_bake_data.h                                                    */
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
#include "core/math/aabb.h"
#include "core/variant/variant.h"

class ReverbBakeData : public Resource {
	GDCLASS(ReverbBakeData, Resource);
	RES_BASE_EXTENSION("rvbbake")

	static constexpr int NUM_REVERB_BANDS = 9;

	AABB bounds;
	PackedVector3Array probe_positions;
	PackedFloat32Array rt60_values; // probe_count * NUM_REVERB_BANDS
	PackedFloat32Array gains;
	PackedInt32Array tetrahedra; // 4 ints per tetrahedron
	PackedInt32Array bsp_tree;

protected:
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;
	static void _bind_methods();

public:
	void set_data(const AABB &p_bounds, const PackedVector3Array &p_positions,
			const PackedFloat32Array &p_rt60, const PackedFloat32Array &p_gains,
			const PackedInt32Array &p_tetrahedra, const PackedInt32Array &p_bsp_tree);

	AABB get_bounds() const { return bounds; }
	PackedVector3Array get_probe_positions() const { return probe_positions; }
	PackedFloat32Array get_rt60_values() const { return rt60_values; }
	PackedFloat32Array get_gains() const { return gains; }
	PackedInt32Array get_tetrahedra() const { return tetrahedra; }
	PackedInt32Array get_bsp_tree() const { return bsp_tree; }
	int get_probe_count() const { return probe_positions.size(); }

	bool lookup_reverb(const Vector3 &p_position, float r_rt60[NUM_REVERB_BANDS], float &r_gain) const;

	ReverbBakeData() {}
};

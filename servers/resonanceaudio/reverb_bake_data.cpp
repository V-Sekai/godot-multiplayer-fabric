/**************************************************************************/
/*  reverb_bake_data.cpp                                                  */
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

#include "reverb_bake_data.h"

#include "core/math/geometry_3d.h"
#include "core/object/class_db.h"

bool ReverbBakeData::_set(const StringName &p_name, const Variant &p_value) {
	String n = p_name;
	if (n == "bounds") {
		bounds = p_value;
	} else if (n == "probe_positions") {
		probe_positions = p_value;
	} else if (n == "rt60_values") {
		rt60_values = p_value;
	} else if (n == "gains") {
		gains = p_value;
	} else if (n == "tetrahedra") {
		tetrahedra = p_value;
	} else if (n == "bsp_tree") {
		bsp_tree = p_value;
	} else {
		return false;
	}
	return true;
}

bool ReverbBakeData::_get(const StringName &p_name, Variant &r_ret) const {
	String n = p_name;
	if (n == "bounds") {
		r_ret = bounds;
	} else if (n == "probe_positions") {
		r_ret = probe_positions;
	} else if (n == "rt60_values") {
		r_ret = rt60_values;
	} else if (n == "gains") {
		r_ret = gains;
	} else if (n == "tetrahedra") {
		r_ret = tetrahedra;
	} else if (n == "bsp_tree") {
		r_ret = bsp_tree;
	} else {
		return false;
	}
	return true;
}

void ReverbBakeData::_get_property_list(List<PropertyInfo> *p_list) const {
	p_list->push_back(PropertyInfo(Variant::AABB, "bounds", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
	p_list->push_back(PropertyInfo(Variant::PACKED_VECTOR3_ARRAY, "probe_positions", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
	p_list->push_back(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "rt60_values", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
	p_list->push_back(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "gains", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
	p_list->push_back(PropertyInfo(Variant::PACKED_INT32_ARRAY, "tetrahedra", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
	p_list->push_back(PropertyInfo(Variant::PACKED_INT32_ARRAY, "bsp_tree", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
}

void ReverbBakeData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_bounds"), &ReverbBakeData::get_bounds);
	ClassDB::bind_method(D_METHOD("get_probe_positions"), &ReverbBakeData::get_probe_positions);
	ClassDB::bind_method(D_METHOD("get_rt60_values"), &ReverbBakeData::get_rt60_values);
	ClassDB::bind_method(D_METHOD("get_gains"), &ReverbBakeData::get_gains);
	ClassDB::bind_method(D_METHOD("get_tetrahedra"), &ReverbBakeData::get_tetrahedra);
	ClassDB::bind_method(D_METHOD("get_bsp_tree"), &ReverbBakeData::get_bsp_tree);
	ClassDB::bind_method(D_METHOD("get_probe_count"), &ReverbBakeData::get_probe_count);
}

void ReverbBakeData::set_data(const AABB &p_bounds, const PackedVector3Array &p_positions,
		const PackedFloat32Array &p_rt60, const PackedFloat32Array &p_gains,
		const PackedInt32Array &p_tetrahedra, const PackedInt32Array &p_bsp_tree) {
	bounds = p_bounds;
	probe_positions = p_positions;
	rt60_values = p_rt60;
	gains = p_gains;
	tetrahedra = p_tetrahedra;
	bsp_tree = p_bsp_tree;
}

bool ReverbBakeData::lookup_reverb(const Vector3 &p_position, float r_rt60[NUM_REVERB_BANDS], float &r_gain) const {
	if (probe_positions.size() == 0) {
		return false;
	}

	// Nearest-probe fallback when BSP tree is not built.
	if (bsp_tree.size() == 0) {
		const Vector3 *positions = probe_positions.ptr();
		const float *rt60_ptr = rt60_values.ptr();
		const float *gains_ptr = gains.ptr();
		int best = 0;
		float best_dist = positions[0].distance_squared_to(p_position);
		for (int i = 1; i < probe_positions.size(); i++) {
			float d = positions[i].distance_squared_to(p_position);
			if (d < best_dist) {
				best_dist = d;
				best = i;
			}
		}
		for (int b = 0; b < NUM_REVERB_BANDS; b++) {
			r_rt60[b] = rt60_ptr[best * NUM_REVERB_BANDS + b];
		}
		r_gain = gains_ptr[best];
		return true;
	}

	// Walk BSP tree to find containing tetrahedron.
	// BSP layout: 6 int32s per node [plane.normal.x, plane.normal.y, plane.normal.z, plane.d, over, under]
	// Encoded as floats in the first 4 slots via memcpy, ints in slots 4-5.
	const int32_t *bsp = bsp_tree.ptr();
	int node = 0;

	while (node >= 0 && node < bsp_tree.size() / 6) {
		int base = node * 6;
		float nx, ny, nz, d;
		memcpy(&nx, &bsp[base + 0], sizeof(float));
		memcpy(&ny, &bsp[base + 1], sizeof(float));
		memcpy(&nz, &bsp[base + 2], sizeof(float));
		memcpy(&d, &bsp[base + 3], sizeof(float));

		Plane plane(Vector3(nx, ny, nz), d);
		if (plane.is_point_over(p_position)) {
			node = bsp[base + 4];
		} else {
			node = bsp[base + 5];
		}
	}

	if (node >= 0) {
		return false;
	}

	int simplex_idx = Math::abs(node) - 1;
	int tet_base = simplex_idx * 4;
	if (tet_base + 3 >= tetrahedra.size()) {
		return false;
	}

	const int32_t *tets = tetrahedra.ptr();
	int v0 = tets[tet_base + 0];
	int v1 = tets[tet_base + 1];
	int v2 = tets[tet_base + 2];
	int v3 = tets[tet_base + 3];

	const Vector3 *positions = probe_positions.ptr();
	Color bary = Geometry3D::tetrahedron_get_barycentric_coords(
			positions[v0], positions[v1], positions[v2], positions[v3], p_position);

	float w[4] = {
		CLAMP(bary.r, 0.0f, 1.0f),
		CLAMP(bary.g, 0.0f, 1.0f),
		CLAMP(bary.b, 0.0f, 1.0f),
		CLAMP(bary.a, 0.0f, 1.0f)
	};

	const float *rt60_ptr = rt60_values.ptr();
	const float *gains_ptr = gains.ptr();
	int verts[4] = { v0, v1, v2, v3 };

	for (int b = 0; b < NUM_REVERB_BANDS; b++) {
		r_rt60[b] = 0.0f;
	}
	r_gain = 0.0f;

	for (int i = 0; i < 4; i++) {
		int probe_idx = verts[i];
		if (probe_idx >= probe_positions.size()) {
			continue;
		}
		for (int b = 0; b < NUM_REVERB_BANDS; b++) {
			r_rt60[b] += w[i] * rt60_ptr[probe_idx * NUM_REVERB_BANDS + b];
		}
		r_gain += w[i] * gains_ptr[probe_idx];
	}

	return true;
}

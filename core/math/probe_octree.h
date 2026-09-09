/**************************************************************************/
/*  probe_octree.h                                                        */
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

#include "core/math/aabb.h"
#include "core/math/geometry_3d.h"
#include "core/math/vector3i.h"
#include "core/os/memory.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"

class ProbeOctree {
public:
	struct Cell {
		Vector3i offset;
		uint32_t size = 0;
		Cell *children[8] = {};
		~Cell() {
			for (int i = 0; i < 8; i++) {
				if (children[i] != nullptr) {
					memdelete(children[i]);
				}
			}
		}
	};

	static void plot_triangle(Cell *p_cell, float p_cell_size, const Vector3 *p_triangle) {
		for (int i = 0; i < 8; i++) {
			Vector3i pos = p_cell->offset;
			uint32_t half_size = p_cell->size / 2;
			if (i & 1) {
				pos.x += half_size;
			}
			if (i & 2) {
				pos.y += half_size;
			}
			if (i & 4) {
				pos.z += half_size;
			}

			AABB subcell;
			subcell.position = Vector3(pos) * p_cell_size;
			subcell.size = Vector3(half_size, half_size, half_size) * p_cell_size;

			if (!Geometry3D::triangle_box_overlap(subcell.get_center(), subcell.size * 0.5, p_triangle)) {
				continue;
			}

			if (p_cell->children[i] == nullptr) {
				Cell *child = memnew(Cell);
				child->offset = pos;
				child->size = half_size;
				p_cell->children[i] = child;
			}

			if (half_size > 1) {
				plot_triangle(p_cell->children[i], p_cell_size, p_triangle);
			}
		}
	}

	static void gen_positions(const Cell *p_cell, float p_cell_size, LocalVector<Vector3> &r_positions, HashMap<Vector3i, bool> &r_used, const AABB &p_bounds) {
		for (int i = 0; i < 8; i++) {
			Vector3i pos = p_cell->offset;
			if (i & 1) {
				pos.x += p_cell->size;
			}
			if (i & 2) {
				pos.y += p_cell->size;
			}
			if (i & 4) {
				pos.z += p_cell->size;
			}

			if (p_cell->size == 1 && !r_used.has(pos)) {
				Vector3 real_pos = p_bounds.position + Vector3(pos) * p_cell_size;
				r_positions.push_back(real_pos);
				r_used[pos] = true;
			}

			if (p_cell->children[i] != nullptr) {
				gen_positions(p_cell->children[i], p_cell_size, r_positions, r_used, p_bounds);
			}
		}
	}

	static Vector<Vector3> generate_probes(const Vector<Vector3> &p_vertices, const Vector<int> &p_indices, const AABB &p_bounds, int p_subdiv) {
		float subdiv_cell_size = p_bounds.size[p_bounds.get_longest_axis_index()] / p_subdiv;
		if (subdiv_cell_size < 0.001f) {
			return Vector<Vector3>();
		}

		Cell octree;
		uint64_t max_dim = (uint64_t)MAX(MAX(
												 (int)Math::ceil(p_bounds.size.x / subdiv_cell_size),
												 (int)Math::ceil(p_bounds.size.y / subdiv_cell_size)),
				(int)Math::ceil(p_bounds.size.z / subdiv_cell_size));
		octree.size = (uint32_t)Math::next_power_of_2(max_dim);

		AABB adjusted_bounds = p_bounds;
		adjusted_bounds.size = Vector3(octree.size, octree.size, octree.size) * subdiv_cell_size;

		for (int t = 0; t + 2 < p_indices.size(); t += 3) {
			Vector3 tri[3] = {
				p_vertices[p_indices[t + 0]],
				p_vertices[p_indices[t + 1]],
				p_vertices[p_indices[t + 2]]
			};
			for (int j = 0; j < 3; j++) {
				tri[j] = (tri[j] - adjusted_bounds.position) / subdiv_cell_size;
			}
			plot_triangle(&octree, 1.0f, tri);
		}

		LocalVector<Vector3> probe_pos;
		HashMap<Vector3i, bool> used;
		gen_positions(&octree, subdiv_cell_size, probe_pos, used, adjusted_bounds);

		for (int i = 0; i < 8; i++) {
			Vector3 corner = adjusted_bounds.position;
			if (i & 1) {
				corner.x += adjusted_bounds.size.x;
			}
			if (i & 2) {
				corner.y += adjusted_bounds.size.y;
			}
			if (i & 4) {
				corner.z += adjusted_bounds.size.z;
			}
			probe_pos.push_back(corner);
		}

		Vector<Vector3> result;
		result.resize(probe_pos.size());
		Vector3 *w = result.ptrw();
		for (uint32_t i = 0; i < probe_pos.size(); i++) {
			w[i] = probe_pos[i];
		}
		return result;
	}
};

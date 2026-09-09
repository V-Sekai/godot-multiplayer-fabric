/**************************************************************************/
/*  cassie_surface_patch.cpp                                              */
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

#include "cassie_surface_patch.h"

#include "core/math/aabb.h"
#include "core/math/geometry_3d.h"
#include "core/object/class_db.h"
#include "core/variant/typed_array.h"

#include <cfloat>

namespace {

// Closest point on triangle ABC to query point P.
// Ericson, Real-Time Collision Detection, p. 141. Mirrors the helper
// already used by cassie_remesh.cpp.
Vector3 closest_point_on_triangle(const Vector3 &p, const Vector3 &a,
		const Vector3 &b, const Vector3 &c) {
	const Vector3 ab = b - a;
	const Vector3 ac = c - a;
	const Vector3 ap = p - a;
	const real_t d1 = ab.dot(ap);
	const real_t d2 = ac.dot(ap);
	if (d1 <= 0.0 && d2 <= 0.0) {
		return a;
	}
	const Vector3 bp = p - b;
	const real_t d3 = ab.dot(bp);
	const real_t d4 = ac.dot(bp);
	if (d3 >= 0.0 && d4 <= d3) {
		return b;
	}
	const real_t vc = d1 * d4 - d3 * d2;
	if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
		return a + ab * (d1 / (d1 - d3));
	}
	const Vector3 cp = p - c;
	const real_t d5 = ab.dot(cp);
	const real_t d6 = ac.dot(cp);
	if (d6 >= 0.0 && d5 <= d6) {
		return c;
	}
	const real_t vb = d5 * d2 - d1 * d6;
	if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
		return a + ac * (d2 / (d2 - d6));
	}
	const real_t va = d3 * d6 - d5 * d4;
	if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
		const real_t w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		return b + (c - b) * w;
	}
	const real_t denom = real_t(1.0) / (va + vb + vc);
	const real_t v = vb * denom;
	const real_t w = vc * denom;
	return a + ab * v + ac * w;
}

AABB triangle_aabb(const Vector3 &a, const Vector3 &b, const Vector3 &c) {
	AABB box;
	box.position = Vector3(MIN(a.x, MIN(b.x, c.x)),
			MIN(a.y, MIN(b.y, c.y)),
			MIN(a.z, MIN(b.z, c.z)));
	const Vector3 hi(MAX(a.x, MAX(b.x, c.x)),
			MAX(a.y, MAX(b.y, c.y)),
			MAX(a.z, MAX(b.z, c.z)));
	box.size = (hi - box.position).maxf(real_t(1e-4)); // avoid zero-size AABB
	return box;
}

} // namespace

void CassieSurfacePatch::set_mesh(const Ref<Mesh> &p_mesh) {
	source_mesh = p_mesh;
	vertices.clear();
	triangles.clear();
	triangle_normals.clear();
	vertex_normals.clear();
	triangle_bvh_ids.clear();
	active_triangle_count = 0;
	bvh.clear();
	initial_search_radius = real_t(0.1);

	if (source_mesh.is_null() || source_mesh->get_surface_count() == 0) {
		return;
	}

	const Array arrays = source_mesh->surface_get_arrays(0);
	if (arrays.size() <= Mesh::ARRAY_VERTEX) {
		return;
	}
	const PackedVector3Array verts_in = arrays[Mesh::ARRAY_VERTEX];
	if (verts_in.is_empty()) {
		return;
	}
	const PackedInt32Array idx_in = arrays.size() > Mesh::ARRAY_INDEX
			? PackedInt32Array(arrays[Mesh::ARRAY_INDEX])
			: PackedInt32Array();
	// Capture source ARRAY_NORMAL when present. Preserves authored
	// split normals + smoothing-group decisions through bind + deform
	// (Linear ENG-65 phase 1). Falls back to angle-weighted (Max 1999)
	// after triangle build when the source glb omits normals.
	const PackedVector3Array normals_in = arrays.size() > Mesh::ARRAY_NORMAL
			? PackedVector3Array(arrays[Mesh::ARRAY_NORMAL])
			: PackedVector3Array();

	vertices.resize(verts_in.size());
	for (int i = 0; i < verts_in.size(); ++i) {
		vertices[i] = verts_in[i];
	}

	const int tri_count = idx_in.is_empty()
			? (verts_in.size() / 3)
			: (idx_in.size() / 3);
	triangles.reserve(tri_count);
	triangle_normals.reserve(tri_count);

	const real_t kDegenEps = real_t(1e-12);
	real_t total_edge_len = 0.0;
	int edge_count = 0;
	// Accumulator for the angle-weighted Max 1999 fallback. We compute
	// these on every pass; if the source provided normals we discard
	// the fallback and copy verts_in's normals at the end.
	LocalVector<Vector3> normal_accum;
	normal_accum.resize(verts_in.size());
	for (int i = 0; i < int(verts_in.size()); ++i) {
		normal_accum[i] = Vector3();
	}
	for (int t = 0; t < tri_count; ++t) {
		const int i0 = idx_in.is_empty() ? (t * 3 + 0) : idx_in[t * 3 + 0];
		const int i1 = idx_in.is_empty() ? (t * 3 + 1) : idx_in[t * 3 + 1];
		const int i2 = idx_in.is_empty() ? (t * 3 + 2) : idx_in[t * 3 + 2];
		if (i0 < 0 || i0 >= int(vertices.size()) ||
				i1 < 0 || i1 >= int(vertices.size()) ||
				i2 < 0 || i2 >= int(vertices.size())) {
			continue;
		}
		const Vector3 &a = vertices[i0];
		const Vector3 &b = vertices[i1];
		const Vector3 &c = vertices[i2];
		const Vector3 n_raw = (b - a).cross(c - a);
		if (n_raw.length_squared() < kDegenEps) {
			continue;
		}
		triangles.push_back(Vector3i(i0, i1, i2));
		const Vector3 face_n = n_raw.normalized();
		triangle_normals.push_back(face_n);

		// Max 1999 angle-weighted contribution per vertex.
		const Vector3 e_ab = (b - a);
		const Vector3 e_ac = (c - a);
		const Vector3 e_ba = -e_ab;
		const Vector3 e_bc = (c - b);
		const Vector3 e_ca = -e_ac;
		const Vector3 e_cb = -e_bc;
		auto vertex_angle = [](const Vector3 &u, const Vector3 &v) {
			const real_t lu = u.length();
			const real_t lv = v.length();
			if (lu < real_t(1e-20) || lv < real_t(1e-20)) {
				return real_t(0);
			}
			real_t cos_a = u.dot(v) / (lu * lv);
			if (cos_a > real_t(1)) {
				cos_a = real_t(1);
			} else if (cos_a < real_t(-1)) {
				cos_a = real_t(-1);
			}
			return Math::acos(cos_a);
		};
		normal_accum[i0] += face_n * vertex_angle(e_ab, e_ac);
		normal_accum[i1] += face_n * vertex_angle(e_ba, e_bc);
		normal_accum[i2] += face_n * vertex_angle(e_ca, e_cb);

		for (int e = 0; e < 3; ++e) {
			const Vector3 &p0 = e == 0 ? a : (e == 1 ? b : c);
			const Vector3 &p1 = e == 0 ? b : (e == 1 ? c : a);
			total_edge_len += p0.distance_to(p1);
			++edge_count;
		}
	}
	if (edge_count > 0) {
		initial_search_radius = real_t(total_edge_len / edge_count) * real_t(2.0);
	}

	// Populate vertex_normals: prefer source ARRAY_NORMAL when present
	// (preserves authored shading), fall back to angle-weighted Max
	// 1999 computed above when absent. Verbatim copy is intentional
	// when the source has normals — even split-normal data shows up as
	// a per-vertex average here because Godot's ArrayMesh layout is
	// strictly per-vertex (corner-split normals would require vertex
	// duplication at smoothing-group seams in the source).
	vertex_normals.resize(vertices.size());
	if (normals_in.size() == verts_in.size()) {
		for (int i = 0; i < int(vertices.size()); ++i) {
			vertex_normals[i] = normals_in[i];
		}
	} else {
		for (int i = 0; i < int(vertices.size()); ++i) {
			const Vector3 &n = normal_accum[i];
			vertex_normals[i] = (n.length_squared() < real_t(1e-20))
					? Vector3(0, 1, 0)
					: n.normalized();
		}
	}

	_rebuild_bvh();
}

void CassieSurfacePatch::_rebuild_bvh() {
	bvh.clear();
	triangle_bvh_ids.resize(triangles.size());
	active_triangle_count = 0;
	for (uint32_t i = 0; i < triangles.size(); ++i) {
		const Vector3i &tri = triangles[i];
		if (tri.x < 0) {
			triangle_bvh_ids[i] = DynamicBVH::ID();
			continue;
		}
		const AABB box = triangle_aabb(vertices[tri.x], vertices[tri.y], vertices[tri.z]);
		triangle_bvh_ids[i] = bvh.insert(box, reinterpret_cast<void *>(uintptr_t(i)));
		++active_triangle_count;
	}
}

int CassieSurfacePatch::add_vertex(const Vector3 &p_pos) {
	const int idx = int(vertices.size());
	vertices.push_back(p_pos);
	return idx;
}

int CassieSurfacePatch::add_triangle(int p_v0, int p_v1, int p_v2) {
	const int n = int(vertices.size());
	if (p_v0 < 0 || p_v0 >= n || p_v1 < 0 || p_v1 >= n || p_v2 < 0 || p_v2 >= n) {
		return -1;
	}
	const Vector3 &a = vertices[p_v0];
	const Vector3 &b = vertices[p_v1];
	const Vector3 &c = vertices[p_v2];
	const Vector3 n_raw = (b - a).cross(c - a);
	if (n_raw.length_squared() < real_t(1e-12)) {
		return -1;
	}
	const int idx = int(triangles.size());
	triangles.push_back(Vector3i(p_v0, p_v1, p_v2));
	triangle_normals.push_back(n_raw.normalized());
	const AABB box = triangle_aabb(a, b, c);
	const DynamicBVH::ID id = bvh.insert(box, reinterpret_cast<void *>(uintptr_t(idx)));
	triangle_bvh_ids.push_back(id);
	++active_triangle_count;
	return idx;
}

bool CassieSurfacePatch::remove_triangle(int p_idx) {
	if (p_idx < 0 || uint32_t(p_idx) >= triangles.size()) {
		return false;
	}
	if (triangles[p_idx].x < 0) {
		return false; // already deleted
	}
	const DynamicBVH::ID id = triangle_bvh_ids[p_idx];
	if (id.is_valid()) {
		bvh.remove(id);
	}
	triangle_bvh_ids[p_idx] = DynamicBVH::ID();
	triangles[p_idx] = Vector3i(-1, -1, -1);
	triangle_normals[p_idx] = Vector3();
	--active_triangle_count;
	return true;
}

Dictionary CassieSurfacePatch::project(const Vector3 &p_pos) const {
	Dictionary result;
	result["on_surface"] = false;
	result["patch_id"] = patch_id;
	result["projected"] = p_pos;
	result["normal"] = Vector3();
	result["distance"] = real_t(INFINITY);

	if (triangles.is_empty()) {
		return result;
	}

	const Vector3 q_local = xform.affine_inverse().xform(p_pos);

	// DynamicBVH::aabb_query terminates when the functor returns `true`.
	// We return `false` so the query visits every leaf inside the search
	// box and we keep a running min across all candidates.
	struct Collector {
		const CassieSurfacePatch *self;
		Vector3 query;
		Vector3 best_pos;
		Vector3 best_normal;
		real_t best_dist_sq;
		bool any = false;
		bool operator()(void *p_data) {
			const uint32_t ti = uint32_t(uintptr_t(p_data));
			const Vector3i &tri = self->triangles[ti];
			const Vector3 p = closest_point_on_triangle(query,
					self->vertices[tri.x],
					self->vertices[tri.y],
					self->vertices[tri.z]);
			const real_t d_sq = query.distance_squared_to(p);
			if (!any || d_sq < best_dist_sq) {
				best_pos = p;
				best_normal = self->triangle_normals[ti];
				best_dist_sq = d_sq;
				any = true;
			}
			return false; // keep visiting
		}
	};

	Collector c{ this, q_local, q_local, Vector3(), real_t(0.0), false };
	// Expand the search box until the running-min distance is fully
	// contained — i.e. every triangle that could be closer than best_dist
	// has its AABB inside the box (its closest-point lies within best_dist
	// of the query, so the AABB must touch the box). Stop once we hit
	// that invariant or pass the patch's bounding diameter.
	real_t r = initial_search_radius;
	for (int attempt = 0; attempt < 24; ++attempt) {
		const AABB box(q_local - Vector3(r, r, r), Vector3(r, r, r) * real_t(2.0));
		const_cast<DynamicBVH &>(bvh).aabb_query(box, c);
		if (c.any) {
			const real_t best_dist = Math::sqrt(c.best_dist_sq);
			if (best_dist <= r) {
				break; // global minimum verified
			}
			// Expand just past the current best so the next query brackets
			// any potentially closer triangle.
			r = best_dist * real_t(1.01) + real_t(1e-6);
		} else {
			r *= real_t(4.0);
		}
	}

	if (!c.any) {
		return result;
	}

	const Vector3 projected_world = xform.xform(c.best_pos);
	const Vector3 normal_world = xform.basis.xform(c.best_normal).normalized();
	result["on_surface"] = true;
	result["projected"] = projected_world;
	result["normal"] = normal_world;
	result["distance"] = real_t(p_pos.distance_to(projected_world));
	return result;
}

Callable CassieSurfacePatch::get_callback() {
	return Callable(this, "project");
}

Vector3i CassieSurfacePatch::get_triangle_indices(int p_idx) const {
	if (p_idx < 0 || uint32_t(p_idx) >= triangles.size()) {
		return Vector3i();
	}
	return triangles[p_idx];
}

Vector3 CassieSurfacePatch::get_vertex_position(int p_idx) const {
	if (p_idx < 0 || uint32_t(p_idx) >= vertices.size()) {
		return Vector3();
	}
	return vertices[p_idx];
}

Vector3 CassieSurfacePatch::get_triangle_normal(int p_idx) const {
	if (p_idx < 0 || uint32_t(p_idx) >= triangle_normals.size()) {
		return Vector3();
	}
	return triangle_normals[p_idx];
}

Vector3 CassieSurfacePatch::get_vertex_normal(int p_idx) const {
	if (p_idx < 0 || uint32_t(p_idx) >= vertex_normals.size()) {
		return Vector3();
	}
	return vertex_normals[p_idx];
}

void CassieSurfacePatch::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mesh", "mesh"), &CassieSurfacePatch::set_mesh);
	ClassDB::bind_method(D_METHOD("get_mesh"), &CassieSurfacePatch::get_mesh);
	ClassDB::bind_method(D_METHOD("set_patch_id", "id"), &CassieSurfacePatch::set_patch_id);
	ClassDB::bind_method(D_METHOD("get_patch_id"), &CassieSurfacePatch::get_patch_id);
	ClassDB::bind_method(D_METHOD("set_transform", "transform"),
			&CassieSurfacePatch::set_transform);
	ClassDB::bind_method(D_METHOD("get_transform"), &CassieSurfacePatch::get_transform);
	ClassDB::bind_method(D_METHOD("get_triangle_count"),
			&CassieSurfacePatch::get_triangle_count);
	ClassDB::bind_method(D_METHOD("get_active_triangle_count"),
			&CassieSurfacePatch::get_active_triangle_count);
	ClassDB::bind_method(D_METHOD("get_vertex_count"),
			&CassieSurfacePatch::get_vertex_count);
	ClassDB::bind_method(D_METHOD("add_vertex", "position"),
			&CassieSurfacePatch::add_vertex);
	ClassDB::bind_method(D_METHOD("add_triangle", "v0", "v1", "v2"),
			&CassieSurfacePatch::add_triangle);
	ClassDB::bind_method(D_METHOD("remove_triangle", "idx"),
			&CassieSurfacePatch::remove_triangle);
	ClassDB::bind_method(D_METHOD("get_triangle_indices", "idx"),
			&CassieSurfacePatch::get_triangle_indices);
	ClassDB::bind_method(D_METHOD("get_vertex_position", "idx"),
			&CassieSurfacePatch::get_vertex_position);
	ClassDB::bind_method(D_METHOD("get_triangle_normal", "idx"),
			&CassieSurfacePatch::get_triangle_normal);
	ClassDB::bind_method(D_METHOD("project", "position"), &CassieSurfacePatch::project);
	ClassDB::bind_method(D_METHOD("get_callback"), &CassieSurfacePatch::get_callback);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"),
			"set_mesh", "get_mesh");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "patch_id"), "set_patch_id", "get_patch_id");
	ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "transform"),
			"set_transform", "get_transform");
}

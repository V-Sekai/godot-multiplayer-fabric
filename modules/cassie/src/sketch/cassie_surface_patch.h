/**************************************************************************/
/*  cassie_surface_patch.h                                                */
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
#include "core/math/dynamic_bvh.h"
#include "core/math/transform_3d.h"
#include "core/math/vector3.h"
#include "core/object/ref_counted.h"
#include "core/templates/local_vector.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"
#include "scene/resources/mesh.h"

// CassieSurfacePatch wraps a Godot Mesh and answers point-projection queries
// in the Dictionary shape the existing Tier-4 beautifier consumes via
// CassieSketchContext.project_on_patch_callback.
//
// BVH-backed from day one — production meshes hit 100k+ triangles. The
// pattern mirrors cassie_remesh.cpp's RefMeshBVH: DynamicBVH over per-triangle
// AABBs, query via aabb_query with an expanding search radius and a
// running-min closest-point-on-triangle collector.
//
// References:
//   - de Goes, Sheffler, Fleischer, "Character Articulation through Profile
//     Curves", ACM TOG 41/4 art. 139 (2022) — the algorithm this patch feeds.
//   - DiffCloth MeshFileHandler::loadOBJFile pattern at
//     E:\TOOL_cloth_dynamics\src\code\engine\MeshFileHandler.h:86 — inspired
//     the simple triangle-list ingest.
class CassieSurfacePatch : public Resource {
	GDCLASS(CassieSurfacePatch, Resource);

	Ref<Mesh> source_mesh;
	LocalVector<Vector3> vertices;
	LocalVector<Vector3i> triangles;
	LocalVector<Vector3> triangle_normals;
	// Per-vertex normals captured from the source mesh's ARRAY_NORMAL.
	// Falls back to angle-weighted (Max 1999) when the source omits
	// them. Preserves authored shading (split normals, smoothing
	// groups) through bind + deform.
	LocalVector<Vector3> vertex_normals;
	// ENG-53 — parallel to `triangles`. Stores the BVH leaf ID per
	// triangle so incremental remove_triangle/add_triangle don't have to
	// rebuild the whole BVH. Deleted slots get triangles[i] = (-1,-1,-1)
	// and an invalid BVH ID — project() skips them.
	LocalVector<DynamicBVH::ID> triangle_bvh_ids;
	int active_triangle_count = 0;
	int patch_id = 0;
	Transform3D xform;

	// Mutable so const project() can lazily rebuild and expand the
	// initial search radius when calls hit empty AABBs.
	mutable DynamicBVH bvh;
	mutable float initial_search_radius = 0.1f;

	// Cycle edge IDs from CassieSketchGraph that produced this patch.
	// Used by export_raw_data_dict to round-trip CASSIE upstream raw_data
	// JSON. Empty when the patch wasn't created from a sketch-graph cycle.
	PackedInt32Array source_cycle_edge_ids;

	void _rebuild_bvh();

protected:
	static void _bind_methods();

public:
	CassieSurfacePatch() = default;

	void set_mesh(const Ref<Mesh> &p_mesh);
	Ref<Mesh> get_mesh() const { return source_mesh; }

	void set_patch_id(int p_id) { patch_id = p_id; }
	int get_patch_id() const { return patch_id; }

	// Source-cycle metadata for round-trip with CASSIE upstream raw_data
	// JSON (see CassieSurfaceManager::export_raw_data_dict). Set by
	// CassieSurfaceManager when the patch is materialized.
	void set_source_cycle_edge_ids(const PackedInt32Array &p_ids) {
		source_cycle_edge_ids = p_ids;
	}
	PackedInt32Array get_source_cycle_edge_ids() const {
		return source_cycle_edge_ids;
	}

	void set_transform(const Transform3D &p_xform) { xform = p_xform; }
	Transform3D get_transform() const { return xform; }

	int get_triangle_count() const { return int(triangles.size()); }
	int get_active_triangle_count() const { return active_triangle_count; }
	int get_vertex_count() const { return int(vertices.size()); }

	// ENG-53 incremental editing — append a triangle indexing existing
	// vertices. Returns the new triangle index, or -1 on invalid indices /
	// degenerate triangle. Updates the BVH in place.
	int add_triangle(int p_v0, int p_v1, int p_v2);
	// Append a new vertex; returns its index. Convenience for callers
	// growing the mesh by sketch strokes.
	int add_vertex(const Vector3 &p_pos);
	// Mark a triangle slot deleted + remove its BVH leaf. Returns true
	// on success. The slot stays in `triangles` (zeroed) — get_triangle_count
	// still returns the raw slot count; get_active_triangle_count returns
	// the live triangle count.
	bool remove_triangle(int p_idx);

	// Triangle / vertex accessors used by CassieCurvenetExtractor (ENG-42).
	// Returns Vector3i() / Vector3() on out-of-range indices so a stray call
	// from GDScript can't crash the editor.
	Vector3i get_triangle_indices(int p_idx) const;
	Vector3 get_vertex_position(int p_idx) const;
	Vector3 get_triangle_normal(int p_idx) const;
	// Per-vertex normal captured from the source mesh's ARRAY_NORMAL.
	// Returns Vector3() on out-of-range. Always populated after
	// set_mesh — angle-weighted (Max 1999) fallback when the source
	// omits normals so callers always see a unit-length normal.
	Vector3 get_vertex_normal(int p_idx) const;

	// Returns Dictionary { "on_surface": bool, "patch_id": int,
	// "projected": Vector3, "normal": Vector3, "distance": float }.
	// "on_surface" is set true when a triangle was actually projected to
	// (i.e., the mesh isn't empty). The caller decides whether the
	// projection is close enough via its own threshold.
	Dictionary project(const Vector3 &p_pos) const;

	// Returns a Callable bound to project() so it plugs straight into
	// CassieSketchContext::set_project_on_patch_callback.
	Callable get_callback();
};

/**************************************************************************/
/*  cassie_sketch_graph.h                                                 */
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
#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/variant/typed_array.h"

// CassieSketchGraph — Tier 4 graph topology for CASSIE editing (ENG-77).
//
// Ships the data classes plus a *planar* minimal-cycle finder —
// sufficient for the verification gate of three strokes forming a
// triangle yielding exactly one cycle of three edges. The full
// CycleDetection.cs port (Rodrigues parallel transport across each
// segment + sharp/smooth node distinction + ShouldReverse normal-flip
// check) stays in the GDScript scaffold as a follow-up; this slice
// closes the topology gap end-to-end via the cheaper planar variant
// that the Yu 2021 reference reduces to when the surface normals don't
// twist along the curves.
//
// Editing-track per [ENG-69 / ENG-74-76] direction; matches the C++
// scope already in `sketch/`.

class CassieSketchGraphNode : public Resource {
	GDCLASS(CassieSketchGraphNode, Resource);

	int id = -1;
	Vector3 position;
	Vector3 normal;
	PackedInt32Array edge_ids;
	bool is_sharp = false;

protected:
	static void _bind_methods();

public:
	CassieSketchGraphNode() = default;
	void set_id(int p_id) { id = p_id; }
	int get_id() const { return id; }
	void set_position(const Vector3 &p) { position = p; }
	Vector3 get_position() const { return position; }
	void set_normal(const Vector3 &n) { normal = n; }
	Vector3 get_normal() const { return normal; }
	void set_edge_ids(const PackedInt32Array &e) { edge_ids = e; }
	PackedInt32Array get_edge_ids() const { return edge_ids; }
	int get_degree() const { return edge_ids.size(); }
	void set_is_sharp(bool s) { is_sharp = s; }
	bool get_is_sharp() const { return is_sharp; }

	void add_edge_id(int p_edge_id);
};

class CassieSketchGraphEdge : public Resource {
	GDCLASS(CassieSketchGraphEdge, Resource);

	int id = -1;
	PackedVector3Array points;
	PackedVector3Array normals;
	int node_a_id = -1;
	int node_b_id = -1;
	// Populated by build_from_polylines: index into the input polyline
	// array. Lets callers map cycle → set-of-strokes for the border-set
	// diff. -1 for edges added via the per-stroke add_stroke path.
	int source_polyline_idx = -1;

protected:
	static void _bind_methods();

public:
	CassieSketchGraphEdge() = default;
	void set_id(int p_id) { id = p_id; }
	int get_id() const { return id; }
	void set_points(const PackedVector3Array &p) { points = p; }
	PackedVector3Array get_points() const { return points; }
	void set_normals(const PackedVector3Array &n) { normals = n; }
	PackedVector3Array get_normals() const { return normals; }
	void set_node_a_id(int n) { node_a_id = n; }
	int get_node_a_id() const { return node_a_id; }
	void set_node_b_id(int n) { node_b_id = n; }
	int get_node_b_id() const { return node_b_id; }
	void set_source_polyline_idx(int n) { source_polyline_idx = n; }
	int get_source_polyline_idx() const { return source_polyline_idx; }

	int get_opposite(int node_id) const;
	Vector3 get_tangent_away_from(int node_id) const;

	// Discrete parallel transport of a vector along this edge's polyline,
	// from one endpoint node to the other. Mirror of upstream
	// Segment.Transport(v, to). Caller passes the FROM node; we walk the
	// polyline and rotate v by the smallest rotation between successive
	// tangents at each polyline vertex. Returns v unchanged if the edge
	// is too short for meaningful transport.
	Vector3 parallel_transport(const Vector3 &p_v, int p_from_node_id) const;
};

class CassieSketchGraph : public Resource {
	GDCLASS(CassieSketchGraph, Resource);

	HashMap<int, Ref<CassieSketchGraphNode>> nodes;
	HashMap<int, Ref<CassieSketchGraphEdge>> edges;
	int next_node_id = 0;
	int next_edge_id = 0;

	real_t merge_epsilon = real_t(0.02);
	real_t sharp_angle_threshold = real_t(Math::PI / 6.0); // 30°

	void _update_node_sharpness(int p_node_id);
	void _update_node_normal(int p_node_id);
	int _find_or_create_node(const Vector3 &p_pos, const Vector3 &p_normal);

	// CycleDetection.cs port: angular ordering at a node. Projects all
	// incident edge tangents into the plane perpendicular to `normal`, picks
	// the edge whose projected tangent is the immediate CCW (want_next=true)
	// or CW (want_next=false) neighbor of the incoming-reversed tangent.
	// Returns -1 if no suitable next edge.
	int _next_edge_at(int p_node_id, int p_incoming_edge,
			const Vector3 &p_normal, bool p_want_next) const;

	// Planar cycle helpers — pick the next edge at a node by angular
	// sweep in the plane perpendicular to the node's normal. CCW pick
	// of the immediately-clockwise edge after the incoming reversed.
	int _next_edge_planar(int p_node_id, int p_incoming_edge,
			const Vector3 &p_plane_normal) const;

protected:
	static void _bind_methods();

public:
	CassieSketchGraph() = default;

	void set_merge_epsilon(real_t e) { merge_epsilon = e; }
	real_t get_merge_epsilon() const { return merge_epsilon; }

	void clear();

	// Add a stroke as a single edge. Endpoints within merge_epsilon of
	// an existing node merge into that node; otherwise new nodes are
	// created. Returns the edge id, or -1 if the input has fewer than
	// two points. NOTE: this is the simple per-stroke commit path used
	// for online drawing — it does NOT detect mid-stroke intersections
	// with previously committed edges. Offline replay of a full sketch
	// should call build_from_polylines (see below) instead, which
	// computes the entire planar arrangement in one pass.
	int add_stroke(const PackedVector3Array &p_points,
			const PackedVector3Array &p_normals);

	// One-shot planar-arrangement build for offline replay. Takes all
	// strokes' flattened polylines at once, finds every pairwise
	// segment-segment intersection within p_proximity, clusters
	// crossings within merge_epsilon into unique node positions, then
	// emits one edge per slice of each polyline between consecutive
	// intersections. Equivalent to running the upstream draw-time
	// intersection enforcement on every stroke in order, but without
	// the incremental split-and-repair bookkeeping needed for VR
	// interactivity. Clears the graph before building. Returns the
	// number of edges added.
	int build_from_polylines(const TypedArray<PackedVector3Array> &p_polylines,
			real_t p_proximity);

	int get_edge_count() const { return edges.size(); }
	int get_node_count() const { return nodes.size(); }

	Ref<CassieSketchGraphNode> get_node(int p_id) const;
	Ref<CassieSketchGraphEdge> get_edge(int p_id) const;

	TypedArray<CassieSketchGraphNode> get_all_nodes() const;
	TypedArray<CassieSketchGraphEdge> get_all_edges() const;

	// Find all minimal cycles. Each returned cycle is a list of edge
	// ids in traversal order. The planar finder works for graphs whose
	// strokes lie roughly in a common tangent plane (true for the
	// triangle gate); the GDScript scaffold retains the full normal-
	// transport variant for non-planar curve networks.
	Array find_cycles() const;

	// Sample a detected cycle's boundary as a sequence of Vector3
	// points at roughly p_target_edge_length spacing. Walks each edge
	// in traversal order (CCW per find_cycles output), flipping the
	// edge sample order at nodes where the cycle visits the edge from
	// node_b. The output is ready to feed into
	// CassieTriangulator::triangulate(boundary, edge_length).
	PackedVector3Array sample_cycle_boundary(const PackedInt32Array &p_cycle_edge_ids,
			real_t p_target_edge_length) const;
};

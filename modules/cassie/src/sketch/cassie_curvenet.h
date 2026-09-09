/**************************************************************************/
/*  cassie_curvenet.h                                                     */
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

#include "cassie_curvenet_knot.h"
#include "cassie_final_stroke.h"
#include "cassie_surface_patch.h"

#include "core/io/resource.h"
#include "core/math/basis.h"
#include "core/math/transform_3d.h"
#include "core/math/vector2.h"
#include "core/math/vector3.h"
#include "core/object/ref_counted.h"
#include "core/templates/local_vector.h"
#include "core/variant/typed_array.h"
#include "core/variant/variant.h"

// CassieCurvenet is the graph of Bézier curves layered over a surface
// patch — Pixar's curvenet primitive from de Goes et al., "Character
// Articulation through Profile Curves" (TOG 41/4 art. 139, 2022), extended
// by the Curvenet Adjustment layer from Nguyen et al., "Shaping the
// Elements" (SIGGRAPH '23 Talks).
//
// Step 1.2 (ENG-44) — this file — ships the data shell + bookkeeping. The
// orientation algorithm (Step 1.3 / ENG-45) writes into rest_pose_transform
// of each knot. The mode toggle + topology operations (Step 1.5 / ENG-47)
// are added on top.
//
// Knot identification:
//   - Each CassieCurvenetKnot::graph_node_id matches a graph node from
//     the sketch_graph.gd cycle-detection layer.
//   - is_intersection == (degree(graph_node) >= 3).
//   - projection_pose_tangents are snapshotted at build_from_graph time.
//   - rest_pose_tangents are re-evaluated on every update_rest_pose call.
class CassieCurvenet : public Resource {
	GDCLASS(CassieCurvenet, Resource);

public:
	enum Mode {
		MODE_EDIT = 0,
		MODE_POSE = 1,
	};

private:
	TypedArray<CassieFinalStroke> curves;
	TypedArray<CassieCurvenetKnot> knots;
	Ref<CassieSurfacePatch> bound_patch;
	Mode mode = MODE_EDIT;

	// Adjacency populated by build_from_graph. knot_incident_curves[k] is
	// the list of curve indices touching knot k; curve_endpoint_knots[c]
	// is the pair (start_knot, end_knot) for curve c (-1 if unset).
	LocalVector<PackedInt32Array> knot_incident_curves;
	LocalVector<Vector2i> curve_endpoint_knots;

protected:
	static void _bind_methods();

public:
	CassieCurvenet() = default;

	void set_curves(const TypedArray<CassieFinalStroke> &p_curves) { curves = p_curves; }
	TypedArray<CassieFinalStroke> get_curves() const { return curves; }

	void set_knots(const TypedArray<CassieCurvenetKnot> &p_knots) { knots = p_knots; }
	TypedArray<CassieCurvenetKnot> get_knots() const { return knots; }

	int get_curve_count() const { return curves.size(); }
	int get_knot_count() const { return knots.size(); }

	void set_mode(Mode p_mode) { mode = p_mode; }
	Mode get_mode() const { return mode; }

	void set_bound_patch(const Ref<CassieSurfacePatch> &p_patch) { bound_patch = p_patch; }
	Ref<CassieSurfacePatch> get_bound_patch() const { return bound_patch; }

	// Build the curvenet graph from the GDScript-side sketch_graph.gd
	// representation. p_graph_data is a Dictionary with keys "nodes" (an
	// Array of { "id": int, "position": Vector3, "incident_curve_ids":
	// PackedInt32Array }) and "curves" (TypedArray<CassieFinalStroke>).
	// Stays in GDScript-Dictionary form so the caller can build the data
	// however it wants — Tier-2 sketch_graph.gd or a hand-built test fixture.
	void build_from_graph(const Dictionary &p_graph_data);

	// Update the rest_pose_tangents on every knot by re-sampling the
	// current curves' anchor handles and projecting via p_patch. Cheap —
	// O(knots × incident_curves_per_knot). The world-space orientations
	// are NOT recomputed here; call compute_orientations() afterwards.
	void update_rest_pose(const Ref<CassieSurfacePatch> &p_patch);

	// Step 1.5 (ENG-47) — topology ops.
	//
	// set_knot_transform: write the new rest_pose_transform on the knot.
	// Returns false on out-of-range index. Does NOT auto-trigger
	// compute_orientations — the caller batches that to amortize cost
	// across multi-knot edits (e.g. a pose-mode tool emitting one transform
	// per frame and re-orienting only when needed).
	bool set_knot_transform(int p_knot_idx, const Transform3D &p_transform);

	// add_curve: append p_curve to the curve list, link its endpoints to
	// the named knots, refresh the affected knots' incident_curve_ids.
	// p_start_knot_idx / p_end_knot_idx may be -1 to leave that endpoint
	// floating (no adjacency). Returns the new curve index.
	int add_curve(const Ref<CassieFinalStroke> &p_curve,
			int p_start_knot_idx, int p_end_knot_idx);

	// delete_curve: remove the curve at p_curve_idx, shift higher curve
	// indices down by 1, and rewrite the affected knots' incident lists.
	// Returns false on out-of-range index.
	bool delete_curve(int p_curve_idx);

	// replace_curve: swap the curve at p_curve_idx for p_new_curve while
	// preserving the original curve's endpoint knot adjacency. Returns
	// false on out-of-range index or null replacement.
	bool replace_curve(int p_curve_idx, const Ref<CassieFinalStroke> &p_new_curve);

	// Step 1.3 (ENG-45) — Pixar Curvenet Adjuster Mover algorithm.
	// Intersection knots (≥ 3 incident tangents): solve Wahba via SVD.
	// Non-intersection knots: parallel-transport from the nearest
	// intersection(s) on incident curves, inverse-distance blend if two.
	// Isolated curves with no neighboring intersection get identity + the
	// needs_setup flag.
	void compute_orientations();

	// Public static helper for ENG-45 Wahba tests. Solves for the
	// rotation R minimizing sum |R p_i - q_i|^2 via the SVD of the cross-
	// covariance matrix H = sum p_i q_i^T. Requires ≥ 3 tangent pairs
	// for a well-determined solution; returns identity when fewer are
	// provided so the caller can fall through to parallel-transport.
	// p_projection_tangents and p_rest_tangents must be the same length;
	// only the first min(N, 3+) entries are used.
	static Basis wahba_align(const PackedVector3Array &p_projection_tangents,
			const PackedVector3Array &p_rest_tangents);

	// Exposes the per-curve endpoint knot indices populated by
	// build_from_graph. Returns Vector2i(-1, -1) when the curve isn't
	// adjacent to any tracked knot. Used by ENG-46 Profile Mover to walk
	// curve samples → endpoint knots without re-computing adjacency.
	Vector2i get_curve_endpoint_knots(int p_curve_idx) const;

	// Public static helper for ENG-45 parallel-transport tests. Returns
	// the rotation at offset p_dest_offset along p_curve given a known
	// rotation p_src_basis at offset p_src_offset. Built on top of
	// Curve3D::sample_baked_with_rotation (Frenet-Serret slerp).
	static Basis parallel_transport_along(const Ref<Curve3D> &p_curve,
			real_t p_src_offset, const Basis &p_src_basis, real_t p_dest_offset);
};

VARIANT_ENUM_CAST(CassieCurvenet::Mode);

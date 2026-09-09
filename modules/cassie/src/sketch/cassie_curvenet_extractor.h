/**************************************************************************/
/*  cassie_curvenet_extractor.h                                           */
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

#include "cassie_curvenet.h"
#include "cassie_surface_patch.h"

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"

// Mesh-to-curvenet auto-extraction (Step 1.0 / ENG-42).
//
// Three passes:
//   1. Per-edge feature scoring (boundary = π, otherwise dihedral angle).
//   2. Adaptive thresholding: log-sweep K (top-K edges to keep), pick the K
//      whose resulting chain count is closest to p_target_curve_count. The
//      sweep avoids the dihedral-threshold cliff that drowns dense scans
//      in noise or starves sparse low-poly meshes.
//   3. Graph simplification: walk chains between vertices of valence ≠ 2,
//      RDP-reduce each chain, refit with cassie_fit_curve.
//
// Output is a Dictionary that CassieCurvenet::build_from_graph consumes
// directly:
//   { "curves": TypedArray<CassieFinalStroke>,
//     "nodes":  Array<Dictionary{ "id": int,
//                                 "position": Vector3,
//                                 "incident_curve_ids": PackedInt32Array }> }
//
// Every curve's start endpoint shows up in exactly one node's
// incident_curve_ids, same for its end endpoint — ENG-45's adjacency
// depends on this invariant (see ENG-57 lessons).
//
// References:
//   - Nguyen et al., "Shaping the Elements" (SIGGRAPH '23 Talks) — the
//     curvenet rig that Pose mode exposes.
//   - de Goes et al., "Character Articulation through Profile Curves"
//     (ACM TOG 41/4 art. 139, 2022) — the underlying curvenet topology.
class CassieCurvenetExtractor : public RefCounted {
	GDCLASS(CassieCurvenetExtractor, RefCounted);

protected:
	static void _bind_methods();

public:
	// Build the graph-data Dictionary directly. Static so callers don't have
	// to manage an extractor instance for a one-shot extraction.
	// p_curvature_weight (ENG-56) blends a per-vertex cotangent-Laplacian
	// magnitude into the edge score. Default 0 reproduces the original
	// dihedral-only behavior; positive values lift extraction quality on
	// smooth scanned input where natural feature lines are curvature
	// ridges rather than crease edges.
	static Dictionary extract_graph_data(const Ref<CassieSurfacePatch> &p_patch,
			int p_target_curve_count = 200,
			float p_rdp_error = 1e-3f,
			float p_fit_error = 1e-2f,
			float p_curvature_weight = 0.0f);

	// Convenience wrapper that extracts and populates a CassieCurvenet
	// in one call. Equivalent to:
	//   Ref<CassieCurvenet> cn; cn.instantiate();
	//   cn->build_from_graph(extract_graph_data(patch, ...));
	//   cn->set_bound_patch(patch); cn->update_rest_pose(patch);
	//   cn->compute_orientations();
	//   return cn;
	static Ref<CassieCurvenet> extract(const Ref<CassieSurfacePatch> &p_patch,
			int p_target_curve_count = 200,
			float p_rdp_error = 1e-3f,
			float p_fit_error = 1e-2f,
			float p_curvature_weight = 0.0f);
};

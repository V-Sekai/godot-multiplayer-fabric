/**************************************************************************/
/*  cassie_surface_manager.h                                              */
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

#include "cassie_sketch_graph.h"
#include "cassie_surface_patch.h"

#include "core/io/resource.h"
#include "core/object/ref_counted.h"
#include "core/object/worker_thread_pool.h"
#include "core/templates/hash_map.h"

// CassieSurfaceManager — reactive cycle → patch lifecycle on top of
// CassieSketchGraph (ENG-77 follow-up). Wraps the
// find_cycles → sample_cycle_boundary → CassieTriangulator → set_mesh
// chain into a single update() call that emits patches for new cycles
// and drops patches for cycles that dissolved.
//
// Identifies cycles by canonical signature (sorted edge ids joined),
// so the same cycle re-detected on later updates reuses its existing
// patch instead of being re-triangulated. The XR demo can call
// surface_mgr.update() once per stroke commit and read the returned
// dict for changed patches.
//
// Quest 3 safety: new-cycle triangulation is kicked off on
// WorkerThreadPool so update() returns immediately. The patch
// materializes on a later frame once the async job completes.
// This hides the ~50 ms triangulation latency without blocking
// the render thread.

class CassieSurfaceManager : public Resource {
	GDCLASS(CassieSurfaceManager, Resource);

	Ref<CassieSketchGraph> graph;
	// Matches the upstream Unity DLL caller (SurfaceManager.cs uses 0.02).
	real_t target_edge_length = real_t(0.02);

	// Canonical signature → active patch. Signature is the cycle's
	// edge ids sorted ascending and joined with ',' — stable across
	// find_cycles invocations regardless of traversal start.
	HashMap<String, Ref<CassieSurfacePatch>> active_patches;

	static String _cycle_signature(const PackedInt32Array &p_cycle);

	// Synchronous patch creation (used when async is disabled or for testing).
	Ref<CassieSurfacePatch> _materialize_patch_sync(
			const PackedInt32Array &p_cycle) const;

	// Turn a triangulation result dict into a surface patch (main thread).
	Ref<CassieSurfacePatch> _patch_from_tri_result(
			const Dictionary &p_tri) const;

	// Async infrastructure ----------------------------------------------------
	struct AsyncPatchJob {
		String signature;
		PackedInt32Array cycle_edge_ids; // for round-trip export_raw_data_dict
		PackedVector3Array boundary;
		real_t target_edge_length = real_t(0.02);
		Dictionary tri_result; // written by worker thread
	};

	// TaskID → owned AsyncPatchJob*.  Erased once the task completes and
	// the patch has been moved into active_patches.
	HashMap<WorkerThreadPool::TaskID, AsyncPatchJob *> pending_jobs;

	static void _async_triangulate(void *p_userdata);
	void _drain_completed_jobs(TypedArray<CassieSurfacePatch> &r_new_patches);
	void _cancel_all_pending();

	bool async_triangulation = true; // runtime toggle

protected:
	static void _bind_methods();

public:
	CassieSurfaceManager() = default;
	~CassieSurfaceManager();

	void set_graph(const Ref<CassieSketchGraph> &p_graph) { graph = p_graph; }
	Ref<CassieSketchGraph> get_graph() const { return graph; }

	void set_target_edge_length(real_t p_l) { target_edge_length = p_l; }
	real_t get_target_edge_length() const { return target_edge_length; }

	void set_async_triangulation(bool p_enable) { async_triangulation = p_enable; }
	bool get_async_triangulation() const { return async_triangulation; }

	// Scan the graph's current cycles. Returns:
	//   "new_patches":     TypedArray<CassieSurfacePatch> — created this call
	//   "removed_patches": TypedArray<CassieSurfacePatch> — gone this call
	//   "active_count":    int — total active after the update
	// Idempotent: calling update() twice with no graph mutation yields
	// empty new/removed arrays and an unchanged active count.
	//
	// When async_triangulation is true, new patches for cycles that
	// have never been seen before are NOT returned immediately.
	// They appear in "new_patches" on the frame where the async worker
	// finishes.  Removed patches are still reported synchronously.
	Dictionary update();

	int get_patch_count() const { return active_patches.size(); }

	// Export the current sketch state as a dictionary matching the upstream
	// CASSIE raw_data/*.json schema (cassie-data/data/raw_data/README.md).
	// Compatible enough to round-trip into the ground-truth diff bench:
	// loaders pick out allSketchedStrokes[*].ctrlPts + allCreatedPatches.
	// Strokes are emitted as 2-pt line ctrlPts (we don't keep beautified
	// polybeziers in the graph today — the curve representation lives on
	// CassieFinalStroke before it lands in the graph). When you need
	// curve fidelity, upgrade the loader to pull from the FinalStroke
	// resource rather than this fallback.
	Dictionary export_raw_data_dict() const;
	TypedArray<CassieSurfacePatch> get_patches() const;
	void clear();
};

/**************************************************************************/
/*  cassie_surface_manager.cpp                                            */
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

#include "cassie_surface_manager.h"

#include "../cassie_triangulator.h"

#include "core/object/class_db.h"
#include "core/object/worker_thread_pool.h"
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "scene/resources/mesh.h"

String CassieSurfaceManager::_cycle_signature(const PackedInt32Array &p_cycle) {
	const int n = p_cycle.size();
	if (n == 0) {
		return String();
	}
	LocalVector<int32_t> sorted;
	sorted.resize(n);
	for (int i = 0; i < n; ++i) {
		sorted[i] = p_cycle[i];
	}
	// std::sort would require <algorithm>; LocalVector::sort delegates
	// to the same so use it directly for trivial ints.
	for (uint32_t i = 1; i < sorted.size(); ++i) {
		const int32_t v = sorted[i];
		int j = int(i) - 1;
		while (j >= 0 && sorted[j] > v) {
			sorted[j + 1] = sorted[j];
			--j;
		}
		sorted[j + 1] = v;
	}
	String out;
	for (uint32_t i = 0; i < sorted.size(); ++i) {
		if (i > 0) {
			out += ",";
		}
		out += itos(sorted[i]);
	}
	return out;
}

#ifdef CASSIE_QUAD_MESHING
// Defined out-of-line in cassie_quad_mesh.cpp (only compiled when the
// SCsub BUILD_QUAD_MESHING toggle is on). Runs Geogram frame field +
// mesh_remesh on the small per-CASSIE-3D-patch tri mesh and produces a
// quad-dominant PackedVector3Array / PackedInt32Array, sized for an
// ArrayMesh PRIMITIVE_TRIANGLE_STRIP or stored as PRIMITIVE_TRIANGLES
// with the quads emitted as 2-tri pairs.
extern bool cassie_quadrangulate_patch(PackedVector3Array &p_verts,
		PackedInt32Array &p_faces);
#endif

Ref<CassieSurfacePatch> CassieSurfaceManager::_patch_from_tri_result(
		const Dictionary &p_tri) const {
	if (!bool(p_tri.get("success", false))) {
		return Ref<CassieSurfacePatch>();
	}
	PackedVector3Array verts = p_tri["vertices"];
	PackedInt32Array faces = p_tri["faces"];
	if (verts.size() < 3 || faces.size() < 3 || faces.size() % 3 != 0) {
		return Ref<CassieSurfacePatch>();
	}

#ifdef CASSIE_QUAD_MESHING
	// Convert this per-CASSIE-3D-patch from tri to quad-dominant in-place.
	// The function leaves verts/faces tri-formatted on failure; the patch
	// still ships, just as the original tri mesh.
	cassie_quadrangulate_patch(verts, faces);
#endif

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_INDEX] = faces;
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(mesh);
	return patch;
}

Ref<CassieSurfacePatch> CassieSurfaceManager::_materialize_patch_sync(
		const PackedInt32Array &p_cycle) const {
	if (graph.is_null()) {
		return Ref<CassieSurfacePatch>();
	}
	const PackedVector3Array boundary =
			graph->sample_cycle_boundary(p_cycle, target_edge_length);
	if (boundary.size() < 3) {
		return Ref<CassieSurfacePatch>();
	}
	Dictionary tri = CassieTriangulator::triangulate(boundary, target_edge_length);
	Ref<CassieSurfacePatch> patch = _patch_from_tri_result(tri);
	if (patch.is_valid()) {
		patch->set_source_cycle_edge_ids(p_cycle);
	}
	return patch;
}

// ------------------------------------------------------------------
// Async worker
// ------------------------------------------------------------------

void CassieSurfaceManager::_async_triangulate(void *p_userdata) {
	AsyncPatchJob *job = static_cast<AsyncPatchJob *>(p_userdata);
	job->tri_result = CassieTriangulator::triangulate(
			job->boundary, job->target_edge_length);
}

void CassieSurfaceManager::_drain_completed_jobs(
		TypedArray<CassieSurfacePatch> &r_new_patches) {
	WorkerThreadPool *wtp = WorkerThreadPool::get_singleton();
	if (!wtp) {
		return;
	}
	LocalVector<WorkerThreadPool::TaskID> done;
	for (const KeyValue<WorkerThreadPool::TaskID, AsyncPatchJob *> &kv :
			pending_jobs) {
		if (wtp->is_task_completed(kv.key)) {
			done.push_back(kv.key);
		}
	}
	for (uint32_t i = 0; i < done.size(); ++i) {
		AsyncPatchJob *job = pending_jobs[done[i]];
		pending_jobs.erase(done[i]);

		Ref<CassieSurfacePatch> patch = _patch_from_tri_result(job->tri_result);
		if (patch.is_valid()) {
			patch->set_source_cycle_edge_ids(job->cycle_edge_ids);
			active_patches.insert(job->signature, patch);
			r_new_patches.push_back(patch);
		}
		memdelete(job);
	}
}

void CassieSurfaceManager::_cancel_all_pending() {
	WorkerThreadPool *wtp = WorkerThreadPool::get_singleton();
	for (const KeyValue<WorkerThreadPool::TaskID, AsyncPatchJob *> &kv :
			pending_jobs) {
		if (wtp) {
			wtp->wait_for_task_completion(kv.key);
		}
		memdelete(kv.value);
	}
	pending_jobs.clear();
}

CassieSurfaceManager::~CassieSurfaceManager() {
	_cancel_all_pending();
}

// ------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------

Dictionary CassieSurfaceManager::update() {
	Dictionary result;
	TypedArray<CassieSurfacePatch> new_patches;
	TypedArray<CassieSurfacePatch> removed_patches;
	if (graph.is_null()) {
		result["new_patches"] = new_patches;
		result["removed_patches"] = removed_patches;
		result["active_count"] = 0;
		return result;
	}

	// 1. Promote any finished async jobs to active patches.
	_drain_completed_jobs(new_patches);

	// 2. Discover current cycles.
	const Array cycles = graph->find_cycles();
	HashSet<String> seen_sigs;
	for (int i = 0; i < cycles.size(); ++i) {
		const PackedInt32Array cycle = cycles[i];
		const String sig = _cycle_signature(cycle);
		if (sig.is_empty()) {
			continue;
		}
		if (seen_sigs.has(sig)) {
			continue;
		}
		seen_sigs.insert(sig);

		// Already active or already pending?  Nothing to do.
		if (active_patches.has(sig)) {
			continue;
		}
		bool already_pending = false;
		for (const KeyValue<WorkerThreadPool::TaskID, AsyncPatchJob *> &kv :
				pending_jobs) {
			if (kv.value->signature == sig) {
				already_pending = true;
				break;
			}
		}
		if (already_pending) {
			continue;
		}

		if (async_triangulation) {
			// Sample boundary on the main thread (fast, reads graph).
			const PackedVector3Array boundary =
					graph->sample_cycle_boundary(cycle, target_edge_length);
			if (boundary.size() < 3) {
				continue;
			}
			AsyncPatchJob *job = memnew(AsyncPatchJob);
			job->signature = sig;
			job->cycle_edge_ids = cycle;
			job->boundary = boundary;
			job->target_edge_length = target_edge_length;
			WorkerThreadPool *wtp = WorkerThreadPool::get_singleton();
			if (wtp) {
				WorkerThreadPool::TaskID tid = wtp->add_native_task(
						&_async_triangulate, job, false,
						"CassieSurfaceManager::triangulate");
				pending_jobs.insert(tid, job);
			} else {
				// Fallback to sync if thread pool is unavailable.
				job->tri_result = CassieTriangulator::triangulate(
						job->boundary, job->target_edge_length);
				Ref<CassieSurfacePatch> patch =
						_patch_from_tri_result(job->tri_result);
				if (patch.is_valid()) {
					patch->set_source_cycle_edge_ids(cycle);
					active_patches.insert(sig, patch);
					new_patches.push_back(patch);
				}
				memdelete(job);
			}
		} else {
			Ref<CassieSurfacePatch> patch = _materialize_patch_sync(cycle);
			if (patch.is_valid()) {
				active_patches.insert(sig, patch);
				new_patches.push_back(patch);
			}
		}
	}

	// 3. Drop signatures we no longer see.
	LocalVector<String> stale;
	for (const KeyValue<String, Ref<CassieSurfacePatch>> &kv : active_patches) {
		if (!seen_sigs.has(kv.key)) {
			stale.push_back(kv.key);
			removed_patches.push_back(kv.value);
		}
	}
	for (uint32_t i = 0; i < stale.size(); ++i) {
		active_patches.erase(stale[i]);
	}

	// Also cancel any pending jobs whose cycle disappeared.
	LocalVector<WorkerThreadPool::TaskID> dead_jobs;
	for (const KeyValue<WorkerThreadPool::TaskID, AsyncPatchJob *> &kv :
			pending_jobs) {
		if (!seen_sigs.has(kv.value->signature)) {
			dead_jobs.push_back(kv.key);
		}
	}
	WorkerThreadPool *wtp = WorkerThreadPool::get_singleton();
	for (uint32_t i = 0; i < dead_jobs.size(); ++i) {
		AsyncPatchJob *job = pending_jobs[dead_jobs[i]];
		pending_jobs.erase(dead_jobs[i]);
		if (wtp) {
			wtp->wait_for_task_completion(dead_jobs[i]);
		}
		memdelete(job);
	}

	result["new_patches"] = new_patches;
	result["removed_patches"] = removed_patches;
	result["active_count"] = active_patches.size();
	return result;
}

TypedArray<CassieSurfacePatch> CassieSurfaceManager::get_patches() const {
	TypedArray<CassieSurfacePatch> out;
	for (const KeyValue<String, Ref<CassieSurfacePatch>> &kv : active_patches) {
		out.push_back(kv.value);
	}
	return out;
}

void CassieSurfaceManager::clear() {
	_cancel_all_pending();
	active_patches.clear();
}

// Helper: serialize a Vector3 as the upstream {"x","y","z"} dict shape.
static Dictionary _v3_to_xyz_dict(const Vector3 &p_v) {
	Dictionary d;
	d["x"] = double(p_v.x);
	d["y"] = double(p_v.y);
	d["z"] = double(p_v.z);
	return d;
}

Dictionary CassieSurfaceManager::export_raw_data_dict() const {
	Dictionary out;
	// Mirror the upstream Unity export's top-level metadata. sketchSystem=2
	// (Patch mode) is our pipeline equivalent — automatic structuring +
	// surface inference is what CassieSurfaceManager does.
	out["sketchSystem"] = 2;
	out["sketchModel"] = 0;
	out["interactionMode"] = 2;
	out["systemStates"] = Array();

	Array strokes;
	if (graph.is_valid()) {
		const int edge_count = graph->get_edge_count();
		for (int i = 0; i < edge_count; ++i) {
			Ref<CassieSketchGraphEdge> e = graph->get_edge(i);
			if (e.is_null()) {
				continue;
			}
			Dictionary s;
			s["id"] = e->get_id();

			// ctrlPts: minimum-fidelity export — 2-pt line ctrlPts from the
			// edge endpoints. Schema explicitly allows 2-pt lines.
			// inputSamples: the polyline samples themselves.
			Array ctrl;
			const PackedVector3Array pts = e->get_points();
			if (pts.size() >= 2) {
				ctrl.push_back(_v3_to_xyz_dict(pts[0]));
				ctrl.push_back(_v3_to_xyz_dict(pts[pts.size() - 1]));
			}
			s["ctrlPts"] = ctrl;

			Array input_samples;
			for (int k = 0; k < pts.size(); ++k) {
				const Vector3 p = pts[k];
				Array xyz;
				xyz.push_back(double(p.x));
				xyz.push_back(double(p.y));
				xyz.push_back(double(p.z));
				input_samples.push_back(xyz);
			}
			s["inputSamples"] = input_samples;

			s["appliedPositionConstraints"] = Array();
			s["rejectedPositionConstraints"] = Array();
			strokes.push_back(s);
		}
	}
	out["allSketchedStrokes"] = strokes;

	Array patches;
	int patch_id = 0;
	for (const KeyValue<String, Ref<CassieSurfacePatch>> &kv : active_patches) {
		if (kv.value.is_null()) {
			continue;
		}
		Dictionary p;
		p["id"] = patch_id++;
		p["foundByAlgo"] = true;
		PackedInt32Array stroke_ids = kv.value->get_source_cycle_edge_ids();
		Array sids;
		for (int i = 0; i < stroke_ids.size(); ++i) {
			sids.push_back(int(stroke_ids[i]));
		}
		p["strokesID"] = sids;
		patches.push_back(p);
	}
	out["allCreatedPatches"] = patches;

	return out;
}

void CassieSurfaceManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_graph", "graph"),
			&CassieSurfaceManager::set_graph);
	ClassDB::bind_method(D_METHOD("get_graph"),
			&CassieSurfaceManager::get_graph);
	ClassDB::bind_method(D_METHOD("set_target_edge_length", "length"),
			&CassieSurfaceManager::set_target_edge_length);
	ClassDB::bind_method(D_METHOD("get_target_edge_length"),
			&CassieSurfaceManager::get_target_edge_length);
	ClassDB::bind_method(D_METHOD("set_async_triangulation", "enable"),
			&CassieSurfaceManager::set_async_triangulation);
	ClassDB::bind_method(D_METHOD("get_async_triangulation"),
			&CassieSurfaceManager::get_async_triangulation);
	ClassDB::bind_method(D_METHOD("export_raw_data_dict"),
			&CassieSurfaceManager::export_raw_data_dict);
	ClassDB::bind_method(D_METHOD("update"), &CassieSurfaceManager::update);
	ClassDB::bind_method(D_METHOD("get_patch_count"),
			&CassieSurfaceManager::get_patch_count);
	ClassDB::bind_method(D_METHOD("get_patches"),
			&CassieSurfaceManager::get_patches);
	ClassDB::bind_method(D_METHOD("clear"), &CassieSurfaceManager::clear);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "async_triangulation"),
			"set_async_triangulation", "get_async_triangulation");
}

/**************************************************************************/
/*  test_cassie_pipeline_bench.h                                          */
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

#include "../src/cassie_triangulator.h"
#include "../src/sketch/cassie_curvenet.h"
#include "../src/sketch/cassie_curvenet_extractor.h"
#include "../src/sketch/cassie_final_stroke.h"
#include "../src/sketch/cassie_sketch_graph.h"
#include "../src/sketch/cassie_surface_manager.h"
#include "../src/sketch/cassie_surface_patch.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/math/vector3.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/variant.h"
#include "scene/resources/curve.h"
#include "scene/resources/mesh.h"
#include "tests/test_macros.h"

#include <cfloat>
#include <vector>

namespace TestCassiePipelineBench {

// ── Stroke generation helpers ─────────────────────────────────────────────

static PackedVector3Array _segment(const Vector3 &a, const Vector3 &b,
		int p_samples = 8) {
	PackedVector3Array pts;
	pts.resize(p_samples);
	for (int i = 0; i < p_samples; ++i) {
		const real_t t = real_t(i) / real_t(p_samples - 1);
		pts.write[i] = a.lerp(b, t);
	}
	return pts;
}

static PackedVector3Array _up_normals(int p_count) {
	PackedVector3Array out;
	out.resize(p_count);
	for (int i = 0; i < p_count; ++i) {
		out.write[i] = Vector3(0, 0, 1); // strokes lie in the xy plane
	}
	return out;
}

// Equilateral-ish triangle in xy — the verification-gate workload. One
// cycle of three edges; cheapest non-degenerate input to the pipeline.
static void _build_triangle(Ref<CassieSketchGraph> p_g) {
	const Vector3 v0(0, 0, 0);
	const Vector3 v1(1, 0, 0);
	const Vector3 v2(real_t(0.5), real_t(0.866), 0);
	p_g->add_stroke(_segment(v0, v1), _up_normals(8));
	p_g->add_stroke(_segment(v1, v2), _up_normals(8));
	p_g->add_stroke(_segment(v2, v0), _up_normals(8));
}

// N×N grid of unit-spaced nodes in the xy plane joined by horizontal +
// vertical strokes. Produces (N-1)² minimal quad cycles. Stroke count is
// 2·N·(N-1); node count is N²; edge count equals stroke count.
static void _build_grid(Ref<CassieSketchGraph> p_g, int N) {
	const real_t spacing = real_t(0.5);
	// Horizontal strokes: rows of N-1 segments × N rows.
	for (int j = 0; j < N; ++j) {
		for (int i = 0; i < N - 1; ++i) {
			const Vector3 a(real_t(i) * spacing, real_t(j) * spacing, 0);
			const Vector3 b(real_t(i + 1) * spacing, real_t(j) * spacing, 0);
			p_g->add_stroke(_segment(a, b), _up_normals(8));
		}
	}
	// Vertical strokes: columns of N-1 segments × N columns.
	for (int i = 0; i < N; ++i) {
		for (int j = 0; j < N - 1; ++j) {
			const Vector3 a(real_t(i) * spacing, real_t(j) * spacing, 0);
			const Vector3 b(real_t(i) * spacing, real_t(j + 1) * spacing, 0);
			p_g->add_stroke(_segment(a, b), _up_normals(8));
		}
	}
}

// ── Memory-proxy collection ───────────────────────────────────────────────

struct PipelineCounts {
	int node_count = 0;
	int edge_count = 0;
	int cycle_count = 0;
	int total_boundary_pts = 0;
	int total_mesh_vertices = 0;
	int total_mesh_faces = 0;
};

// Sum mesh-array sizes across all active patches. Each patch is one
// ArrayMesh surface authored by CassieTriangulator → ARRAY_VERTEX +
// ARRAY_INDEX, so vertex count = ARRAY_VERTEX.size() and face count =
// ARRAY_INDEX.size() / 3.
static void _accumulate_mesh_counts(const TypedArray<CassieSurfacePatch> &p_patches,
		PipelineCounts &r_counts) {
	for (int i = 0; i < p_patches.size(); ++i) {
		Ref<CassieSurfacePatch> patch = p_patches[i];
		if (patch.is_null()) {
			continue;
		}
		Ref<Mesh> mesh = patch->get_mesh();
		if (mesh.is_null() || mesh->get_surface_count() == 0) {
			continue;
		}
		Array arrays = mesh->surface_get_arrays(0);
		if (arrays.size() < Mesh::ARRAY_MAX) {
			continue;
		}
		PackedVector3Array verts = arrays[Mesh::ARRAY_VERTEX];
		PackedInt32Array idx = arrays[Mesh::ARRAY_INDEX];
		r_counts.total_mesh_vertices += verts.size();
		r_counts.total_mesh_faces += idx.size() / 3;
	}
}

// ── Core measurement routine ──────────────────────────────────────────────
//
// One scale → six lines of output, all prefixed with [CassiePipelineBench]
// for grep extraction. Stages:
//   populate_us         — sum of add_stroke() calls
//   find_cycles_us      — single find_cycles() invocation (warmed)
//   sample_boundary_us  — sum of sample_cycle_boundary() across all cycles
//   manager_update_us   — full update() (signature → triangulate → mesh)
//   counts              — node/edge/cycle/boundary-pt totals
//   mesh                — vertex + face totals across all patches
//
// Returns the total wall time consumed by the four timed stages so the
// caller can enforce a soft budget on optional larger scales.
static uint64_t _run_scale(const String &p_label, int p_grid_N,
		real_t p_target_edge_length) {
	const Time *time = Time::get_singleton();

	// Build the graph fresh for the populate measurement. The triangle
	// case uses _build_triangle; everything else uses _build_grid.
	auto build_into = [&](Ref<CassieSketchGraph> g) {
		if (p_grid_N <= 0) {
			_build_triangle(g);
		} else {
			_build_grid(g, p_grid_N);
		}
	};

	// Warm-up: populate, find_cycles, sample, materialize once on a
	// throwaway graph + manager so JIT, allocator, and CSR-style cache
	// effects don't bleed into the first measured stage.
	{
		Ref<CassieSketchGraph> warm_g;
		warm_g.instantiate();
		build_into(warm_g);
		Array warm_cycles = warm_g->find_cycles();
		for (int i = 0; i < warm_cycles.size(); ++i) {
			const PackedInt32Array c = warm_cycles[i];
			warm_g->sample_cycle_boundary(c, p_target_edge_length);
		}
		Ref<CassieSurfaceManager> warm_mgr;
		warm_mgr.instantiate();
		warm_mgr->set_graph(warm_g);
		warm_mgr->set_target_edge_length(p_target_edge_length);
		warm_mgr->set_async_triangulation(false); // bench measures sync latency
		warm_mgr->update();
	}

	// Stage 1 — graph populate (sum of add_stroke calls).
	Ref<CassieSketchGraph> g;
	g.instantiate();
	const uint64_t pop_t0 = time->get_ticks_usec();
	build_into(g);
	const uint64_t pop_us = time->get_ticks_usec() - pop_t0;

	PipelineCounts counts;
	counts.node_count = g->get_node_count();
	counts.edge_count = g->get_edge_count();

	// Stage 2 — find_cycles.
	const uint64_t fc_t0 = time->get_ticks_usec();
	Array cycles = g->find_cycles();
	const uint64_t fc_us = time->get_ticks_usec() - fc_t0;
	counts.cycle_count = cycles.size();

	// Stage 3 — sample_cycle_boundary summed across all cycles.
	uint64_t sb_us = 0;
	for (int i = 0; i < cycles.size(); ++i) {
		const PackedInt32Array c = cycles[i];
		const uint64_t sb_t0 = time->get_ticks_usec();
		PackedVector3Array boundary = g->sample_cycle_boundary(c, p_target_edge_length);
		sb_us += time->get_ticks_usec() - sb_t0;
		counts.total_boundary_pts += boundary.size();
	}

	// Stage 4 — SurfaceManager::update (full materialize-and-track loop).
	Ref<CassieSurfaceManager> mgr;
	mgr.instantiate();
	mgr->set_graph(g);
	mgr->set_target_edge_length(p_target_edge_length);
	mgr->set_async_triangulation(false); // bench measures sync latency
	const uint64_t mu_t0 = time->get_ticks_usec();
	Dictionary upd = mgr->update();
	const uint64_t mu_us = time->get_ticks_usec() - mu_t0;

	const TypedArray<CassieSurfacePatch> new_p = upd["new_patches"];
	_accumulate_mesh_counts(new_p, counts);

	// Godot's String::sprintf only supports %d/%f/%s — cast uint64 µs
	// to int (max ~2.1 G µs ≈ 35 min, plenty of headroom for this bench).
	MESSAGE(vformat(
			"[CassiePipelineBench] %s  populate=%d us  find_cycles=%d us  "
			"sample_boundary=%d us  manager_update=%d us",
			p_label, int(pop_us), int(fc_us), int(sb_us), int(mu_us)));
	MESSAGE(vformat(
			"[CassiePipelineBench] %s  nodes=%d  edges=%d  cycles=%d  "
			"boundary_pts=%d  mesh_verts=%d  mesh_faces=%d",
			p_label, counts.node_count, counts.edge_count, counts.cycle_count,
			counts.total_boundary_pts, counts.total_mesh_vertices,
			counts.total_mesh_faces));

	return pop_us + fc_us + sb_us + mu_us;
}

// ── Trivial scale ─────────────────────────────────────────────────────────

TEST_CASE_PENDING("[Cassie][PipelineBench] Trivial: 3-stroke triangle, 1 cycle") {
	_run_scale("trivial(3-stroke-triangle)", /*grid_N=*/0, real_t(0.15));
}

// ── Small scale ───────────────────────────────────────────────────────────

TEST_CASE_PENDING("[Cassie][PipelineBench] Small: 4x4 grid, 9 quad cycles") {
	_run_scale("small(4x4-grid)", /*grid_N=*/4, real_t(0.1));
}

// ── Medium scale ──────────────────────────────────────────────────────────

TEST_CASE_PENDING("[Cassie][PipelineBench] Medium: 8x8 grid, 49 quad cycles") {
	_run_scale("medium(8x8-grid)", /*grid_N=*/8, real_t(0.1));
}

// ── Optional larger scale, guarded by a 2-second wall budget ──────────────
//
// 16x16 = 225 cycles. find_cycles is roughly O(E · (E+2) · d_max), so this
// scale is where the planar finder is expected to feel non-trivial. If the
// summed measured wall time exceeds 2 s we MESSAGE the breach instead of
// failing — this case is opt-in (TEST_CASE_PENDING) and only meaningful
// when the four stages all complete within a single interactive frame's
// worth of headroom.

TEST_CASE_PENDING("[Cassie][PipelineBench] Large: 16x16 grid, 225 quad cycles (2 s budget)") {
	const uint64_t budget_us = 2'000'000ULL;
	const uint64_t total_us = _run_scale("large(16x16-grid)", /*grid_N=*/16, real_t(0.1));
	if (total_us > budget_us) {
		MESSAGE(vformat(
				"[CassiePipelineBench] large(16x16-grid) BUDGET-EXCEEDED  "
				"total=%d us (budget=%d us) — treat as soft skip; tune "
				"target_edge_length or drop scale.",
				int(total_us), int(budget_us)));
	}
}

// ── Real-mesh scale — Sketchfab character mesh ────────────────────────────
//
// Loads a binary mesh dump from `E:\tmp\cassie_bench_character.bin` exported
// from the Sketchfab "Low Poly Character Base Mesh" (Qirin, CC-BY,
// uid 130eb07f553d49bd9012ab6717c897e8) via Blender at 1.7 m scale —
// 1918 verts, 1580 triangles. Binary format:
//   magic 4 bytes "CSBM"
//   uint32 LE vertex_count
//   uint32 LE triangle_count
//   vertex_count × float32 LE × 3   (positions)
//   triangle_count × uint32 LE × 3  (indices)
//
// Pipeline routed through CassieCurvenetExtractor (per the standing
// memory: "For real-mesh benches use CassieCurvenetExtractor.extract(patch,
// 200); never hand-author Curve3Ds through the AABB"). The extractor
// produces a curvenet; each curve's tessellated points are pushed as a
// stroke into the same CassieSketchGraph → SurfaceManager pipeline the
// synthetic scales exercise.
//
// Skips with a MESSAGE if the .bin isn't on disk so CI without the
// asset stays green.

static Ref<ArrayMesh> _load_bench_mesh(const String &p_path) {
	Ref<ArrayMesh> mesh;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	if (f.is_null()) {
		return mesh;
	}
	uint8_t magic[4] = {};
	if (f->get_buffer(magic, 4) != 4 ||
			magic[0] != 'C' || magic[1] != 'S' || magic[2] != 'B' || magic[3] != 'M') {
		return mesh;
	}
	const uint32_t vc = f->get_32();
	const uint32_t tc = f->get_32();
	if (vc == 0 || tc == 0) {
		return mesh;
	}
	PackedVector3Array verts;
	verts.resize(int(vc));
	for (uint32_t i = 0; i < vc; ++i) {
		const float x = f->get_float();
		const float y = f->get_float();
		const float z = f->get_float();
		verts.write[i] = Vector3(real_t(x), real_t(y), real_t(z));
	}
	PackedInt32Array idx;
	idx.resize(int(tc) * 3);
	for (uint32_t i = 0; i < tc; ++i) {
		idx.write[i * 3 + 0] = int(f->get_32());
		idx.write[i * 3 + 1] = int(f->get_32());
		idx.write[i * 3 + 2] = int(f->get_32());
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_INDEX] = idx;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

TEST_CASE_PENDING("[Cassie][PipelineBench] Real mesh: Sketchfab character → curvenet → graph → SurfaceManager") {
	// Path resolution: CASSIE_BENCH_MESH_PATH env var wins, otherwise the
	// platform-default tmp paths. Lets the same binary run on desktop
	// (Windows E:/tmp) and the Steam Deck / Quest 3 (/home/deck/cassie-bench)
	// without re-compilation.
	String mesh_path;
	if (OS::get_singleton()->has_environment("CASSIE_BENCH_MESH_PATH")) {
		mesh_path = OS::get_singleton()->get_environment("CASSIE_BENCH_MESH_PATH");
	} else {
#ifdef WINDOWS_ENABLED
		mesh_path = "E:/tmp/cassie_bench_character.bin";
#else
		mesh_path = "/tmp/cassie_bench_character.bin";
#endif
	}
	Ref<ArrayMesh> mesh = _load_bench_mesh(mesh_path);
	if (mesh.is_null()) {
		MESSAGE(vformat(
				"[CassiePipelineBench] real-mesh skipped: %s not found. Run the "
				"Blender export step from the session log to regenerate.",
				mesh_path));
		return;
	}
	const Time *time = Time::get_singleton();

	// Stage 0 — wrap mesh in SurfacePatch (rest-pose ingestion, normal
	// computation, BVH build).
	const uint64_t sp_t0 = time->get_ticks_usec();
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(mesh);
	const uint64_t sp_us = time->get_ticks_usec() - sp_t0;

	// Stage 1 — curvenet extraction (the "automatic detection" path the
	// editing demo uses for first-time mesh ingestion).
	const uint64_t ex_t0 = time->get_ticks_usec();
	Ref<CassieCurvenet> cn = CassieCurvenetExtractor::extract(patch, 200);
	const uint64_t ex_us = time->get_ticks_usec() - ex_t0;
	REQUIRE_MESSAGE(cn.is_valid(),
			"CassieCurvenetExtractor::extract should yield a valid curvenet for the character");
	const TypedArray<CassieFinalStroke> curves = cn->get_curves();
	const int curve_count = curves.size();

	// Stage 2 — populate CassieSketchGraph from each extracted curve.
	// Curve3D::tessellate_even_length gives the stroke samples; flat
	// up-normals (mesh is roughly Y-up after the export).
	Ref<CassieSketchGraph> g;
	g.instantiate();
	int total_input_samples = 0;
	const uint64_t pop_t0 = time->get_ticks_usec();
	for (int i = 0; i < curve_count; ++i) {
		Ref<CassieFinalStroke> stroke = curves[i];
		if (stroke.is_null()) {
			continue;
		}
		Ref<Curve3D> c = stroke->get_curve();
		if (c.is_null() || c->get_point_count() < 2) {
			continue;
		}
		const PackedVector3Array pts = c->tessellate_even_length(4, real_t(0.02));
		if (pts.size() < 2) {
			continue;
		}
		PackedVector3Array nrms;
		nrms.resize(pts.size());
		for (int j = 0; j < pts.size(); ++j) {
			nrms.write[j] = Vector3(0, 1, 0);
		}
		g->add_stroke(pts, nrms);
		total_input_samples += pts.size();
	}
	const uint64_t pop_us = time->get_ticks_usec() - pop_t0;

	// Stage 3 — find_cycles.
	const uint64_t fc_t0 = time->get_ticks_usec();
	Array cycles = g->find_cycles();
	const uint64_t fc_us = time->get_ticks_usec() - fc_t0;

	// Stage 4 — SurfaceManager update.
	Ref<CassieSurfaceManager> mgr;
	mgr.instantiate();
	mgr->set_graph(g);
	mgr->set_target_edge_length(real_t(0.05));
	mgr->set_async_triangulation(false); // bench measures sync latency
	const uint64_t mu_t0 = time->get_ticks_usec();
	Dictionary upd = mgr->update();
	const uint64_t mu_us = time->get_ticks_usec() - mu_t0;

	PipelineCounts counts;
	counts.node_count = g->get_node_count();
	counts.edge_count = g->get_edge_count();
	counts.cycle_count = cycles.size();
	const TypedArray<CassieSurfacePatch> new_patches = upd["new_patches"];
	_accumulate_mesh_counts(new_patches, counts);

	MESSAGE(vformat(
			"[CassiePipelineBench] real-mesh(character-1.7m)  source_verts=%d  "
			"source_tris=%d  curves=%d  input_samples=%d",
			mesh->get_surface_count() > 0
					? int(PackedVector3Array(mesh->surface_get_arrays(0)[Mesh::ARRAY_VERTEX]).size())
					: 0,
			mesh->get_surface_count() > 0
					? int(PackedInt32Array(mesh->surface_get_arrays(0)[Mesh::ARRAY_INDEX]).size()) / 3
					: 0,
			curve_count, total_input_samples));
	MESSAGE(vformat(
			"[CassiePipelineBench] real-mesh(character-1.7m)  set_mesh=%d us  "
			"extract_curvenet=%d us  populate_graph=%d us  find_cycles=%d us  "
			"manager_update=%d us",
			int(sp_us), int(ex_us), int(pop_us), int(fc_us), int(mu_us)));
	MESSAGE(vformat(
			"[CassiePipelineBench] real-mesh(character-1.7m)  nodes=%d  edges=%d  "
			"cycles=%d  new_patches=%d  mesh_verts=%d  mesh_faces=%d",
			counts.node_count, counts.edge_count, counts.cycle_count,
			new_patches.size(), counts.total_mesh_vertices,
			counts.total_mesh_faces));
}

// ── Planar-loop probe ─────────────────────────────────────────────────────
//
// Picks the kind of input you'd get if the user clicked on a flat region of
// a Sketchfab model — coplanar samples on a closed boundary loop. Three
// shapes exercise different RDP regimes:
//   - square_20pts: 4 corners + 4 mid-edge samples per side → RDP best case
//     (long colinear runs collapse to 4 corners)
//   - circle_20pts: uniformly curved → RDP worst case (every sample preserved)
//   - L_shape_24pts: concavity with mixed straight & corner segments
//
// All loops lie in y=0 — Geogram's 3D Delaunay returns empty on the first try,
// MingCurve perturbs (CASSIE_TRIANGULATOR_PROFILE shows perturb iters>0) then
// retries until 3D works. The 2D fallback also exists in cassie::DelaunayFaces
// for the truly-flat case. Watch the probe output to see which path each loop
// takes.

static PackedVector3Array _make_planar_square(int p_per_side) {
	// 4 sides × p_per_side samples, last sample of each side shared with first
	// of next. Total = 4*(p_per_side - 1) points, evenly spaced.
	PackedVector3Array pts;
	const Vector3 corners[4] = {
		Vector3(0, 0, 0),
		Vector3(1, 0, 0),
		Vector3(1, 0, 1),
		Vector3(0, 0, 1),
	};
	for (int s = 0; s < 4; ++s) {
		const Vector3 a = corners[s];
		const Vector3 b = corners[(s + 1) % 4];
		for (int i = 0; i < p_per_side - 1; ++i) {
			const real_t t = real_t(i) / real_t(p_per_side - 1);
			pts.push_back(a.lerp(b, t));
		}
	}
	return pts;
}

static PackedVector3Array _make_planar_circle(int p_count, real_t p_radius) {
	PackedVector3Array pts;
	pts.resize(p_count);
	for (int i = 0; i < p_count; ++i) {
		const real_t a = real_t(i) * real_t(Math::TAU) / real_t(p_count);
		pts.write[i] = Vector3(p_radius * Math::cos(a), 0, p_radius * Math::sin(a));
	}
	return pts;
}

static PackedVector3Array _make_planar_L(int p_per_side) {
	// L-shape outline, six straight segments. Tests concavity + straight runs.
	const Vector3 corners[6] = {
		Vector3(0, 0, 0),
		Vector3(2, 0, 0),
		Vector3(2, 0, 1),
		Vector3(1, 0, 1),
		Vector3(1, 0, 2),
		Vector3(0, 0, 2),
	};
	PackedVector3Array pts;
	for (int s = 0; s < 6; ++s) {
		const Vector3 a = corners[s];
		const Vector3 b = corners[(s + 1) % 6];
		for (int i = 0; i < p_per_side - 1; ++i) {
			const real_t t = real_t(i) / real_t(p_per_side - 1);
			pts.push_back(a.lerp(b, t));
		}
	}
	return pts;
}

static void _probe_planar(const String &p_label, const PackedVector3Array &p_loop,
		real_t p_target_edge_length) {
	const Time *time = Time::get_singleton();
	const uint64_t t0 = time->get_ticks_usec();
	Dictionary r = CassieTriangulator::triangulate(p_loop, p_target_edge_length);
	const uint64_t us = time->get_ticks_usec() - t0;
	const bool ok = r.get("success", false);
	PackedVector3Array v = r.get("vertices", PackedVector3Array());
	PackedInt32Array f = r.get("faces", PackedInt32Array());
	MESSAGE(vformat(
			"[CassiePlanarProbe] %s  nB_in=%d  ok=%s  out_verts=%d  out_faces=%d  total=%d us",
			p_label, p_loop.size(), ok ? "yes" : "no",
			v.size(), int(f.size() / 3), int(us)));
}

// ── Real CASSIE-data input strokes ────────────────────────────────────────
//
// Loads from thirdparty/cassie-data/curves/<name>.curves and runs the
// triangulator on each stroke (closed by appending the start sample).
// Lets the perf benches measure against hand-jittered user data instead
// of synthetic loops — see thirdparty/cassie-data/README.md for the
// common-vs-wow case discussion.

static std::vector<PackedVector3Array> _load_curves_file(const String &p_path) {
	std::vector<PackedVector3Array> strokes;
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	if (f.is_null() || err != OK) {
		return strokes;
	}
	PackedVector3Array cur;
	int need = 0;
	while (!f->eof_reached()) {
		const String line = f->get_line().strip_edges();
		if (line.is_empty()) {
			continue;
		}
		if (line.begins_with("v ")) {
			if (cur.size() > 0) {
				strokes.push_back(cur);
				cur = PackedVector3Array();
			}
			need = line.substr(2).to_int();
			cur.reserve(need);
			continue;
		}
		const PackedFloat64Array parts = line.split_floats(" ");
		if (parts.size() < 3) {
			continue;
		}
		cur.push_back(Vector3(float(parts[0]), float(parts[1]), float(parts[2])));
	}
	if (cur.size() > 0) {
		strokes.push_back(cur);
	}
	return strokes;
}

static String _cassie_data_curve_path(const String &p_file) {
	// Try CASSIE_DATA_PATH env override first (lets the bench run from
	// arbitrary working directories on CI), otherwise resolve relative
	// to the repo root.
	if (OS::get_singleton()->has_environment("CASSIE_DATA_PATH")) {
		return OS::get_singleton()->get_environment("CASSIE_DATA_PATH").path_join(p_file);
	}
	return String("thirdparty/cassie-data/curves").path_join(p_file);
}

// Dump our pipeline's per-patch mesh output as JSON, for Blender comparison.
// Gated by CASSIE_DUMP_PATCHES_JSON (=output file path). Triggered by the
// dedicated dump TEST_CASE; the helper is reusable from any harness.
static void _dump_our_patches_json(const String &p_label,
		const Ref<CassieSketchGraph> &p_graph, const Array &p_cycles,
		real_t p_target_edge_length) {
	if (!OS::get_singleton()->has_environment("CASSIE_DUMP_PATCHES_JSON")) {
		return;
	}
	const String out_path =
			OS::get_singleton()->get_environment("CASSIE_DUMP_PATCHES_JSON");
	Ref<FileAccess> wf = FileAccess::open(out_path, FileAccess::WRITE);
	if (wf.is_null()) {
		return;
	}
	wf->store_string(vformat("{\"label\":\"%s\",\"patches\":[", p_label));
	bool first_patch = true;
	for (int i = 0; i < p_cycles.size(); ++i) {
		const PackedInt32Array cycle = p_cycles[i];
		if (cycle.size() < 3) {
			continue;
		}
		const PackedVector3Array boundary =
				p_graph->sample_cycle_boundary(cycle, p_target_edge_length);
		if (boundary.size() < 3) {
			continue;
		}
		Dictionary r =
				CassieTriangulator::triangulate(boundary, p_target_edge_length);
		if (!bool(r.get("success", false))) {
			continue;
		}
		PackedVector3Array verts = r.get("vertices", PackedVector3Array());
		PackedInt32Array faces = r.get("faces", PackedInt32Array());
		if (verts.size() == 0 || faces.size() == 0) {
			continue;
		}
		if (!first_patch) {
			wf->store_string(",");
		}
		first_patch = false;
		wf->store_string("{\"verts\":[");
		for (int v = 0; v < verts.size(); ++v) {
			if (v > 0) {
				wf->store_string(",");
			}
			const Vector3 p = verts[v];
			wf->store_string(vformat("[%.6f,%.6f,%.6f]",
					double(p.x), double(p.y), double(p.z)));
		}
		wf->store_string("],\"faces\":[");
		for (int fi = 0; fi < faces.size(); ++fi) {
			if (fi > 0) {
				wf->store_string(",");
			}
			wf->store_string(itos(faces[fi]));
		}
		wf->store_string("]}");
	}
	wf->store_string("]}\n");
}

// Runs the real editing pipeline on a .curves file:
//   load strokes → CassieSketchGraph::add_stroke (auto-merges nearby
//   endpoints into shared nodes) → find_cycles → for each cycle,
//   sample_cycle_boundary → CassieTriangulator::triangulate.
// Reports per-cycle wall time and aggregate refine sub-stage breakdown.
static void _probe_curves_file(const String &p_label, const String &p_filename,
		real_t p_target_edge_length, int p_max_cycles = 16) {
	const String path = _cassie_data_curve_path(p_filename);
	std::vector<PackedVector3Array> strokes = _load_curves_file(path);
	if (strokes.empty()) {
		MESSAGE(vformat(
				"[CassieDataProbe] %s skipped: %s not found or empty", p_label, path));
		return;
	}
	const Time *time = Time::get_singleton();
	Ref<CassieSketchGraph> graph;
	graph.instantiate();
	const PackedVector3Array empty_normals;
	const uint64_t t_pop0 = time->get_ticks_usec();
	int added = 0;
	for (size_t i = 0; i < strokes.size(); ++i) {
		if (strokes[i].size() < 2) {
			continue;
		}
		if (graph->add_stroke(strokes[i], empty_normals) >= 0) {
			added++;
		}
	}
	const uint64_t pop_us = time->get_ticks_usec() - t_pop0;

	const uint64_t t_fc0 = time->get_ticks_usec();
	const Array cycles = graph->find_cycles();
	const uint64_t fc_us = time->get_ticks_usec() - t_fc0;

	int succ = 0, fail = 0, attempted = 0;
	uint64_t tri_total_us = 0;
	int total_nB = 0, total_out_v = 0, total_out_f = 0;
	int nB_min = INT32_MAX, nB_max = 0;
	for (int i = 0; i < cycles.size() && attempted < p_max_cycles; ++i) {
		const PackedInt32Array cycle = cycles[i];
		if (cycle.size() < 3) {
			continue;
		}
		const PackedVector3Array boundary =
				graph->sample_cycle_boundary(cycle, p_target_edge_length);
		if (boundary.size() < 3) {
			continue;
		}
		attempted++;
		const int nb = boundary.size();
		if (nb < nB_min) {
			nB_min = nb;
		}
		if (nb > nB_max) {
			nB_max = nb;
		}
		// CASSIE_DUMP_BOUNDARY=<dir> writes each cycle's boundary as a flat
		// stride-3 double text file, one row "x y z" per sample. Lets the
		// Unity Triangulation_dll harness consume the exact same input we
		// feed to CassieTriangulator for apples-to-apples timing.
		if (OS::get_singleton()->has_environment("CASSIE_DUMP_BOUNDARY")) {
			const String dir = OS::get_singleton()->get_environment("CASSIE_DUMP_BOUNDARY");
			const String out_path = dir.path_join(vformat(
					"%s_cycle%d_nB%d_tel%.3f.txt", p_label, i, nb,
					double(p_target_edge_length)));
			Ref<FileAccess> wf = FileAccess::open(out_path, FileAccess::WRITE);
			if (wf.is_valid()) {
				for (int j = 0; j < nb; ++j) {
					const Vector3 p = boundary[j];
					wf->store_line(vformat("%.17g %.17g %.17g", double(p.x), double(p.y), double(p.z)));
				}
			}
		}
		const uint64_t t0 = time->get_ticks_usec();
		Dictionary r = CassieTriangulator::triangulate(boundary, p_target_edge_length);
		const uint64_t us = time->get_ticks_usec() - t0;
		tri_total_us += us;
		if (bool(r.get("success", false))) {
			succ++;
			PackedVector3Array v = r.get("vertices", PackedVector3Array());
			PackedInt32Array fi = r.get("faces", PackedInt32Array());
			total_nB += nb;
			total_out_v += v.size();
			total_out_f += int(fi.size() / 3);
		} else {
			fail++;
		}
	}
	MESSAGE(vformat(
			"[CassieDataProbe] %s  strokes_loaded=%d  added=%d  nodes=%d  edges=%d  "
			"cycles_found=%d  populate=%d us  find_cycles=%d us",
			p_label, int(strokes.size()), added,
			graph->get_node_count(), graph->get_edge_count(),
			int(cycles.size()), int(pop_us), int(fc_us)));
	MESSAGE(vformat(
			"[CassieDataProbe] %s  cycles_attempted=%d  succ=%d  fail=%d  "
			"nB_min/avg/max=(%d,%d,%d)  triangulate_total=%d us  avg_per_cycle=%d us  "
			"out_verts_sum=%d  out_faces_sum=%d",
			p_label, attempted, succ, fail,
			(attempted > 0 ? nB_min : 0),
			(succ > 0 ? total_nB / succ : 0),
			nB_max,
			int(tri_total_us),
			(attempted > 0 ? int(tri_total_us) / attempted : 0),
			total_out_v, total_out_f));
}

// ── Border-set diff against raw_data ─────────────────────────────────────
//
// The .curves dataset only has raw stroke samples — no graph topology,
// no patches. raw_data/*.json is the full export from CASSIE's Unity
// app: each stroke has beautified ctrlPts AND raw inputSamples AND
// the appliedPositionConstraints that determined endpoint snapping,
// plus allCreatedPatches[].strokesID — the SET of stroke IDs that
// borders each surface patch the upstream algorithm created. This
// is NOT a reference mesh — the dataset README is explicit: "the
// mesh data for each patch was not recorded, only creation events
// and manual deletion events". Calling it "ground truth output"
// would be wrong; it's a border-stroke-set ground truth only.
//
// We can't compare meshes (the dataset never recorded them — only
// strokesID per patch). But we CAN compare cycle topology: which
// strokes form a patch boundary, set-equality. That's the right
// validation surface for our CassieSketchGraph::find_cycles.
//
// Note: the raw_data was captured by an older Unity CASSIE; current
// E:\cassie may produce a slightly different patch count. The diff is
// a guide to whether we're in the right ballpark, not a literal
// must-match target.

static Vector3 _v3_from_json(const Dictionary &p_d) {
	return Vector3(real_t(double(p_d.get("x", 0.0))),
			real_t(double(p_d.get("y", 0.0))),
			real_t(double(p_d.get("z", 0.0))));
}

// Flatten a polybezier ctrlPts list to a polyline. ctrlPts.size() == 2
// is a line segment; otherwise it's a cubic polybezier with
// (size - 1) / 3 segments (sharing endpoints), per the dataset README.
static PackedVector3Array _flatten_ctrl_pts(const Array &p_ctrl_pts,
		int p_samples_per_segment = 8) {
	PackedVector3Array out;
	const int n = p_ctrl_pts.size();
	if (n < 2) {
		return out;
	}
	if (n == 2) {
		out.push_back(_v3_from_json(p_ctrl_pts[0]));
		out.push_back(_v3_from_json(p_ctrl_pts[1]));
		return out;
	}
	const int segments = (n - 1) / 3;
	if (segments < 1) {
		return out;
	}
	const int spp = MAX(2, p_samples_per_segment);
	for (int s = 0; s < segments; ++s) {
		const Vector3 p0 = _v3_from_json(p_ctrl_pts[3 * s]);
		const Vector3 p1 = _v3_from_json(p_ctrl_pts[3 * s + 1]);
		const Vector3 p2 = _v3_from_json(p_ctrl_pts[3 * s + 2]);
		const Vector3 p3 = _v3_from_json(p_ctrl_pts[3 * s + 3]);
		const int t_count = (s == segments - 1) ? spp + 1 : spp;
		for (int i = 0; i < t_count; ++i) {
			const real_t t = real_t(i) / real_t(spp);
			const real_t mt = real_t(1) - t;
			const Vector3 p = mt * mt * mt * p0 + real_t(3) * mt * mt * t * p1 +
					real_t(3) * mt * t * t * p2 + t * t * t * p3;
			out.push_back(p);
		}
	}
	return out;
}

static Dictionary _load_raw_data_json(const String &p_path) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	if (f.is_null()) {
		return Dictionary();
	}
	const String text = f->get_as_text();
	const Variant v = JSON::parse_string(text);
	if (v.get_type() != Variant::DICTIONARY) {
		return Dictionary();
	}
	return Dictionary(v);
}

static String _raw_data_path(const String &p_filename) {
	if (OS::get_singleton()->has_environment("CASSIE_RAW_DATA_PATH")) {
		return OS::get_singleton()->get_environment("CASSIE_RAW_DATA_PATH").path_join(p_filename);
	}
	return String("thirdparty/cassie-data/raw_data").path_join(p_filename);
}

static String _stroke_set_signature(Vector<int> p_ids) {
	p_ids.sort();
	String sig;
	for (int i = 0; i < p_ids.size(); ++i) {
		if (i > 0) {
			sig += ",";
		}
		sig += itos(p_ids[i]);
	}
	return sig;
}

static void _border_set_diff(const String &p_label, const String &p_filename) {
	const String path = _raw_data_path(p_filename);
	const Dictionary j = _load_raw_data_json(path);
	if (j.is_empty()) {
		MESSAGE(vformat(
				"[CassieBorderDiff] %s skipped: %s not found / parse failure",
				p_label, path));
		return;
	}
	const Array strokes = j.get("allSketchedStrokes", Array());
	const Array patches = j.get("allCreatedPatches", Array());

	// Load strokes into the graph using the offline planar-arrangement
	// build (build_from_polylines), which finds all pairwise intersections
	// at once rather than splitting edges incrementally per add_stroke.
	// We track the source stroke id of each polyline via the polyline
	// index → sid table, then after build, read each edge's
	// source_polyline_idx to populate eid_to_sid.
	Ref<CassieSketchGraph> graph;
	graph.instantiate();
	HashMap<int, int> sid_to_eid;
	HashMap<int, int> eid_to_sid;
	const Time *time = Time::get_singleton();
	const uint64_t t_pop0 = time->get_ticks_usec();
	TypedArray<PackedVector3Array> polylines;
	Vector<int> poly_idx_to_sid;
	int loaded = 0;
	for (int i = 0; i < strokes.size(); ++i) {
		const Dictionary s = strokes[i];
		const int sid = int(s.get("id", -1));
		if (sid < 0) {
			continue;
		}
		const Array ctrl = s.get("ctrlPts", Array());
		const PackedVector3Array poly = _flatten_ctrl_pts(ctrl, 8);
		if (poly.size() < 2) {
			continue;
		}
		polylines.push_back(poly);
		poly_idx_to_sid.push_back(sid);
		loaded++;
	}
	// Single planar-arrangement build replaces the per-stroke loop.
	graph->build_from_polylines(polylines, graph->get_merge_epsilon());
	// Populate eid_to_sid from each edge's source polyline index.
	{
		TypedArray<CassieSketchGraphEdge> all_e = graph->get_all_edges();
		for (int k = 0; k < all_e.size(); ++k) {
			Ref<CassieSketchGraphEdge> e = all_e[k];
			if (e.is_null()) {
				continue;
			}
			const int eid = e->get_id();
			const int pi = e->get_source_polyline_idx();
			if (pi >= 0 && pi < poly_idx_to_sid.size()) {
				const int sid = poly_idx_to_sid[pi];
				if (!sid_to_eid.has(sid)) {
					sid_to_eid[sid] = eid;
				}
				eid_to_sid[eid] = sid;
			}
		}
	}
	const uint64_t pop_us = time->get_ticks_usec() - t_pop0;

	const uint64_t t_fc0 = time->get_ticks_usec();
	const Array cycles = graph->find_cycles();
	const uint64_t fc_us = time->get_ticks_usec() - t_fc0;

	// Ground-truth set-of-sets.
	HashSet<String> alg_border_sigs;
	int alg_auto = 0;
	int alg_manual = 0;
	for (int i = 0; i < patches.size(); ++i) {
		const Dictionary p = patches[i];
		const bool auto_found = bool(p.get("foundByAlgo", false));
		if (auto_found) {
			alg_auto++;
		} else {
			alg_manual++;
		}
		const Array sids = p.get("strokesID", Array());
		Vector<int> ids;
		for (int k = 0; k < sids.size(); ++k) {
			ids.push_back(int(sids[k]));
		}
		alg_border_sigs.insert(_stroke_set_signature(ids));
	}

	// Detected set-of-sets — map each cycle's edge list back to stroke ids.
	HashSet<String> det_sigs;
	int det_with_unknown_edge = 0;
	for (int i = 0; i < cycles.size(); ++i) {
		const PackedInt32Array cycle = cycles[i];
		Vector<int> ids;
		bool any_unknown = false;
		for (int k = 0; k < cycle.size(); ++k) {
			const int eid = cycle[k];
			HashMap<int, int>::Iterator it = eid_to_sid.find(eid);
			if (it == eid_to_sid.end()) {
				any_unknown = true;
				continue;
			}
			ids.push_back(it->value);
		}
		if (any_unknown) {
			det_with_unknown_edge++;
		}
		det_sigs.insert(_stroke_set_signature(ids));
	}

	int matched = 0;
	for (const String &s : det_sigs) {
		if (alg_border_sigs.has(s)) {
			matched++;
		}
	}
	const int alg_border_total = int(alg_border_sigs.size());
	const int det_total = int(det_sigs.size());
	const int false_pos = det_total - matched;
	const int false_neg = alg_border_total - matched;

	MESSAGE(vformat(
			"[CassieBorderDiff] %s  strokes_in_json=%d  loaded=%d  "
			"graph_nodes=%d  graph_edges=%d  populate=%d us  find_cycles=%d us",
			p_label, int(strokes.size()), loaded,
			graph->get_node_count(), graph->get_edge_count(),
			int(pop_us), int(fc_us)));
	MESSAGE(vformat(
			"[CassieBorderDiff] %s  alg_borders=%d (auto=%d manual=%d)  "
			"detected_cycles=%d  matched=%d  false_pos=%d  false_neg=%d  "
			"cycles_w_unknown_edges=%d",
			p_label, alg_border_total, alg_auto, alg_manual,
			det_total, matched, false_pos, false_neg, det_with_unknown_edge));
}

TEST_CASE_PENDING("[Cassie][PipelineBench] Border-set diff: hat / flower / vintage_car") {
	_border_set_diff("hat", "hat.json");
	_border_set_diff("flower", "flower.json");
	_border_set_diff("vintage_car", "vintage_car.json");
}

// ── Quantitative invariants on the assembled sketch process ──────────────
//
// End-to-end pipeline run on each raw_data fixture: load strokes → build
// graph → find_cycles → triangulate each cycle. Reports per-patch
// triangle count + surface area + bounding-box extent + edge-length
// stats, plus aggregates across all patches. Tells us at a glance
// whether patches are dimensionally sensible (no zero-area degenerates,
// no edge-length blowups) without needing a visual check.

struct PatchInvariants {
	int triangles = 0;
	double area = 0.0;
	Vector3 aabb_min = Vector3(FLT_MAX, FLT_MAX, FLT_MAX);
	Vector3 aabb_max = Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	double edge_len_min = 1e30;
	double edge_len_max = 0.0;
	double edge_len_sum = 0.0;
	int edge_count = 0;
};

static PatchInvariants _measure_patch(const PackedVector3Array &p_verts,
		const PackedInt32Array &p_faces) {
	PatchInvariants inv;
	inv.triangles = int(p_faces.size() / 3);
	for (int t = 0; t < inv.triangles; ++t) {
		const Vector3 p0 = p_verts[p_faces[t * 3 + 0]];
		const Vector3 p1 = p_verts[p_faces[t * 3 + 1]];
		const Vector3 p2 = p_verts[p_faces[t * 3 + 2]];
		const Vector3 cross = (p1 - p0).cross(p2 - p0);
		inv.area += 0.5 * double(cross.length());
		// AABB and per-edge length on each triangle.
		const Vector3 verts[3] = { p0, p1, p2 };
		for (int k = 0; k < 3; ++k) {
			inv.aabb_min = inv.aabb_min.min(verts[k]);
			inv.aabb_max = inv.aabb_max.max(verts[k]);
			const double len = double((verts[(k + 1) % 3] - verts[k]).length());
			if (len < inv.edge_len_min) {
				inv.edge_len_min = len;
			}
			if (len > inv.edge_len_max) {
				inv.edge_len_max = len;
			}
			inv.edge_len_sum += len;
			inv.edge_count++;
		}
	}
	return inv;
}

static void _sketch_invariants(const String &p_label, const String &p_filename) {
	const String path = _raw_data_path(p_filename);
	const Dictionary j = _load_raw_data_json(path);
	if (j.is_empty()) {
		MESSAGE(vformat(
				"[CassieInvariants] %s skipped: %s not found / parse failure",
				p_label, path));
		return;
	}
	const Array strokes = j.get("allSketchedStrokes", Array());

	Ref<CassieSketchGraph> graph;
	graph.instantiate();
	const PackedVector3Array empty_normals;
	for (int i = 0; i < strokes.size(); ++i) {
		const Dictionary s = strokes[i];
		const Array ctrl = s.get("ctrlPts", Array());
		const PackedVector3Array poly = _flatten_ctrl_pts(ctrl, 8);
		if (poly.size() >= 2) {
			graph->add_stroke(poly, empty_normals);
		}
	}
	const Array cycles = graph->find_cycles();
	const real_t target_edge_length = real_t(0.05);

	int succ = 0, fail = 0;
	int empty_patches = 0;
	int total_tris = 0;
	double total_area = 0.0;
	Vector3 agg_aabb_min(FLT_MAX, FLT_MAX, FLT_MAX);
	Vector3 agg_aabb_max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	double agg_edge_min = 1e30;
	double agg_edge_max = 0.0;
	double agg_edge_sum = 0.0;
	int agg_edge_count = 0;
	int tris_min = INT32_MAX, tris_max = 0;
	double area_min = 1e30, area_max = 0.0;
	const Time *time = Time::get_singleton();
	const uint64_t t0 = time->get_ticks_usec();
	for (int i = 0; i < cycles.size(); ++i) {
		const PackedInt32Array cycle = cycles[i];
		if (cycle.size() < 3) {
			continue;
		}
		const PackedVector3Array boundary =
				graph->sample_cycle_boundary(cycle, target_edge_length);
		if (boundary.size() < 3) {
			continue;
		}
		Dictionary r = CassieTriangulator::triangulate(boundary, target_edge_length);
		if (!bool(r.get("success", false))) {
			fail++;
			continue;
		}
		succ++;
		const PackedVector3Array verts = r.get("vertices", PackedVector3Array());
		const PackedInt32Array faces = r.get("faces", PackedInt32Array());
		if (verts.size() == 0 || faces.size() == 0) {
			empty_patches++;
			continue;
		}
		const PatchInvariants pi = _measure_patch(verts, faces);
		total_tris += pi.triangles;
		total_area += pi.area;
		agg_aabb_min = agg_aabb_min.min(pi.aabb_min);
		agg_aabb_max = agg_aabb_max.max(pi.aabb_max);
		if (pi.edge_len_min < agg_edge_min) {
			agg_edge_min = pi.edge_len_min;
		}
		if (pi.edge_len_max > agg_edge_max) {
			agg_edge_max = pi.edge_len_max;
		}
		agg_edge_sum += pi.edge_len_sum;
		agg_edge_count += pi.edge_count;
		if (pi.triangles < tris_min) {
			tris_min = pi.triangles;
		}
		if (pi.triangles > tris_max) {
			tris_max = pi.triangles;
		}
		if (pi.area < area_min) {
			area_min = pi.area;
		}
		if (pi.area > area_max) {
			area_max = pi.area;
		}
	}
	const uint64_t total_us = time->get_ticks_usec() - t0;
	const Vector3 ext = agg_aabb_max - agg_aabb_min;

	MESSAGE(vformat(
			"[CassieInvariants] %s  strokes=%d  cycles=%d  succ=%d  fail=%d  "
			"empty=%d  triangulate_total=%d us",
			p_label, int(strokes.size()), int(cycles.size()),
			succ, fail, empty_patches, int(total_us)));
	if (succ == 0) {
		return;
	}
	MESSAGE(vformat(
			"[CassieInvariants] %s  per_patch_tris min/avg/max=(%d,%d,%d)  "
			"per_patch_area min/avg/max=(%.6f,%.6f,%.6f) m^2  total_tris=%d  total_area=%.4f m^2",
			p_label,
			tris_min, total_tris / succ, tris_max,
			area_min, total_area / double(succ), area_max,
			total_tris, total_area));
	MESSAGE(vformat(
			"[CassieInvariants] %s  edge_len min/avg/max=(%.4f,%.4f,%.4f) m  "
			"aabb_extent=(%.3f,%.3f,%.3f) m",
			p_label,
			agg_edge_min,
			(agg_edge_count > 0 ? (agg_edge_sum / double(agg_edge_count)) : 0.0),
			agg_edge_max,
			double(ext.x), double(ext.y), double(ext.z)));
}

TEST_CASE_PENDING("[Cassie][PipelineBench] Sketch invariants: hat / flower / vintage_car") {
	_sketch_invariants("hat", "hat.json");
	_sketch_invariants("flower", "flower.json");
	_sketch_invariants("vintage_car", "vintage_car.json");
}

// ── Exhaustive train/test sweep over the full upstream dataset ───────────
//
// Split mirrors the dataset README's own categorization:
//   * Train (study set, 72 files): `<participant>-<system>-<model>.json`.
//     Controlled conditions, 12 participants × 3 systems × 2 models.
//   * Test (demo set, 16 files): varied free creations, more complex.
//
// Defaults to thirdparty/cassie-data/raw_data/ which only has 3 vendored
// fixtures — for the full sweep set CASSIE_RAW_DATA_PATH to point at the
// upstream E:/cassie-data/data/raw_data dir.

struct ExhaustiveAggregate {
	int files_total = 0;
	int files_loaded = 0;
	int cycles_total = 0;
	int cycles_succ = 0;
	int cycles_fail = 0;
	int tris_total = 0;
	double area_total = 0.0;
	// per-file medians for compactness
	Vector<int> per_file_succ;
	Vector<int> per_file_tris;
	Vector<double> per_file_area;
	// failure classification — explains the 20-25 % "fail" count.
	// boundary_lt_4: sample_cycle_boundary returned < 4 pts (cycle of
	//   1-2 edges; triangulator legitimately rejects)
	// degenerate_extent: boundary AABB max axis < 1mm — entire cycle
	//   collapsed to a point or near-degenerate stroke
	// collinear: boundary's smallest PCA eigenvalue / largest < 1e-4 —
	//   all points on a single line, can't form 2D area
	// real_fail: triangulator declined a topologically + geometrically
	//   non-degenerate boundary — these are the concerning failures.
	int fail_lt_4 = 0;
	int fail_degenerate_extent = 0;
	int fail_collinear = 0;
	int fail_dup_samples = 0; // consecutive boundary points < 1 mm apart
	int fail_self_intersect = 0; // projected 2D boundary self-crosses
	int fail_real = 0;
	uint64_t wall_us = 0;
};

// 2D segment-segment intersection test. Returns true iff seg AB and CD
// strictly cross (don't share endpoints, don't merely touch).
static bool _segs_cross_2d(const Vector2 &a, const Vector2 &b,
		const Vector2 &c, const Vector2 &d) {
	const Vector2 r = b - a;
	const Vector2 s = d - c;
	const real_t denom = r.x * s.y - r.y * s.x;
	if (Math::abs(denom) < real_t(1e-12)) {
		return false; // parallel / collinear
	}
	const Vector2 ca = c - a;
	const real_t t = (ca.x * s.y - ca.y * s.x) / denom;
	const real_t u = (ca.x * r.y - ca.y * r.x) / denom;
	const real_t eps = real_t(1e-6);
	return t > eps && t < real_t(1) - eps && u > eps && u < real_t(1) - eps;
}

static void _classify_failure(const PackedVector3Array &p_boundary,
		ExhaustiveAggregate &agg) {
	const int n = p_boundary.size();
	if (n < 4) {
		agg.fail_lt_4++;
		return;
	}
	// AABB extent.
	Vector3 lo = p_boundary[0], hi = p_boundary[0];
	for (int i = 1; i < n; ++i) {
		lo = lo.min(p_boundary[i]);
		hi = hi.max(p_boundary[i]);
	}
	const Vector3 ext = hi - lo;
	const float max_ext = MAX(MAX(ext.x, ext.y), ext.z);
	if (max_ext < 0.001f) { // < 1 mm
		agg.fail_degenerate_extent++;
		return;
	}
	// Collinearity via covariance matrix eigenvalue ratio. Centroid first.
	Vector3 centroid;
	for (int i = 0; i < n; ++i) {
		centroid += p_boundary[i];
	}
	centroid /= float(n);
	// 3x3 covariance.
	double cov[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
	for (int i = 0; i < n; ++i) {
		const Vector3 d = p_boundary[i] - centroid;
		cov[0][0] += double(d.x) * d.x;
		cov[1][1] += double(d.y) * d.y;
		cov[2][2] += double(d.z) * d.z;
		cov[0][1] += double(d.x) * d.y;
		cov[0][2] += double(d.x) * d.z;
		cov[1][2] += double(d.y) * d.z;
	}
	cov[1][0] = cov[0][1];
	cov[2][0] = cov[0][2];
	cov[2][1] = cov[1][2];
	// Approximate eigenvalue spread by trace and Frobenius norm —
	// avoids a full eigensolver. If the trace is much larger than
	// the squared off-diagonal sum, one axis dominates → collinear.
	const double tr = cov[0][0] + cov[1][1] + cov[2][2];
	double max_diag = MAX(MAX(cov[0][0], cov[1][1]), cov[2][2]);
	if (tr > 0 && (tr - max_diag) / tr < 1e-4) {
		agg.fail_collinear++;
		return;
	}
	// Consecutive-duplicate sample check (boundary has a 0-length edge).
	for (int i = 0; i < n; ++i) {
		const Vector3 a = p_boundary[i];
		const Vector3 b = p_boundary[(i + 1) % n];
		if (a.distance_to(b) < 0.001f) { // < 1 mm
			agg.fail_dup_samples++;
			return;
		}
	}
	// Self-intersection check on the projection to the best-fit plane.
	// Drop the axis whose variance is smallest, keep the other two.
	int drop = 0;
	if (cov[1][1] < cov[drop][drop]) {
		drop = 1;
	}
	if (cov[2][2] < cov[drop][drop]) {
		drop = 2;
	}
	const int ax0 = (drop + 1) % 3;
	const int ax1 = (drop + 2) % 3;
	auto proj2 = [&](const Vector3 &v) -> Vector2 {
		const real_t arr[3] = { v.x, v.y, v.z };
		return Vector2(arr[ax0], arr[ax1]);
	};
	for (int i = 0; i < n; ++i) {
		const Vector2 a = proj2(p_boundary[i]);
		const Vector2 b = proj2(p_boundary[(i + 1) % n]);
		// Skip adjacent + identical edges.
		for (int j = i + 2; j < n; ++j) {
			if (i == 0 && j == n - 1) {
				continue;
			}
			const Vector2 c = proj2(p_boundary[j]);
			const Vector2 d = proj2(p_boundary[(j + 1) % n]);
			if (_segs_cross_2d(a, b, c, d)) {
				agg.fail_self_intersect++;
				return;
			}
		}
	}
	agg.fail_real++;
}

static void _exhaustive_run_one(const char *p_filename,
		ExhaustiveAggregate &agg) {
	agg.files_total++;
	const String path = _raw_data_path(p_filename);
	const Dictionary j = _load_raw_data_json(path);
	if (j.is_empty()) {
		return;
	}
	agg.files_loaded++;
	const Array strokes = j.get("allSketchedStrokes", Array());

	const Time *time = Time::get_singleton();
	const uint64_t t0 = time->get_ticks_usec();

	Ref<CassieSketchGraph> graph;
	graph.instantiate();
	const PackedVector3Array empty_normals;
	for (int i = 0; i < strokes.size(); ++i) {
		const Dictionary s = strokes[i];
		const Array ctrl = s.get("ctrlPts", Array());
		const PackedVector3Array poly = _flatten_ctrl_pts(ctrl, 8);
		if (poly.size() >= 2) {
			graph->add_stroke(poly, empty_normals);
		}
	}
	const Array cycles = graph->find_cycles();
	const real_t tel = real_t(0.05);

	int succ = 0, fail = 0, tris = 0;
	double area = 0.0;
	for (int i = 0; i < cycles.size(); ++i) {
		const PackedInt32Array cycle = cycles[i];
		if (cycle.size() < 3) {
			continue;
		}
		const PackedVector3Array boundary =
				graph->sample_cycle_boundary(cycle, tel);
		if (boundary.size() < 3) {
			continue;
		}
		Dictionary r = CassieTriangulator::triangulate(boundary, tel);
		if (!bool(r.get("success", false))) {
			fail++;
			_classify_failure(boundary, agg);
			continue;
		}
		succ++;
		const PackedVector3Array verts = r.get("vertices", PackedVector3Array());
		const PackedInt32Array faces = r.get("faces", PackedInt32Array());
		if (verts.size() == 0 || faces.size() == 0) {
			continue;
		}
		const PatchInvariants pi = _measure_patch(verts, faces);
		tris += pi.triangles;
		area += pi.area;
	}
	agg.wall_us += time->get_ticks_usec() - t0;
	agg.cycles_total += int(cycles.size());
	agg.cycles_succ += succ;
	agg.cycles_fail += fail;
	agg.tris_total += tris;
	agg.area_total += area;
	agg.per_file_succ.push_back(succ);
	agg.per_file_tris.push_back(tris);
	agg.per_file_area.push_back(area);
}

template <typename T>
static T _median(Vector<T> p_v) {
	p_v.sort();
	const int n = p_v.size();
	if (n == 0) {
		return T(0);
	}
	return p_v[n / 2];
}

static void _exhaustive_report(const String &p_label,
		const ExhaustiveAggregate &agg) {
	MESSAGE(vformat(
			"[CassieExhaustive] %s  files=%d  loaded=%d  cycles_total=%d  "
			"succ=%d  fail=%d  tris_total=%d  area_total=%.3f m^2  wall=%d ms",
			p_label, agg.files_total, agg.files_loaded,
			agg.cycles_total, agg.cycles_succ, agg.cycles_fail,
			agg.tris_total, agg.area_total,
			int(agg.wall_us / 1000)));
	if (agg.files_loaded == 0) {
		return;
	}
	MESSAGE(vformat(
			"[CassieExhaustive] %s  per_file median: succ=%d  tris=%d  area=%.4f m^2",
			p_label,
			_median(agg.per_file_succ),
			_median(agg.per_file_tris),
			_median(agg.per_file_area)));
	const double mean_area = agg.area_total / double(agg.files_loaded);
	const double mean_tris = double(agg.tris_total) / double(agg.files_loaded);
	const double mean_succ = double(agg.cycles_succ) / double(agg.files_loaded);
	const double succ_rate = agg.cycles_total > 0
			? double(agg.cycles_succ) / double(agg.cycles_total)
			: 0.0;
	MESSAGE(vformat(
			"[CassieExhaustive] %s  per_file mean: succ=%.1f  tris=%.1f  area=%.4f m^2  "
			"cycle_succ_rate=%.1f %%",
			p_label, mean_succ, mean_tris, mean_area, succ_rate * 100.0));
	const int fail_legit = agg.fail_lt_4 + agg.fail_degenerate_extent + agg.fail_collinear;
	const double real_fail_rate = agg.cycles_total > 0
			? double(agg.fail_real) / double(agg.cycles_total)
			: 0.0;
	MESSAGE(vformat(
			"[CassieExhaustive] %s  fail breakdown: lt_4=%d  degen_extent=%d  "
			"collinear=%d  dup_samples=%d  self_intersect=%d  real=%d  "
			"legit_fail_rate=%.1f %%  real_fail_rate=%.1f %%",
			p_label, agg.fail_lt_4, agg.fail_degenerate_extent,
			agg.fail_collinear, agg.fail_dup_samples, agg.fail_self_intersect,
			agg.fail_real,
			(agg.cycles_total > 0
							? double(fail_legit + agg.fail_dup_samples + agg.fail_self_intersect) / double(agg.cycles_total) * 100.0
							: 0.0),
			real_fail_rate * 100.0));
}

// 72 user-study files: <participant>-<system>-<model>.json
// participants 1-12 × systems {0,1,2} × models {1,2}.
static const char *kTrainStudyFiles[] = {
	"01-0-1.json",
	"01-0-2.json",
	"01-1-1.json",
	"01-1-2.json",
	"01-2-1.json",
	"01-2-2.json",
	"02-0-1.json",
	"02-0-2.json",
	"02-1-1.json",
	"02-1-2.json",
	"02-2-1.json",
	"02-2-2.json",
	"03-0-1.json",
	"03-0-2.json",
	"03-1-1.json",
	"03-1-2.json",
	"03-2-1.json",
	"03-2-2.json",
	"04-0-1.json",
	"04-0-2.json",
	"04-1-1.json",
	"04-1-2.json",
	"04-2-1.json",
	"04-2-2.json",
	"05-0-1.json",
	"05-0-2.json",
	"05-1-1.json",
	"05-1-2.json",
	"05-2-1.json",
	"05-2-2.json",
	"06-0-1.json",
	"06-0-2.json",
	"06-1-1.json",
	"06-1-2.json",
	"06-2-1.json",
	"06-2-2.json",
	"07-0-1.json",
	"07-0-2.json",
	"07-1-1.json",
	"07-1-2.json",
	"07-2-1.json",
	"07-2-2.json",
	"08-0-1.json",
	"08-0-2.json",
	"08-1-1.json",
	"08-1-2.json",
	"08-2-1.json",
	"08-2-2.json",
	"09-0-1.json",
	"09-0-2.json",
	"09-1-1.json",
	"09-1-2.json",
	"09-2-1.json",
	"09-2-2.json",
	"10-0-1.json",
	"10-0-2.json",
	"10-1-1.json",
	"10-1-2.json",
	"10-2-1.json",
	"10-2-2.json",
	"11-0-1.json",
	"11-0-2.json",
	"11-1-1.json",
	"11-1-2.json",
	"11-2-1.json",
	"11-2-2.json",
	"12-0-1.json",
	"12-0-2.json",
	"12-1-1.json",
	"12-1-2.json",
	"12-2-1.json",
	"12-2-2.json",
};

// 16 demo files (varied, no naming-convention constraints).
static const char *kTestDemoFiles[] = {
	"architecture.json",
	"architecture-2.json",
	"chair.json",
	"computer-mouse.json",
	"dress.json",
	"flower.json",
	"guitar.json",
	"hat.json",
	"hmd_modern.json",
	"hmd_sutherland.json",
	"large_hat.json",
	"monster_head.json",
	"sewing-machine.json",
	"shield.json",
	"teapot.json",
	"vacuum.json",
	"vintage_car.json",
};

// Single-fixture dumper for Blender side-by-side comparison. Defaults to
// hat.json; override via CASSIE_DUMP_FIXTURE.
TEST_CASE_PENDING("[Cassie][PipelineBench] Dump our patches as JSON (Blender)") {
	if (!OS::get_singleton()->has_environment("CASSIE_DUMP_PATCHES_JSON")) {
		return;
	}
	String filename = "hat.json";
	if (OS::get_singleton()->has_environment("CASSIE_DUMP_FIXTURE")) {
		filename = OS::get_singleton()->get_environment("CASSIE_DUMP_FIXTURE");
	}
	const String path = _raw_data_path(filename);
	const Dictionary j = _load_raw_data_json(path);
	if (j.is_empty()) {
		return;
	}
	const Array strokes = j.get("allSketchedStrokes", Array());
	Ref<CassieSketchGraph> graph;
	graph.instantiate();
	TypedArray<PackedVector3Array> polylines;
	for (int i = 0; i < strokes.size(); ++i) {
		const Dictionary s = strokes[i];
		const Array ctrl = s.get("ctrlPts", Array());
		const PackedVector3Array poly = _flatten_ctrl_pts(ctrl, 8);
		if (poly.size() >= 2) {
			polylines.push_back(poly);
		}
	}
	graph->build_from_polylines(polylines, graph->get_merge_epsilon());
	const Array cycles = graph->find_cycles();
	_dump_our_patches_json(filename, graph, cycles, real_t(0.05));
	MESSAGE(vformat("[CassieDumpPatches] %s  cycles=%d  written to %s",
			filename, int(cycles.size()),
			OS::get_singleton()->get_environment("CASSIE_DUMP_PATCHES_JSON")));
}

TEST_CASE_PENDING("[Cassie][PipelineBench] Exhaustive sweep: train (study set)") {
	ExhaustiveAggregate agg;
	const int n = int(sizeof(kTrainStudyFiles) / sizeof(kTrainStudyFiles[0]));
	for (int i = 0; i < n; ++i) {
		_exhaustive_run_one(kTrainStudyFiles[i], agg);
	}
	_exhaustive_report("train(study,72)", agg);
}

TEST_CASE_PENDING("[Cassie][PipelineBench] Exhaustive sweep: test (demo set)") {
	ExhaustiveAggregate agg;
	const int n = int(sizeof(kTestDemoFiles) / sizeof(kTestDemoFiles[0]));
	for (int i = 0; i < n; ++i) {
		_exhaustive_run_one(kTestDemoFiles[i], agg);
	}
	_exhaustive_report("test(demo,17)", agg);
}

TEST_CASE_PENDING("[Cassie][PipelineBench] Cassie-data: hat (common case)") {
	_probe_curves_file("hat-tel=0.05", "hat.curves", real_t(0.05), /*max_cycles=*/16);
	_probe_curves_file("hat-tel=0.10", "hat.curves", real_t(0.10), /*max_cycles=*/16);
	_probe_curves_file("hat-tel=0.20", "hat.curves", real_t(0.20), /*max_cycles=*/16);
}

TEST_CASE_PENDING("[Cassie][PipelineBench] Cassie-data: vintage_car (wow case)") {
	_probe_curves_file("vintage_car.curves", "vintage_car.curves", real_t(0.05), /*max_strokes=*/16);
}

TEST_CASE_PENDING("[Cassie][PipelineBench] Cassie-data: flower (small/sparse)") {
	_probe_curves_file("flower.curves", "flower.curves", real_t(0.05), /*max_strokes=*/16);
}

TEST_CASE_PENDING("[Cassie][PipelineBench] Planar loop probe: square / circle / L") {
	_probe_planar("square(4x6=20pt)", _make_planar_square(6), real_t(0.1));
	_probe_planar("circle(20pt r=0.5)", _make_planar_circle(20, real_t(0.5)), real_t(0.1));
	_probe_planar("L_shape(6x4=18pt)", _make_planar_L(4), real_t(0.1));
}

} // namespace TestCassiePipelineBench

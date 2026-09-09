/**************************************************************************/
/*  cassie_profile_mover.h                                                */
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

#include "core/math/basis.h"
#include "core/math/dynamic_bvh.h"
#include "core/object/ref_counted.h"
#include "core/templates/local_vector.h"
#include "scene/resources/3d/skin.h"
#include "scene/resources/mesh.h"

#include <memory>

// Eigen sparse types are deliberately PIMPL'd into the .cpp via
// CassieProfileMoverHarmonic — test_main.cpp's compile env doesn't pick up
// modules/cassie's thirdparty/eigen include, and forwarding the Eigen
// types here would break test discovery.
struct CassieProfileMoverHarmonic;

// Curvenet-driven mesh deformer (Step 1.4 / ENG-46).
//
// Phase 1.4a (this file) ships the inverse-distance skinning fallback that
// unblocks Pose-mode preview. Phase 1.4b will replace the per-vertex
// IDW deform with a cut-cell harmonic solve (de Goes et al., TOG 2022) that
// preserves surface detail across curvenet "cuts" — same bind/API surface,
// different deform routine.
//
// IDW model (Phase 1.4a):
//   bind:
//     for each curvenet curve c, sample N_per_curve points along c at
//     param t ∈ [0, 1]. Store sample positions + endpoint knot indices +
//     param t per sample.
//     for each mesh vertex v, find the k=6 nearest curvenet samples,
//     compute normalized inverse-distance weights, store (indices, weights).
//   deform:
//     for each knot, compute delta = current_origin - bind_origin.
//     for each curve sample s on curve c (endpoint knots A, B):
//         sample_delta[s] = lerp(delta_A, delta_B, t_s)
//     for each mesh vertex v:
//         disp = Σ w_i * sample_delta[sample_idx_i]
//         v_new = v_rest + disp
//
// Pure-translation faithfully (Σ w_i = 1 ⇒ uniform translation reproduces);
// ignores per-knot rotation (1.4b will pick that up via harmonic boundary
// positions evaluated from the deformed curves).
class CassieProfileMover : public RefCounted {
	GDCLASS(CassieProfileMover, RefCounted);

	Ref<CassieSurfacePatch> bound_patch;
	Ref<CassieCurvenet> bound_curvenet;

	// Mesh snapshot at bind time.
	LocalVector<Vector3> rest_vertex_positions;
	// Per-vertex rest normals captured from the patch's source
	// ARRAY_NORMAL (preserves authored split normals / smoothing
	// groups). Falls back to angle-weighted (Max 1999) when the
	// source omits normals. Both get_bound_mesh() and the Sumner &
	// Popovic 2004 J^-T transfer in deform() consume these.
	LocalVector<Vector3> rest_vertex_normals;
	LocalVector<Vector3i> triangles;
	// Pre-inverted per-face rest frame for Sumner & Popovic 2004:
	// F_rest = [e1_rest | e2_rest | n_rest_unit] (3x3, columns).
	// The deform's per-face J = F_def * F_rest^-1; pre-inverting at
	// bind saves ~12K Basis::inverse() calls / frame on the character
	// fixture (Ni=5989, ~12K triangles).
	LocalVector<Basis> rest_face_frame_inv;

	// Per-curve sample range [curve_sample_offset[c], curve_sample_offset[c+1]).
	LocalVector<int> curve_sample_offset;

	// Flat sample buffer.
	LocalVector<Vector3> bind_sample_positions;
	LocalVector<int> sample_knot_a; // endpoint knot indices (-1 if absent)
	LocalVector<int> sample_knot_b;
	LocalVector<real_t> sample_t; // [0, 1] param along curve

	// Knot positions at bind time. Index parallels CassieCurvenet::get_knots.
	LocalVector<Vector3> bind_knot_positions;
	// Full Transform3D at bind time per knot. Lets deform() compute
	// the per-sample rotation delta as `R_curr * R_bind^-1` (slerped
	// along the curve) on top of the translation delta. Sibling of
	// bind_knot_positions for ENG-65 phase 1.
	LocalVector<Transform3D> bind_knot_transforms;

	// Per-vertex K-nearest-sample IDW. Flat arrays of size vertex_count * K.
	// vertex_sample_indices[v * K + k] = -1 when fewer than k samples exist.
	LocalVector<int> vertex_sample_indices;
	LocalVector<real_t> vertex_sample_weights;

	// BVH over bind_sample_positions for O(log N_samples) nearest-K
	// queries. Mutable so the bind() rebuild path can clear() (DynamicBVH
	// is move-only — its SpinLock member deletes copy assignment).
	mutable DynamicBVH sample_bvh;
	real_t initial_sample_search_radius = real_t(0.1);

	static constexpr int K = 6;
	static constexpr int kSamplesPerCurve = 16;

	// Phase 1.4b — cut-aware harmonic state. Built alongside the IDW state
	// in bind() and used in deform() when harmonic_ready == true. The
	// explicit cut-cell crack indexing from de Goes 2022 §4 is deferred —
	// this implementation runs the harmonic solve over the original mesh
	// topology, which gives smooth surface-detail-preserving deformation
	// without enforcing per-curve discontinuities. Crack support is a
	// follow-up tracked under ENG-46 phase 1.4b-extension.
	bool harmonic_ready = false;
	LocalVector<int> mesh_to_partition; // mesh vertex → I-index or B-index
	LocalVector<bool> is_boundary; // mesh vertex → boundary flag
	LocalVector<int> interior_to_mesh; // I-index → mesh vertex idx
	LocalVector<int> boundary_to_mesh; // B-index → mesh vertex idx
	LocalVector<int> boundary_driver_sample; // B-index → driving sample idx
	LocalVector<Vector3> boundary_rest_offset; // B-index → rest offset from sample
	LocalVector<Vector3> interior_detail; // I-index → rest residual

	// PIMPL: the SimplicialLDLT factor + L_IB block live in the .cpp via
	// Eigen-aware types. Header stays Eigen-free for test discovery.
	std::unique_ptr<CassieProfileMoverHarmonic> harmonic_impl;

	// ENG-52 — level-set schedule for sparse triangular back-substitution.
	// Computed at bind time from the L factor's sparsity pattern. Within a
	// level, all interior rows can solve independently; serialize between
	// levels. forward_level_offset[k+1] - forward_level_offset[k] = number
	// of rows in level k; forward_level_rows[forward_level_offset[k] ..]
	// lists the row indices in that level.
	LocalVector<int> forward_level_offset;
	LocalVector<int> forward_level_rows;
	LocalVector<int> backward_level_offset;
	LocalVector<int> backward_level_rows;

	// ENG-59 cut-cell state. _apply_cut_topology duplicates each mesh
	// vertex that lies on a curvenet curve when its incident triangles
	// straddle the curve (one set on each side of the in-surface curve
	// normal). After the pass, rest_vertex_positions + triangles describe
	// the cut topology — duplicates appended to the back of the vertex
	// list, triangles re-indexed to the appropriate copy.
	int crack_edge_count_value = 0;

	void _build_harmonic_state();
	void _apply_cut_topology();

protected:
	static void _bind_methods();

public:
	CassieProfileMover();
	~CassieProfileMover();

	// Microbenchmark: build a representative tridiagonal SPD CSR of size
	// p_n, upload to GPU once, time p_iterations of spmv_uploaded
	// dispatches and the same count of CPU `cassie_pcg::spmv` calls,
	// return per-call timings. Used to decide whether a CG-iter
	// ubershader is required: at 90 Hz Quest 3 the per-dispatch budget
	// is ~4 µs (2700 dispatches per frame for a full GPU CG). If gpu_us
	// >> 4, modular dispatch can't make budget.
	//
	// Returns a Dictionary with keys:
	//   "available" : bool   — false means no local RD, skip the rest
	//   "n"         : int    — matrix size used
	//   "nnz"       : int    — nonzero count
	//   "gpu_us"    : float  — per-call GPU spmv_uploaded µs (avg)
	//   "cpu_us"    : float  — per-call CPU cassie_pcg::spmv µs (avg)
	//   "ratio"     : float  — gpu_us / cpu_us
	//   "frame_ms"  : float  — projected GPU dispatch time per frame
	//                          assuming 2700 dispatches (9 kernels × 100
	//                          iters × 3 axes)
	static Dictionary benchmark_gpu_spmv(int p_n = 4096, int p_iterations = 100);

	// Bench the CgUbershader CG-solve path: build a tridiag SPD CSR of
	// size p_n with diag 4, off-diag -1 (1D Poisson stencil); upload
	// once; time p_max_iter PCG iterations on the GPU (single submit)
	// vs cassie_pcg::solve_sparse on CPU with the same matrix.
	//
	// Returns a Dictionary with keys:
	//   "available"     : bool   — false means no local RD
	//   "n", "nnz"      : ints
	//   "max_iter"      : int    — iters scheduled per solve
	//   "gpu_solve_ms"  : float  — wall ms for the GPU solve (1 submit)
	//   "cpu_solve_ms"  : float  — wall ms for cassie_pcg::solve_sparse
	//   "cpu_iters"     : int    — iters CPU CG ran (early-exits on tol)
	//   "ratio"         : float  — gpu_solve_ms / cpu_solve_ms
	//   "per_iter_gpu_us": float — gpu_solve_ms × 1000 / max_iter
	static Dictionary benchmark_gpu_pcg_solve(int p_n = 4096, int p_max_iter = 100);

	// Apples-to-apples solve comparison on a bound mover's REAL
	// harmonic state. Call after bind() returns is_harmonic_ready()=true.
	// Uses the same L_II + diag_inv that deform() would, picks a
	// synthetic RHS (sin pattern over the interior) so both paths
	// have something non-trivial to solve, runs:
	//   - CPU: cassie_pcg::solve_sparse with the deform()'s usual
	//     kMaxIter / kTol settings; reports actual iter count.
	//   - GPU: cassie_slang_gpu::solve_sparse_gpu with p_max_iter
	//     fixed iterations under the constant-work design.
	// Computes per-vertex drift between the two solutions and returns:
	//   "available"     : bool   — false means no local RD
	//   "ni"            : int    — interior vertex count
	//   "nnz"           : int    — L_II nonzeros
	//   "cpu_solve_ms"  : float
	//   "cpu_iters"     : int    — CPU converged in this many iters
	//   "cpu_residual"  : float
	//   "gpu_solve_ms"  : float  — for p_max_iter fixed iters
	//   "gpu_residual"  : float  — final ||r||² from end-of-solve check
	//   "max_drift"     : float  — max(|x_cpu[i] - x_gpu[i]|) over i
	//   "mean_drift"    : float  — avg(|x_cpu[i] - x_gpu[i]|) over i
	Dictionary compare_cpu_gpu_solve(int p_max_iter = 100);

	// Jacobi-PCG vs MAS-PCG comparison on the bound mover's REAL L_II,
	// driven through the float3 CG outer loop (cg_pcg3) so both paths
	// solve all 3 xyz axes as a single vector PCG. Uses a synthetic
	// sin-pattern float3 RHS (so each axis gets a non-trivial,
	// uncorrelated input).
	//
	// Returns a Dictionary with keys:
	//   "available"        : bool   — false if no RD or harmonic state
	//   "ni", "nnz"        : int
	//   "max_iter"         : int    — same fixed iter count for both
	//   "jacobi3_ms"       : float  — wall ms for solve_sparse_gpu_jacobi3
	//   "jacobi3_residual" : float  — final ||r||²
	//   "mas3_ms"          : float  — wall ms for solve_sparse_gpu_mas3
	//   "mas3_residual"    : float
	//   "max_drift"        : float  — max |x_jacobi[i] - x_mas[i]|
	//   "mean_drift"       : float
	Dictionary compare_jacobi_mas_solve(int p_max_iter = 100);

	// Connected-components diagnostic on the bound mover's L_II graph.
	// Walks the post-cut L_II sparsity pattern with a union-find
	// (core/math/disjoint_set.h) and reports the partition. If the
	// curvenet forms closed loops on the surface, the interior splits
	// into multiple disjoint regions and a block-aware solver could
	// run each region independently (smaller per-block condition
	// number → far fewer PCG iters per block, embarrassingly
	// parallel across blocks). If component_count == 1 the
	// decomposition gives nothing and that path is dead.
	//
	// Returns a Dictionary with keys:
	//   "available"         : bool — false if harmonic state not built
	//   "interior_count"    : int  — L_II row count (=Ni)
	//   "component_count"   : int  — number of disjoint blocks
	//   "largest_component" : int  — biggest block size
	//   "smallest_component": int  — smallest block size
	//   "sizes"             : PackedInt32Array — every block's size
	Dictionary count_interior_components() const;

	// Bind to a (patch, curvenet) pair. Snapshots mesh + curvenet positions,
	// samples curves, computes per-vertex IDW weights.
	void bind(const Ref<CassieSurfacePatch> &p_patch,
			const Ref<CassieCurvenet> &p_curvenet);

	// Returns the deformed mesh based on the curvenet's current
	// rest_pose_transforms. Returns the rest mesh on no-op deltas. Empty
	// mesh when not bound.
	Ref<ArrayMesh> deform() const;

	// Returns the rest-pose ArrayMesh (the mesh as it was at bind time).
	Ref<ArrayMesh> get_bound_mesh() const;

	// ENG-66 — bake the harmonic basis into linear blend skin weights.
	// For each knot k, solve the harmonic Dirichlet problem with phi_k = 1
	// at knot k's boundary footprint and 0 at all other knots. Each
	// vertex then has a weight vector across all knots (partition of
	// unity); keep the top K and renormalize, write into ARRAY_BONES +
	// ARRAY_WEIGHTS. Pair with build_skeleton() to drive at runtime
	// via standard Godot skinning — zero CASSIE per-frame cost, normals
	// transformed by J^-T via the standard skin vertex shader, rotation
	// handled exactly via per-knot bone transforms.
	//
	// p_max_weights_per_vert: 4 (default Godot) or 8 (ARRAY_FLAG_USE_8_BONE_WEIGHTS).
	// Bone index ordering matches CassieCurvenet::get_knots().
	//
	// Returns an empty Ref if the harmonic state isn't built or the
	// curvenet has no knots. Bake is single-threaded CPU PCG today;
	// ENG-63's multi-level MAS speeds it up later if needed.
	Ref<ArrayMesh> bake_skin(int p_max_weights_per_vert = 8) const;

	// ENG-66 — emit a Skin resource describing the bind pose for
	// runtime LBS. Each bone is named "knot_<i>" with rest transform =
	// the knot's bind_knot_transform (snapshot taken at bind time).
	// Pair with bake_skin's ArrayMesh + a Skeleton3D where each bone
	// pose is updated from the live knot transforms each frame; the
	// standard Godot skin vertex shader handles deformation + normal
	// J^-T for free.
	//
	// Bone index ordering matches CassieCurvenet::get_knots().
	// Returns an empty Ref if the harmonic state isn't built or the
	// curvenet has no knots.
	Ref<Skin> build_skin() const;

	// Returns the number of mesh vertices that were duplicated by the
	// cut-cell crack pass. 0 means no curvenet curve cut through interior
	// triangles (or the curvenet was empty).
	int get_crack_edge_count() const { return crack_edge_count_value; }

	bool is_bound() const { return !rest_vertex_positions.is_empty(); }
	int get_vertex_count() const { return int(rest_vertex_positions.size()); }
	int get_sample_count() const { return int(bind_sample_positions.size()); }

	// True when the harmonic state was built successfully and deform()
	// will use the sparse PCG path. False = falls back to IDW.
	bool is_harmonic_ready() const { return harmonic_ready; }

	// Total PCG iterations spent in the last deform() call summed
	// across x/y/z axes. 0 if no deform has run since bind(). Used by
	// the iter-count regression tests added for Phase A.
	int get_last_solve_iters() const;
	int get_interior_count() const { return int(interior_to_mesh.size()); }
	int get_boundary_count() const { return int(boundary_to_mesh.size()); }

	// ENG-52 introspection — the level-set schedule for the L factor's
	// forward / backward substitution. Used by the GPU back-sub path and
	// exposed for tests that pin the scheduler's correctness without
	// requiring a RenderingDevice.
	int get_forward_level_count() const {
		return forward_level_offset.is_empty()
				? 0
				: int(forward_level_offset.size()) - 1;
	}
	int get_backward_level_count() const {
		return backward_level_offset.is_empty()
				? 0
				: int(backward_level_offset.size()) - 1;
	}
	int get_forward_level_size(int p_level) const;
	int get_backward_level_size(int p_level) const;
};

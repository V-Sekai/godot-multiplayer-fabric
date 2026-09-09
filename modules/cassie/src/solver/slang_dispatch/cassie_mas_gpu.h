/**************************************************************************/
/*  cassie_mas_gpu.h                                                      */
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

// GPU dispatch for the MAS preconditioner (mas_precond ubershader).
// Paper: Wu/Wang/Wang 2022, "A GPU-Based Multilevel Additive Schwarz
// Preconditioner for Cloth and Deformable Body Simulation"
// (ACM TOG 41(4):63). Lean source at
// `modules/cassie/lean/CassieAvbd/MasPreconditioner.lean`.
//
// Parallel architecture to `cassie_slang_gpu`: one slangc-emitted
// ubershader, multiple compute pipelines built from the same shader
// module, one persistent handle per solver instance. The handle owns
// all GPU buffers (CSR matrix, hierarchy, M⁻¹ packed factorization)
// across the CG outer loop's lifetime — the per-iter apply only
// uploads the residual and downloads the preconditioned output.
//
// Not thread-safe. One instance per worker thread.

#include "../cassie_pcg.h"

#include "core/templates/local_vector.h"
#include "core/templates/rid.h"
#include "servers/rendering/rendering_device.h"

namespace cassie_mas_gpu {

enum Kernel {
	// Bind-time chain (paper §5, §6). Dispatched once per
	// CassieMasGpu::build_mas_state call.
	KERNEL_MAS_MORTON_COMPUTE = 0,
	KERNEL_MAS_BUILD_CONNECT_MASK,
	KERNEL_MAS_AGGREGATION,
	KERNEL_MAS_ASSEMBLE_SUBMATRIX,
	KERNEL_MAS_GJ_INVERT,
	// Per-CG-iter apply (paper §7). Dispatched per CG iter inside
	// the PCG outer loop. Chain: identity_copy_l0 → coarsen_residual
	// → per_domain_solve (×num_levels) → sum_levels.
	KERNEL_MAS_IDENTITY_COPY_L0,
	KERNEL_MAS_COARSEN_RESIDUAL,
	KERNEL_MAS_PER_DOMAIN_SOLVE,
	KERNEL_MAS_SUM_LEVELS,
	KERNEL_COUNT,
};

// Persistent state for one MAS preconditioner instance. Owns every
// GPU buffer the mas_precond ubershader references (19 bindings) +
// 8 uniform sets, one per entry-point pipeline. The CassieMasGpu
// instance that produced the handle owns its destructor — call
// `destroy_mas_state` before destroying the CassieMasGpu, or let the
// CassieMasGpu destructor sweep live handles.
struct MasGpuHandle {
	// --- Bindings 0..18 of mas_precond.slang ---
	// 0  ConstantBuffer<MasParams>  — ni, num_levels, domain_size,
	//                                 AABB extents, current level
	RID b_params;
	// 1-3  L_II CSR (read at bind only by assemble_submatrix; lives
	//      persistent for symmetry with the cg_pcg pattern)
	RID b_row_ptr;
	RID b_col_idx;
	RID b_values;
	// 4    RWStructuredBuffer<uint> — 60-bit Morton codes per vert
	//      (2 uints per code: low30 + high30)
	RID b_morton;
	// 5    StructuredBuffer<int> — sorted-slot → original vert index;
	//      built CPU-side by std::sort on Morton codes (plan locked
	//      decision: radix sort stays on CPU for n ≈ 6k)
	RID b_sorted_idx;
	// 6    RWStructuredBuffer<int> — flat (L × ni) map_l[i] parent
	//      supernode index per (level, vertex)
	RID b_map_per_level;
	// 7    StructuredBuffer<uint> — per-domain offset into m_inv_packed
	//      (prefix-sum of triangular row counts)
	RID b_domain_offsets;
	// 8    RWStructuredBuffer<float> — all domains' M⁻¹ packed in §7.1
	//      lower-triangular format; written by gj_invert, read by
	//      per_domain_solve
	RID b_m_inv_packed;
	// 9-10 RWStructuredBuffer<float3> — per-level r and z slices, flat
	//      across levels. Layout: level 0 occupies [0, ni); level l>0
	//      occupies [ni + Σ_{k<l} N_k, ni + Σ_{k≤l} N_k)
	RID b_r_per_level;
	RID b_z_per_level;
	// 11   StructuredBuffer<float3> — input residual (one per CG iter)
	RID b_r_input;
	// 12   RWStructuredBuffer<float3> — output z (one per CG iter,
	//      read by the CG outer loop's beta_update etc.)
	RID b_z_output;
	// 13-14  Inverted coarse map for deferred-reduction coarsen pass.
	//        coarse_offsets[s..s+1] = range of fine-vert indices for
	//        supernode s in coarse_indices. Flat across levels.
	RID b_coarse_offsets;
	RID b_coarse_indices;
	// 15   StructuredBuffer<uint> — N_l per level (L entries)
	RID b_level_sizes;
	// 16   StructuredBuffer<float3> — interior vert positions (rest
	//      pose, in original order; Morton compute reads these)
	RID b_positions;
	// 17   RWStructuredBuffer<uint> — per-supernode connectivity
	//      bitmask, one entry per supernode candidate
	RID b_connect_mask;
	// 18   RWStructuredBuffer<float> — per-domain σ² dense scratch
	//      written by assemble_submatrix, read by gj_invert
	RID b_dense_workspace;

	// One uniform set per ubershader pipeline. slangc strips bindings
	// an entry doesn't reference, so each pipeline has its own subset
	// and Godot validates strictly. All sets share the SSBOs/UBO
	// above — only the bound subset differs.
	// Indexed by Kernel enum value.
	// NOTE: per_domain_solve's slot is unused (per-level sets live in
	// uniform_sets_per_level_solve below); kept at full KERNEL_COUNT
	// length so direct enum indexing stays valid for the other kernels.
	RID uniform_sets[KERNEL_COUNT];

	// --- Multi-level apply state (paper §7) ---
	//
	// mas_per_domain_solve runs once per level. Each level needs its
	// own MasParams UBO (different level_r_offset / level_z_offset /
	// level_domain_offset / level_ni), bound at slot 0. The kernel's
	// bindings to r_per_level / z_per_level (slots 9/10) are constant
	// across levels — only the in-kernel offset arithmetic changes.
	LocalVector<RID> b_params_per_level; // L entries
	LocalVector<RID> uniform_sets_per_level_solve; // L entries
	// mas_coarsen_residual needs total_coarse_supernodes set in its
	// params UBO. b_params (the main UBO) leaves the field zero so
	// other kernels never see a stale value.
	RID b_params_coarsen;
	RID uniform_set_coarsen;

	// Topology metadata for the dispatch shape.
	int ni = 0; // interior vert count
	int nnz = 0; // L_II non-zeros
	int num_levels = 0; // L
	int domain_size = 32;
	int total_domains = 0; // Σ_l ceil(N_l / σ)
	// total_coarse_supernodes = Σ_{l>0} N_l (for the coarsen pass
	// dispatch size; level 0 is handled by mas_identity_copy_l0)
	int total_coarse_supernodes = 0;
	// Per-level supernode counts (level_sizes[0] = ni, then N_1, N_2, ...).
	// Used by apply to compute dispatch shapes.
	LocalVector<int> level_sizes_cpu;

	bool is_valid() const { return b_params.is_valid(); }
};

class CassieMasGpu {
public:
	CassieMasGpu();
	// Shared-RD constructor — uses an externally-provided RenderingDevice
	// instead of creating its own. Caller retains ownership of the RD.
	// Enables in-compute-list MAS dispatch via apply_mas_to_compute_list
	// (no host roundtrip per CG iter). Pass cassie_slang_gpu's
	// get_rd() to wire cg + MAS through the same Vulkan logical device.
	explicit CassieMasGpu(RenderingDevice *p_external_rd);
	~CassieMasGpu();

	bool is_available() const { return rd != nullptr; }

	// Build the MAS preconditioner state for a given L_II + interior-
	// vertex positions. Runs:
	//   1. Compute AABB from positions
	//   2. Dispatch mas_morton_compute → fills b_morton
	//   3. Download Morton codes
	//   4. CPU std::sort verts by Morton code → sorted_to_original
	//   5. CPU build hierarchy via DisjointSet (paper §5.2 skipping):
	//      - For each level l: assign supernodes; build coarse_offsets
	//        and coarse_indices for the inverted map
	//   6. Upload sorted_idx, map_per_level, coarse_offsets/indices,
	//      level_sizes, domain_offsets
	//   7. For each (domain, level):
	//      - Dispatch mas_assemble_submatrix → fills dense_workspace
	//      - Dispatch mas_gj_invert → fills m_inv_packed
	//
	// Returns an invalid handle if GPU is unavailable or any
	// allocation fails.
	MasGpuHandle build_mas_state(const cassie_pcg::CSRMatrix &p_L_II,
			const Vector3 *p_positions);

	// Apply M⁻¹ to a float3 residual vector. Reads p_r (length
	// 3 × handle.ni floats); writes r_z (same length). Internally:
	//   1. mas_coarsen_residual: deferred-reduction restrict r → r^(l)
	//   2. mas_per_domain_solve: per-warp SymMV on packed M⁻¹
	//   3. mas_sum_levels: prolongate + sum z^(l) → z_output
	//
	// Three dispatches, one compute list, one submit per call. The
	// CG outer loop calls this in place of jacobi_z each iter.
	bool apply_mas_gpu(const MasGpuHandle &p_handle,
			const float *p_r, float *r_z);

	// In-compute-list MAS apply. Same algorithm as apply_mas_gpu but
	// dispatches on a caller-owned compute list, reading from
	// p_external_r (slot 11 of mas_identity_copy_l0) and writing to
	// p_external_z (slot 12 of mas_sum_levels). NO submit/sync — the
	// caller's compute_list_end + submit + sync covers it.
	//
	// Requires p_external_r / p_external_z to live on the SAME RD as
	// this CassieMasGpu instance — i.e., this instance was constructed
	// with the shared-RD constructor. Returns false otherwise.
	//
	// p_external_r and p_external_z must be float[ni * 4] (Slang
	// float3 padded to 16-byte alignment). Sizes are NOT validated
	// at runtime — caller is responsible.
	bool apply_mas_to_compute_list(const MasGpuHandle &p_handle,
			RID p_external_r, RID p_external_z,
			RenderingDevice::ComputeListID p_cl);

	// Cached external uniform sets for the apply chain. Only the two
	// kernels that touch caller-owned buffers need external rebinding:
	// mas_identity_copy_l0 reads slot 11 (external r), mas_sum_levels
	// writes slot 12 (external z). The intermediate kernels (coarsen,
	// per_domain_solve) use handle-internal r_per_level / z_per_level
	// and share the handle's pre-built uniform sets.
	struct ExternalApplySets {
		RID identity_copy_l0;
		RID sum_levels;
		bool is_valid() const {
			return identity_copy_l0.is_valid() && sum_levels.is_valid();
		}
	};

	// Build the external sets once per CG outer-loop lifetime.
	// Amortizes uniform_set_create (~50 µs each) over many iters.
	// Caller owns the returned RIDs; free via free_external_uniform_sets.
	ExternalApplySets build_external_uniform_sets(
			const MasGpuHandle &p_handle,
			RID p_external_r, RID p_external_z);
	void free_external_uniform_sets(ExternalApplySets &r_sets);

	// apply_mas_to_compute_list variant using pre-built uniform sets.
	bool apply_mas_to_compute_list_cached(const MasGpuHandle &p_handle,
			const ExternalApplySets &p_sets,
			RenderingDevice::ComputeListID p_cl);

	// Release every GPU buffer + uniform set in the handle.
	// Idempotent on invalid handles.
	void destroy_mas_state(MasGpuHandle &r_handle);

private:
	RenderingDevice *rd = nullptr;
	bool owns_rd = true;
	RID shader[KERNEL_COUNT];
	RID pipeline[KERNEL_COUNT];

	bool _load_pipeline(int p_kernel, const String &p_spv_path);
	void _destroy();
};

} // namespace cassie_mas_gpu

/**************************************************************************/
/*  cassie_slang_gpu.h                                                    */
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

// GPU dispatch for the Slang-emitted CASSIE kernels.
//
// Parallel architecture to the CPU dispatch wrappers under
// `src/solver/slang_dispatch/*_dispatch.{h,cpp}`. Same Lean → Slang
// source, just slangc -target spirv this time. The CPU and GPU paths
// produce bit-identical output (within float32 mantissa) on identical
// inputs — the CPU dispatch is the parity oracle for tests, and the
// GPU dispatch is the perf path for the Quest 3 90 Hz interactive
// deform.
//
// Construction probes `DisplayServer::can_create_rendering_device()`
// and `RenderingServer::create_local_rendering_device()`. On
// headless / driverless environments these return null and
// `is_available()` reports false — callers must fall back to the CPU
// path. The SPIR-V kernels are loaded from
// `<executable_dir>/../modules/cassie/thirdparty/avbd/*.spv` (dev
// build layout) or `res://modules/cassie/thirdparty/avbd/*.spv`
// (packed project), whichever resolves first.
//
// Not thread-safe — one instance per worker thread.

#include "core/templates/rid.h"
#include "servers/rendering/rendering_device.h"

#include <cstdint>

namespace cassie_slang_gpu {

enum Kernel {
	KERNEL_SPMV = 0,
	KERNEL_SAXPBY,
	// CgUbershader entries — 10 compute pipelines emitted from one
	// cg_pcg.slang source (modules/cassie/lean/CassieAvbd/CgUbershader.lean).
	// All 10 reference the same 12-binding set; the dispatch loop
	// rebinds nothing between them.
	KERNEL_CG_INIT,
	KERNEL_CG_SPMV_P_TO_AP,
	KERNEL_CG_DOT_P_AP,
	KERNEL_CG_ALPHA_UPDATE,
	KERNEL_CG_X_AXPY_P,
	KERNEL_CG_R_AXPY_NEG_AP,
	KERNEL_CG_JACOBI_Z,
	KERNEL_CG_DOT_R_Z,
	KERNEL_CG_BETA_UPDATE,
	KERNEL_CG_P_UPDATE,
	KERNEL_CG_CHECK_RESIDUAL,
	// CgUbershader3 entries — float3 variant of cg_pcg for multi-RHS
	// (xyz axes as one vector solve). Same 11-entry sequence as cg_pcg
	// but b/x/r/z/p/Ap are RWStructuredBuffer<float3> not <float>.
	// Drives MAS preconditioning via the swap of KERNEL_CG3_JACOBI_Z
	// for an external CassieMasGpu::apply_mas_gpu call.
	KERNEL_CG3_INIT,
	KERNEL_CG3_SPMV_P_TO_AP,
	KERNEL_CG3_DOT_P_AP,
	KERNEL_CG3_ALPHA_UPDATE,
	KERNEL_CG3_X_AXPY_P,
	KERNEL_CG3_R_AXPY_NEG_AP,
	KERNEL_CG3_JACOBI_Z,
	KERNEL_CG3_DOT_R_Z,
	KERNEL_CG3_BETA_UPDATE,
	KERNEL_CG3_P_UPDATE,
	KERNEL_CG3_CHECK_RESIDUAL,
	KERNEL_COUNT,
};

// Persistent upload handle for a CSR matrix. The matrix-shaped buffers
// (params UBO + row_ptr + col_idx + values SSBOs) live on the GPU for
// the handle's lifetime; the dispatch path that consumes a CsrHandle
// only uploads x and downloads y per call. Use for inner-loop CG
// dispatches where the matrix is reused N times per frame.
//
// Ownership: the CassieSlangGpu instance that produced the handle. Call
// `free_matrix` before destroying the CassieSlangGpu, OR let the
// CassieSlangGpu destructor sweep all live handles. Reusing a freed
// handle is undefined.
struct CsrHandle {
	RID b_params;
	RID b_row_ptr;
	RID b_col_idx;
	RID b_values;
	// Persistent scratch buffers for x (input) and y (output) of
	// spmv_uploaded — allocated once at upload_matrix time, reused
	// across every dispatch via buffer_update / buffer_get_data. Avoids
	// the storage_buffer_create alloc churn that dominated per-call
	// dispatch cost on the first cut (539 µs → expected ~50 µs on
	// RTX 3090; see ENG-61 follow-up).
	RID b_x_scratch;
	RID b_y_scratch;
	// Uniform set built once and reused — uniform_set_create per call
	// was another non-trivial overhead.
	RID uniform_set_spmv;
	int rows = 0;
	int cols = 0;
	int nnz = 0;

	bool is_valid() const { return b_params.is_valid(); }
};

class CassieSlangGpu {
public:
	CassieSlangGpu();
	~CassieSlangGpu();

	bool is_available() const { return rd != nullptr; }

	// Exposes the underlying RenderingDevice. Used by classes that want
	// to share this instance's RD — see CassieMasGpu(external_rd) — so
	// the MAS preconditioner's GPU buffers live on the same RD as the
	// CG outer loop's, allowing in-compute-list dispatch without
	// per-iter host roundtrips.
	RenderingDevice *get_rd() const { return rd; }

	// y = A · x, sparse, float32, CSR layout. row_ptr length rows + 1;
	// col_idx and values length nnz; x length cols; y length rows.
	// Allocates 6 buffers (1 UBO, 5 SSBOs) per call and frees them
	// before returning. For inner-loop CG, switch to
	// upload_matrix + spmv_uploaded — that path keeps the 4 matrix
	// buffers persistent and only round-trips x and y per dispatch.
	bool spmv(int p_rows, int p_cols,
			const int32_t *p_row_ptr,
			const int32_t *p_col_idx, int p_nnz,
			const float *p_values,
			const float *p_x,
			float *r_y);

	// Upload a CSR matrix's params UBO + row_ptr/col_idx/values SSBOs
	// to the GPU and return a handle for repeated dispatch. Returns an
	// invalid handle if GPU is unavailable or any allocation fails.
	CsrHandle upload_matrix(int p_rows, int p_cols,
			const int32_t *p_row_ptr,
			const int32_t *p_col_idx, int p_nnz,
			const float *p_values);

	// Release the GPU buffers backing a handle. Idempotent — invalid
	// handles are no-ops. Zeros the handle's RIDs.
	void free_matrix(CsrHandle &r_handle);

	// y = A · x using a pre-uploaded matrix. Only x is uploaded and y
	// is downloaded per call; A's three SSBOs + params UBO are reused.
	// p_x length must equal handle.cols; r_y length must equal
	// handle.rows. Returns false on dispatch failure.
	bool spmv_uploaded(const CsrHandle &p_handle,
			const float *p_x, float *r_y);

	// Dispatch p_count repeated SPMVs in a single compute list, one
	// submit, one sync. Each iter writes y back to x for the next
	// iter (so this computes y = A^p_count · x). The point of the
	// method is NOT correctness — it's purely a microbenchmark for
	// measuring per-dispatch incremental cost vs the fixed
	// submit+sync overhead. If per-dispatch cost in a batched list
	// is small, command batching is a viable substitute for an
	// ubershader. p_iterations >= 1.
	bool spmv_batched_benchmark(const CsrHandle &p_handle,
			const float *p_x, float *r_y, int p_iterations);

	// dst[i] = alpha * x[i] + beta * y[i]
	bool saxpby(int p_n, float p_alpha, float p_beta,
			const float *p_x,
			const float *p_y,
			float *r_dst);

	// --- CgUbershader (Jacobi-PCG) -----------------------------------

	// Persistent state for one solver instance. All 12 bindings of the
	// cg_pcg ubershader live here; the uniform set is built once at
	// upload time and reused across every solve. Matrix + RHS +
	// preconditioner are uploaded once; x/r/z/p/Ap/scalars stay GPU-
	// resident across calls so a re-solve with a new initial x just
	// updates the x buffer.
	struct CgPcgHandle {
		RID b_params;
		RID b_row_ptr;
		RID b_col_idx;
		RID b_values;
		RID b_diag_inv;
		RID b_rhs;
		RID b_x;
		RID b_r;
		RID b_z;
		RID b_p;
		RID b_ap;
		RID b_scalars;
		// One uniform set per ubershader pipeline. slangc strips bindings
		// an entry point doesn't reference, so each pipeline has its own
		// expected binding subset and Godot validates strictly against
		// that. All sets reference the same SSBOs/UBO above — the GPU
		// memory is shared.
		// Indexed by (kernel_id - KERNEL_CG_INIT).
		RID uniform_sets[11];
		int rows = 0;
		int nnz = 0;

		bool is_valid() const { return b_params.is_valid(); }
	};

	// Upload a CSR matrix + diag preconditioner + RHS to the GPU and
	// pre-allocate the iteration scratch (r, z, p, Ap, scalars[10]).
	// Builds 11 per-pipeline uniform sets. Returns an invalid handle
	// if GPU is unavailable or any allocation fails. p_tol_sq is the
	// convergence threshold: check_residual sets scalars[8] = 1 when
	// ||r||² < p_tol_sq, and every subsequent dispatch in the iter
	// loop early-returns once that flag is set.
	CgPcgHandle upload_cg_state(int p_rows,
			const int32_t *p_row_ptr,
			const int32_t *p_col_idx, int p_nnz,
			const float *p_values,
			const float *p_diag_inv,
			const float *p_b, float p_tol_sq);

	// Release every buffer tied to a CgPcgHandle. Idempotent.
	void free_cg_state(CgPcgHandle &r_handle);

	// Solve A x = b via Jacobi-PCG on the GPU under constant-work
	// semantics: records exactly p_max_iter iterations (no early-exit,
	// no convergence branching), submits once, syncs once. Wall time
	// is deterministic — the same p_max_iter always takes the same
	// number of dispatches regardless of input state.
	//
	// p_x_initial uploads as the warm-start guess (pass zero for cold).
	// On return, r_x_out holds the solution; *r_residual (optional)
	// holds the final ||r||² from a single end-of-solve check_residual
	// dispatch. Caller compares to whatever tolerance matters to them.
	bool solve_sparse_gpu(const CgPcgHandle &p_handle,
			const float *p_x_initial, int p_max_iter,
			float *r_x_out,
			float *r_residual = nullptr);

	// --- CgUbershader3 (float3 Jacobi-PCG, multi-RHS via xyz axes) -----

	// Float3 variant of CgPcgHandle. Same 11-entry uniform-set pattern,
	// just buffer sizes are 4× larger (float[4] alignment) for the
	// b/x/r/z/p/Ap vector buffers. values, diag_inv, scalars stay
	// scalar (cotangent Laplacian off-diag is scalar; alpha/beta
	// arithmetic stays scalar from float3 dot reductions).
	struct CgPcg3Handle {
		RID b_params;
		RID b_row_ptr;
		RID b_col_idx;
		RID b_values;
		RID b_diag_inv;
		RID b_rhs; // RWStructuredBuffer<float3>, length rows
		RID b_x; // RWStructuredBuffer<float3>, length rows
		RID b_r;
		RID b_z;
		RID b_p;
		RID b_ap;
		RID b_scalars; // length 10 floats — same layout as cg_pcg
		RID uniform_sets[11];
		int rows = 0;
		int nnz = 0;

		bool is_valid() const { return b_params.is_valid(); }
	};

	// Upload a CSR matrix + Jacobi diag preconditioner + float3 RHS to
	// the GPU, allocate per-iter scratch (r/z/p/Ap each float3 per row,
	// scalars[10] of float). Builds 11 per-pipeline uniform sets.
	// Returns an invalid handle on failure.
	CgPcg3Handle upload_cg3_state(int p_rows,
			const int32_t *p_row_ptr,
			const int32_t *p_col_idx, int p_nnz,
			const float *p_values,
			const float *p_diag_inv,
			const float *p_b_float3); // length 3 * rows, packed xyz

	// Release every buffer + uniform set in a CgPcg3Handle. Idempotent.
	void free_cg3_state(CgPcg3Handle &r_handle);

	// Update b_rhs in place with a new float3 RHS (length 3 * rows
	// packed xyz). One buffer_update; matrix + diag_inv + 11 uniform
	// sets stay cached. Lets the handle be reused across solves whose
	// only difference is the RHS — e.g. per-frame curvenet deform.
	void update_cg3_rhs(const CgPcg3Handle &p_handle,
			const float *p_new_rhs_float3);

	// Float3 Jacobi-PCG solve. p_x_initial is float[3 * rows] xyz;
	// r_x_out (same layout) holds the solution on return. *r_residual
	// holds the final ||r||² from a single end-of-solve check_residual.
	bool solve_sparse_gpu_jacobi3(const CgPcg3Handle &p_handle,
			const float *p_x_initial, int p_max_iter,
			float *r_x_out,
			float *r_residual = nullptr);

	// MAS-preconditioned float3 PCG. Same outer loop as
	// solve_sparse_gpu_jacobi3 except the per-iter jacobi_z dispatch
	// is replaced by a callback into the MAS preconditioner apply
	// path. p_mas_apply receives (r → z) for each CG iter; the float
	// buffers are size 3 * rows. Returns false if p_mas_apply ever
	// returns false (caller's MAS dispatch failed).
	using MasApplyFn = bool (*)(void *user_data, const float *p_r,
			float *r_z, int p_count_xyz);
	bool solve_sparse_gpu_mas3(const CgPcg3Handle &p_handle,
			const float *p_x_initial, int p_max_iter,
			MasApplyFn p_mas_apply, void *p_user_data,
			float *r_x_out,
			float *r_residual = nullptr);

	// Shared-RD variant — uses an in-compute-list MAS apply via two
	// caller-provided buffer RIDs (the cg's `b_r` and `b_z`) and a
	// callback that records dispatch commands on the same compute
	// list. NO host roundtrip per iter; everything stays on GPU.
	//
	// The callback receives the compute_list_id and is expected to
	// run GPU buffer_copy + dispatch commands on it, NOT to begin/end
	// the list. Returns true on successful record; the outer solve
	// closes + submits + syncs.
	using MasApplyOnListFn = bool (*)(void *user_data,
			RID p_external_r, RID p_external_z,
			RenderingDevice::ComputeListID p_cl);
	bool solve_sparse_gpu_mas3_shared(const CgPcg3Handle &p_handle,
			const float *p_x_initial, int p_max_iter,
			MasApplyOnListFn p_mas_apply, void *p_user_data,
			float *r_x_out,
			float *r_residual = nullptr);

private:
	RenderingDevice *rd = nullptr;
	RID shader[KERNEL_COUNT];
	RID pipeline[KERNEL_COUNT];

	bool _load_pipeline(int p_kernel, const String &p_spv_path);
	void _destroy();
};

// Resolve the avbd .spv directory: tries the dev-build path
// `<executable_dir>/../modules/cassie/thirdparty/avbd/` first, then
// `res://modules/cassie/thirdparty/avbd/`. Returns the first path
// where `spmv.spv` exists, or empty String if neither resolves.
String resolve_spv_dir();

} // namespace cassie_slang_gpu

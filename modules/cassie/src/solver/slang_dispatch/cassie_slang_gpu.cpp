/**************************************************************************/
/*  cassie_slang_gpu.cpp                                                  */
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

#include "cassie_slang_gpu.h"

// SPIR-V byte arrays are baked at SCons build time from the .spv files
// at modules/cassie/thirdparty/avbd/. The .spv.gen.h headers each
// declare `cassie_slang_spirv::<name>_spv` and `<name>_spv_size`. The
// cg_pcg.<entry>.spv files emit symbols `cg_pcg_<entry>_spv` because
// the SCons builder rewrites `.` → `_` for C-identifier safety.
#include "../../../thirdparty/avbd/cg_pcg.alpha_update.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg.beta_update.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg.check_residual.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg.dot_p_ap.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg.dot_r_z.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg.init.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg.jacobi_z.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg.p_update.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg.r_axpy_neg_ap.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg.spmv_p_to_ap.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg.x_axpy_p.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg3.alpha_update.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg3.beta_update.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg3.check_residual.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg3.dot_p_ap.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg3.dot_r_z.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg3.init.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg3.jacobi_z.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg3.p_update.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg3.r_axpy_neg_ap.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg3.spmv_p_to_ap.spv.gen.h"
#include "../../../thirdparty/avbd/cg_pcg3.x_axpy_p.spv.gen.h"
#include "../../../thirdparty/avbd/saxpby.spv.gen.h"
#include "../../../thirdparty/avbd/spmv.spv.gen.h"

#include "core/string/ustring.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"

namespace cassie_slang_gpu {

namespace {

Vector<uint8_t> _bytes_view(const uint8_t *p_data, size_t p_size) {
	Vector<uint8_t> v;
	v.resize(int(p_size));
	memcpy(v.ptrw(), p_data, p_size);
	return v;
}

// Wrap a raw float buffer into Vector<uint8_t> for storage_buffer_create.
Vector<uint8_t> _floats_to_bytes(const float *p_src, int p_count) {
	Vector<uint8_t> v;
	v.resize(int(p_count * sizeof(float)));
	if (p_count > 0) {
		memcpy(v.ptrw(), p_src, size_t(p_count) * sizeof(float));
	}
	return v;
}

Vector<uint8_t> _ints_to_bytes(const int32_t *p_src, int p_count) {
	Vector<uint8_t> v;
	v.resize(int(p_count * sizeof(int32_t)));
	if (p_count > 0) {
		memcpy(v.ptrw(), p_src, size_t(p_count) * sizeof(int32_t));
	}
	return v;
}

} // namespace

CassieSlangGpu::CassieSlangGpu() {
	if (!DisplayServer::can_create_rendering_device()) {
		return;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs == nullptr) {
		return;
	}
	rd = rs->create_local_rendering_device();
	if (rd == nullptr) {
		return;
	}

	// Load each kernel from the embedded byte arrays. Order must match
	// the enum Kernel { ... } in the header.
	struct KernelBlob {
		const uint8_t *data;
		size_t size;
		const char *name;
	};
	using namespace cassie_slang_spirv;
	const KernelBlob blobs[KERNEL_COUNT] = {
		{ spmv_spv, spmv_spv_size, "cassie_slang_gpu_spmv" },
		{ saxpby_spv, saxpby_spv_size, "cassie_slang_gpu_saxpby" },
		{ cg_pcg_init_spv, cg_pcg_init_spv_size, "cassie_slang_gpu_cg_init" },
		{ cg_pcg_spmv_p_to_ap_spv, cg_pcg_spmv_p_to_ap_spv_size, "cassie_slang_gpu_cg_spmv_p_to_ap" },
		{ cg_pcg_dot_p_ap_spv, cg_pcg_dot_p_ap_spv_size, "cassie_slang_gpu_cg_dot_p_ap" },
		{ cg_pcg_alpha_update_spv, cg_pcg_alpha_update_spv_size, "cassie_slang_gpu_cg_alpha_update" },
		{ cg_pcg_x_axpy_p_spv, cg_pcg_x_axpy_p_spv_size, "cassie_slang_gpu_cg_x_axpy_p" },
		{ cg_pcg_r_axpy_neg_ap_spv, cg_pcg_r_axpy_neg_ap_spv_size, "cassie_slang_gpu_cg_r_axpy_neg_ap" },
		{ cg_pcg_jacobi_z_spv, cg_pcg_jacobi_z_spv_size, "cassie_slang_gpu_cg_jacobi_z" },
		{ cg_pcg_dot_r_z_spv, cg_pcg_dot_r_z_spv_size, "cassie_slang_gpu_cg_dot_r_z" },
		{ cg_pcg_beta_update_spv, cg_pcg_beta_update_spv_size, "cassie_slang_gpu_cg_beta_update" },
		{ cg_pcg_p_update_spv, cg_pcg_p_update_spv_size, "cassie_slang_gpu_cg_p_update" },
		{ cg_pcg_check_residual_spv, cg_pcg_check_residual_spv_size, "cassie_slang_gpu_cg_check_residual" },
		// cg_pcg3 — float3 variant of the CG outer loop, same 11-entry
		// sequence. The bind masks are identical to cg_pcg's; only the
		// buffer sizes change.
		{ cg_pcg3_init_spv, cg_pcg3_init_spv_size, "cassie_slang_gpu_cg3_init" },
		{ cg_pcg3_spmv_p_to_ap_spv, cg_pcg3_spmv_p_to_ap_spv_size, "cassie_slang_gpu_cg3_spmv_p_to_ap" },
		{ cg_pcg3_dot_p_ap_spv, cg_pcg3_dot_p_ap_spv_size, "cassie_slang_gpu_cg3_dot_p_ap" },
		{ cg_pcg3_alpha_update_spv, cg_pcg3_alpha_update_spv_size, "cassie_slang_gpu_cg3_alpha_update" },
		{ cg_pcg3_x_axpy_p_spv, cg_pcg3_x_axpy_p_spv_size, "cassie_slang_gpu_cg3_x_axpy_p" },
		{ cg_pcg3_r_axpy_neg_ap_spv, cg_pcg3_r_axpy_neg_ap_spv_size, "cassie_slang_gpu_cg3_r_axpy_neg_ap" },
		{ cg_pcg3_jacobi_z_spv, cg_pcg3_jacobi_z_spv_size, "cassie_slang_gpu_cg3_jacobi_z" },
		{ cg_pcg3_dot_r_z_spv, cg_pcg3_dot_r_z_spv_size, "cassie_slang_gpu_cg3_dot_r_z" },
		{ cg_pcg3_beta_update_spv, cg_pcg3_beta_update_spv_size, "cassie_slang_gpu_cg3_beta_update" },
		{ cg_pcg3_p_update_spv, cg_pcg3_p_update_spv_size, "cassie_slang_gpu_cg3_p_update" },
		{ cg_pcg3_check_residual_spv, cg_pcg3_check_residual_spv_size, "cassie_slang_gpu_cg3_check_residual" },
	};

	for (int i = 0; i < KERNEL_COUNT; ++i) {
		RenderingDevice::ShaderStageSPIRVData stage;
		stage.shader_stage = RenderingDevice::SHADER_STAGE_COMPUTE;
		stage.spirv = _bytes_view(blobs[i].data, blobs[i].size);

		Vector<RenderingDevice::ShaderStageSPIRVData> stages;
		stages.push_back(stage);

		shader[i] = rd->shader_create_from_spirv(stages, String(blobs[i].name));
		if (shader[i].is_null()) {
			ERR_PRINT("cassie_slang_gpu: shader_create_from_spirv failed for kernel " + itos(i));
			_destroy();
			return;
		}
		pipeline[i] = rd->compute_pipeline_create(shader[i]);
		if (pipeline[i].is_null()) {
			ERR_PRINT("cassie_slang_gpu: compute_pipeline_create failed for kernel " + itos(i));
			_destroy();
			return;
		}
	}
}

CassieSlangGpu::~CassieSlangGpu() {
	_destroy();
}

void CassieSlangGpu::_destroy() {
	if (rd == nullptr) {
		return;
	}
	for (int i = 0; i < KERNEL_COUNT; ++i) {
		if (pipeline[i].is_valid()) {
			rd->free_rid(pipeline[i]);
			pipeline[i] = RID();
		}
		if (shader[i].is_valid()) {
			rd->free_rid(shader[i]);
			shader[i] = RID();
		}
	}
	memdelete(rd);
	rd = nullptr;
}

bool CassieSlangGpu::spmv(int p_rows, int p_cols,
		const int32_t *p_row_ptr,
		const int32_t *p_col_idx, int p_nnz,
		const float *p_values,
		const float *p_x,
		float *r_y) {
	if (rd == nullptr || pipeline[KERNEL_SPMV].is_null() || p_rows == 0) {
		return false;
	}

	// SpmvDf32 binding layout (set=0):
	//   0 : UBO   SpmvDf32Params { uint rows }
	//   1 : SSBO  rowPtr  (int32, length rows+1)
	//   2 : SSBO  colIdx  (int32, length nnz)
	//   3 : SSBO  values  (float, length nnz)
	//   4 : SSBO  x       (float, length cols)
	//   5 : SSBO  y       (float, length rows)  RW
	struct alignas(16) Params {
		uint32_t rows;
		uint32_t pad0 = 0;
		uint32_t pad1 = 0;
		uint32_t pad2 = 0;
	} params;
	params.rows = uint32_t(p_rows);

	Vector<uint8_t> param_bytes;
	param_bytes.resize(sizeof(Params));
	memcpy(param_bytes.ptrw(), &params, sizeof(Params));

	RID b_params = rd->uniform_buffer_create(param_bytes.size(), param_bytes);
	RID b_row_ptr = rd->storage_buffer_create(MAX(int((p_rows + 1) * sizeof(int32_t)), 4),
			_ints_to_bytes(p_row_ptr, p_rows + 1));
	RID b_col_idx = rd->storage_buffer_create(MAX(int(p_nnz * sizeof(int32_t)), 4),
			_ints_to_bytes(p_col_idx, p_nnz));
	RID b_values = rd->storage_buffer_create(MAX(int(p_nnz * sizeof(float)), 4),
			_floats_to_bytes(p_values, p_nnz));
	RID b_x = rd->storage_buffer_create(MAX(int(p_cols * sizeof(float)), 4),
			_floats_to_bytes(p_x, p_cols));

	Vector<uint8_t> y_zero;
	y_zero.resize(int(p_rows * sizeof(float)));
	memset(y_zero.ptrw(), 0, y_zero.size());
	RID b_y = rd->storage_buffer_create(y_zero.size(), y_zero);

	Vector<RenderingDevice::Uniform> uniforms;
	{
		RenderingDevice::Uniform u;
		u.uniform_type = RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER;
		u.binding = 0;
		u.append_id(b_params);
		uniforms.push_back(u);
	}
	const RID storage_rids[] = { b_row_ptr, b_col_idx, b_values, b_x, b_y };
	for (int i = 0; i < 5; ++i) {
		RenderingDevice::Uniform u;
		u.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u.binding = uint32_t(i + 1);
		u.append_id(storage_rids[i]);
		uniforms.push_back(u);
	}

	RID uniform_set = rd->uniform_set_create(uniforms, shader[KERNEL_SPMV], 0);

	const uint32_t groups = uint32_t((p_rows + 255) / 256);
	RenderingDevice::ComputeListID cl = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(cl, pipeline[KERNEL_SPMV]);
	rd->compute_list_bind_uniform_set(cl, uniform_set, 0);
	rd->compute_list_dispatch(cl, groups, 1, 1);
	rd->compute_list_end();
	rd->submit();
	rd->sync();

	Vector<uint8_t> y_back = rd->buffer_get_data(b_y);
	memcpy(r_y, y_back.ptr(), size_t(p_rows) * sizeof(float));

	rd->free_rid(uniform_set);
	rd->free_rid(b_y);
	rd->free_rid(b_x);
	rd->free_rid(b_values);
	rd->free_rid(b_col_idx);
	rd->free_rid(b_row_ptr);
	rd->free_rid(b_params);
	return true;
}

CsrHandle CassieSlangGpu::upload_matrix(int p_rows, int p_cols,
		const int32_t *p_row_ptr,
		const int32_t *p_col_idx, int p_nnz,
		const float *p_values) {
	CsrHandle h;
	if (rd == nullptr || pipeline[KERNEL_SPMV].is_null() || p_rows == 0) {
		return h;
	}

	struct alignas(16) Params {
		uint32_t rows;
		uint32_t pad0 = 0;
		uint32_t pad1 = 0;
		uint32_t pad2 = 0;
	} params;
	params.rows = uint32_t(p_rows);

	Vector<uint8_t> param_bytes;
	param_bytes.resize(sizeof(Params));
	memcpy(param_bytes.ptrw(), &params, sizeof(Params));

	h.b_params = rd->uniform_buffer_create(param_bytes.size(), param_bytes);
	h.b_row_ptr = rd->storage_buffer_create(MAX(int((p_rows + 1) * sizeof(int32_t)), 4),
			_ints_to_bytes(p_row_ptr, p_rows + 1));
	h.b_col_idx = rd->storage_buffer_create(MAX(int(p_nnz * sizeof(int32_t)), 4),
			_ints_to_bytes(p_col_idx, p_nnz));
	h.b_values = rd->storage_buffer_create(MAX(int(p_nnz * sizeof(float)), 4),
			_floats_to_bytes(p_values, p_nnz));

	// Persistent x and y scratch — allocated once, reused via buffer_update
	// and buffer_get_data on every spmv_uploaded call.
	Vector<uint8_t> zero_x;
	zero_x.resize(int(p_cols * sizeof(float)));
	memset(zero_x.ptrw(), 0, zero_x.size());
	h.b_x_scratch = rd->storage_buffer_create(MAX(int(zero_x.size()), 4), zero_x);

	Vector<uint8_t> zero_y;
	zero_y.resize(int(p_rows * sizeof(float)));
	memset(zero_y.ptrw(), 0, zero_y.size());
	h.b_y_scratch = rd->storage_buffer_create(zero_y.size(), zero_y);

	if (h.b_params.is_null() || h.b_row_ptr.is_null() || h.b_col_idx.is_null() ||
			h.b_values.is_null() || h.b_x_scratch.is_null() || h.b_y_scratch.is_null()) {
		free_matrix(h);
		return CsrHandle();
	}

	// Pre-build the uniform set so we don't pay uniform_set_create per
	// dispatch. The set references the persistent SSBOs by RID — as
	// long as those RIDs stay valid, the set remains bindable.
	Vector<RenderingDevice::Uniform> uniforms;
	{
		RenderingDevice::Uniform u;
		u.uniform_type = RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER;
		u.binding = 0;
		u.append_id(h.b_params);
		uniforms.push_back(u);
	}
	const RID storage_rids[] = { h.b_row_ptr, h.b_col_idx, h.b_values,
		h.b_x_scratch, h.b_y_scratch };
	for (int i = 0; i < 5; ++i) {
		RenderingDevice::Uniform u;
		u.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u.binding = uint32_t(i + 1);
		u.append_id(storage_rids[i]);
		uniforms.push_back(u);
	}
	h.uniform_set_spmv = rd->uniform_set_create(uniforms, shader[KERNEL_SPMV], 0);

	h.rows = p_rows;
	h.cols = p_cols;
	h.nnz = p_nnz;
	return h;
}

void CassieSlangGpu::free_matrix(CsrHandle &r_handle) {
	if (rd == nullptr) {
		return;
	}
	if (r_handle.uniform_set_spmv.is_valid()) {
		rd->free_rid(r_handle.uniform_set_spmv);
		r_handle.uniform_set_spmv = RID();
	}
	if (r_handle.b_y_scratch.is_valid()) {
		rd->free_rid(r_handle.b_y_scratch);
		r_handle.b_y_scratch = RID();
	}
	if (r_handle.b_x_scratch.is_valid()) {
		rd->free_rid(r_handle.b_x_scratch);
		r_handle.b_x_scratch = RID();
	}
	if (r_handle.b_values.is_valid()) {
		rd->free_rid(r_handle.b_values);
		r_handle.b_values = RID();
	}
	if (r_handle.b_col_idx.is_valid()) {
		rd->free_rid(r_handle.b_col_idx);
		r_handle.b_col_idx = RID();
	}
	if (r_handle.b_row_ptr.is_valid()) {
		rd->free_rid(r_handle.b_row_ptr);
		r_handle.b_row_ptr = RID();
	}
	if (r_handle.b_params.is_valid()) {
		rd->free_rid(r_handle.b_params);
		r_handle.b_params = RID();
	}
	r_handle.rows = 0;
	r_handle.cols = 0;
	r_handle.nnz = 0;
}

bool CassieSlangGpu::spmv_uploaded(const CsrHandle &p_handle,
		const float *p_x, float *r_y) {
	if (rd == nullptr || pipeline[KERNEL_SPMV].is_null() || !p_handle.is_valid()) {
		return false;
	}

	// Update x via buffer_update (no realloc), dispatch against the
	// pre-built uniform set, read back y via buffer_get_data. Avoids
	// the storage_buffer_create + uniform_set_create per-call churn
	// that pushed naive dispatch to 540 µs/call on RTX 3090.
	rd->buffer_update(p_handle.b_x_scratch, 0,
			uint32_t(p_handle.cols * sizeof(float)), p_x);

	const uint32_t groups = uint32_t((p_handle.rows + 255) / 256);
	RenderingDevice::ComputeListID cl = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(cl, pipeline[KERNEL_SPMV]);
	rd->compute_list_bind_uniform_set(cl, p_handle.uniform_set_spmv, 0);
	rd->compute_list_dispatch(cl, groups, 1, 1);
	rd->compute_list_end();
	rd->submit();
	rd->sync();

	Vector<uint8_t> y_back = rd->buffer_get_data(p_handle.b_y_scratch);
	memcpy(r_y, y_back.ptr(), size_t(p_handle.rows) * sizeof(float));
	return true;
}

bool CassieSlangGpu::spmv_batched_benchmark(const CsrHandle &p_handle,
		const float *p_x, float *r_y, int p_iterations) {
	if (rd == nullptr || pipeline[KERNEL_SPMV].is_null() || !p_handle.is_valid() ||
			p_iterations < 1) {
		return false;
	}

	// Upload initial x to the persistent x scratch.
	rd->buffer_update(p_handle.b_x_scratch, 0,
			uint32_t(p_handle.cols * sizeof(float)), p_x);

	const uint32_t groups = uint32_t((p_handle.rows + 255) / 256);
	RenderingDevice::ComputeListID cl = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(cl, pipeline[KERNEL_SPMV]);
	rd->compute_list_bind_uniform_set(cl, p_handle.uniform_set_spmv, 0);
	for (int i = 0; i < p_iterations; ++i) {
		rd->compute_list_dispatch(cl, groups, 1, 1);
		// No memory barrier between dispatches: the benchmark reuses
		// the same x as input every iter and writes to the same y, so
		// the iters carry no data dependency. The N dispatches pipeline
		// freely on the GPU — what we measure here is the lower bound
		// on per-dispatch driver overhead, not the cost of a real
		// barriered CG iter chain.
	}
	rd->compute_list_end();
	rd->submit();
	rd->sync();

	Vector<uint8_t> y_back = rd->buffer_get_data(p_handle.b_y_scratch);
	memcpy(r_y, y_back.ptr(), size_t(p_handle.rows) * sizeof(float));
	return true;
}

bool CassieSlangGpu::saxpby(int p_n, float p_alpha, float p_beta,
		const float *p_x,
		const float *p_y,
		float *r_dst) {
	if (rd == nullptr || pipeline[KERNEL_SAXPBY].is_null() || p_n == 0) {
		return false;
	}

	// Saxpby binding layout (set=0):
	//   0 : UBO   SaxpbyParams { uint n; float alpha; float beta }
	//   1 : SSBO  x   (float, length n)
	//   2 : SSBO  y   (float, length n)
	//   3 : SSBO  dst (float, length n)  RW
	struct alignas(16) Params {
		uint32_t n;
		float alpha;
		float beta;
		uint32_t pad0 = 0;
	} params;
	params.n = uint32_t(p_n);
	params.alpha = p_alpha;
	params.beta = p_beta;

	Vector<uint8_t> param_bytes;
	param_bytes.resize(sizeof(Params));
	memcpy(param_bytes.ptrw(), &params, sizeof(Params));

	RID b_params = rd->uniform_buffer_create(param_bytes.size(), param_bytes);
	RID b_x = rd->storage_buffer_create(int(p_n * sizeof(float)),
			_floats_to_bytes(p_x, p_n));
	RID b_y = rd->storage_buffer_create(int(p_n * sizeof(float)),
			_floats_to_bytes(p_y, p_n));

	Vector<uint8_t> dst_zero;
	dst_zero.resize(int(p_n * sizeof(float)));
	memset(dst_zero.ptrw(), 0, dst_zero.size());
	RID b_dst = rd->storage_buffer_create(dst_zero.size(), dst_zero);

	Vector<RenderingDevice::Uniform> uniforms;
	{
		RenderingDevice::Uniform u;
		u.uniform_type = RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER;
		u.binding = 0;
		u.append_id(b_params);
		uniforms.push_back(u);
	}
	const RID storage_rids[] = { b_x, b_y, b_dst };
	for (int i = 0; i < 3; ++i) {
		RenderingDevice::Uniform u;
		u.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u.binding = uint32_t(i + 1);
		u.append_id(storage_rids[i]);
		uniforms.push_back(u);
	}

	RID uniform_set = rd->uniform_set_create(uniforms, shader[KERNEL_SAXPBY], 0);

	const uint32_t groups = uint32_t((p_n + 255) / 256);
	RenderingDevice::ComputeListID cl = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(cl, pipeline[KERNEL_SAXPBY]);
	rd->compute_list_bind_uniform_set(cl, uniform_set, 0);
	rd->compute_list_dispatch(cl, groups, 1, 1);
	rd->compute_list_end();
	rd->submit();
	rd->sync();

	Vector<uint8_t> dst_back = rd->buffer_get_data(b_dst);
	memcpy(r_dst, dst_back.ptr(), size_t(p_n) * sizeof(float));

	rd->free_rid(uniform_set);
	rd->free_rid(b_dst);
	rd->free_rid(b_y);
	rd->free_rid(b_x);
	rd->free_rid(b_params);
	return true;
}

String resolve_spv_dir() {
	// Kept as a stub for the file-load fallback design that was
	// superseded by the embedded byte-array headers. Returns empty
	// String now — callers should not need to resolve a path.
	return String();
}

// === CgUbershader (Jacobi-PCG) dispatch ====================================

CassieSlangGpu::CgPcgHandle CassieSlangGpu::upload_cg_state(int p_rows,
		const int32_t *p_row_ptr,
		const int32_t *p_col_idx, int p_nnz,
		const float *p_values,
		const float *p_diag_inv,
		const float *p_b, float p_tol_sq) {
	CgPcgHandle h;
	if (rd == nullptr || pipeline[KERNEL_CG_INIT].is_null() || p_rows == 0) {
		return h;
	}

	struct alignas(16) CgPcgParams {
		uint32_t rows;
		uint32_t pad0 = 0;
		uint32_t pad1 = 0;
		uint32_t pad2 = 0;
	} params;
	params.rows = uint32_t(p_rows);
	(void)p_tol_sq; // constant-work design: tol no longer drives the kernel

	Vector<uint8_t> param_bytes;
	param_bytes.resize(sizeof(CgPcgParams));
	memcpy(param_bytes.ptrw(), &params, sizeof(CgPcgParams));

	h.b_params = rd->uniform_buffer_create(param_bytes.size(), param_bytes);
	h.b_row_ptr = rd->storage_buffer_create(MAX(int((p_rows + 1) * sizeof(int32_t)), 4),
			_ints_to_bytes(p_row_ptr, p_rows + 1));
	h.b_col_idx = rd->storage_buffer_create(MAX(int(p_nnz * sizeof(int32_t)), 4),
			_ints_to_bytes(p_col_idx, p_nnz));
	h.b_values = rd->storage_buffer_create(MAX(int(p_nnz * sizeof(float)), 4),
			_floats_to_bytes(p_values, p_nnz));
	h.b_diag_inv = rd->storage_buffer_create(int(p_rows * sizeof(float)),
			_floats_to_bytes(p_diag_inv, p_rows));
	h.b_rhs = rd->storage_buffer_create(int(p_rows * sizeof(float)),
			_floats_to_bytes(p_b, p_rows));

	// Scratch buffers — zero-initialized; init kernel writes r, z, p
	// from b - A·x_initial on the first dispatch.
	Vector<uint8_t> zero_rows;
	zero_rows.resize(int(p_rows * sizeof(float)));
	memset(zero_rows.ptrw(), 0, zero_rows.size());
	h.b_x = rd->storage_buffer_create(zero_rows.size(), zero_rows);
	h.b_r = rd->storage_buffer_create(zero_rows.size(), zero_rows);
	h.b_z = rd->storage_buffer_create(zero_rows.size(), zero_rows);
	h.b_p = rd->storage_buffer_create(zero_rows.size(), zero_rows);
	h.b_ap = rd->storage_buffer_create(zero_rows.size(), zero_rows);

	// scalars: 10 floats — [rz_hi, rz_lo, pAp_hi, pAp_lo, alpha, -alpha,
	// beta_or_rz_new_hi, rz_new_lo, converged_flag, residual_norm2]
	Vector<uint8_t> zero_scalars;
	zero_scalars.resize(10 * sizeof(float));
	memset(zero_scalars.ptrw(), 0, zero_scalars.size());
	h.b_scalars = rd->storage_buffer_create(zero_scalars.size(), zero_scalars);

	if (h.b_params.is_null() || h.b_row_ptr.is_null() || h.b_col_idx.is_null() ||
			h.b_values.is_null() || h.b_diag_inv.is_null() || h.b_rhs.is_null() ||
			h.b_x.is_null() || h.b_r.is_null() || h.b_z.is_null() ||
			h.b_p.is_null() || h.b_ap.is_null() || h.b_scalars.is_null()) {
		free_cg_state(h);
		return CgPcgHandle();
	}

	// Per-pipeline binding masks. Each entry point in cg_pcg.slang
	// references a subset of the 12 module-level bindings; slangc strips
	// the unreferenced ones from the pipeline's expected layout, so we
	// build a uniform set tailored to each pipeline's subset.
	//
	// Layout: bit b set iff binding b is referenced. Derived from the
	// CgUbershader.lean source (the entry point bodies grep "params" →
	// 0, "rowPtr" → 1, ..., "scalars" → 11). If the Lean kernel changes,
	// the mask here MUST follow — there's no auto-reflection yet.
	// Per-pipeline binding masks under the constant-work design
	// (no earlyExitGuard; per-iter entries don't read scalars[8]).
	static constexpr uint32_t bind_mask_init = 0b0000'0011'1111'1111u; // 0-9
	static constexpr uint32_t bind_mask_spmv_p_to_ap = 0b0000'0110'0000'1111u; // 0,1,2,3,9,10
	static constexpr uint32_t bind_mask_dot_p_ap = 0b0000'1110'0000'0001u; // 0,9,10,11
	static constexpr uint32_t bind_mask_alpha_update = 0b0000'1000'0000'0000u; // 11
	static constexpr uint32_t bind_mask_x_axpy_p = 0b0000'1010'0100'0001u; // 0,6,9,11
	static constexpr uint32_t bind_mask_r_axpy_neg_ap = 0b0000'1100'1000'0001u; // 0,7,10,11
	static constexpr uint32_t bind_mask_jacobi_z = 0b0000'0001'1001'0001u; // 0,4,7,8
	static constexpr uint32_t bind_mask_dot_r_z = 0b0000'1001'1000'0001u; // 0,7,8,11
	static constexpr uint32_t bind_mask_beta_update = 0b0000'1000'0000'0000u; // 11
	static constexpr uint32_t bind_mask_p_update = 0b0000'1011'0000'0001u; // 0,8,9,11
	static constexpr uint32_t bind_mask_check_residual = 0b0000'1000'1000'0001u; // 0,7,11
	const uint32_t bind_masks[11] = {
		bind_mask_init,
		bind_mask_spmv_p_to_ap,
		bind_mask_dot_p_ap,
		bind_mask_alpha_update,
		bind_mask_x_axpy_p,
		bind_mask_r_axpy_neg_ap,
		bind_mask_jacobi_z,
		bind_mask_dot_r_z,
		bind_mask_beta_update,
		bind_mask_p_update,
		bind_mask_check_residual,
	};
	// Binding b → RID + uniform_type. Index by binding number.
	const RID bind_rids[12] = {
		h.b_params,
		h.b_row_ptr,
		h.b_col_idx,
		h.b_values,
		h.b_diag_inv,
		h.b_rhs,
		h.b_x,
		h.b_r,
		h.b_z,
		h.b_p,
		h.b_ap,
		h.b_scalars,
	};
	for (int k = 0; k < 11; ++k) {
		Vector<RenderingDevice::Uniform> uniforms;
		const uint32_t mask = bind_masks[k];
		for (int b = 0; b < 12; ++b) {
			if (!(mask & (1u << b))) {
				continue;
			}
			RenderingDevice::Uniform u;
			u.uniform_type = (b == 0)
					? RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER
					: RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
			u.binding = uint32_t(b);
			u.append_id(bind_rids[b]);
			uniforms.push_back(u);
		}
		const int kernel_id = KERNEL_CG_INIT + k;
		h.uniform_sets[k] = rd->uniform_set_create(uniforms,
				shader[kernel_id], 0);
		if (h.uniform_sets[k].is_null()) {
			ERR_PRINT("cassie_slang_gpu: uniform_set_create failed for cg kernel " + itos(k));
			free_cg_state(h);
			return CgPcgHandle();
		}
	}

	h.rows = p_rows;
	h.nnz = p_nnz;
	return h;
}

void CassieSlangGpu::free_cg_state(CgPcgHandle &r_handle) {
	if (rd == nullptr) {
		return;
	}
	for (int i = 0; i < 11; ++i) {
		if (r_handle.uniform_sets[i].is_valid()) {
			rd->free_rid(r_handle.uniform_sets[i]);
		}
	}
	const RID rids[] = {
		r_handle.b_scalars,
		r_handle.b_ap,
		r_handle.b_p,
		r_handle.b_z,
		r_handle.b_r,
		r_handle.b_x,
		r_handle.b_rhs,
		r_handle.b_diag_inv,
		r_handle.b_values,
		r_handle.b_col_idx,
		r_handle.b_row_ptr,
		r_handle.b_params,
	};
	for (RID rid : rids) {
		if (rid.is_valid()) {
			rd->free_rid(rid);
		}
	}
	r_handle = CgPcgHandle();
}

bool CassieSlangGpu::solve_sparse_gpu(const CgPcgHandle &p_handle,
		const float *p_x_initial, int p_max_iter, float *r_x_out,
		float *r_residual) {
	if (rd == nullptr || !p_handle.is_valid() || p_max_iter < 1) {
		return false;
	}

	const int rows = p_handle.rows;

	// Upload the initial guess to x.
	rd->buffer_update(p_handle.b_x, 0,
			uint32_t(rows * sizeof(float)), p_x_initial);

	// Reset scalars[9] (residual) so the end-of-solve check_residual
	// writes a fresh value rather than reading whatever the previous
	// solve left there. scalars[8] is reserved (no flag in
	// constant-work design) but cleared too for tidiness.
	const float zero_pair[2] = { 0.0f, 0.0f };
	rd->buffer_update(p_handle.b_scalars, uint32_t(8 * sizeof(float)),
			sizeof(zero_pair), zero_pair);

	const uint32_t per_row_groups = uint32_t((rows + 255) / 256);

	// All N iters land in one compute list, one submit, one sync.
	// Per iter the dispatch sequence:
	//
	//   spmv_p_to_ap → dot_p_ap → alpha_update → x_axpy_p → r_axpy_neg_ap
	//   → jacobi_z → dot_r_z → beta_update → p_update
	//
	// The `init` entry runs once before the loop. Every dispatch
	// references the same uniform set — no rebinds across the entire
	// solve. The compute_list_add_barrier between dispatches forces
	// the GPU to wait for prior writes to commit before the next
	// dispatch reads them.
	RenderingDevice::ComputeListID cl = rd->compute_list_begin();

	// Helper: bind pipeline + its specific uniform set, then dispatch.
	// Per-pipeline uniform sets (slangc strips bindings the entry point
	// doesn't reference, so each pipeline has its own subset).
	auto cg_dispatch = [&](int kernel_id, uint32_t groups) {
		const int ub_idx = kernel_id - KERNEL_CG_INIT;
		rd->compute_list_bind_compute_pipeline(cl, pipeline[kernel_id]);
		rd->compute_list_bind_uniform_set(cl, p_handle.uniform_sets[ub_idx], 0);
		rd->compute_list_dispatch(cl, groups, 1, 1);
	};

	// init: r = b - A·x_initial; z = M⁻¹r; p = z
	cg_dispatch(KERNEL_CG_INIT, per_row_groups);
	rd->compute_list_add_barrier(cl);

	// Single-workgroup dot reductions use 1 group of 256 threads.
	const uint32_t dot_groups = 1;

	for (int iter = 0; iter < p_max_iter; ++iter) {
		// Ap = A · p
		cg_dispatch(KERNEL_CG_SPMV_P_TO_AP, per_row_groups);
		rd->compute_list_add_barrier(cl);

		// scalars[2..3] = p · Ap  (df32 reduce)
		cg_dispatch(KERNEL_CG_DOT_P_AP, dot_groups);
		rd->compute_list_add_barrier(cl);

		// scalars[4] = alpha; scalars[5] = -alpha
		cg_dispatch(KERNEL_CG_ALPHA_UPDATE, 1);
		rd->compute_list_add_barrier(cl);

		// x += alpha · p     ; r -= alpha · Ap   (independent — no
		// barrier between them; both read alpha; the next iter's spmv
		// reads x/r so we barrier after both finish.)
		cg_dispatch(KERNEL_CG_X_AXPY_P, per_row_groups);
		cg_dispatch(KERNEL_CG_R_AXPY_NEG_AP, per_row_groups);
		rd->compute_list_add_barrier(cl);

		// z = diag_inv · r
		cg_dispatch(KERNEL_CG_JACOBI_Z, per_row_groups);
		rd->compute_list_add_barrier(cl);

		// scalars[6..7] = r · z  (df32 reduce)
		cg_dispatch(KERNEL_CG_DOT_R_Z, dot_groups);
		rd->compute_list_add_barrier(cl);

		// beta = rz_new / rz_old ; rolls rz_new into scalars[0..1]
		cg_dispatch(KERNEL_CG_BETA_UPDATE, 1);
		rd->compute_list_add_barrier(cl);

		// p = z + beta · p
		cg_dispatch(KERNEL_CG_P_UPDATE, per_row_groups);
		rd->compute_list_add_barrier(cl);
	}

	// One end-of-solve residual check for caller observability —
	// constant-work design (see REFERENCES.bib @maccarthaigh_constant_work):
	// the iter loop runs a fixed N regardless of convergence, and the
	// residual is reported rather than acted upon. Caller decides
	// whether to increase max_iter on subsequent solves.
	cg_dispatch(KERNEL_CG_CHECK_RESIDUAL, dot_groups);
	rd->compute_list_add_barrier(cl);

	rd->compute_list_end();
	rd->submit();
	rd->sync();

	Vector<uint8_t> x_back = rd->buffer_get_data(p_handle.b_x);
	memcpy(r_x_out, x_back.ptr(), size_t(rows) * sizeof(float));

	if (r_residual) {
		Vector<uint8_t> scalars_back = rd->buffer_get_data(p_handle.b_scalars);
		const float *sc = reinterpret_cast<const float *>(scalars_back.ptr());
		*r_residual = sc[9];
	}
	return true;
}

// --- CgUbershader3 (float3) -------------------------------------------

CassieSlangGpu::CgPcg3Handle CassieSlangGpu::upload_cg3_state(int p_rows,
		const int32_t *p_row_ptr,
		const int32_t *p_col_idx, int p_nnz,
		const float *p_values,
		const float *p_diag_inv,
		const float *p_b_float3) {
	CgPcg3Handle h;
	if (rd == nullptr || pipeline[KERNEL_CG3_INIT].is_null() || p_rows == 0) {
		return h;
	}

	// Same layout as CgPcgParams — just one uint rows field, 16-byte pad.
	struct alignas(16) CgPcg3Params {
		uint32_t rows;
		uint32_t pad0 = 0, pad1 = 0, pad2 = 0;
	} params;
	params.rows = uint32_t(p_rows);

	Vector<uint8_t> param_bytes;
	param_bytes.resize(sizeof(CgPcg3Params));
	memcpy(param_bytes.ptrw(), &params, sizeof(CgPcg3Params));
	h.b_params = rd->uniform_buffer_create(param_bytes.size(), param_bytes);

	h.b_row_ptr = rd->storage_buffer_create(MAX(int((p_rows + 1) * sizeof(int32_t)), 4),
			_ints_to_bytes(p_row_ptr, p_rows + 1));
	h.b_col_idx = rd->storage_buffer_create(MAX(int(p_nnz * sizeof(int32_t)), 4),
			_ints_to_bytes(p_col_idx, p_nnz));
	h.b_values = rd->storage_buffer_create(MAX(int(p_nnz * sizeof(float)), 4),
			_floats_to_bytes(p_values, p_nnz));
	h.b_diag_inv = rd->storage_buffer_create(int(p_rows * sizeof(float)),
			_floats_to_bytes(p_diag_inv, p_rows));

	// float3 buffers: 4 floats per vert (16-byte align).
	Vector<uint8_t> rhs_packed;
	rhs_packed.resize(int(p_rows * 4 * sizeof(float)));
	float *rhs_ptr = reinterpret_cast<float *>(rhs_packed.ptrw());
	for (int i = 0; i < p_rows; ++i) {
		rhs_ptr[i * 4 + 0] = p_b_float3[i * 3 + 0];
		rhs_ptr[i * 4 + 1] = p_b_float3[i * 3 + 1];
		rhs_ptr[i * 4 + 2] = p_b_float3[i * 3 + 2];
		rhs_ptr[i * 4 + 3] = 0.0f;
	}
	h.b_rhs = rd->storage_buffer_create(rhs_packed.size(), rhs_packed);

	Vector<uint8_t> zero_float3_rows;
	zero_float3_rows.resize(int(p_rows * 4 * sizeof(float)));
	memset(zero_float3_rows.ptrw(), 0, zero_float3_rows.size());
	h.b_x = rd->storage_buffer_create(zero_float3_rows.size(), zero_float3_rows);
	h.b_r = rd->storage_buffer_create(zero_float3_rows.size(), zero_float3_rows);
	h.b_z = rd->storage_buffer_create(zero_float3_rows.size(), zero_float3_rows);
	h.b_p = rd->storage_buffer_create(zero_float3_rows.size(), zero_float3_rows);
	h.b_ap = rd->storage_buffer_create(zero_float3_rows.size(), zero_float3_rows);

	Vector<uint8_t> zero_scalars;
	zero_scalars.resize(10 * sizeof(float));
	memset(zero_scalars.ptrw(), 0, zero_scalars.size());
	h.b_scalars = rd->storage_buffer_create(zero_scalars.size(), zero_scalars);

	if (h.b_params.is_null() || h.b_row_ptr.is_null() || h.b_col_idx.is_null() ||
			h.b_values.is_null() || h.b_diag_inv.is_null() || h.b_rhs.is_null() ||
			h.b_x.is_null() || h.b_r.is_null() || h.b_z.is_null() ||
			h.b_p.is_null() || h.b_ap.is_null() || h.b_scalars.is_null()) {
		free_cg3_state(h);
		return CgPcg3Handle();
	}

	// cg_pcg3 bind masks match cg_pcg's exactly — the entry-name → binding
	// reference mapping is identical between the scalar and float3
	// variants. Only the underlying buffer SIZES differ.
	static constexpr uint32_t bind_mask_init = 0b0000'0011'1111'1111u;
	static constexpr uint32_t bind_mask_spmv_p_to_ap = 0b0000'0110'0000'1111u;
	static constexpr uint32_t bind_mask_dot_p_ap = 0b0000'1110'0000'0001u;
	static constexpr uint32_t bind_mask_alpha_update = 0b0000'1000'0000'0000u;
	static constexpr uint32_t bind_mask_x_axpy_p = 0b0000'1010'0100'0001u;
	static constexpr uint32_t bind_mask_r_axpy_neg_ap = 0b0000'1100'1000'0001u;
	static constexpr uint32_t bind_mask_jacobi_z = 0b0000'0001'1001'0001u;
	static constexpr uint32_t bind_mask_dot_r_z = 0b0000'1001'1000'0001u;
	static constexpr uint32_t bind_mask_beta_update = 0b0000'1000'0000'0000u;
	static constexpr uint32_t bind_mask_p_update = 0b0000'1011'0000'0001u;
	static constexpr uint32_t bind_mask_check_residual = 0b0000'1000'1000'0001u;
	const uint32_t bind_masks[11] = {
		bind_mask_init,
		bind_mask_spmv_p_to_ap,
		bind_mask_dot_p_ap,
		bind_mask_alpha_update,
		bind_mask_x_axpy_p,
		bind_mask_r_axpy_neg_ap,
		bind_mask_jacobi_z,
		bind_mask_dot_r_z,
		bind_mask_beta_update,
		bind_mask_p_update,
		bind_mask_check_residual,
	};
	const RID bind_rids[12] = {
		h.b_params,
		h.b_row_ptr,
		h.b_col_idx,
		h.b_values,
		h.b_diag_inv,
		h.b_rhs,
		h.b_x,
		h.b_r,
		h.b_z,
		h.b_p,
		h.b_ap,
		h.b_scalars,
	};
	for (int k = 0; k < 11; ++k) {
		Vector<RenderingDevice::Uniform> uniforms;
		const uint32_t mask = bind_masks[k];
		for (int b = 0; b < 12; ++b) {
			if (!(mask & (1u << b))) {
				continue;
			}
			RenderingDevice::Uniform u;
			u.uniform_type = (b == 0)
					? RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER
					: RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
			u.binding = uint32_t(b);
			u.append_id(bind_rids[b]);
			uniforms.push_back(u);
		}
		const int kernel_id = KERNEL_CG3_INIT + k;
		h.uniform_sets[k] = rd->uniform_set_create(uniforms, shader[kernel_id], 0);
		if (h.uniform_sets[k].is_null()) {
			ERR_PRINT("cassie_slang_gpu: uniform_set_create failed for cg3 kernel " + itos(k));
			free_cg3_state(h);
			return CgPcg3Handle();
		}
	}

	h.rows = p_rows;
	h.nnz = p_nnz;
	return h;
}

void CassieSlangGpu::free_cg3_state(CgPcg3Handle &r_handle) {
	if (rd == nullptr) {
		return;
	}
	for (int i = 0; i < 11; ++i) {
		if (r_handle.uniform_sets[i].is_valid()) {
			rd->free_rid(r_handle.uniform_sets[i]);
		}
	}
	const RID rids[] = {
		r_handle.b_scalars,
		r_handle.b_ap,
		r_handle.b_p,
		r_handle.b_z,
		r_handle.b_r,
		r_handle.b_x,
		r_handle.b_rhs,
		r_handle.b_diag_inv,
		r_handle.b_values,
		r_handle.b_col_idx,
		r_handle.b_row_ptr,
		r_handle.b_params,
	};
	for (RID rid : rids) {
		if (rid.is_valid()) {
			rd->free_rid(rid);
		}
	}
	r_handle = CgPcg3Handle();
}

// Helper used by both jacobi3 and mas3 solve paths.
// Uploads initial x (float3 packed to float[4]), zeroes the residual
// scalar, then dispatches the CG3 init entry. The caller drives the
// per-iter loop afterwards.
namespace {

void _upload_packed_float3(RenderingDevice *rd, RID buffer,
		const float *p_xyz, int rows) {
	Vector<uint8_t> packed;
	packed.resize(int(rows * 4 * sizeof(float)));
	float *dst = reinterpret_cast<float *>(packed.ptrw());
	for (int i = 0; i < rows; ++i) {
		dst[i * 4 + 0] = p_xyz[i * 3 + 0];
		dst[i * 4 + 1] = p_xyz[i * 3 + 1];
		dst[i * 4 + 2] = p_xyz[i * 3 + 2];
		dst[i * 4 + 3] = 0.0f;
	}
	rd->buffer_update(buffer, 0, packed.size(), packed.ptr());
}

void _download_packed_float3(RenderingDevice *rd, RID buffer,
		float *r_xyz, int rows) {
	Vector<uint8_t> back = rd->buffer_get_data(buffer);
	const float *src = reinterpret_cast<const float *>(back.ptr());
	for (int i = 0; i < rows; ++i) {
		r_xyz[i * 3 + 0] = src[i * 4 + 0];
		r_xyz[i * 3 + 1] = src[i * 4 + 1];
		r_xyz[i * 3 + 2] = src[i * 4 + 2];
	}
}

} // namespace

void CassieSlangGpu::update_cg3_rhs(const CgPcg3Handle &p_handle,
		const float *p_new_rhs_float3) {
	if (rd == nullptr || !p_handle.is_valid()) {
		return;
	}
	_upload_packed_float3(rd, p_handle.b_rhs, p_new_rhs_float3, p_handle.rows);
}

bool CassieSlangGpu::solve_sparse_gpu_jacobi3(const CgPcg3Handle &p_handle,
		const float *p_x_initial, int p_max_iter, float *r_x_out,
		float *r_residual) {
	if (rd == nullptr || !p_handle.is_valid() || p_max_iter < 1) {
		return false;
	}
	const int rows = p_handle.rows;
	_upload_packed_float3(rd, p_handle.b_x, p_x_initial, rows);

	// Reset rz_old [0..1] and the end-of-solve residual [8..9]. Without
	// the [0..1] zero, beta_update's seed dispatch after INIT computes a
	// garbage beta from stale data — harmless because we overwrite beta
	// at the end of iter 0 before p_update reads it, but cleaner to
	// start from a known state.
	const float zero_pair[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	rd->buffer_update(p_handle.b_scalars, 0,
			2 * sizeof(float), zero_pair);
	rd->buffer_update(p_handle.b_scalars, uint32_t(8 * sizeof(float)),
			2 * sizeof(float), zero_pair);

	const uint32_t per_row_groups = uint32_t((rows + 255) / 256);
	const uint32_t dot_groups = 1;

	RenderingDevice::ComputeListID cl = rd->compute_list_begin();
	auto cg3_dispatch = [&](int kernel_id, uint32_t groups) {
		const int ub_idx = kernel_id - KERNEL_CG3_INIT;
		rd->compute_list_bind_compute_pipeline(cl, pipeline[kernel_id]);
		rd->compute_list_bind_uniform_set(cl, p_handle.uniform_sets[ub_idx], 0);
		rd->compute_list_dispatch(cl, groups, 1, 1);
	};

	cg3_dispatch(KERNEL_CG3_INIT, per_row_groups);
	rd->compute_list_add_barrier(cl);
	// Seed scalars[0..1] = rz_init so iter 0's alpha_update can divide
	// by a non-zero pAp into a non-zero rz. Without this, iter 0 ran
	// with alpha=0 and x didn't move from the warm start — the bug
	// that hid when x_init was zero (subsequent iters drove the solve
	// regardless) but surfaced as a frozen output when x_init was the
	// rest fixed point. dot_r_z writes [6..7]; beta_update rolls it
	// into [0..1] (and writes a garbage beta to [6] that gets
	// overwritten before iter 0's p_update reads it).
	cg3_dispatch(KERNEL_CG3_DOT_R_Z, dot_groups);
	rd->compute_list_add_barrier(cl);
	cg3_dispatch(KERNEL_CG3_BETA_UPDATE, 1);
	rd->compute_list_add_barrier(cl);

	for (int iter = 0; iter < p_max_iter; ++iter) {
		cg3_dispatch(KERNEL_CG3_SPMV_P_TO_AP, per_row_groups);
		rd->compute_list_add_barrier(cl);
		cg3_dispatch(KERNEL_CG3_DOT_P_AP, dot_groups);
		rd->compute_list_add_barrier(cl);
		cg3_dispatch(KERNEL_CG3_ALPHA_UPDATE, 1);
		rd->compute_list_add_barrier(cl);
		cg3_dispatch(KERNEL_CG3_X_AXPY_P, per_row_groups);
		cg3_dispatch(KERNEL_CG3_R_AXPY_NEG_AP, per_row_groups);
		rd->compute_list_add_barrier(cl);
		cg3_dispatch(KERNEL_CG3_JACOBI_Z, per_row_groups);
		rd->compute_list_add_barrier(cl);
		cg3_dispatch(KERNEL_CG3_DOT_R_Z, dot_groups);
		rd->compute_list_add_barrier(cl);
		cg3_dispatch(KERNEL_CG3_BETA_UPDATE, 1);
		rd->compute_list_add_barrier(cl);
		cg3_dispatch(KERNEL_CG3_P_UPDATE, per_row_groups);
		rd->compute_list_add_barrier(cl);
	}
	cg3_dispatch(KERNEL_CG3_CHECK_RESIDUAL, dot_groups);
	rd->compute_list_add_barrier(cl);

	rd->compute_list_end();
	rd->submit();
	rd->sync();

	_download_packed_float3(rd, p_handle.b_x, r_x_out, rows);
	if (r_residual) {
		Vector<uint8_t> scalars_back = rd->buffer_get_data(p_handle.b_scalars);
		const float *sc = reinterpret_cast<const float *>(scalars_back.ptr());
		*r_residual = sc[9];
	}
	return true;
}

bool CassieSlangGpu::solve_sparse_gpu_mas3(const CgPcg3Handle &p_handle,
		const float *p_x_initial, int p_max_iter,
		MasApplyFn p_mas_apply, void *p_user_data,
		float *r_x_out, float *r_residual) {
	if (rd == nullptr || !p_handle.is_valid() || p_max_iter < 1 ||
			p_mas_apply == nullptr) {
		return false;
	}
	const int rows = p_handle.rows;
	_upload_packed_float3(rd, p_handle.b_x, p_x_initial, rows);

	const float zero_pair[2] = { 0.0f, 0.0f };
	rd->buffer_update(p_handle.b_scalars, uint32_t(8 * sizeof(float)),
			sizeof(zero_pair), zero_pair);

	const uint32_t per_row_groups = uint32_t((rows + 255) / 256);
	const uint32_t dot_groups = 1;

	auto cg3_dispatch_one = [&](int kernel_id, uint32_t groups) {
		const int ub_idx = kernel_id - KERNEL_CG3_INIT;
		RenderingDevice::ComputeListID cl = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(cl, pipeline[kernel_id]);
		rd->compute_list_bind_uniform_set(cl, p_handle.uniform_sets[ub_idx], 0);
		rd->compute_list_dispatch(cl, groups, 1, 1);
		rd->compute_list_end();
		rd->submit();
		rd->sync();
	};
	auto cg3_chunk = [&](const std::initializer_list<std::pair<int, uint32_t>> ops) {
		RenderingDevice::ComputeListID cl = rd->compute_list_begin();
		bool first = true;
		for (const auto &op : ops) {
			if (!first) {
				rd->compute_list_add_barrier(cl);
			}
			first = false;
			const int ub_idx = op.first - KERNEL_CG3_INIT;
			rd->compute_list_bind_compute_pipeline(cl, pipeline[op.first]);
			rd->compute_list_bind_uniform_set(cl, p_handle.uniform_sets[ub_idx], 0);
			rd->compute_list_dispatch(cl, op.second, 1, 1);
		}
		rd->compute_list_end();
		rd->submit();
		rd->sync();
	};

	// init: r = b - A·x_initial; z = M⁻¹r via jacobi (we'll overwrite z
	// via the MAS apply); p = z. The init kernel computes z with the
	// scalar Jacobi diag_inv; we replace z immediately after via MAS.
	cg3_chunk({ { KERNEL_CG3_INIT, per_row_groups } });

	// Apply MAS to obtain proper z from r at iter 0. Download r → CPU
	// → apply MAS → upload z. NOT efficient; the bridge across two
	// separate RenderingDevice instances (cassie_slang_gpu and
	// cassie_mas_gpu each create their own local RD) requires CPU
	// roundtrip per MAS apply. The bench measures this honestly —
	// production deform integration (Row 12) shares one RD instance.
	LocalVector<float> r_host;
	r_host.resize(rows * 3);
	LocalVector<float> z_host;
	z_host.resize(rows * 3);
	_download_packed_float3(rd, p_handle.b_r, r_host.ptr(), rows);
	if (!p_mas_apply(p_user_data, r_host.ptr(), z_host.ptr(), rows * 3)) {
		return false;
	}
	_upload_packed_float3(rd, p_handle.b_z, z_host.ptr(), rows);
	// p = z (re-run init's z assignment, but for p only — saxpby with
	// alpha=1, beta=0 would be cleaner; simpler here to re-upload as
	// the same packed bytes).
	_upload_packed_float3(rd, p_handle.b_p, z_host.ptr(), rows);
	// rz_old = r · z. Run dot_r_z to populate scalars[6..7], then
	// roll into scalars[0..1] via beta_update — but rz_old is needed
	// as scalars[0..1] BEFORE the first iter. Just dispatch dot_r_z
	// targeting [0..1] by patching scalars after read.
	cg3_chunk({ { KERNEL_CG3_DOT_R_Z, dot_groups } });
	// dot_r_z writes scalars[6..7]; copy to scalars[0..1] via download/upload.
	Vector<uint8_t> sc = rd->buffer_get_data(p_handle.b_scalars);
	const float *sc_r = reinterpret_cast<const float *>(sc.ptr());
	const float rz_init[2] = { sc_r[6], sc_r[7] };
	rd->buffer_update(p_handle.b_scalars, 0, sizeof(rz_init), rz_init);

	for (int iter = 0; iter < p_max_iter; ++iter) {
		cg3_chunk({
				{ KERNEL_CG3_SPMV_P_TO_AP, per_row_groups },
				{ KERNEL_CG3_DOT_P_AP, dot_groups },
				{ KERNEL_CG3_ALPHA_UPDATE, 1 },
				{ KERNEL_CG3_X_AXPY_P, per_row_groups },
				{ KERNEL_CG3_R_AXPY_NEG_AP, per_row_groups },
		});
		// MAS apply: z = M_MAS⁻¹ r. CPU roundtrip per iter — see note
		// above. The bench captures the real cost.
		_download_packed_float3(rd, p_handle.b_r, r_host.ptr(), rows);
		if (!p_mas_apply(p_user_data, r_host.ptr(), z_host.ptr(), rows * 3)) {
			return false;
		}
		_upload_packed_float3(rd, p_handle.b_z, z_host.ptr(), rows);

		cg3_chunk({
				{ KERNEL_CG3_DOT_R_Z, dot_groups },
				{ KERNEL_CG3_BETA_UPDATE, 1 },
				{ KERNEL_CG3_P_UPDATE, per_row_groups },
		});
	}
	cg3_dispatch_one(KERNEL_CG3_CHECK_RESIDUAL, dot_groups);

	_download_packed_float3(rd, p_handle.b_x, r_x_out, rows);
	if (r_residual) {
		Vector<uint8_t> scalars_back = rd->buffer_get_data(p_handle.b_scalars);
		const float *sc2 = reinterpret_cast<const float *>(scalars_back.ptr());
		*r_residual = sc2[9];
	}
	return true;
}

// Shared-RD MAS3 solve — single compute list, no host roundtrip per
// iter. The MAS apply callback records its own GPU buffer_copy and
// dispatch commands on the SAME compute list as the cg outer loop.
bool CassieSlangGpu::solve_sparse_gpu_mas3_shared(
		const CgPcg3Handle &p_handle, const float *p_x_initial,
		int p_max_iter, MasApplyOnListFn p_mas_apply, void *p_user_data,
		float *r_x_out, float *r_residual) {
	if (rd == nullptr || !p_handle.is_valid() || p_max_iter < 1 ||
			p_mas_apply == nullptr) {
		return false;
	}
	const int rows = p_handle.rows;
	_upload_packed_float3(rd, p_handle.b_x, p_x_initial, rows);

	const float zero_pair[2] = { 0.0f, 0.0f };
	rd->buffer_update(p_handle.b_scalars, uint32_t(8 * sizeof(float)),
			sizeof(zero_pair), zero_pair);

	const uint32_t per_row_groups = uint32_t((rows + 255) / 256);
	const uint32_t dot_groups = 1;

	auto bind_cg3 = [&](RenderingDevice::ComputeListID cl, int kernel_id,
							uint32_t groups) {
		const int ub_idx = kernel_id - KERNEL_CG3_INIT;
		rd->compute_list_bind_compute_pipeline(cl, pipeline[kernel_id]);
		rd->compute_list_bind_uniform_set(cl, p_handle.uniform_sets[ub_idx], 0);
		rd->compute_list_dispatch(cl, groups, 1, 1);
	};

	// SETUP PHASE — one short compute list to: init r/z/p, apply MAS
	// to get the proper MAS-flavored z, compute initial rz via dot_r_z.
	// Submit + sync once so the small buffer_copys below can run.
	{
		RenderingDevice::ComputeListID cl = rd->compute_list_begin();
		bind_cg3(cl, KERNEL_CG3_INIT, per_row_groups);
		rd->compute_list_add_barrier(cl);
		if (!p_mas_apply(p_user_data, p_handle.b_r, p_handle.b_z, cl)) {
			rd->compute_list_end();
			return false;
		}
		bind_cg3(cl, KERNEL_CG3_DOT_R_Z, dot_groups);
		rd->compute_list_end();
		rd->submit();
		rd->sync();
	}
	// Outside any compute list — buffer_copy is allowed now.
	// 1) p ← z (so the first iter's spmv operates on the proper MAS
	//    search direction).
	rd->buffer_copy(p_handle.b_z, p_handle.b_p, 0, 0,
			uint32_t(rows * 4 * sizeof(float)));
	// 2) scalars[0..1] ← scalars[6..7] (rz_old = r · z just computed).
	rd->buffer_copy(p_handle.b_scalars, p_handle.b_scalars,
			uint32_t(6 * sizeof(float)), 0, uint32_t(2 * sizeof(float)));

	// MAIN PHASE — single compute list for the entire iter loop +
	// end-of-solve check.
	RenderingDevice::ComputeListID cl = rd->compute_list_begin();
	auto cg3 = [&](int kernel_id, uint32_t groups) {
		bind_cg3(cl, kernel_id, groups);
	};

	for (int iter = 0; iter < p_max_iter; ++iter) {
		cg3(KERNEL_CG3_SPMV_P_TO_AP, per_row_groups);
		rd->compute_list_add_barrier(cl);
		cg3(KERNEL_CG3_DOT_P_AP, dot_groups);
		rd->compute_list_add_barrier(cl);
		cg3(KERNEL_CG3_ALPHA_UPDATE, 1);
		rd->compute_list_add_barrier(cl);
		cg3(KERNEL_CG3_X_AXPY_P, per_row_groups);
		cg3(KERNEL_CG3_R_AXPY_NEG_AP, per_row_groups);
		rd->compute_list_add_barrier(cl);
		// MAS apply on the updated r, producing new z, all on this list.
		if (!p_mas_apply(p_user_data, p_handle.b_r, p_handle.b_z, cl)) {
			rd->compute_list_end();
			return false;
		}
		cg3(KERNEL_CG3_DOT_R_Z, dot_groups);
		rd->compute_list_add_barrier(cl);
		cg3(KERNEL_CG3_BETA_UPDATE, 1);
		rd->compute_list_add_barrier(cl);
		cg3(KERNEL_CG3_P_UPDATE, per_row_groups);
		rd->compute_list_add_barrier(cl);
	}
	cg3(KERNEL_CG3_CHECK_RESIDUAL, dot_groups);
	rd->compute_list_add_barrier(cl);

	rd->compute_list_end();
	rd->submit();
	rd->sync();

	_download_packed_float3(rd, p_handle.b_x, r_x_out, rows);
	if (r_residual) {
		Vector<uint8_t> sc = rd->buffer_get_data(p_handle.b_scalars);
		const float *scp = reinterpret_cast<const float *>(sc.ptr());
		*r_residual = scp[9];
	}
	return true;
}

} // namespace cassie_slang_gpu

/**************************************************************************/
/*  cassie_mas_gpu.cpp                                                    */
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

#include "cassie_mas_gpu.h"

#include "core/math/aabb.h"
#include "core/string/ustring.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"

#include <algorithm>
#include <cstring>

// Per-entry SPIR-V byte arrays. Generated at SCons build time by
// modules/cassie/spv_to_header.py from
// modules/cassie/thirdparty/avbd/mas_precond.<entry>.spv. The byte
// arrays live in namespace cassie_slang_spirv (shared with the
// cg_pcg ubershader's .gen.h files).
#include "../../../thirdparty/avbd/mas_precond.mas_aggregation.spv.gen.h"
#include "../../../thirdparty/avbd/mas_precond.mas_assemble_submatrix.spv.gen.h"
#include "../../../thirdparty/avbd/mas_precond.mas_build_connect_mask.spv.gen.h"
#include "../../../thirdparty/avbd/mas_precond.mas_coarsen_residual.spv.gen.h"
#include "../../../thirdparty/avbd/mas_precond.mas_gj_invert.spv.gen.h"
#include "../../../thirdparty/avbd/mas_precond.mas_identity_copy_l0.spv.gen.h"
#include "../../../thirdparty/avbd/mas_precond.mas_morton_compute.spv.gen.h"
#include "../../../thirdparty/avbd/mas_precond.mas_per_domain_solve.spv.gen.h"
#include "../../../thirdparty/avbd/mas_precond.mas_sum_levels.spv.gen.h"

namespace cassie_mas_gpu {

namespace {

Vector<uint8_t> _bytes_view(const uint8_t *p_data, size_t p_size) {
	Vector<uint8_t> v;
	v.resize(int(p_size));
	memcpy(v.ptrw(), p_data, p_size);
	return v;
}

Vector<uint8_t> _bytes_from(const void *p_src, int p_byte_count) {
	Vector<uint8_t> v;
	v.resize(MAX(p_byte_count, 4));
	if (p_byte_count > 0) {
		memcpy(v.ptrw(), p_src, p_byte_count);
	} else {
		memset(v.ptrw(), 0, 4);
	}
	return v;
}

Vector<uint8_t> _zeros(int p_byte_count) {
	Vector<uint8_t> v;
	v.resize(MAX(p_byte_count, 4));
	memset(v.ptrw(), 0, v.size());
	return v;
}

// 20-bit Morton-code expansion: spread an integer's low 20 bits across
// 60 output positions with two zeros between each input bit. Matches
// the in-kernel implementation in mas_morton_compute so the CPU and
// GPU hierarchy build produce the same ordering.
uint32_t _expand_bits20(uint32_t v) {
	v &= 0x000FFFFF;
	v = (v | (v << 16)) & 0x030000FF;
	v = (v | (v << 8)) & 0x0300F00F;
	v = (v | (v << 4)) & 0x030C30C3;
	v = (v | (v << 2)) & 0x09249249;
	return v;
}

// Paper §5.1 — 60-bit Morton code from normalized AABB position.
uint64_t _morton60(const Vector3 &p, const Vector3 &aabb_min,
		const Vector3 &aabb_size) {
	auto axis = [](real_t v, real_t lo, real_t sz) -> uint32_t {
		real_t n = (v - lo) / sz * real_t(1048576.0); // 2^20
		if (n < 0) {
			n = 0;
		}
		if (n > 1048575) {
			n = 1048575;
		}
		return uint32_t(n);
	};
	const uint32_t ix = axis(p.x, aabb_min.x, aabb_size.x);
	const uint32_t iy = axis(p.y, aabb_min.y, aabb_size.y);
	const uint32_t iz = axis(p.z, aabb_min.z, aabb_size.z);
	const uint64_t x = _expand_bits20(ix);
	const uint64_t y = _expand_bits20(iy);
	const uint64_t z = _expand_bits20(iz);
	return x | (y << 1) | (z << 2);
}

// Bind one full uniform set covering bindings 0..18. slangc strips
// bindings an entry doesn't reference, so each pipeline's uniform set
// is its own RID — but the underlying buffer RIDs are the same.
// p_params_override lets the caller swap in a per-level / coarsen UBO
// at slot 0 without rebuilding the rest of the set. Pass RID() to use
// h.b_params (the bind-time chain's "globals only" UBO).
// p_r_override / p_z_override do the same for slots 11 / 12 — used by
// the external-uniform-set path (caller's r/z) and by the handle's own
// identity_copy_l0 / sum_levels sets (handle.b_r_input / b_z_output).
// Returns an invalid RID on uniform_set_create failure.
RID _build_uniform_set(RenderingDevice *rd, const MasGpuHandle &h,
		RID p_shader, RID p_params_override = RID(),
		RID p_r_override = RID(), RID p_z_override = RID()) {
	struct Slot {
		uint32_t binding;
		RID rid;
		bool is_uniform_buffer;
	};
	const Slot slots[] = {
		{ 0, p_params_override.is_valid() ? p_params_override : h.b_params,
				true },
		{ 1, h.b_row_ptr, false },
		{ 2, h.b_col_idx, false },
		{ 3, h.b_values, false },
		{ 4, h.b_morton, false },
		{ 5, h.b_sorted_idx, false },
		{ 6, h.b_map_per_level, false },
		{ 7, h.b_domain_offsets, false },
		{ 8, h.b_m_inv_packed, false },
		{ 9, h.b_r_per_level, false },
		{ 10, h.b_z_per_level, false },
		{ 11, p_r_override.is_valid() ? p_r_override : h.b_r_input, false },
		{ 12, p_z_override.is_valid() ? p_z_override : h.b_z_output, false },
		{ 13, h.b_coarse_offsets, false },
		{ 14, h.b_coarse_indices, false },
		{ 15, h.b_level_sizes, false },
		{ 16, h.b_positions, false },
		{ 17, h.b_connect_mask, false },
		{ 18, h.b_dense_workspace, false },
	};
	Vector<RenderingDevice::Uniform> uniforms;
	for (const Slot &s : slots) {
		RenderingDevice::Uniform u;
		u.uniform_type = s.is_uniform_buffer
				? RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER
				: RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u.binding = s.binding;
		u.append_id(s.rid);
		uniforms.push_back(u);
	}
	return rd->uniform_set_create(uniforms, p_shader, 0);
}

} // namespace

namespace {

// Load the 8 mas_precond pipelines into an already-set-up RD.
// Used by both constructors. Returns true on success; on failure the
// caller's _destroy will clean up any partially-loaded state.
bool _load_mas_pipelines(RenderingDevice *rd, RID shader[KERNEL_COUNT],
		RID pipeline[KERNEL_COUNT]);

} // namespace

CassieMasGpu::CassieMasGpu() {
	owns_rd = true;
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
	if (!_load_mas_pipelines(rd, shader, pipeline)) {
		_destroy();
	}
}

CassieMasGpu::CassieMasGpu(RenderingDevice *p_external_rd) {
	owns_rd = false;
	rd = p_external_rd;
	if (rd == nullptr) {
		return;
	}
	if (!_load_mas_pipelines(rd, shader, pipeline)) {
		// On failure free pipelines but don't memdelete the external rd.
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
		rd = nullptr;
	}
}

namespace {

bool _load_mas_pipelines(RenderingDevice *rd,
		RID shader[KERNEL_COUNT],
		RID pipeline[KERNEL_COUNT]) {
	// Load each kernel from its embedded byte array. Order MUST match
	// the enum Kernel { ... } in the header.
	struct KernelBlob {
		const uint8_t *data;
		size_t size;
		const char *name;
	};
	using namespace cassie_slang_spirv;
	const KernelBlob blobs[KERNEL_COUNT] = {
		{ mas_precond_mas_morton_compute_spv,
				mas_precond_mas_morton_compute_spv_size,
				"cassie_mas_gpu_morton_compute" },
		{ mas_precond_mas_build_connect_mask_spv,
				mas_precond_mas_build_connect_mask_spv_size,
				"cassie_mas_gpu_build_connect_mask" },
		{ mas_precond_mas_aggregation_spv,
				mas_precond_mas_aggregation_spv_size,
				"cassie_mas_gpu_aggregation" },
		{ mas_precond_mas_assemble_submatrix_spv,
				mas_precond_mas_assemble_submatrix_spv_size,
				"cassie_mas_gpu_assemble_submatrix" },
		{ mas_precond_mas_gj_invert_spv,
				mas_precond_mas_gj_invert_spv_size,
				"cassie_mas_gpu_gj_invert" },
		{ mas_precond_mas_identity_copy_l0_spv,
				mas_precond_mas_identity_copy_l0_spv_size,
				"cassie_mas_gpu_identity_copy_l0" },
		{ mas_precond_mas_coarsen_residual_spv,
				mas_precond_mas_coarsen_residual_spv_size,
				"cassie_mas_gpu_coarsen_residual" },
		{ mas_precond_mas_per_domain_solve_spv,
				mas_precond_mas_per_domain_solve_spv_size,
				"cassie_mas_gpu_per_domain_solve" },
		{ mas_precond_mas_sum_levels_spv,
				mas_precond_mas_sum_levels_spv_size,
				"cassie_mas_gpu_sum_levels" },
	};

	for (int i = 0; i < KERNEL_COUNT; ++i) {
		RenderingDevice::ShaderStageSPIRVData stage;
		stage.shader_stage = RenderingDevice::SHADER_STAGE_COMPUTE;
		stage.spirv = _bytes_view(blobs[i].data, blobs[i].size);

		Vector<RenderingDevice::ShaderStageSPIRVData> stages;
		stages.push_back(stage);

		shader[i] = rd->shader_create_from_spirv(stages, String(blobs[i].name));
		if (shader[i].is_null()) {
			ERR_PRINT("cassie_mas_gpu: shader_create_from_spirv failed for kernel " + itos(i));
			return false;
		}
		pipeline[i] = rd->compute_pipeline_create(shader[i]);
		if (pipeline[i].is_null()) {
			ERR_PRINT("cassie_mas_gpu: compute_pipeline_create failed for kernel " + itos(i));
			return false;
		}
	}
	return true;
}

} // namespace

CassieMasGpu::~CassieMasGpu() {
	_destroy();
}

void CassieMasGpu::_destroy() {
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
	if (owns_rd) {
		memdelete(rd);
	}
	rd = nullptr;
}

// --- Stubs for build / apply / destroy ---
//
// The remaining methods are scaffolded for follow-up commits in the
// MAS port's execution order:
//
// - build_mas_state: needs the CPU-side hierarchy build via DisjointSet
//   (mirrors the connected-components diagnostic at ENG-61) + per-level
//   buffer uploads + per-domain assemble + GJ dispatch. ~250 LOC.
//
// - apply_mas_gpu: 3-dispatch sequence (coarsen + per_domain_solve +
//   sum_levels) inside one compute list. ~80 LOC.
//
// - destroy_mas_state: free 19 buffer RIDs + 8 uniform set RIDs.
//   Mostly mechanical. ~30 LOC.

MasGpuHandle CassieMasGpu::build_mas_state(
		const cassie_pcg::CSRMatrix &p_L_II, const Vector3 *p_positions) {
	MasGpuHandle h;
	if (rd == nullptr || pipeline[KERNEL_MAS_GJ_INVERT].is_null() ||
			p_L_II.rows == 0) {
		return h;
	}
	const int sigma = 32;
	const int ni = p_L_II.rows;
	const int nnz = int(p_L_II.col_idx.size());

	// --- CPU phase: AABB + Morton + sort + simple hierarchy ---
	AABB box(p_positions[0], Vector3());
	for (int i = 1; i < ni; ++i) {
		box.expand_to(p_positions[i]);
	}
	if (box.size.x == 0) {
		box.size.x = real_t(1e-6);
	}
	if (box.size.y == 0) {
		box.size.y = real_t(1e-6);
	}
	if (box.size.z == 0) {
		box.size.z = real_t(1e-6);
	}

	struct MortonSlot {
		uint64_t code;
		int vert;
	};
	LocalVector<MortonSlot> slots;
	slots.resize(ni);
	for (int i = 0; i < ni; ++i) {
		slots[i].code = _morton60(p_positions[i], box.position, box.size);
		slots[i].vert = i;
	}
	std::sort(slots.ptr(), slots.ptr() + slots.size(),
			[](const MortonSlot &a, const MortonSlot &b) { return a.code < b.code; });

	// sorted_idx[slot] = original vertex; orig_to_slot[vert] = sorted slot.
	LocalVector<int32_t> sorted_idx;
	sorted_idx.resize(ni);
	LocalVector<int32_t> orig_to_slot;
	orig_to_slot.resize(ni);
	for (int i = 0; i < ni; ++i) {
		sorted_idx[i] = slots[i].vert;
		orig_to_slot[slots[i].vert] = i;
	}

	// Hierarchy build — simple σ-bucket (paper §5.2 base case; the
	// skipping-approach refinement is a follow-up). Per level l, each
	// sorted slot i belongs to supernode (i / σ); higher levels divide
	// further by σ. Number of levels chosen so the coarsest level has
	// ≤ σ supernodes.
	LocalVector<int32_t> level_sizes;
	level_sizes.push_back(ni);
	int cur = ni;
	while (cur > sigma) {
		cur = (cur + sigma - 1) / sigma;
		level_sizes.push_back(cur);
	}
	const int num_levels = int(level_sizes.size());

	// map_per_level — flat (L-1) × ni. For interior vertex i (in
	// original-vert order), entry at offset (l-1)*ni + i is the
	// parent supernode at level l (sorted slot of i at level 0,
	// then floor(slot / σ) at level 1, etc.).
	const int map_count = MAX(num_levels - 1, 0) * ni;
	LocalVector<int32_t> map_per_level;
	map_per_level.resize(MAX(map_count, 1));
	for (int l = 1; l < num_levels; ++l) {
		int32_t *dst = map_per_level.ptr() + (l - 1) * ni;
		for (int i = 0; i < ni; ++i) {
			// Drop down the sorted-slot path: level 1's parent is
			// floor(sorted_slot(i) / σ); level l's parent is the level
			// l-1 parent divided by σ again.
			int32_t bucket = (l == 1)
					? orig_to_slot[i]
					: map_per_level[(l - 2) * ni + i];
			bucket /= sigma;
			dst[i] = bucket;
		}
	}

	// Inverted coarse map: for each non-zero level supernode in flat
	// order (level 1's supernodes, then level 2's, etc.), record the
	// member fine-vertex indices in coarse_indices, with start/end
	// offsets in coarse_offsets. Flat over levels.
	int total_coarse = 0;
	for (int l = 1; l < num_levels; ++l) {
		total_coarse += level_sizes[l];
	}

	LocalVector<uint32_t> coarse_offsets;
	coarse_offsets.resize(total_coarse + 1);
	LocalVector<int32_t> coarse_indices;
	coarse_indices.resize(MAX((num_levels - 1) * ni, 1));

	int flat_s = 0;
	int member_cursor = 0;
	for (int l = 1; l < num_levels; ++l) {
		const int N_l = level_sizes[l];
		// Count members per supernode at this level.
		LocalVector<int> counts;
		counts.resize(N_l);
		for (int s = 0; s < N_l; ++s) {
			counts[s] = 0;
		}
		const int32_t *map_l = map_per_level.ptr() + (l - 1) * ni;
		for (int i = 0; i < ni; ++i) {
			const int32_t s = map_l[i];
			if (s >= 0 && s < N_l) {
				counts[s]++;
			}
		}
		// Build offsets prefix-sum from member_cursor.
		LocalVector<int> level_starts;
		level_starts.resize(N_l + 1);
		level_starts[0] = member_cursor;
		for (int s = 0; s < N_l; ++s) {
			level_starts[s + 1] = level_starts[s] + counts[s];
		}
		// Record per-supernode offsets in flat coarse_offsets.
		for (int s = 0; s < N_l; ++s) {
			coarse_offsets[flat_s + s] = uint32_t(level_starts[s]);
		}
		// Reset cursors for fill phase.
		LocalVector<int> cursors;
		cursors.resize(N_l);
		for (int s = 0; s < N_l; ++s) {
			cursors[s] = level_starts[s];
		}
		for (int i = 0; i < ni; ++i) {
			const int32_t s = map_l[i];
			if (s >= 0 && s < N_l) {
				coarse_indices[cursors[s]++] = i;
			}
		}
		flat_s += N_l;
		member_cursor = level_starts[N_l];
	}
	coarse_offsets[total_coarse] = uint32_t(member_cursor);

	// Per-domain triangular packed offsets. At each level l, domains
	// span ceil(N_l / σ) workgroups; each holds σ(σ+1)/2 floats.
	int total_domains = 0;
	for (int l = 0; l < num_levels; ++l) {
		total_domains += (level_sizes[l] + sigma - 1) / sigma;
	}
	LocalVector<uint32_t> domain_offsets;
	domain_offsets.resize(total_domains + 1);
	{
		uint32_t off = 0;
		int d = 0;
		const int tri_per_domain = sigma * (sigma + 1) / 2;
		for (int l = 0; l < num_levels; ++l) {
			const int dom_l = (level_sizes[l] + sigma - 1) / sigma;
			for (int i = 0; i < dom_l; ++i) {
				domain_offsets[d++] = off;
				off += uint32_t(tri_per_domain);
			}
		}
		domain_offsets[d] = off;
	}
	const int m_inv_packed_floats = int(domain_offsets[total_domains]);

	// Total per-level flat size for r_per_level / z_per_level (float3
	// per entry). Level 0 = ni; subsequent levels = N_l.
	int per_level_total = 0;
	for (int l = 0; l < num_levels; ++l) {
		per_level_total += level_sizes[l];
	}

	// --- GPU phase: upload all buffers ---
	// Layout MUST match MasPreconditioner.lean's paramsStruct field order
	// (Slang std140 packs scalars at 4-byte stride; struct end pads to 16).
	struct alignas(16) MasParams {
		uint32_t ni;
		uint32_t num_levels;
		uint32_t domain_size;
		float aabb_min_x, aabb_min_y, aabb_min_z;
		float aabb_size_x, aabb_size_y, aabb_size_z;
		uint32_t level;
		// Per-dispatch offsets for the apply chain (§7 multi-level).
		uint32_t level_r_offset;
		uint32_t level_z_offset;
		uint32_t level_domain_offset;
		uint32_t level_ni;
		uint32_t total_coarse_supernodes;
		uint32_t pad = 0; // pad to 64 B (16 × uint)
	} params;
	params.ni = uint32_t(ni);
	params.num_levels = uint32_t(num_levels);
	params.domain_size = uint32_t(sigma);
	params.aabb_min_x = box.position.x;
	params.aabb_min_y = box.position.y;
	params.aabb_min_z = box.position.z;
	params.aabb_size_x = box.size.x;
	params.aabb_size_y = box.size.y;
	params.aabb_size_z = box.size.z;
	params.level = 0;
	// The "global" params UBO has the apply-chain offsets zeroed; only
	// the per-level and coarsen UBOs (built below) carry non-zero values.
	params.level_r_offset = 0;
	params.level_z_offset = 0;
	params.level_domain_offset = 0;
	params.level_ni = uint32_t(ni);
	params.total_coarse_supernodes = 0;
	h.b_params = rd->uniform_buffer_create(int(sizeof(MasParams)),
			_bytes_from(&params, int(sizeof(MasParams))));

	// L_II CSR — convert from double values to float for the GPU.
	LocalVector<int32_t> row_ptr_i;
	row_ptr_i.resize(ni + 1);
	for (int i = 0; i <= ni; ++i) {
		row_ptr_i[i] = int32_t(p_L_II.row_ptr[i]);
	}
	LocalVector<int32_t> col_idx_i;
	col_idx_i.resize(nnz);
	LocalVector<float> values_f;
	values_f.resize(nnz);
	for (int k = 0; k < nnz; ++k) {
		col_idx_i[k] = int32_t(p_L_II.col_idx[k]);
		values_f[k] = float(p_L_II.val[k]);
	}
	h.b_row_ptr = rd->storage_buffer_create(int((ni + 1) * sizeof(int32_t)),
			_bytes_from(row_ptr_i.ptr(), int((ni + 1) * sizeof(int32_t))));
	h.b_col_idx = rd->storage_buffer_create(int(nnz * sizeof(int32_t)),
			_bytes_from(col_idx_i.ptr(), int(nnz * sizeof(int32_t))));
	h.b_values = rd->storage_buffer_create(int(nnz * sizeof(float)),
			_bytes_from(values_f.ptr(), int(nnz * sizeof(float))));

	// Float3 positions — pack as 4 floats per entry (Slang's float3 in
	// a StructuredBuffer is 16-byte aligned).
	LocalVector<float> pos_f;
	pos_f.resize(ni * 4);
	for (int i = 0; i < ni; ++i) {
		pos_f[i * 4 + 0] = float(p_positions[i].x);
		pos_f[i * 4 + 1] = float(p_positions[i].y);
		pos_f[i * 4 + 2] = float(p_positions[i].z);
		pos_f[i * 4 + 3] = 0.0f;
	}
	h.b_positions = rd->storage_buffer_create(int(ni * 4 * sizeof(float)),
			_bytes_from(pos_f.ptr(), int(ni * 4 * sizeof(float))));

	h.b_morton = rd->storage_buffer_create(int(ni * 2 * sizeof(uint32_t)),
			_zeros(int(ni * 2 * sizeof(uint32_t))));
	h.b_sorted_idx = rd->storage_buffer_create(int(ni * sizeof(int32_t)),
			_bytes_from(sorted_idx.ptr(), int(ni * sizeof(int32_t))));
	h.b_map_per_level = rd->storage_buffer_create(
			int(MAX(map_count, 1) * sizeof(int32_t)),
			_bytes_from(map_per_level.ptr(), int(MAX(map_count, 1) * sizeof(int32_t))));
	h.b_domain_offsets = rd->storage_buffer_create(
			int((total_domains + 1) * sizeof(uint32_t)),
			_bytes_from(domain_offsets.ptr(), int((total_domains + 1) * sizeof(uint32_t))));

	h.b_m_inv_packed = rd->storage_buffer_create(
			int(MAX(m_inv_packed_floats, 1) * sizeof(float)),
			_zeros(int(MAX(m_inv_packed_floats, 1) * sizeof(float))));
	h.b_r_per_level = rd->storage_buffer_create(
			int(per_level_total * 4 * sizeof(float)),
			_zeros(int(per_level_total * 4 * sizeof(float))));
	h.b_z_per_level = rd->storage_buffer_create(
			int(per_level_total * 4 * sizeof(float)),
			_zeros(int(per_level_total * 4 * sizeof(float))));
	h.b_r_input = rd->storage_buffer_create(int(ni * 4 * sizeof(float)),
			_zeros(int(ni * 4 * sizeof(float))));
	h.b_z_output = rd->storage_buffer_create(int(ni * 4 * sizeof(float)),
			_zeros(int(ni * 4 * sizeof(float))));

	h.b_coarse_offsets = rd->storage_buffer_create(
			int((total_coarse + 1) * sizeof(uint32_t)),
			_bytes_from(coarse_offsets.ptr(), int((total_coarse + 1) * sizeof(uint32_t))));
	h.b_coarse_indices = rd->storage_buffer_create(
			int(MAX((num_levels - 1) * ni, 1) * sizeof(int32_t)),
			_bytes_from(coarse_indices.ptr(),
					int(MAX((num_levels - 1) * ni, 1) * sizeof(int32_t))));

	LocalVector<uint32_t> ls_u32;
	ls_u32.resize(num_levels);
	for (int l = 0; l < num_levels; ++l) {
		ls_u32[l] = uint32_t(level_sizes[l]);
	}
	h.b_level_sizes = rd->storage_buffer_create(int(num_levels * sizeof(uint32_t)),
			_bytes_from(ls_u32.ptr(), int(num_levels * sizeof(uint32_t))));

	h.b_connect_mask = rd->storage_buffer_create(
			int(MAX(total_domains, 1) * sizeof(uint32_t)),
			_zeros(int(MAX(total_domains, 1) * sizeof(uint32_t))));
	h.b_dense_workspace = rd->storage_buffer_create(
			int(MAX(total_domains * sigma * sigma, 1) * sizeof(float)),
			_zeros(int(MAX(total_domains * sigma * sigma, 1) * sizeof(float))));

	// Per-level params UBOs: one per level for mas_per_domain_solve,
	// plus one with total_coarse_supernodes for mas_coarsen_residual.
	// Each UBO is the same struct as b_params; only the apply-chain
	// offset fields vary. Built once at bind time so apply_mas_gpu
	// can dispatch the whole multi-level chain in one compute list
	// without any inter-dispatch buffer_update.
	{
		// Per-level: precompute r/z/domain offsets via prefix sums.
		uint32_t r_off = 0;
		uint32_t z_off = 0;
		uint32_t dom_off = 0;
		h.b_params_per_level.resize(num_levels);
		for (int l = 0; l < num_levels; ++l) {
			MasParams pl = params;
			pl.level = uint32_t(l);
			pl.level_r_offset = r_off;
			pl.level_z_offset = z_off;
			pl.level_domain_offset = dom_off;
			pl.level_ni = uint32_t(level_sizes[l]);
			pl.total_coarse_supernodes = 0;
			h.b_params_per_level[l] = rd->uniform_buffer_create(
					int(sizeof(MasParams)),
					_bytes_from(&pl, int(sizeof(MasParams))));
			r_off += uint32_t(level_sizes[l]);
			z_off += uint32_t(level_sizes[l]);
			dom_off += uint32_t((level_sizes[l] + sigma - 1) / sigma);
		}

		MasParams pc = params;
		pc.total_coarse_supernodes = uint32_t(total_coarse);
		h.b_params_coarsen = rd->uniform_buffer_create(
				int(sizeof(MasParams)),
				_bytes_from(&pc, int(sizeof(MasParams))));
	}

	// Build 9 default uniform sets — one per pipeline (uses h.b_params).
	// The coarsen + per_domain_solve slots get overridden with their
	// dedicated params UBOs below; the assemble / GJ / morton / etc.
	// bind-time kernels keep h.b_params (globals-only).
	for (int i = 0; i < KERNEL_COUNT; ++i) {
		RID params_override;
		if (i == KERNEL_MAS_COARSEN_RESIDUAL) {
			params_override = h.b_params_coarsen;
		}
		h.uniform_sets[i] = _build_uniform_set(rd, h, shader[i],
				params_override);
		if (h.uniform_sets[i].is_null()) {
			destroy_mas_state(h);
			return MasGpuHandle();
		}
	}
	h.uniform_set_coarsen = h.uniform_sets[KERNEL_MAS_COARSEN_RESIDUAL];

	// Per-level uniform sets for mas_per_domain_solve: each binds
	// b_params_per_level[l] at slot 0. The kernel reads/writes
	// r_per_level / z_per_level at the level's offsets.
	h.uniform_sets_per_level_solve.resize(num_levels);
	for (int l = 0; l < num_levels; ++l) {
		h.uniform_sets_per_level_solve[l] = _build_uniform_set(rd, h,
				shader[KERNEL_MAS_PER_DOMAIN_SOLVE],
				h.b_params_per_level[l]);
		if (h.uniform_sets_per_level_solve[l].is_null()) {
			destroy_mas_state(h);
			return MasGpuHandle();
		}
	}

	h.ni = ni;
	h.nnz = nnz;
	h.num_levels = num_levels;
	h.domain_size = sigma;
	h.total_domains = total_domains;
	h.total_coarse_supernodes = total_coarse;
	h.level_sizes_cpu.resize(num_levels);
	for (int l = 0; l < num_levels; ++l) {
		h.level_sizes_cpu[l] = level_sizes[l];
	}

	// --- Dispatch the bind-time chain: assemble then GJ-invert per
	// domain. One compute list, one submit, one sync. ---
	RenderingDevice::ComputeListID cl = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(cl, pipeline[KERNEL_MAS_ASSEMBLE_SUBMATRIX]);
	rd->compute_list_bind_uniform_set(cl,
			h.uniform_sets[KERNEL_MAS_ASSEMBLE_SUBMATRIX], 0);
	rd->compute_list_dispatch(cl, uint32_t(total_domains), 1, 1);
	rd->compute_list_add_barrier(cl);
	rd->compute_list_bind_compute_pipeline(cl, pipeline[KERNEL_MAS_GJ_INVERT]);
	rd->compute_list_bind_uniform_set(cl,
			h.uniform_sets[KERNEL_MAS_GJ_INVERT], 0);
	rd->compute_list_dispatch(cl, uint32_t(total_domains), 1, 1);
	rd->compute_list_end();
	rd->submit();
	rd->sync();

	return h;
}

bool CassieMasGpu::apply_mas_gpu(const MasGpuHandle &p_handle,
		const float *p_r, float *r_z) {
	if (rd == nullptr || !p_handle.is_valid() || p_r == nullptr || r_z == nullptr) {
		return false;
	}
	const int ni = p_handle.ni;
	const int total_domains_l0 =
			(ni + p_handle.domain_size - 1) / p_handle.domain_size;

	// Pack the caller's float3-per-vert residual into the 16-byte aligned
	// float[4] layout that Slang's StructuredBuffer<float3> expects.
	LocalVector<float> r_packed;
	r_packed.resize(ni * 4);
	for (int i = 0; i < ni; ++i) {
		r_packed[i * 4 + 0] = p_r[i * 3 + 0];
		r_packed[i * 4 + 1] = p_r[i * 3 + 1];
		r_packed[i * 4 + 2] = p_r[i * 3 + 2];
		r_packed[i * 4 + 3] = 0.0f;
	}
	{
		const Vector<uint8_t> bytes = _bytes_from(r_packed.ptr(),
				int(ni * 4 * sizeof(float)));
		rd->buffer_update(p_handle.b_r_input, 0, bytes.size(), bytes.ptr());
	}

	// Multi-level apply chain (paper §7):
	//   identity_copy_l0 → coarsen_residual → per_domain_solve (×L)
	//     → sum_levels
	// All dispatched into one compute list with barriers; one
	// submit+sync per apply.
	(void)total_domains_l0; // computed inline per-level below
	const int sigma = p_handle.domain_size;
	const int L = p_handle.num_levels;
	const int total_coarse = p_handle.total_coarse_supernodes;

	RenderingDevice::ComputeListID cl = rd->compute_list_begin();

	// 1. identity_copy_l0: r_per_level[0..ni) ← b_r_input[0..ni)
	rd->compute_list_bind_compute_pipeline(cl,
			pipeline[KERNEL_MAS_IDENTITY_COPY_L0]);
	rd->compute_list_bind_uniform_set(cl,
			p_handle.uniform_sets[KERNEL_MAS_IDENTITY_COPY_L0], 0);
	rd->compute_list_dispatch(cl, uint32_t((ni + 255) / 256), 1, 1);
	rd->compute_list_add_barrier(cl);

	// 2. coarsen_residual: fill r_per_level[ni .. ni + total_coarse).
	if (total_coarse > 0) {
		rd->compute_list_bind_compute_pipeline(cl,
				pipeline[KERNEL_MAS_COARSEN_RESIDUAL]);
		rd->compute_list_bind_uniform_set(cl,
				p_handle.uniform_sets[KERNEL_MAS_COARSEN_RESIDUAL], 0);
		rd->compute_list_dispatch(cl,
				uint32_t((total_coarse + 255) / 256), 1, 1);
		rd->compute_list_add_barrier(cl);
	}

	// 3. per_domain_solve at each level. Reads r_per_level[level slice],
	//    writes z_per_level[level slice]. The per-level uniform sets
	//    carry the level offsets in their params UBOs.
	rd->compute_list_bind_compute_pipeline(cl,
			pipeline[KERNEL_MAS_PER_DOMAIN_SOLVE]);
	for (int l = 0; l < L; ++l) {
		const int Nl = p_handle.level_sizes_cpu[l];
		const int domains_l = (Nl + sigma - 1) / sigma;
		rd->compute_list_bind_uniform_set(cl,
				p_handle.uniform_sets_per_level_solve[l], 0);
		rd->compute_list_dispatch(cl, uint32_t(domains_l), 1, 1);
		rd->compute_list_add_barrier(cl);
	}

	// 4. sum_levels: z_output[i] = Σ_l z_per_level[level_off_l + map_l(i)]
	rd->compute_list_bind_compute_pipeline(cl,
			pipeline[KERNEL_MAS_SUM_LEVELS]);
	rd->compute_list_bind_uniform_set(cl,
			p_handle.uniform_sets[KERNEL_MAS_SUM_LEVELS], 0);
	rd->compute_list_dispatch(cl, uint32_t((ni + 255) / 256), 1, 1);

	rd->compute_list_end();
	rd->submit();
	rd->sync();

	// Download z_output into the caller's float3-per-vert buffer.
	const Vector<uint8_t> z_bytes = rd->buffer_get_data(p_handle.b_z_output);
	if (z_bytes.size() < int(ni * 4 * sizeof(float))) {
		return false;
	}
	const float *z_packed = reinterpret_cast<const float *>(z_bytes.ptr());
	for (int i = 0; i < ni; ++i) {
		r_z[i * 3 + 0] = z_packed[i * 4 + 0];
		r_z[i * 3 + 1] = z_packed[i * 4 + 1];
		r_z[i * 3 + 2] = z_packed[i * 4 + 2];
	}
	return true;
}

bool CassieMasGpu::apply_mas_to_compute_list(const MasGpuHandle &p_handle,
		RID p_external_r, RID p_external_z,
		RenderingDevice::ComputeListID p_cl) {
	// Build temp external sets for the two kernels that touch caller
	// buffers, then run the cached path. Frees the temp sets after
	// recording (Godot defers destruction until the GPU completes).
	ExternalApplySets temp = build_external_uniform_sets(p_handle,
			p_external_r, p_external_z);
	if (!temp.is_valid()) {
		return false;
	}
	const bool ok = apply_mas_to_compute_list_cached(p_handle, temp, p_cl);
	free_external_uniform_sets(temp);
	return ok;
}

CassieMasGpu::ExternalApplySets CassieMasGpu::build_external_uniform_sets(
		const MasGpuHandle &p_handle,
		RID p_external_r, RID p_external_z) {
	ExternalApplySets r;
	if (rd == nullptr || !p_handle.is_valid()) {
		return r;
	}
	// identity_copy_l0 reads slot 11 (external r), writes slot 9
	// (handle-internal r_per_level). Override slot 11 only.
	r.identity_copy_l0 = _build_uniform_set(rd, p_handle,
			shader[KERNEL_MAS_IDENTITY_COPY_L0],
			RID(), p_external_r, RID());
	// sum_levels reads slot 10 (handle-internal z_per_level), writes
	// slot 12 (external z). Override slot 12 only.
	r.sum_levels = _build_uniform_set(rd, p_handle,
			shader[KERNEL_MAS_SUM_LEVELS],
			RID(), RID(), p_external_z);
	if (!r.is_valid()) {
		free_external_uniform_sets(r);
	}
	return r;
}

void CassieMasGpu::free_external_uniform_sets(ExternalApplySets &r_sets) {
	if (rd == nullptr) {
		r_sets.identity_copy_l0 = RID();
		r_sets.sum_levels = RID();
		return;
	}
	if (r_sets.identity_copy_l0.is_valid()) {
		rd->free_rid(r_sets.identity_copy_l0);
		r_sets.identity_copy_l0 = RID();
	}
	if (r_sets.sum_levels.is_valid()) {
		rd->free_rid(r_sets.sum_levels);
		r_sets.sum_levels = RID();
	}
}

bool CassieMasGpu::apply_mas_to_compute_list_cached(
		const MasGpuHandle &p_handle, const ExternalApplySets &p_sets,
		RenderingDevice::ComputeListID p_cl) {
	if (rd == nullptr || !p_handle.is_valid() || !p_sets.is_valid()) {
		return false;
	}
	const int ni = p_handle.ni;
	const int sigma = p_handle.domain_size;
	const int L = p_handle.num_levels;
	const int total_coarse = p_handle.total_coarse_supernodes;

	// 1. identity_copy_l0: r_per_level[0..ni) ← external_r[0..ni)
	rd->compute_list_bind_compute_pipeline(p_cl,
			pipeline[KERNEL_MAS_IDENTITY_COPY_L0]);
	rd->compute_list_bind_uniform_set(p_cl, p_sets.identity_copy_l0, 0);
	rd->compute_list_dispatch(p_cl, uint32_t((ni + 255) / 256), 1, 1);
	rd->compute_list_add_barrier(p_cl);

	// 2. coarsen_residual into r_per_level[ni..].
	if (total_coarse > 0) {
		rd->compute_list_bind_compute_pipeline(p_cl,
				pipeline[KERNEL_MAS_COARSEN_RESIDUAL]);
		rd->compute_list_bind_uniform_set(p_cl,
				p_handle.uniform_sets[KERNEL_MAS_COARSEN_RESIDUAL], 0);
		rd->compute_list_dispatch(p_cl,
				uint32_t((total_coarse + 255) / 256), 1, 1);
		rd->compute_list_add_barrier(p_cl);
	}

	// 3. per_domain_solve at each level.
	rd->compute_list_bind_compute_pipeline(p_cl,
			pipeline[KERNEL_MAS_PER_DOMAIN_SOLVE]);
	for (int l = 0; l < L; ++l) {
		const int Nl = p_handle.level_sizes_cpu[l];
		const int domains_l = (Nl + sigma - 1) / sigma;
		rd->compute_list_bind_uniform_set(p_cl,
				p_handle.uniform_sets_per_level_solve[l], 0);
		rd->compute_list_dispatch(p_cl, uint32_t(domains_l), 1, 1);
		rd->compute_list_add_barrier(p_cl);
	}

	// 4. sum_levels: accumulate per-level z into external_z.
	rd->compute_list_bind_compute_pipeline(p_cl,
			pipeline[KERNEL_MAS_SUM_LEVELS]);
	rd->compute_list_bind_uniform_set(p_cl, p_sets.sum_levels, 0);
	rd->compute_list_dispatch(p_cl, uint32_t((ni + 255) / 256), 1, 1);
	rd->compute_list_add_barrier(p_cl);
	return true;
}

void CassieMasGpu::destroy_mas_state(MasGpuHandle &r_handle) {
	if (rd == nullptr || !r_handle.is_valid()) {
		return;
	}
	// Free uniform sets first (they reference the buffers below).
	for (int i = 0; i < KERNEL_COUNT; ++i) {
		if (r_handle.uniform_sets[i].is_valid()) {
			rd->free_rid(r_handle.uniform_sets[i]);
			r_handle.uniform_sets[i] = RID();
		}
	}
	for (RID &s : r_handle.uniform_sets_per_level_solve) {
		if (s.is_valid()) {
			rd->free_rid(s);
			s = RID();
		}
	}
	r_handle.uniform_sets_per_level_solve.clear();
	// uniform_set_coarsen aliases h.uniform_sets[KERNEL_MAS_COARSEN_RESIDUAL]
	// (freed above) — just clear the cached RID.
	r_handle.uniform_set_coarsen = RID();

	// Free per-level + coarsen params UBOs.
	for (RID &b : r_handle.b_params_per_level) {
		if (b.is_valid()) {
			rd->free_rid(b);
			b = RID();
		}
	}
	r_handle.b_params_per_level.clear();
	if (r_handle.b_params_coarsen.is_valid()) {
		rd->free_rid(r_handle.b_params_coarsen);
		r_handle.b_params_coarsen = RID();
	}

	// Free all 19 main buffers.
	RID *bufs[] = {
		&r_handle.b_params,
		&r_handle.b_row_ptr,
		&r_handle.b_col_idx,
		&r_handle.b_values,
		&r_handle.b_morton,
		&r_handle.b_sorted_idx,
		&r_handle.b_map_per_level,
		&r_handle.b_domain_offsets,
		&r_handle.b_m_inv_packed,
		&r_handle.b_r_per_level,
		&r_handle.b_z_per_level,
		&r_handle.b_r_input,
		&r_handle.b_z_output,
		&r_handle.b_coarse_offsets,
		&r_handle.b_coarse_indices,
		&r_handle.b_level_sizes,
		&r_handle.b_positions,
		&r_handle.b_connect_mask,
		&r_handle.b_dense_workspace,
	};
	for (RID *r : bufs) {
		if (r->is_valid()) {
			rd->free_rid(*r);
			*r = RID();
		}
	}
}

} // namespace cassie_mas_gpu

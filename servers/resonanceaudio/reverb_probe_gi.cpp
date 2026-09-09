/**************************************************************************/
/*  reverb_probe_gi.cpp                                                   */
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

#include "reverb_probe_gi.h"

#include "core/math/delaunay_3d.h"
#include "core/math/static_raycaster.h"
#include "core/object/class_db.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "scene/3d/mesh_instance_3d.h"
#ifdef REAL_T_IS_DOUBLE
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#endif // REAL_T_IS_DOUBLE
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"
#include "servers/resonanceaudio/resonance_audio_material_map.h"
#include "servers/resonanceaudio/resonance_audio_wrapper.h"
#include "servers/resonanceaudio/shaders/reverb_bake.spv.gen.h"

#define _USE_MATH_DEFINES
#include "platforms/common/room_effects_utils.h"

#include <cmath>

void ReverbProbeGI::_collect_mesh_triangles(Node *p_node, Vector<Vector3> &r_vertices, Vector<int> &r_indices, Vector<int> &r_tri_materials, AABB &r_bounds) {
	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(p_node);
	if (mi && mi->get_mesh().is_valid()) {
		Ref<Mesh> mesh = mi->get_mesh();
		Transform3D xform = mi->get_global_transform();

		for (int s = 0; s < mesh->get_surface_count(); s++) {
			if (mesh->surface_get_primitive_type(s) != Mesh::PRIMITIVE_TRIANGLES) {
				continue;
			}
			Array arrays = mesh->surface_get_arrays(s);
			if (arrays.size() == 0) {
				continue;
			}
			PackedVector3Array verts = arrays[Mesh::ARRAY_VERTEX];
			PackedInt32Array idx = arrays[Mesh::ARRAY_INDEX];

			// Determine acoustic material for this surface.
			int acoustic_mat = (int)wall_material;
			if (material_map.is_valid()) {
				Ref<Material> mat = mi->get_active_material(s);
				if (mat.is_valid()) {
					String mat_key = mat->get_path();
					if (mat_key.is_empty()) {
						mat_key = mat->get_name();
					}
					if (!mat_key.is_empty()) {
						acoustic_mat = (int)material_map->get_material_mapping(mat_key);
					}
				}
			}

			int base = r_vertices.size();
			for (int v = 0; v < verts.size(); v++) {
				Vector3 world_v = xform.xform(verts[v]);
				r_vertices.push_back(world_v);
				if (r_vertices.size() == 1) {
					r_bounds.position = world_v;
				} else {
					r_bounds.expand_to(world_v);
				}
			}

			int num_tris = 0;
			if (idx.size() > 0) {
				for (int t = 0; t < idx.size(); t++) {
					r_indices.push_back(base + idx[t]);
				}
				num_tris = idx.size() / 3;
			} else {
				for (int v = 0; v < verts.size(); v++) {
					r_indices.push_back(base + v);
				}
				num_tris = verts.size() / 3;
			}
			for (int t = 0; t < num_tris; t++) {
				r_tri_materials.push_back(acoustic_mat);
			}
		}
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_mesh_triangles(p_node->get_child(i), r_vertices, r_indices, r_tri_materials, r_bounds);
	}
}

void ReverbProbeGI::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process_internal(true);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_update_reverb_from_listener();
		} break;
		case NOTIFICATION_PREDELETE: {
			_free_gpu_resources();
		} break;
	}
}

void ReverbProbeGI::_update_reverb_from_listener() {
	if (bake_data.is_null() || bake_data->get_probe_count() == 0) {
		static bool data_warned = false;
		if (!data_warned) {
			print_line("ReverbProbeGI: no bake data");
			data_warned = true;
		}
		return;
	}

	ResonanceAudioServer *server = ResonanceAudioServer::get_singleton();
	if (!server) {
		static bool server_warned = false;
		if (!server_warned) {
			print_line("ReverbProbeGI: no ResonanceAudioServer");
			server_warned = true;
		}
		return;
	}

	// Use this node's position as the listener position.
	// Attach ReverbProbeGI as a child of the camera/player node.
	Vector3 listener_pos = get_global_position();
	float rt60[9];
	float gain;

	if (bake_data->lookup_reverb(listener_pos, rt60, gain)) {
		static int debug_count = 0;
		if (debug_count < 5 || debug_count % 300 == 0) {
			print_line(vformat("ReverbProbeGI: pos=(%.1f,%.1f,%.1f) rt60_1k=%.3f gain=%.4f", listener_pos.x, listener_pos.y, listener_pos.z, rt60[5], gain));
		}
		debug_count++;
		server->set_reverb_properties(rt60, gain);
	}
}

struct BakeProbeTask {
	int probe_index;
	Vector3 probe_pos;
	Ref<StaticRaycaster> raycaster;
	int ray_count;
	int max_bounces;
	int default_material;
	const int *tri_materials;
	int tri_count;
	float volume;
	float surface_area;
	float *rt60_out;
	float *gain_out;
};

static void _bake_probe_task(void *p_userdata, uint32_t p_index) {
	static constexpr int NUM_BANDS = 9;
	BakeProbeTask *tasks = (BakeProbeTask *)p_userdata;
	BakeProbeTask &task = tasks[p_index];

	float absorbed[NUM_BANDS] = {};
	float total_hits = 0;

	uint32_t rng = (uint32_t)(task.probe_index * 1337 + 49502741);
	auto pcg = [](uint32_t &s) -> float {
		s = s * 747796405u + 2891336453u;
		uint32_t word = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
		word = (word >> 22u) ^ word;
		return (float)word / (float)0xFFFFFFFFu;
	};

	for (int ray = 0; ray < task.ray_count; ray++) {
		float z = 2.0f * pcg(rng) - 1.0f;
		float phi = 2.0f * (float)M_PI * pcg(rng);
		float r = sqrtf(MAX(0.0f, 1.0f - z * z));
		Vector3 ray_dir(r * cosf(phi), r * sinf(phi), z);
		Vector3 ray_origin = task.probe_pos;

		for (int bounce = 0; bounce < task.max_bounces; bounce++) {
			StaticRaycaster::Ray sr(ray_origin, ray_dir, 1e-4f);
			if (!task.raycaster->intersect(sr)) {
				break;
			}

			int hit_mat = task.default_material;
			if (task.tri_materials && sr.primID < (unsigned int)task.tri_count) {
				hit_mat = task.tri_materials[sr.primID];
			}
			vraudio::RoomMaterial rm = vraudio::GetRoomMaterial(hit_mat);
			for (int b = 0; b < NUM_BANDS; b++) {
				absorbed[b] += rm.absorption_coefficients[b];
			}
			total_hits += 1.0f;

			Vector3 hit_normal = sr.normal.normalized();
			if (hit_normal.dot(ray_dir) > 0.0f) {
				hit_normal = -hit_normal;
			}
			ray_origin = ray_origin + ray_dir * sr.tfar + hit_normal * 0.001f;
			float bz = 2.0f * pcg(rng) - 1.0f;
			float bphi = 2.0f * (float)M_PI * pcg(rng);
			float br = sqrtf(MAX(0.0f, 1.0f - bz * bz));
			Vector3 rand_dir(br * cosf(bphi), br * sinf(bphi), bz);
			ray_dir = (rand_dir + hit_normal).normalized();
		}
	}

	static constexpr float kEyring = 0.161f;
	static const float kAirAbsorption[NUM_BANDS] = {
		0.0006f, 0.0006f, 0.0007f, 0.0008f, 0.0010f, 0.0015f, 0.0026f, 0.0060f, 0.0207f
	};

	for (int b = 0; b < NUM_BANDS; b++) {
		if (total_hits < 1.0f) {
			task.rt60_out[b] = 0.0f;
			continue;
		}
		float mean_alpha = absorbed[b] / total_hits;
		mean_alpha = CLAMP(mean_alpha, 0.001f, 0.999f);
		float rt60 = kEyring * task.volume / (-task.surface_area * logf(1.0f - mean_alpha) + 4.0f * kAirAbsorption[b] * task.volume);
		task.rt60_out[b] = MAX(rt60, 0.0f);
	}
	float enclosure = total_hits / (float)(task.ray_count * task.max_bounces);
	*task.gain_out = 0.045f * CLAMP(enclosure * 4.0f, 0.01f, 1.0f);
}

void ReverbProbeGI::_ensure_gpu_resources() {
	if (_gpu_rd) {
		return;
	}
	_gpu_rd = RenderingServer::get_singleton()->create_local_rendering_device();
	if (!_gpu_rd) {
		return;
	}
	Vector<uint8_t> spirv;
	spirv.resize(reverb_bake_spv_size);
	memcpy(spirv.ptrw(), reverb_bake_spv, reverb_bake_spv_size);
	RenderingDevice::ShaderStageSPIRVData stage;
	stage.shader_stage = RenderingDevice::SHADER_STAGE_COMPUTE;
	stage.spirv = spirv;
	Vector<RenderingDevice::ShaderStageSPIRVData> stages;
	stages.push_back(stage);
	_gpu_shader = _gpu_rd->shader_create_from_spirv(stages);
	if (_gpu_shader.is_null()) {
		print_line("ReverbProbeGI: GPU shader creation failed");
		memdelete(_gpu_rd);
		_gpu_rd = nullptr;
		return;
	}
	_gpu_pipeline = _gpu_rd->compute_pipeline_create(_gpu_shader);
}

void ReverbProbeGI::_free_gpu_resources() {
	if (_gpu_rd) {
		if (_gpu_pipeline.is_valid()) {
			_gpu_rd->free_rid(_gpu_pipeline);
			_gpu_pipeline = RID();
		}
		if (_gpu_shader.is_valid()) {
			_gpu_rd->free_rid(_gpu_shader);
			_gpu_shader = RID();
		}
		memdelete(_gpu_rd);
		_gpu_rd = nullptr;
	}
}

bool ReverbProbeGI::_bake_gpu(const PackedVector3Array &p_probes, const Vector<Vector3> &p_vertices, const Vector<int> &p_indices, const Vector<int> &p_tri_materials, const AABB &p_bounds, int p_ray_count, int p_max_bounces, PackedFloat32Array &r_rt60, PackedFloat32Array &r_gains) {
	static constexpr int NUM_BANDS = 9;

	_ensure_gpu_resources();
	if (!_gpu_rd) {
		return false;
	}
	RenderingDevice *rd = _gpu_rd;
	RID shader = _gpu_shader;
	RID pipeline = _gpu_pipeline;

	int probe_count = p_probes.size();
	int tri_count = p_indices.size() / 3;
	float volume = p_bounds.size.x * p_bounds.size.y * p_bounds.size.z;
	float surface_area = 2.0f * (p_bounds.size.x * p_bounds.size.y + p_bounds.size.y * p_bounds.size.z + p_bounds.size.x * p_bounds.size.z);

	// Build 3D grid acceleration structure (same algorithm as lightmapper_rd).
	static constexpr uint32_t CLUSTER_SIZE = 32;
	// Auto-tune grid: aim for ~8 triangles per occupied cell.
	int grid_size = MAX(4, (int)Math::ceil(cbrtf((float)tri_count / 8.0f)));
	grid_size = MIN(grid_size, 32);
	grid_size = (int)Math::next_power_of_2((uint32_t)grid_size);
	AABB bounds = p_bounds;
	bounds.grow_by(0.01f);

	float cell_size_x = bounds.size.x / grid_size;
	float cell_size_y = bounds.size.y / grid_size;
	float cell_size_z = bounds.size.z / grid_size;

	// Plot triangles into grid cells.
	struct TriSort {
		uint32_t cell_index;
		uint32_t triangle_index;
		AABB tri_aabb;
		bool operator<(const TriSort &o) const { return cell_index < o.cell_index; }
	};
	LocalVector<TriSort> tri_sort;

	for (int t = 0; t < tri_count; t++) {
		Vector3 v0 = p_vertices[p_indices[t * 3 + 0]];
		Vector3 v1 = p_vertices[p_indices[t * 3 + 1]];
		Vector3 v2 = p_vertices[p_indices[t * 3 + 2]];
		AABB tri_aabb;
		tri_aabb.position = v0;
		tri_aabb.expand_to(v1);
		tri_aabb.expand_to(v2);
		// Find all grid cells this triangle overlaps.
		int min_x = CLAMP((int)((tri_aabb.position.x - bounds.position.x) / cell_size_x), 0, grid_size - 1);
		int min_y = CLAMP((int)((tri_aabb.position.y - bounds.position.y) / cell_size_y), 0, grid_size - 1);
		int min_z = CLAMP((int)((tri_aabb.position.z - bounds.position.z) / cell_size_z), 0, grid_size - 1);
		int max_x = CLAMP((int)((tri_aabb.position.x + tri_aabb.size.x - bounds.position.x) / cell_size_x), 0, grid_size - 1);
		int max_y = CLAMP((int)((tri_aabb.position.y + tri_aabb.size.y - bounds.position.y) / cell_size_y), 0, grid_size - 1);
		int max_z = CLAMP((int)((tri_aabb.position.z + tri_aabb.size.z - bounds.position.z) / cell_size_z), 0, grid_size - 1);
		for (int z = min_z; z <= max_z; z++) {
			for (int y = min_y; y <= max_y; y++) {
				for (int x = min_x; x <= max_x; x++) {
					TriSort ts;
					ts.cell_index = x + y * grid_size + z * grid_size * grid_size;
					ts.triangle_index = t;
					ts.tri_aabb = tri_aabb;
					tri_sort.push_back(ts);
				}
			}
		}
	}
	tri_sort.sort();

	// Build grid data: per cell (triangle_count, solid_cell_index).
	int total_cells = grid_size * grid_size * grid_size;
	Vector<uint8_t> grid_data_bytes;
	grid_data_bytes.resize(total_cells * sizeof(uint32_t) * 2);
	uint32_t *grid_w = (uint32_t *)grid_data_bytes.ptrw();
	memset(grid_w, 0, total_cells * sizeof(uint32_t) * 2);

	// Build cluster indices and AABBs.
	struct ClusterAABBData {
		float min_bounds[3];
		float pad0 = 0;
		float max_bounds[3];
		float pad1 = 0;
	};
	LocalVector<uint32_t> cluster_idx_data; // pairs: (cluster_start, tri_start)
	LocalVector<ClusterAABBData> cluster_aabb_data;
	Vector<uint8_t> sorted_tri_indices_bytes;
	sorted_tri_indices_bytes.resize(tri_sort.size() * sizeof(uint32_t));
	uint32_t *sti = (uint32_t *)sorted_tri_indices_bytes.ptrw();

	uint32_t last_cell = 0xFFFFFFFF;
	uint32_t solid_cell_count = 0;
	uint32_t cluster_count = 0;

	// First pass: count triangles per cell and solid cells.
	for (uint32_t i = 0; i < tri_sort.size(); i++) {
		uint32_t cell = tri_sort[i].cell_index;
		if (cell != last_cell) {
			grid_w[cell * 2 + 1] = solid_cell_count;
			solid_cell_count++;
		}
		if ((grid_w[cell * 2] % CLUSTER_SIZE) == 0) {
			cluster_count++;
		}
		grid_w[cell * 2]++;
		last_cell = cell;
	}

	cluster_idx_data.resize(solid_cell_count * 2);
	cluster_aabb_data.resize(cluster_count);

	// Second pass: fill sorted indices and cluster AABBs.
	uint32_t i = 0;
	uint32_t ci = 0;
	uint32_t si = 0;
	while (i < tri_sort.size()) {
		uint32_t cell = tri_sort[i].cell_index;
		uint32_t count = grid_w[cell * 2];
		cluster_idx_data[si * 2] = ci;
		cluster_idx_data[si * 2 + 1] = i;
		uint32_t cell_clusters = (count + CLUSTER_SIZE - 1) / CLUSTER_SIZE;
		for (uint32_t c = 0; c < cell_clusters; c++) {
			uint32_t start = c * CLUSTER_SIZE;
			uint32_t end = MIN(start + CLUSTER_SIZE, count);
			AABB caabb = tri_sort[i + start].tri_aabb;
			for (uint32_t j = start + 1; j < end; j++) {
				caabb.merge_with(tri_sort[i + j].tri_aabb);
			}
			ClusterAABBData &ca = cluster_aabb_data[ci + c];
			Vector3 ca_end = caabb.get_end();
			ca.min_bounds[0] = caabb.position.x;
			ca.min_bounds[1] = caabb.position.y;
			ca.min_bounds[2] = caabb.position.z;
			ca.max_bounds[0] = ca_end.x;
			ca.max_bounds[1] = ca_end.y;
			ca.max_bounds[2] = ca_end.z;
		}
		for (uint32_t j = 0; j < count; j++) {
			sti[i + j] = tri_sort[i + j].triangle_index;
		}
		i += count;
		ci += cell_clusters;
		si++;
	}

	// BakeParams uniform buffer.
	// Must match std140 layout: vec3 padded to 16 bytes.
	struct BakeParamsGPU {
		uint32_t ray_count;
		uint32_t max_bounces;
		uint32_t probe_count;
		uint32_t triangle_count;
		uint32_t ray_from;
		uint32_t ray_to;
		uint32_t _pad0[2];
		float to_cell_offset[3];
		int32_t grid_size_val;
		float to_cell_size[3];
		float bias;
	};
	BakeParamsGPU params{};
	params.ray_count = p_ray_count;
	params.max_bounces = p_max_bounces;
	params.probe_count = probe_count;
	params.triangle_count = tri_count;
	params.bias = 0.001f;
	params.grid_size_val = grid_size;
	params.to_cell_offset[0] = bounds.position.x;
	params.to_cell_offset[1] = bounds.position.y;
	params.to_cell_offset[2] = bounds.position.z;
	params.to_cell_size[0] = 1.0f / cell_size_x;
	params.to_cell_size[1] = 1.0f / cell_size_y;
	params.to_cell_size[2] = 1.0f / cell_size_z;

	// Vertex buffer: position_hi + position_lo (double-float split).
	Vector<uint8_t> vertex_data;
	vertex_data.resize(p_vertices.size() * sizeof(float) * 6);
	float *vd = (float *)vertex_data.ptrw();
	for (int vi = 0; vi < p_vertices.size(); vi++) {
#ifdef REAL_T_IS_DOUBLE
		RendererRD::MaterialStorage::split_double(p_vertices[vi].x, &vd[vi * 6 + 0], &vd[vi * 6 + 3]);
		RendererRD::MaterialStorage::split_double(p_vertices[vi].y, &vd[vi * 6 + 1], &vd[vi * 6 + 4]);
		RendererRD::MaterialStorage::split_double(p_vertices[vi].z, &vd[vi * 6 + 2], &vd[vi * 6 + 5]);
#else
		vd[vi * 6 + 0] = p_vertices[vi].x;
		vd[vi * 6 + 1] = p_vertices[vi].y;
		vd[vi * 6 + 2] = p_vertices[vi].z;
		vd[vi * 6 + 3] = 0;
		vd[vi * 6 + 4] = 0;
		vd[vi * 6 + 5] = 0;
#endif
	}

	// Triangle buffer.
	Vector<uint8_t> tri_data;
	tri_data.resize(tri_count * sizeof(uint32_t) * 4);
	uint32_t *td = (uint32_t *)tri_data.ptrw();
	for (int ti = 0; ti < tri_count; ti++) {
		td[ti * 4 + 0] = p_indices[ti * 3 + 0];
		td[ti * 4 + 1] = p_indices[ti * 3 + 1];
		td[ti * 4 + 2] = p_indices[ti * 3 + 2];
		td[ti * 4 + 3] = (ti < p_tri_materials.size()) ? p_tri_materials[ti] : (int)wall_material;
	}

	// Probe positions: hi + lo (double-float split).
	Vector<uint8_t> probe_data;
	probe_data.resize(probe_count * sizeof(float) * 8);
	float *pd = (float *)probe_data.ptrw();
	for (int pi = 0; pi < probe_count; pi++) {
#ifdef REAL_T_IS_DOUBLE
		RendererRD::MaterialStorage::split_double(p_probes[pi].x, &pd[pi * 8 + 0], &pd[pi * 8 + 4]);
		RendererRD::MaterialStorage::split_double(p_probes[pi].y, &pd[pi * 8 + 1], &pd[pi * 8 + 5]);
		RendererRD::MaterialStorage::split_double(p_probes[pi].z, &pd[pi * 8 + 2], &pd[pi * 8 + 6]);
#else
		pd[pi * 8 + 0] = p_probes[pi].x;
		pd[pi * 8 + 1] = p_probes[pi].y;
		pd[pi * 8 + 2] = p_probes[pi].z;
		pd[pi * 8 + 4] = 0;
		pd[pi * 8 + 5] = 0;
		pd[pi * 8 + 6] = 0;
#endif
		pd[pi * 8 + 3] = 0;
		pd[pi * 8 + 7] = 0;
	}

	// Material absorption buffer.
	Vector<uint8_t> mat_data;
	mat_data.resize(24 * NUM_BANDS * sizeof(float));
	float *md = (float *)mat_data.ptrw();
	for (int m = 0; m < 24; m++) {
		vraudio::RoomMaterial rm = vraudio::GetRoomMaterial(m);
		for (int b = 0; b < NUM_BANDS; b++) {
			md[m * NUM_BANDS + b] = rm.absorption_coefficients[b];
		}
	}

	// Output accumulator.
	int accum_size = probe_count * (NUM_BANDS + 1) * sizeof(float);
	Vector<uint8_t> accum_init;
	accum_init.resize(accum_size);
	memset(accum_init.ptrw(), 0, accum_size);

	// Cluster indices as uint2 pairs.
	Vector<uint8_t> cluster_idx_bytes;
	cluster_idx_bytes.resize(MAX(1u, (uint32_t)cluster_idx_data.size()) * sizeof(uint32_t));
	if (cluster_idx_data.size() > 0) {
		memcpy(cluster_idx_bytes.ptrw(), cluster_idx_data.ptr(), cluster_idx_data.size() * sizeof(uint32_t));
	} else {
		memset(cluster_idx_bytes.ptrw(), 0, cluster_idx_bytes.size());
	}

	// Cluster AABBs.
	Vector<uint8_t> cluster_aabb_bytes;
	cluster_aabb_bytes.resize(MAX(1u, (uint32_t)cluster_aabb_data.size()) * sizeof(ClusterAABBData));
	if (cluster_aabb_data.size() > 0) {
		memcpy(cluster_aabb_bytes.ptrw(), cluster_aabb_data.ptr(), cluster_aabb_data.size() * sizeof(ClusterAABBData));
	} else {
		memset(cluster_aabb_bytes.ptrw(), 0, cluster_aabb_bytes.size());
	}

	// Create GPU buffers.
	Vector<uint8_t> params_bytes;
	params_bytes.resize(sizeof(BakeParamsGPU));
	memcpy(params_bytes.ptrw(), &params, sizeof(BakeParamsGPU));
	RID params_buf = rd->uniform_buffer_create(sizeof(BakeParamsGPU), params_bytes);
	RID vertex_buf = rd->storage_buffer_create(vertex_data.size(), vertex_data);
	RID tri_buf = rd->storage_buffer_create(tri_data.size(), tri_data);
	RID probe_buf = rd->storage_buffer_create(probe_data.size(), probe_data);
	RID mat_buf = rd->storage_buffer_create(mat_data.size(), mat_data);
	RID grid_buf = rd->storage_buffer_create(grid_data_bytes.size(), grid_data_bytes);
	RID cluster_idx_buf = rd->storage_buffer_create(cluster_idx_bytes.size(), cluster_idx_bytes);
	RID cluster_aabb_buf = rd->storage_buffer_create(cluster_aabb_bytes.size(), cluster_aabb_bytes);
	RID sorted_tri_buf = rd->storage_buffer_create(MAX((int)sorted_tri_indices_bytes.size(), 4), sorted_tri_indices_bytes.size() > 0 ? sorted_tri_indices_bytes : Vector<uint8_t>({ 0, 0, 0, 0 }));
	RID accum_buf = rd->storage_buffer_create(accum_size, accum_init);

	// Uniform set 0: matches Slang bindings.
	Vector<RenderingDevice::Uniform> set0;
	{
		RenderingDevice::Uniform u;
		u.uniform_type = RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER;
		u.binding = 0;
		u.append_id(params_buf);
		set0.push_back(u);
	}
	{
		RenderingDevice::Uniform u;
		u.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u.binding = 1;
		u.append_id(vertex_buf);
		set0.push_back(u);
	}
	{
		RenderingDevice::Uniform u;
		u.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u.binding = 2;
		u.append_id(tri_buf);
		set0.push_back(u);
	}
	{
		RenderingDevice::Uniform u;
		u.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u.binding = 3;
		u.append_id(probe_buf);
		set0.push_back(u);
	}
	{
		RenderingDevice::Uniform u;
		u.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u.binding = 4;
		u.append_id(mat_buf);
		set0.push_back(u);
	}
	{
		RenderingDevice::Uniform u;
		u.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u.binding = 5;
		u.append_id(grid_buf);
		set0.push_back(u);
	}
	{
		RenderingDevice::Uniform u;
		u.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u.binding = 6;
		u.append_id(cluster_idx_buf);
		set0.push_back(u);
	}
	{
		RenderingDevice::Uniform u;
		u.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u.binding = 7;
		u.append_id(cluster_aabb_buf);
		set0.push_back(u);
	}
	{
		RenderingDevice::Uniform u;
		u.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u.binding = 8;
		u.append_id(sorted_tri_buf);
		set0.push_back(u);
	}
	RID uniform_set0 = rd->uniform_set_create(set0, shader, 0);

	// Uniform set 1: output accumulator.
	Vector<RenderingDevice::Uniform> set1;
	{
		RenderingDevice::Uniform u;
		u.uniform_type = RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER;
		u.binding = 0;
		u.append_id(accum_buf);
		set1.push_back(u);
	}
	RID uniform_set1 = rd->uniform_set_create(set1, shader, 1);

	// Dispatch parallelized over (rays, probes): groups_x = rays, groups_y = probes.
	// Each thread traces ONE ray; workgroup reduces via shared memory + atomicAdd.
	int max_rays_per_batch = 32768;

	for (int batch_from = 0; batch_from < p_ray_count; batch_from += max_rays_per_batch) {
		int batch_to = MIN(batch_from + max_rays_per_batch, p_ray_count);
		params.ray_from = batch_from;
		params.ray_to = batch_to;
		rd->buffer_update(params_buf, 0, sizeof(BakeParamsGPU), &params);

		int groups_x = Math::division_round_up(batch_to - batch_from, 64);
		int groups_y = probe_count;

		RenderingDevice::ComputeListID cl = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(cl, pipeline);
		rd->compute_list_bind_uniform_set(cl, uniform_set0, 0);
		rd->compute_list_bind_uniform_set(cl, uniform_set1, 1);
		rd->compute_list_dispatch(cl, groups_x, groups_y, 1);
		rd->compute_list_end();
		rd->submit();
		rd->sync();
	}

	// Read back results.
	Vector<uint8_t> result = rd->buffer_get_data(accum_buf);
	const float *accum = (const float *)result.ptr();

	static constexpr float kEyring = 0.161f;
	static const float kAirAbsorption[NUM_BANDS] = {
		0.0006f, 0.0006f, 0.0007f, 0.0008f, 0.0010f, 0.0015f, 0.0026f, 0.0060f, 0.0207f
	};

	float *rt60_w = r_rt60.ptrw();
	float *gains_w = r_gains.ptrw();
	for (int pi = 0; pi < probe_count; pi++) {
		int base = pi * (NUM_BANDS + 1);
		float total_hits = accum[base + NUM_BANDS];
		for (int b = 0; b < NUM_BANDS; b++) {
			if (total_hits < 1.0f) {
				rt60_w[pi * NUM_BANDS + b] = 0.0f;
				continue;
			}
			float mean_alpha = accum[base + b] / total_hits;
			mean_alpha = CLAMP(mean_alpha, 0.001f, 0.999f);
			float rt60 = kEyring * volume / (-surface_area * logf(1.0f - mean_alpha) + 4.0f * kAirAbsorption[b] * volume);
			rt60_w[pi * NUM_BANDS + b] = MAX(rt60, 0.0f);
		}
		float enclosure = total_hits / (float)(p_ray_count * p_max_bounces);
		gains_w[pi] = 0.045f * CLAMP(enclosure * 4.0f, 0.01f, 1.0f);
	}

	// Cleanup.
	rd->free_rid(accum_buf);
	rd->free_rid(sorted_tri_buf);
	rd->free_rid(cluster_aabb_buf);
	rd->free_rid(cluster_idx_buf);
	rd->free_rid(grid_buf);
	rd->free_rid(mat_buf);
	rd->free_rid(probe_buf);
	rd->free_rid(tri_buf);
	rd->free_rid(vertex_buf);
	rd->free_rid(params_buf);
	rd->free_rid(uniform_set1);
	rd->free_rid(uniform_set0);

	return true;
}

ReverbProbeGI::BakeError ReverbProbeGI::bake(Node *p_from_node, const PackedVector3Array &p_probe_positions, int p_ray_count, int p_max_bounces) {
	static constexpr int NUM_BANDS = 9;

	Vector<Vector3> vertices;
	Vector<int> indices;
	Vector<int> tri_materials;
	AABB bounds;
	_collect_mesh_triangles(p_from_node, vertices, indices, tri_materials, bounds);

	if (indices.size() < 3) {
		return BAKE_ERROR_NO_MESHES;
	}

	int probe_count = p_probe_positions.size();
	if (probe_count == 0) {
		return BAKE_ERROR_NO_PROBES;
	}

	float volume = bounds.size.x * bounds.size.y * bounds.size.z;
	float surface_area = 2.0f * (bounds.size.x * bounds.size.y + bounds.size.y * bounds.size.z + bounds.size.x * bounds.size.z);

	PackedFloat32Array rt60_values;
	rt60_values.resize(probe_count * NUM_BANDS);

	PackedFloat32Array gains;
	gains.resize(probe_count);

	uint64_t start_usec = OS::get_singleton()->get_ticks_usec();

	// Try GPU first, fall back to CPU.
	bool gpu_ok = _bake_gpu(p_probe_positions, vertices, indices, tri_materials, bounds, p_ray_count, p_max_bounces, rt60_values, gains);
	if (gpu_ok) {
		uint64_t elapsed_usec = OS::get_singleton()->get_ticks_usec() - start_usec;
		double elapsed_sec = elapsed_usec / 1000000.0;
		double total_rays = (double)probe_count * p_ray_count;
		print_line(vformat("ReverbProbeGI GPU bake: %d probes, %d rays, %d bounces in %.2fs (%.0f probes/s, %.0f Mrays/s)",
				probe_count, p_ray_count, p_max_bounces, elapsed_sec,
				probe_count / elapsed_sec, total_rays / elapsed_sec / 1e6));
	} else {
		print_line("ReverbProbeGI: GPU bake unavailable, falling back to CPU (Embree).");
		start_usec = OS::get_singleton()->get_ticks_usec();

		// Build BVH acceleration structure via StaticRaycaster (Embree).
		Ref<StaticRaycaster> raycaster = StaticRaycaster::create();
		if (raycaster.is_null()) {
			print_line("ReverbProbeGI: StaticRaycaster not available (Embree not compiled in). Cannot bake.");
			return BAKE_ERROR_NO_MESHES;
		}

		PackedVector3Array mesh_verts;
		PackedInt32Array mesh_indices;
		mesh_verts.resize(vertices.size());
		mesh_indices.resize(indices.size());
		for (int i = 0; i < vertices.size(); i++) {
			mesh_verts.write[i] = vertices[i];
		}
		for (int i = 0; i < indices.size(); i++) {
			mesh_indices.write[i] = indices[i];
		}
		raycaster->add_mesh(mesh_verts, mesh_indices, 0);
		raycaster->commit();

		uint64_t start_usec = OS::get_singleton()->get_ticks_usec();

		LocalVector<BakeProbeTask> tasks;
		tasks.resize(probe_count);
		for (int i = 0; i < probe_count; i++) {
			tasks[i].probe_index = i;
			tasks[i].probe_pos = p_probe_positions[i];
			tasks[i].raycaster = raycaster;
			tasks[i].ray_count = p_ray_count;
			tasks[i].max_bounces = p_max_bounces;
			tasks[i].default_material = (int)wall_material;
			tasks[i].tri_materials = tri_materials.size() > 0 ? tri_materials.ptr() : nullptr;
			tasks[i].tri_count = tri_materials.size();
			tasks[i].volume = volume;
			tasks[i].surface_area = surface_area;
			tasks[i].rt60_out = rt60_values.ptrw() + i * NUM_BANDS;
			tasks[i].gain_out = gains.ptrw() + i;
		}

		WorkerThreadPool::GroupID group = WorkerThreadPool::get_singleton()->add_native_group_task(
				_bake_probe_task, tasks.ptr(), probe_count, -1, true);
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group);

		uint64_t elapsed_usec = OS::get_singleton()->get_ticks_usec() - start_usec;
		double elapsed_sec = elapsed_usec / 1000000.0;
		double total_rays = (double)probe_count * p_ray_count;
		print_line(vformat("ReverbProbeGI CPU bake: %d probes, %d rays, %d bounces in %.2fs (%.0f probes/s, %.0f Mrays/s)",
				probe_count, p_ray_count, p_max_bounces, elapsed_sec,
				probe_count / elapsed_sec, total_rays / elapsed_sec / 1e6));
	} // end else (CPU fallback)

	Vector<Vector3> points_vec;
	points_vec.resize(probe_count);
	for (int i = 0; i < probe_count; i++) {
		points_vec.write[i] = p_probe_positions[i];
	}
	Vector<Delaunay3D::OutputSimplex> simplices = Delaunay3D::tetrahedralize(points_vec);

	PackedInt32Array tetrahedra;
	tetrahedra.resize(simplices.size() * 4);
	int32_t *tet_w = tetrahedra.ptrw();
	for (int i = 0; i < simplices.size(); i++) {
		tet_w[i * 4 + 0] = simplices[i].points[0];
		tet_w[i * 4 + 1] = simplices[i].points[1];
		tet_w[i * 4 + 2] = simplices[i].points[2];
		tet_w[i * 4 + 3] = simplices[i].points[3];
	}

	PackedInt32Array bsp_tree;

	Ref<ReverbBakeData> data;
	data.instantiate();
	data->set_data(bounds, p_probe_positions, rt60_values, gains, tetrahedra, bsp_tree);
	set_bake_data(data);

	return BAKE_ERROR_OK;
}

void ReverbProbeGI::set_bake_data(const Ref<ReverbBakeData> &p_data) {
	bake_data = p_data;
}

void ReverbProbeGI::set_wall_material(WallMaterial p_material) {
	wall_material = p_material;
}

void ReverbProbeGI::set_material_map(const Ref<ResonanceAudioMaterialMap> &p_map) {
	material_map = p_map;
}

Ref<ResonanceAudioMaterialMap> ReverbProbeGI::get_material_map() const {
	return material_map;
}

void ReverbProbeGI::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_bake_data", "data"), &ReverbProbeGI::set_bake_data);
	ClassDB::bind_method(D_METHOD("get_bake_data"), &ReverbProbeGI::get_bake_data);

	ClassDB::bind_method(D_METHOD("set_wall_material", "material"), &ReverbProbeGI::set_wall_material);
	ClassDB::bind_method(D_METHOD("get_wall_material"), &ReverbProbeGI::get_wall_material);

	ClassDB::bind_method(D_METHOD("set_material_map", "map"), &ReverbProbeGI::set_material_map);
	ClassDB::bind_method(D_METHOD("get_material_map"), &ReverbProbeGI::get_material_map);

	ClassDB::bind_method(D_METHOD("bake", "from_node", "probe_positions", "ray_count", "max_bounces"), &ReverbProbeGI::bake, DEFVAL(50000), DEFVAL(100));

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "bake_data", PROPERTY_HINT_RESOURCE_TYPE, "ReverbBakeData"), "set_bake_data", "get_bake_data");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "wall_material", PROPERTY_HINT_ENUM, "Transparent,Acoustic Ceiling Tiles,Brick Bare,Brick Painted,Concrete Block Coarse,Concrete Block Painted,Curtain Heavy,Fiber Glass Insulation,Glass Thin,Glass Thick,Grass,Linoleum On Concrete,Marble,Metal,Parquet On Concrete,Plaster Rough,Plaster Smooth,Plywood Panel,Polished Concrete Or Tile,Sheetrock,Water Or Ice Surface,Wood Ceiling,Wood Panel,Uniform"), "set_wall_material", "get_wall_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material_map", PROPERTY_HINT_RESOURCE_TYPE, "ResonanceAudioMaterialMap", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_EDITOR_INSTANTIATE_OBJECT), "set_material_map", "get_material_map");

	BIND_ENUM_CONSTANT(MATERIAL_TRANSPARENT);
	BIND_ENUM_CONSTANT(MATERIAL_ACOUSTIC_CEILING_TILES);
	BIND_ENUM_CONSTANT(MATERIAL_BRICK_BARE);
	BIND_ENUM_CONSTANT(MATERIAL_BRICK_PAINTED);
	BIND_ENUM_CONSTANT(MATERIAL_CONCRETE_BLOCK_COARSE);
	BIND_ENUM_CONSTANT(MATERIAL_CONCRETE_BLOCK_PAINTED);
	BIND_ENUM_CONSTANT(MATERIAL_CURTAIN_HEAVY);
	BIND_ENUM_CONSTANT(MATERIAL_FIBER_GLASS_INSULATION);
	BIND_ENUM_CONSTANT(MATERIAL_GLASS_THIN);
	BIND_ENUM_CONSTANT(MATERIAL_GLASS_THICK);
	BIND_ENUM_CONSTANT(MATERIAL_GRASS);
	BIND_ENUM_CONSTANT(MATERIAL_LINOLEUM_ON_CONCRETE);
	BIND_ENUM_CONSTANT(MATERIAL_MARBLE);
	BIND_ENUM_CONSTANT(MATERIAL_METAL);
	BIND_ENUM_CONSTANT(MATERIAL_PARQUET_ON_CONCRETE);
	BIND_ENUM_CONSTANT(MATERIAL_PLASTER_ROUGH);
	BIND_ENUM_CONSTANT(MATERIAL_PLASTER_SMOOTH);
	BIND_ENUM_CONSTANT(MATERIAL_PLYWOOD_PANEL);
	BIND_ENUM_CONSTANT(MATERIAL_POLISHED_CONCRETE_OR_TILE);
	BIND_ENUM_CONSTANT(MATERIAL_SHEETROCK);
	BIND_ENUM_CONSTANT(MATERIAL_WATER_OR_ICE_SURFACE);
	BIND_ENUM_CONSTANT(MATERIAL_WOOD_CEILING);
	BIND_ENUM_CONSTANT(MATERIAL_WOOD_PANEL);
	BIND_ENUM_CONSTANT(MATERIAL_UNIFORM);

	BIND_ENUM_CONSTANT(BAKE_ERROR_OK);
	BIND_ENUM_CONSTANT(BAKE_ERROR_NO_MESHES);
	BIND_ENUM_CONSTANT(BAKE_ERROR_NO_PROBES);
}

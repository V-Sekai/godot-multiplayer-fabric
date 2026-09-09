#version 460
#extension GL_KHR_memory_scope_semantics : require
#extension GL_EXT_control_flow_attributes : require
layout(row_major) uniform;
layout(row_major) buffer;

#line 17 0
struct BakeParams_0 {
	uint ray_count_0;
	uint max_bounces_0;
	uint probe_count_0;
	uint triangle_count_0;
	uint ray_from_0;
	uint ray_to_0;
	vec3 to_cell_offset_0;
	int grid_size_0;
	vec3 to_cell_size_0;
	float bias_0;
};

#line 82
layout(binding = 0)
		layout(std140) uniform block_BakeParams_0 {
	uint ray_count_0;
	uint max_bounces_0;
	uint probe_count_0;
	uint triangle_count_0;
	uint ray_from_0;
	uint ray_to_0;
	vec3 to_cell_offset_0;
	int grid_size_0;
	vec3 to_cell_size_0;
	float bias_0;
}
params_0;

#line 53
struct ProbePosition_0 {
	vec3 position_0;
	float pad0_0;
	vec3 position_lo_0;
	float pad1_0;
};

#line 85
layout(std430, binding = 3) readonly buffer StructuredBuffer_ProbePosition_t_0 {
	ProbePosition_0 _data[];
}
probe_positions_0;

#line 87
layout(std430, binding = 5) readonly buffer StructuredBuffer_vectorx3Cuintx2C2x3E_t_0 {
	uvec2 _data[];
}
grid_data_0;

#line 88
layout(std430, binding = 6) readonly buffer StructuredBuffer_vectorx3Cuintx2C2x3E_t_1 {
	uvec2 _data[];
}
cluster_indices_0;

#line 46
struct ClusterAABB_0 {
	vec3 min_bounds_0;
	float pad0_1;
	vec3 max_bounds_0;
	float pad1_1;
};

#line 89
layout(std430, binding = 7) readonly buffer StructuredBuffer_ClusterAABB_t_0 {
	ClusterAABB_0 _data[];
}
cluster_aabbs_0;

#line 90
layout(std430, binding = 8) readonly buffer StructuredBuffer_uint_t_0 {
	uint _data[];
}
triangle_indices_0;

#line 35
struct Triangle_0 {
	uint i0_0;
	uint i1_0;
	uint i2_0;
	uint material_index_0;
};

#line 84
layout(std430, binding = 2) readonly buffer StructuredBuffer_Triangle_t_0 {
	Triangle_0 _data[];
}
triangles_0;

#line 30
struct Vertex_0 {
	vec3 position_1;
	vec3 position_lo_1;
};

#line 83
layout(std430, binding = 1) readonly buffer StructuredBuffer_Vertex_t_0 {
	Vertex_0 _data[];
}
vertices_0;

#line 42
struct MaterialAbsorption_0 {
	float coefficients_0[9];
};

#line 86
layout(std430, binding = 4) readonly buffer StructuredBuffer_MaterialAbsorption_t_0 {
	MaterialAbsorption_0 _data[];
}
materials_0;

layout(std430, binding = 0, set = 1) buffer StructuredBuffer_uint_t_1 {
	uint _data[];
}
output_accum_0;

#line 108
uint pcg_hash_0(uint state_0) {
#line 109
	uint s_0 = state_0 * 747796405U + 2891336453U;
	uint word_0 = ((s_0 >> ((s_0 >> 28U) + 4U)) ^ s_0) * 277803737U;
	return (word_0 >> 22U) ^ word_0;
}

float rand_float_0(inout uint state_1) {
#line 115
	uint _S1 = pcg_hash_0(state_1);

#line 115
	state_1 = _S1;
	return float(_S1) / 4.294967296e+09;
}

vec3 random_sphere_direction_0(inout uint rng_0) {
#line 120
	float _S2 = rand_float_0(rng_0);

#line 120
	float z_0 = 2.0 * _S2 - 1.0;
	float _S3 = rand_float_0(rng_0);

#line 121
	float phi_0 = 6.28318548202514648 * _S3;
	float r_0 = sqrt(max(0.0, 1.0 - z_0 * z_0));
	return vec3(r_0 * cos(phi_0), r_0 * sin(phi_0), z_0);
}

#line 159
bool ray_box_test_0(vec3 origin_0, vec3 inv_dir_0, vec3 box_min_0, vec3 box_max_0) {
#line 160
	vec3 t0_0 = (box_min_0 - origin_0) * inv_dir_0;
	vec3 t1_0 = (box_max_0 - origin_0) * inv_dir_0;
	vec3 tmin_0 = min(t0_0, t1_0);
	vec3 tmax_0 = max(t0_0, t1_0);

	float _S4 = min(min(tmax_0.x, tmax_0.y), tmax_0.z);

#line 165
	bool _S5;
	if ((max(max(tmin_0.x, tmin_0.y), tmin_0.z)) <= _S4) {
#line 166
		_S5 = _S4 >= 0.0;

#line 166
	} else {
#line 166
		_S5 = false;

#line 166
	}

#line 166
	return _S5;
}

#line 131
bool ray_triangle_intersect_0(vec3 ro_0, vec3 rd_0, vec3 v0_0, vec3 v1_0, vec3 v2_0, out float t_0, out vec3 normal_out_0) {
	t_0 = 0.0;
	normal_out_0 = vec3(0.0, 0.0, 0.0);
	vec3 e1_0 = v1_0 - v0_0;
	vec3 e2_0 = v2_0 - v0_0;
	vec3 h_0 = cross(rd_0, e2_0);
	float a_0 = dot(e1_0, h_0);
	if ((abs(a_0)) < 9.99999993922529029e-09) {
#line 140
		return false;
	}

#line 141
	float f_0 = 1.0 / a_0;
	vec3 s_1 = ro_0 - v0_0;
	float u_0 = f_0 * dot(s_1, h_0);

#line 143
	bool _S6;
	if (u_0 < 0.0) {
#line 144
		_S6 = true;

#line 144
	} else {
#line 144
		_S6 = u_0 > 1.0;

#line 144
	}

#line 144
	if (_S6) {
#line 145
		return false;
	}

#line 146
	vec3 q_0 = cross(s_1, e1_0);
	float v_0 = f_0 * dot(rd_0, q_0);
	if (v_0 < 0.0) {
#line 148
		_S6 = true;

#line 148
	} else {
#line 148
		_S6 = (u_0 + v_0) > 1.0;

#line 148
	}

#line 148
	if (_S6) {
#line 149
		return false;
	}

#line 150
	float _S7 = f_0 * dot(e2_0, q_0);

#line 150
	t_0 = _S7;
	if (_S7 < 0.00000999999974738) {
#line 152
		return false;
	}

#line 153
	vec3 _S8 = normalize(cross(e1_0, e2_0));

#line 153
	normal_out_0 = _S8;
	if ((dot(_S8, rd_0)) > 0.0) {
#line 155
		normal_out_0 = -normal_out_0;

#line 154
	}

	return true;
}

#line 171
bool trace_ray_grid_0(vec3 ray_origin_0, vec3 ray_dir_0, out float hit_t_0, out uint hit_tri_0, out vec3 hit_normal_0) {
#line 172
	hit_t_0 = 1.00000001504746622e+30;
	hit_tri_0 = 4294967295U;
	const vec3 _S9 = vec3(0.0, 0.0, 0.0);

#line 174
	hit_normal_0 = _S9;

	vec3 _S10 = 1.0 / ray_dir_0;
	int gs_0 = params_0.grid_size_0;

	vec3 from_cell_0 = (ray_origin_0 - params_0.to_cell_offset_0) * params_0.to_cell_size_0;
	ivec3 _S11 = ivec3(floor(from_cell_0));

#line 181
	ivec3 icell_0 = _S11;

	ivec3 step_0 = (ivec3(sign((ray_dir_0))));
	vec3 dir_cell_0 = normalize(ray_dir_0 * (1.0 / params_0.to_cell_size_0));
	vec3 _S12 = min(abs(1.0 / dir_cell_0), vec3(float(params_0.grid_size_0)));

	vec3 t_max_0 = (vec3(_S11) + max(_S9, vec3(step_0)) - from_cell_0) / dir_cell_0;

	int _S13 = step_0.x;

#line 191
	if (_S13 == 0) {
#line 191
		t_max_0[0] = 1.00000001504746622e+30;

#line 191
	}
	int _S14 = step_0.y;

#line 192
	if (_S14 == 0) {
#line 192
		t_max_0[1] = 1.00000001504746622e+30;

#line 192
	}
	int _S15 = step_0.z;

#line 193
	if (_S15 == 0) {
#line 193
		t_max_0[2] = 1.00000001504746622e+30;

#line 193
	}

#line 193
	uint iters_0 = 0U;

	for (;;) {
#line 196
		bool _S16;

#line 196
		if ((all(bvec3((greaterThanEqual(icell_0, ivec3(0, 0, 0))))))) {
#line 196
			_S16 = (all(bvec3((lessThan(icell_0, ivec3(gs_0, gs_0, gs_0))))));

#line 196
		} else {
#line 196
			_S16 = false;

#line 196
		}

#line 196
		bool _S17;

#line 196
		if (_S16) {
#line 196
			_S17 = iters_0 < 1000U;

#line 196
		} else {
#line 196
			_S17 = false;

#line 196
		}

#line 196
		if (_S17) {
		} else {
#line 196
			break;
		}
		uvec2 cell_data_0 = grid_data_0._data[uint(uint(icell_0.x + icell_0.y * gs_0 + icell_0.z * gs_0 * gs_0))];
		uint tri_count_0 = cell_data_0.x;

		if (tri_count_0 > 0U) {
#line 202
			uint cell_solid_index_0 = cell_data_0.y;
			uint _S18 = cluster_indices_0._data[uint(cell_solid_index_0)].x;
			uint _S19 = cluster_indices_0._data[uint(cell_solid_index_0)].y;
			uint _S20 = (tri_count_0 + 32U - 1U) / 32U;

#line 205
			uint ci_0 = 0U;

			for (;;) {
#line 207
				if (ci_0 < _S20) {
				} else {
#line 207
					break;
				}

#line 208
				ClusterAABB_0 ca_0 = cluster_aabbs_0._data[uint(_S18 + ci_0)];
				if (!ray_box_test_0(ray_origin_0, _S10, ca_0.min_bounds_0, ca_0.max_bounds_0)) {
#line 210
					ci_0 = ci_0 + 1U;

#line 207
					continue;
				}

				uint tri_base_0 = ci_0 * 32U;
				uint _S21 = min(32U, tri_count_0 - tri_base_0);

#line 213
				uint ti_0 = 0U;
				for (;;) {
#line 214
					if (ti_0 < _S21) {
					} else {
#line 214
						break;
					}

#line 215
					uint tri_idx_0 = triangle_indices_0._data[uint(_S19 + tri_base_0 + ti_0)];
					Triangle_0 tri_0 = triangles_0._data[uint(tri_idx_0)];

#line 221
					float t_1;
					vec3 n_0;
					bool _S22 = ray_triangle_intersect_0(ray_origin_0, ray_dir_0, vertices_0._data[uint(tri_0.i0_0)].position_1 + vertices_0._data[uint(tri_0.i0_0)].position_lo_1, vertices_0._data[uint(tri_0.i1_0)].position_1 + vertices_0._data[uint(tri_0.i1_0)].position_lo_1, vertices_0._data[uint(tri_0.i2_0)].position_1 + vertices_0._data[uint(tri_0.i2_0)].position_lo_1, t_1, n_0);

#line 223
					bool _S23;

#line 223
					if (_S22) {
#line 223
						_S23 = t_1 < hit_t_0;

#line 223
					} else {
#line 223
						_S23 = false;

#line 223
					}

#line 223
					if (_S23) {
#line 224
						hit_t_0 = t_1;
						hit_tri_0 = tri_idx_0;
						hit_normal_0 = n_0;

#line 223
					}

#line 214
					ti_0 = ti_0 + 1U;

#line 214
				}

#line 207
				ci_0 = ci_0 + 1U;

#line 207
			}

#line 201
		}

#line 233
		if ((t_max_0.x) < (t_max_0.y)) {
#line 234
			if ((t_max_0.x) < (t_max_0.z)) {
#line 235
				icell_0[0] = icell_0[0] + _S13;
				t_max_0[0] = t_max_0[0] + _S12.x;

#line 234
			} else {
				icell_0[2] = icell_0[2] + _S15;
				t_max_0[2] = t_max_0[2] + _S12.z;

#line 234
			}

#line 233
		} else {
#line 242
			if ((t_max_0.y) < (t_max_0.z)) {
#line 243
				icell_0[1] = icell_0[1] + _S14;
				t_max_0[1] = t_max_0[1] + _S12.y;

#line 242
			} else {
				icell_0[2] = icell_0[2] + _S15;
				t_max_0[2] = t_max_0[2] + _S12.z;

#line 242
			}

#line 233
		}

#line 250
		uint _S24 = iters_0 + 1U;

		if (hit_tri_0 != 4294967295U) {
			if (hit_t_0 < (min(min(t_max_0.x, t_max_0.y), t_max_0.z) / length(params_0.to_cell_size_0))) {
#line 257
				break;
			}

#line 253
		}

#line 253
		iters_0 = _S24;

#line 196
	}

#line 261
	return hit_tri_0 != 4294967295U;
}

#line 126
vec3 random_cosine_direction_0(vec3 normal_0, inout uint rng_1) {
#line 127
	vec3 dir_0 = random_sphere_direction_0(rng_1);
	return normalize(dir_0 + normal_0);
}

#line 93
shared float s_absorbed_0[9][64];

#line 94
shared float s_hits_0[64];

void atomicAddFloat_0(uint index_0, float value_0) {
#line 96
	uint expected_0 = output_accum_0._data[uint(index_0)];

	[[dont_unroll]]
	for (;;) {
		uint original_0 = atomicCompSwap(output_accum_0._data[uint(index_0)], expected_0, floatBitsToUint(uintBitsToFloat(expected_0) + value_0));
		if (original_0 == expected_0) {
#line 103
			break;
		}

#line 98
		expected_0 = original_0;

#line 98
	}

#line 106
	return;
}

#line 265
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
void main() {
#line 266
	uint probe_index_0 = gl_WorkGroupID.y;
	uint local_id_0 = gl_LocalInvocationID.x;
	uint ray_index_0 = params_0.ray_from_0 + gl_WorkGroupID.x * 64U + local_id_0;

	if (probe_index_0 >= (params_0.probe_count_0)) {
#line 271
		return;
	}

	vec3 position_2 = probe_positions_0._data[uint(probe_index_0)].position_0 + probe_positions_0._data[uint(probe_index_0)].position_lo_0;

	float local_absorbed_0[9];

#line 276
	uint b_0 = 0U;
	for (;;) {
#line 277
		if (b_0 < 9U) {
		} else {
#line 277
			break;
		}

#line 278
		local_absorbed_0[b_0] = 0.0;

#line 277
		b_0 = b_0 + 1U;

#line 277
	}

#line 277
	uint stride_0;

#line 277
	float local_hits_0;

#line 282
	if (ray_index_0 < (params_0.ray_to_0)) {
#line 283
		uint rng_2 = pcg_hash_0(probe_index_0 * 1337U + ray_index_0 * 7919U);
		vec3 _S25 = random_sphere_direction_0(rng_2);

#line 284
		vec3 ray_origin_1 = position_2;

#line 284
		vec3 ray_dir_1 = _S25;

#line 284
		stride_0 = 0U;

#line 284
		local_hits_0 = 0.0;

		for (;;) {
#line 287
			if (stride_0 < (params_0.max_bounces_0)) {
			} else {
#line 287
				break;
			}

#line 288
			float hit_t_1;
			uint hit_tri_1;
			vec3 hit_normal_1;
			bool _S26 = trace_ray_grid_0(ray_origin_1, ray_dir_1, hit_t_1, hit_tri_1, hit_normal_1);

#line 291
			if (!_S26) {
#line 292
				break;
			}
			Triangle_0 _S27 = triangles_0._data[uint(hit_tri_1)];

#line 294
			float max_alpha_0 = 0.0;

#line 294
			b_0 = 0U;

			for (;;) {
#line 296
				if (b_0 < 9U) {
				} else {
#line 296
					break;
				}

#line 296
				float alpha_0 = materials_0._data[uint(_S27.material_index_0)].coefficients_0[b_0];

				local_absorbed_0[b_0] = local_absorbed_0[b_0] + materials_0._data[uint(_S27.material_index_0)].coefficients_0[b_0];
				if ((materials_0._data[uint(_S27.material_index_0)].coefficients_0[b_0]) > max_alpha_0) {
#line 299
					max_alpha_0 = alpha_0;

#line 299
				}

#line 296
				b_0 = b_0 + 1U;

#line 296
			}

#line 301
			float local_hits_1 = local_hits_0 + 1.0;

			if (stride_0 > 4U) {
#line 305
				float _S28 = max(1.0 - max_alpha_0, 0.05000000074505806);
				float _S29 = rand_float_0(rng_2);

#line 306
				if (_S29 > _S28) {
#line 306
					local_hits_0 = local_hits_1;
					break;
				}

#line 304
			}

#line 310
			vec3 _S30 = ray_origin_1 + ray_dir_1 * hit_t_1 + hit_normal_1 * params_0.bias_0;
			vec3 _S31 = random_cosine_direction_0(hit_normal_1, rng_2);

#line 287
			uint _S32 = stride_0 + 1U;

#line 287
			ray_origin_1 = _S30;

#line 287
			ray_dir_1 = _S31;

#line 287
			stride_0 = _S32;

#line 287
			local_hits_0 = local_hits_1;

#line 287
		}

#line 282
	} else {
#line 282
		local_hits_0 = 0.0;

#line 282
	}

#line 282
	b_0 = 0U;

#line 316
	for (;;) {
#line 316
		if (b_0 < 9U) {
		} else {
#line 316
			break;
		}

#line 317
		s_absorbed_0[b_0][local_id_0] = local_absorbed_0[b_0];

#line 316
		b_0 = b_0 + 1U;

#line 316
	}

	s_hits_0[local_id_0] = local_hits_0;
	controlBarrier(gl_ScopeWorkgroup, gl_ScopeWorkgroup, gl_StorageSemanticsShared, gl_SemanticsAcquireRelease);

#line 319
	stride_0 = 32U;

	for (;;) {
#line 322
		if (stride_0 > 0U) {
		} else {
#line 322
			break;
		}

#line 323
		if (local_id_0 < stride_0) {
#line 323
			b_0 = 0U;
			for (;;) {
#line 324
				if (b_0 < 9U) {
				} else {
#line 324
					break;
				}

#line 325
				s_absorbed_0[b_0][local_id_0] = s_absorbed_0[b_0][local_id_0] + s_absorbed_0[b_0][local_id_0 + stride_0];

#line 324
				b_0 = b_0 + 1U;

#line 324
			}

			s_hits_0[local_id_0] = s_hits_0[local_id_0] + s_hits_0[local_id_0 + stride_0];

#line 323
		}

#line 328
		controlBarrier(gl_ScopeWorkgroup, gl_ScopeWorkgroup, gl_StorageSemanticsShared, gl_SemanticsAcquireRelease);

#line 322
		stride_0 = stride_0 >> 1;

#line 322
	}

#line 332
	if (local_id_0 == 0U) {
#line 333
		uint base_0 = probe_index_0 * 10U;

#line 333
		b_0 = 0U;
		for (;;) {
#line 334
			if (b_0 < 9U) {
			} else {
#line 334
				break;
			}

#line 335
			atomicAddFloat_0(base_0 + b_0, s_absorbed_0[b_0][0]);

#line 334
			b_0 = b_0 + 1U;

#line 334
		}

		atomicAddFloat_0(base_0 + 9U, s_hits_0[0]);

#line 332
	}

#line 338
	return;
}

/**************************************************************************/
/*  reverb_bake_cpu.cpp                                                   */
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

#include "slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif

#line 12 "E:/multiplayer-fabric-godot/servers/resonanceaudio/shaders/reverb_bake.slang"
struct BakeParams_0 {
	uint32_t ray_count_0;
	uint32_t max_bounces_0;
	uint32_t probe_count_0;
	uint32_t triangle_count_0;
	uint32_t ray_from_0;
	uint32_t ray_to_0;
	Vector<float, 3> to_cell_offset_0;
	int32_t grid_size_0;
	Vector<float, 3> to_cell_size_0;
	float bias_0;
};

#line 25
struct Vertex_0 {
	Vector<float, 3> position_0;
	Vector<float, 3> position_lo_0;
};

#line 30
struct Triangle_0 {
	uint32_t i0_0;
	uint32_t i1_0;
	uint32_t i2_0;
	uint32_t material_index_0;
};

#line 53
struct ProbePosition_0 {
	Vector<float, 3> position_1;
	float pad0_0;
	Vector<float, 3> position_lo_1;
	float pad1_0;
};

#line 37
struct MaterialAbsorption_0 {
	FixedArray<float, 9> coefficients_0;
};

#line 41
struct ClusterAABB_0 {
	Vector<float, 3> min_bounds_0;
	float pad0_1;
	Vector<float, 3> max_bounds_0;
	float pad1_1;
};

#line 48
struct ProbeAccum_0 {
	FixedArray<float, 9> absorbed_0;
	float total_hits_0;
};

#line 303
struct GlobalParams_0 {
	BakeParams_0 *params_0;
	StructuredBuffer<Vertex_0> vertices_0;
	StructuredBuffer<Triangle_0> triangles_0;
	StructuredBuffer<ProbePosition_0> probe_positions_0;
	StructuredBuffer<MaterialAbsorption_0> materials_0;
	StructuredBuffer<Vector<uint32_t, 2>> grid_data_0;
	StructuredBuffer<Vector<uint32_t, 2>> cluster_indices_0;
	StructuredBuffer<ClusterAABB_0> cluster_aabbs_0;
	StructuredBuffer<uint32_t> triangle_indices_0;
	RWStructuredBuffer<ProbeAccum_0> output_accum_0;
};

#line 303
struct KernelContext_0 {
	GlobalParams_0 *globalParams_0;
};

#line 9865 "hlsl.meta.slang"
static float dot_0(Vector<float, 3> x_0, Vector<float, 3> y_0) {
#line 9865
	int32_t i_0 = int(0);

#line 9865
	float result_0 = 0.0f;

#line 9884
	for (;;) {
#line 9884
		if (i_0 < int(3)) {
		} else {
#line 9884
			break;
		}

#line 9885
		float result_1 = result_0 + _slang_vector_get_element(x_0, i_0) * _slang_vector_get_element(y_0, i_0);

#line 9884
		i_0 = i_0 + int(1);

#line 9884
		result_0 = result_1;

#line 9884
	}

	return result_0;
}

#line 12120
static float length_0(Vector<float, 3> x_1) {
#line 12132
	return (F32_sqrt((dot_0(x_1, x_1))));
}

#line 9310
static Vector<float, 3> cross_0(Vector<float, 3> left_0, Vector<float, 3> right_0) {
#line 9324
	float _S1 = left_0.y;

#line 9324
	float _S2 = right_0.z;

#line 9324
	float _S3 = left_0.z;

#line 9324
	float _S4 = right_0.y;
	float _S5 = right_0.x;

#line 9325
	float _S6 = left_0.x;

#line 9323
	return Vector<float, 3>(_S1 * _S2 - _S3 * _S4, _S3 * _S5 - _S6 * _S2, _S6 * _S4 - _S1 * _S5);
}

#line 7476
static bool all_0(Vector<bool, 3> x_2) {
#line 7476
	bool result_2 = true;

#line 7476
	int32_t i_1 = int(0);

#line 7528
	for (;;) {
#line 7528
		if (i_1 < int(3)) {
		} else {
#line 7528
			break;
		}

#line 7529
		if (result_2) {
#line 7529
			result_2 = (bool((_slang_vector_get_element(x_2, i_1))));

#line 7529
		} else {
#line 7529
			result_2 = false;

#line 7529
		}

#line 7528
		i_1 = i_1 + int(1);

#line 7528
	}

	return result_2;
}

#line 12673
static Vector<float, 3> max_0(Vector<float, 3> x_3, Vector<float, 3> y_1) {
#line 7153
	Vector<float, 3> result_3;

#line 7153
	int32_t i_2 = int(0);

#line 7153
	for (;;) {
#line 7153
		if (i_2 < int(3)) {
		} else {
#line 7153
			break;
		}

#line 7153
		result_3[i_2] = (F32_max((_slang_vector_get_element(x_3, i_2)), (_slang_vector_get_element(y_1, i_2))));

#line 7153
		i_2 = i_2 + int(1);

#line 7153
	}

#line 7153
	return result_3;
}

#line 12930
static Vector<float, 3> min_0(Vector<float, 3> x_4, Vector<float, 3> y_2) {
#line 7153
	Vector<float, 3> result_4;

#line 7153
	int32_t i_3 = int(0);

#line 7153
	for (;;) {
#line 7153
		if (i_3 < int(3)) {
		} else {
#line 7153
			break;
		}

#line 7153
		result_4[i_3] = (F32_min((_slang_vector_get_element(x_4, i_3)), (_slang_vector_get_element(y_2, i_3))));

#line 7153
		i_3 = i_3 + int(1);

#line 7153
	}

#line 7153
	return result_4;
}

#line 7252
static Vector<float, 3> abs_0(Vector<float, 3> x_5) {
#line 7147
	Vector<float, 3> result_5;

#line 7147
	int32_t i_4 = int(0);

#line 7147
	for (;;) {
#line 7147
		if (i_4 < int(3)) {
		} else {
#line 7147
			break;
		}

#line 7147
		result_5[i_4] = (F32_abs((_slang_vector_get_element(x_5, i_4))));

#line 7147
		i_4 = i_4 + int(1);

#line 7147
	}

#line 7147
	return result_5;
}

#line 13723
static Vector<float, 3> normalize_0(Vector<float, 3> x_6) {
#line 13735
	return x_6 / (Vector<float, 3>)length_0(x_6);
}

#line 14518
static Vector<int32_t, 3> sign_0(Vector<float, 3> x_7) {
#line 7147
	Vector<int32_t, 3> result_6;

#line 7147
	int32_t i_5 = int(0);

#line 7147
	for (;;) {
#line 7147
		if (i_5 < int(3)) {
		} else {
#line 7147
			break;
		}

#line 7147
		result_6[i_5] = (F32_sign((_slang_vector_get_element(x_7, i_5))));

#line 7147
		i_5 = i_5 + int(1);

#line 7147
	}

#line 7147
	return result_6;
}

#line 10895
static Vector<float, 3> floor_0(Vector<float, 3> x_8) {
#line 7147
	Vector<float, 3> result_7;

#line 7147
	int32_t i_6 = int(0);

#line 7147
	for (;;) {
#line 7147
		if (i_6 < int(3)) {
		} else {
#line 7147
			break;
		}

#line 7147
		result_7[i_6] = (F32_floor((_slang_vector_get_element(x_8, i_6))));

#line 7147
		i_6 = i_6 + int(1);

#line 7147
	}

#line 7147
	return result_7;
}

#line 93 "E:/multiplayer-fabric-godot/servers/resonanceaudio/shaders/reverb_bake.slang"
static uint32_t pcg_hash_0(uint32_t state_0) {
#line 94
	uint32_t s_0 = state_0 * 747796405U + 2891336453U;
	uint32_t word_0 = ((s_0 >> ((s_0 >> 28U) + 4U)) ^ s_0) * 277803737U;
	return (word_0 >> 22U) ^ word_0;
}

static float rand_float_0(uint32_t *state_1) {
#line 100
	uint32_t _S7 = pcg_hash_0(*state_1);

#line 100
	*state_1 = _S7;
	return float(_S7) / 4.294967296e+09f;
}

static Vector<float, 3> random_sphere_direction_0(uint32_t *rng_0) {
#line 105
	float _S8 = rand_float_0(rng_0);

#line 105
	float z_0 = 2.0f * _S8 - 1.0f;
	float _S9 = rand_float_0(rng_0);

#line 106
	float phi_0 = 6.28318548202514648f * _S9;
	float r_0 = (F32_sqrt(((F32_max((0.0f), (1.0f - z_0 * z_0))))));
	return Vector<float, 3>(r_0 * (F32_cos((phi_0))), r_0 * (F32_sin((phi_0))), z_0);
}

#line 144
static bool ray_box_test_0(Vector<float, 3> origin_0, Vector<float, 3> inv_dir_0, Vector<float, 3> box_min_0, Vector<float, 3> box_max_0) {
#line 145
	Vector<float, 3> t0_0 = (box_min_0 - origin_0) * inv_dir_0;
	Vector<float, 3> t1_0 = (box_max_0 - origin_0) * inv_dir_0;
	Vector<float, 3> tmin_0 = min_0(t0_0, t1_0);
	Vector<float, 3> tmax_0 = max_0(t0_0, t1_0);

	float _S10 = (F32_min(((F32_min((tmax_0.x), (tmax_0.y)))), (tmax_0.z)));

#line 150
	bool _S11;
	if ((F32_max(((F32_max((tmin_0.x), (tmin_0.y)))), (tmin_0.z))) <= _S10) {
#line 151
		_S11 = _S10 >= 0.0f;

#line 151
	} else {
#line 151
		_S11 = false;

#line 151
	}

#line 151
	return _S11;
}

#line 116
static bool ray_triangle_intersect_0(Vector<float, 3> ro_0, Vector<float, 3> rd_0, Vector<float, 3> v0_0, Vector<float, 3> v1_0, Vector<float, 3> v2_0, float *t_0, Vector<float, 3> *normal_out_0) {
	*t_0 = 0.0f;
	*normal_out_0 = Vector<float, 3>(0.0f, 0.0f, 0.0f);
	Vector<float, 3> e1_0 = v1_0 - v0_0;
	Vector<float, 3> e2_0 = v2_0 - v0_0;
	Vector<float, 3> h_0 = cross_0(rd_0, e2_0);
	float a_0 = dot_0(e1_0, h_0);
	if ((F32_abs((a_0))) < 9.99999993922529029e-09f) {
#line 125
		return false;
	}

#line 126
	float f_0 = 1.0f / a_0;
	Vector<float, 3> s_1 = ro_0 - v0_0;
	float u_0 = f_0 * dot_0(s_1, h_0);

#line 128
	bool _S12;
	if (u_0 < 0.0f) {
#line 129
		_S12 = true;

#line 129
	} else {
#line 129
		_S12 = u_0 > 1.0f;

#line 129
	}

#line 129
	if (_S12) {
#line 130
		return false;
	}

#line 131
	Vector<float, 3> q_0 = cross_0(s_1, e1_0);
	float v_0 = f_0 * dot_0(rd_0, q_0);
	if (v_0 < 0.0f) {
#line 133
		_S12 = true;

#line 133
	} else {
#line 133
		_S12 = (u_0 + v_0) > 1.0f;

#line 133
	}

#line 133
	if (_S12) {
#line 134
		return false;
	}

#line 135
	float _S13 = f_0 * dot_0(e2_0, q_0);

#line 135
	*t_0 = _S13;
	if (_S13 < 0.00000999999974738f) {
#line 137
		return false;
	}

#line 138
	Vector<float, 3> _S14 = normalize_0(cross_0(e1_0, e2_0));

#line 138
	*normal_out_0 = _S14;
	if ((dot_0(_S14, rd_0)) > 0.0f) {
#line 140
		*normal_out_0 = -*normal_out_0;

#line 139
	}

	return true;
}

#line 155
static bool trace_ray_grid_0(Vector<float, 3> ray_origin_0, Vector<float, 3> ray_dir_0, float *hit_t_0, uint32_t *hit_tri_0, KernelContext_0 *kernelContext_0) {
#line 156
	*hit_t_0 = 1.00000001504746622e+30f;
	*hit_tri_0 = 4294967295U;

	Vector<float, 3> _S15 = (Vector<float, 3>)1.0f / ray_dir_0;
	int32_t gs_0 = kernelContext_0->globalParams_0->params_0->grid_size_0;

	Vector<float, 3> from_cell_0 = (ray_origin_0 - kernelContext_0->globalParams_0->params_0->to_cell_offset_0) * kernelContext_0->globalParams_0->params_0->to_cell_size_0;
	Vector<float, 3> _S16 = floor_0(from_cell_0);

#line 164
	Vector<int32_t, 3> _S17 = Vector<int32_t, 3>{ (int32_t)_slang_vector_get_element(_S16, 0), (int32_t)_slang_vector_get_element(_S16, 1), (int32_t)_slang_vector_get_element(_S16, 2) };

#line 164
	Vector<int32_t, 3> icell_0 = _S17;

	Vector<int32_t, 3> step_0 = sign_0(ray_dir_0);
	Vector<float, 3> dir_cell_0 = normalize_0(ray_dir_0 * ((Vector<float, 3>)1.0f / kernelContext_0->globalParams_0->params_0->to_cell_size_0));
	Vector<float, 3> _S18 = min_0(abs_0((Vector<float, 3>)1.0f / dir_cell_0), (Vector<float, 3>)float(gs_0));
	Vector<float, 3> _S19 = Vector<float, 3>{ (float)_slang_vector_get_element(_S17, 0), (float)_slang_vector_get_element(_S17, 1), (float)_slang_vector_get_element(_S17, 2) };

#line 170
	Vector<float, 3> _S20 = Vector<float, 3>{ (float)_slang_vector_get_element(step_0, 0), (float)_slang_vector_get_element(step_0, 1), (float)_slang_vector_get_element(step_0, 2) };
	Vector<float, 3> t_max_0 = (_S19 + max_0(Vector<float, 3>(0.0f, 0.0f, 0.0f), _S20) - from_cell_0) / dir_cell_0;

	int32_t _S21 = step_0.x;

#line 174
	if (_S21 == int(0)) {
#line 174
		t_max_0.x = 1.00000001504746622e+30f;

#line 174
	}
	int32_t _S22 = step_0.y;

#line 175
	if (_S22 == int(0)) {
#line 175
		t_max_0.y = 1.00000001504746622e+30f;

#line 175
	}
	int32_t _S23 = step_0.z;

#line 176
	if (_S23 == int(0)) {
#line 176
		t_max_0.z = 1.00000001504746622e+30f;

#line 176
	}

#line 176
	uint32_t iters_0 = 0U;

	for (;;) {
#line 179
		bool _S24;

#line 179
		if (all_0(icell_0 >= Vector<int32_t, 3>(int(0), int(0), int(0)))) {
#line 179
			_S24 = all_0(icell_0 < Vector<int32_t, 3>(gs_0, gs_0, gs_0));

#line 179
		} else {
#line 179
			_S24 = false;

#line 179
		}

#line 179
		bool _S25;

#line 179
		if (_S24) {
#line 179
			_S25 = iters_0 < 1000U;

#line 179
		} else {
#line 179
			_S25 = false;

#line 179
		}

#line 179
		if (_S25) {
		} else {
#line 179
			break;
		}
		Vector<uint32_t, 2> cell_data_0 = kernelContext_0->globalParams_0->grid_data_0.Load(uint32_t(icell_0.x + icell_0.y * gs_0 + icell_0.z * gs_0 * gs_0));
		uint32_t tri_count_0 = cell_data_0.x;

		if (tri_count_0 > 0U) {
#line 185
			uint32_t cell_solid_index_0 = cell_data_0.y;
			uint32_t _S26 = kernelContext_0->globalParams_0->cluster_indices_0.Load(cell_solid_index_0).x;
			uint32_t _S27 = kernelContext_0->globalParams_0->cluster_indices_0.Load(cell_solid_index_0).y;
			uint32_t _S28 = (tri_count_0 + 32U - 1U) / 32U;

#line 188
			uint32_t ci_0 = 0U;

			for (;;) {
#line 190
				if (ci_0 < _S28) {
				} else {
#line 190
					break;
				}

#line 191
				ClusterAABB_0 ca_0 = kernelContext_0->globalParams_0->cluster_aabbs_0.Load(_S26 + ci_0);
				if (!ray_box_test_0(ray_origin_0, _S15, ca_0.min_bounds_0, ca_0.max_bounds_0)) {
#line 193
					ci_0 = ci_0 + 1U;

#line 190
					continue;
				}

				uint32_t tri_base_0 = ci_0 * 32U;
				uint32_t _S29 = (U32_min((32U), (tri_count_0 - tri_base_0)));

#line 196
				uint32_t ti_0 = 0U;
				for (;;) {
#line 197
					if (ti_0 < _S29) {
					} else {
#line 197
						break;
					}

#line 198
					uint32_t tri_idx_0 = kernelContext_0->globalParams_0->triangle_indices_0.Load(_S27 + tri_base_0 + ti_0);
					Triangle_0 tri_0 = kernelContext_0->globalParams_0->triangles_0.Load(tri_idx_0);

#line 204
					float t_1;
					Vector<float, 3> n_0;
					bool _S30 = ray_triangle_intersect_0(ray_origin_0, ray_dir_0, kernelContext_0->globalParams_0->vertices_0.Load(tri_0.i0_0).position_0 + kernelContext_0->globalParams_0->vertices_0.Load(tri_0.i0_0).position_lo_0, kernelContext_0->globalParams_0->vertices_0.Load(tri_0.i1_0).position_0 + kernelContext_0->globalParams_0->vertices_0.Load(tri_0.i1_0).position_lo_0, kernelContext_0->globalParams_0->vertices_0.Load(tri_0.i2_0).position_0 + kernelContext_0->globalParams_0->vertices_0.Load(tri_0.i2_0).position_lo_0, &t_1, &n_0);

#line 206
					bool _S31;

#line 206
					if (_S30) {
#line 206
						_S31 = t_1 < (*hit_t_0);

#line 206
					} else {
#line 206
						_S31 = false;

#line 206
					}

#line 206
					if (_S31) {
#line 207
						*hit_t_0 = t_1;
						*hit_tri_0 = tri_idx_0;

#line 206
					}

#line 197
					ti_0 = ti_0 + 1U;

#line 197
				}

#line 190
				ci_0 = ci_0 + 1U;

#line 190
			}

#line 184
		}

#line 215
		if ((t_max_0.x) < (t_max_0.y)) {
#line 216
			if ((t_max_0.x) < (t_max_0.z)) {
#line 217
				icell_0.x = icell_0.x + _S21;
				t_max_0.x = t_max_0.x + _S18.x;

#line 216
			} else {
				icell_0.z = icell_0.z + _S23;
				t_max_0.z = t_max_0.z + _S18.z;

#line 216
			}

#line 215
		} else {
#line 224
			if ((t_max_0.y) < (t_max_0.z)) {
#line 225
				icell_0.y = icell_0.y + _S22;
				t_max_0.y = t_max_0.y + _S18.y;

#line 224
			} else {
				icell_0.z = icell_0.z + _S23;
				t_max_0.z = t_max_0.z + _S18.z;

#line 224
			}

#line 215
		}

#line 232
		uint32_t _S32 = iters_0 + 1U;

		if ((*hit_tri_0) != 4294967295U) {
			if ((*hit_t_0) < ((F32_min(((F32_min((t_max_0.x), (t_max_0.y)))), (t_max_0.z))) / length_0(kernelContext_0->globalParams_0->params_0->to_cell_size_0))) {
#line 239
				break;
			}

#line 235
		}

#line 235
		iters_0 = _S32;

#line 179
	}

#line 243
	return (*hit_tri_0) != 4294967295U;
}

#line 111
static Vector<float, 3> random_cosine_direction_0(Vector<float, 3> normal_0, uint32_t *rng_1) {
#line 112
	Vector<float, 3> dir_0 = random_sphere_direction_0(rng_1);
	return normalize_0(dir_0 + normal_0);
}

#line 247
void _main_0(void *_S33, void *entryPointParams_0, void *globalParams_1) {
#line 247
	ComputeThreadVaryingInput *_S34 = (slang_bit_cast<ComputeThreadVaryingInput *>(_S33));

#line 247
	KernelContext_0 kernelContext_1;

#line 247
	(&kernelContext_1)->globalParams_0 = (slang_bit_cast<GlobalParams_0 *>(globalParams_1));
	uint32_t probe_index_0 = (_S34->groupID * Vector<uint32_t, 3>(64U, 1U, 1U) + _S34->groupThreadID).x;
	if (probe_index_0 >= ((slang_bit_cast<GlobalParams_0 *>(globalParams_1))->params_0->probe_count_0)) {
#line 250
		return;
	}

	Vector<float, 3> _S35 = (&kernelContext_1)->globalParams_0->probe_positions_0.Load(probe_index_0).position_1 + (&kernelContext_1)->globalParams_0->probe_positions_0.Load(probe_index_0).position_lo_1;
	uint32_t rng_2 = pcg_hash_0(probe_index_0 * 1337U + (slang_bit_cast<GlobalParams_0 *>(globalParams_1))->params_0->ray_from_0 * 7919U);

	FixedArray<float, 9> local_absorbed_0;

#line 256
	uint32_t b_0 = 0U;
	for (;;) {
#line 257
		if (b_0 < 9U) {
		} else {
#line 257
			break;
		}

#line 258
		local_absorbed_0[b_0] = 0.0f;

#line 257
		b_0 = b_0 + 1U;

#line 257
	}

#line 257
	uint32_t ray_idx_0 = (slang_bit_cast<GlobalParams_0 *>(globalParams_1))->params_0->ray_from_0;

#line 257
	float local_hits_0 = 0.0f;

	for (;;) {
#line 261
		if (ray_idx_0 < ((slang_bit_cast<GlobalParams_0 *>(globalParams_1))->params_0->ray_to_0)) {
		} else {
#line 261
			break;
		}

#line 262
		Vector<float, 3> _S36 = random_sphere_direction_0(&rng_2);

#line 262
		Vector<float, 3> ray_origin_1 = _S35;

#line 262
		Vector<float, 3> ray_dir_1 = _S36;

#line 262
		uint32_t bounce_0 = 0U;

#line 262
		float local_hits_1 = local_hits_0;

		for (;;) {
#line 265
			if (bounce_0 < ((slang_bit_cast<GlobalParams_0 *>(globalParams_1))->params_0->max_bounces_0)) {
			} else {
#line 265
				local_hits_0 = local_hits_1;

#line 265
				break;
			}

#line 266
			float hit_t_1;
			uint32_t hit_tri_1;

#line 267
			bool _S37 = trace_ray_grid_0(ray_origin_1, ray_dir_1, &hit_t_1, &hit_tri_1, &kernelContext_1);
			if (!_S37) {
#line 268
				local_hits_0 = local_hits_1;
				break;
			}
			Triangle_0 _S38 = (&kernelContext_1)->globalParams_0->triangles_0.Load(hit_tri_1);

#line 271
			float max_alpha_0 = 0.0f;

#line 271
			b_0 = 0U;

			for (;;) {
#line 273
				if (b_0 < 9U) {
				} else {
#line 273
					break;
				}

#line 273
				float alpha_0 = (&((&kernelContext_1)->globalParams_0->materials_0)[_S38.material_index_0])->coefficients_0[b_0];

				local_absorbed_0[b_0] = local_absorbed_0[b_0] + (&((&kernelContext_1)->globalParams_0->materials_0)[_S38.material_index_0])->coefficients_0[b_0];
				if (((&((&kernelContext_1)->globalParams_0->materials_0)[_S38.material_index_0])->coefficients_0[b_0]) > max_alpha_0) {
#line 276
					max_alpha_0 = alpha_0;

#line 276
				}

#line 273
				b_0 = b_0 + 1U;

#line 273
			}

#line 278
			float local_hits_2 = local_hits_1 + 1.0f;

			if (bounce_0 > 4U) {
#line 283
				float _S39 = (F32_max((1.0f - max_alpha_0), (0.05000000074505806f)));
				float _S40 = rand_float_0(&rng_2);

#line 284
				if (_S40 > _S39) {
#line 284
					local_hits_0 = local_hits_2;
					break;
				}

#line 282
			}

#line 289
			Triangle_0 tri_1 = (&kernelContext_1)->globalParams_0->triangles_0.Load(hit_tri_1);
			Vector<float, 3> v0_1 = (&kernelContext_1)->globalParams_0->vertices_0.Load(tri_1.i0_0).position_0 + (&kernelContext_1)->globalParams_0->vertices_0.Load(tri_1.i0_0).position_lo_0;

			Vector<float, 3> hit_normal_0 = normalize_0(cross_0((&kernelContext_1)->globalParams_0->vertices_0.Load(tri_1.i1_0).position_0 + (&kernelContext_1)->globalParams_0->vertices_0.Load(tri_1.i1_0).position_lo_0 - v0_1, (&kernelContext_1)->globalParams_0->vertices_0.Load(tri_1.i2_0).position_0 + (&kernelContext_1)->globalParams_0->vertices_0.Load(tri_1.i2_0).position_lo_0 - v0_1));

#line 293
			Vector<float, 3> hit_normal_1;
			if ((dot_0(hit_normal_0, ray_dir_1)) > 0.0f) {
#line 294
				hit_normal_1 = -hit_normal_0;

#line 294
			} else {
#line 294
				hit_normal_1 = hit_normal_0;

#line 294
			}

			Vector<float, 3> _S41 = ray_origin_1 + ray_dir_1 * (Vector<float, 3>)hit_t_1 + hit_normal_1 * (Vector<float, 3>)(slang_bit_cast<GlobalParams_0 *>(globalParams_1))->params_0->bias_0;
			Vector<float, 3> _S42 = random_cosine_direction_0(hit_normal_1, &rng_2);

#line 265
			uint32_t _S43 = bounce_0 + 1U;

#line 265
			ray_origin_1 = _S41;

#line 265
			ray_dir_1 = _S42;

#line 265
			bounce_0 = _S43;

#line 265
			local_hits_1 = local_hits_2;

#line 265
		}

#line 261
		ray_idx_0 = ray_idx_0 + 1U;

#line 261
	}

#line 261
	b_0 = 0U;

#line 302
	for (;;) {
#line 302
		if (b_0 < 9U) {
		} else {
#line 302
			break;
		}

#line 303
		(&((&kernelContext_1)->globalParams_0->output_accum_0)[probe_index_0])->absorbed_0[b_0] = (&((&kernelContext_1)->globalParams_0->output_accum_0)[probe_index_0])->absorbed_0[b_0] + local_absorbed_0[b_0];

#line 302
		b_0 = b_0 + 1U;

#line 302
	}

	(&((&kernelContext_1)->globalParams_0->output_accum_0)[probe_index_0])->total_hits_0 = (&((&kernelContext_1)->globalParams_0->output_accum_0)[probe_index_0])->total_hits_0 + local_hits_0;
	return;
}

// [numthreads(64, 1, 1)]
SLANG_PRELUDE_EXPORT
void main_0_Thread(ComputeThreadVaryingInput *varyingInput, void *entryPointParams, void *globalParams) {
	_main_0(varyingInput, entryPointParams, globalParams);
}
// [numthreads(64, 1, 1)]
SLANG_PRELUDE_EXPORT
void main_0_Group(ComputeVaryingInput *varyingInput, void *entryPointParams, void *globalParams) {
	ComputeThreadVaryingInput threadInput = {};
	threadInput.groupID = varyingInput->startGroupID;
	for (uint32_t x = 0; x < 64; ++x) {
		threadInput.groupThreadID.x = x;
		_main_0(&threadInput, entryPointParams, globalParams);
	}
}
// [numthreads(64, 1, 1)]
SLANG_PRELUDE_EXPORT
void main_0(ComputeVaryingInput *varyingInput, void *entryPointParams, void *globalParams) {
	ComputeVaryingInput vi = *varyingInput;
	ComputeVaryingInput groupVaryingInput = {};
	for (uint32_t z = vi.startGroupID.z; z < vi.endGroupID.z; ++z) {
		groupVaryingInput.startGroupID.z = z;
		for (uint32_t y = vi.startGroupID.y; y < vi.endGroupID.y; ++y) {
			groupVaryingInput.startGroupID.y = y;
			for (uint32_t x = vi.startGroupID.x; x < vi.endGroupID.x; ++x) {
				groupVaryingInput.startGroupID.x = x;
				main_0_Group(&groupVaryingInput, entryPointParams, globalParams);
			}
		}
	}
}

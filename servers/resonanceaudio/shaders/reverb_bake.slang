// Reverb probe bake compute shader with grid acceleration.
// Per-probe ray tracing to estimate mean absorption per frequency band.
// Uses 3D grid + cluster AABB traversal (same as lightmapper_rd).
//
// Dispatch: groups_x = ceil(rays_per_batch / 64), groups_y = probe_count
// Each thread traces ONE ray for ONE probe, then workgroup reduces via
// shared memory and atomicAdds the partial sum to the global accumulator.
//
// Compile targets:
//   GPU: slangc reverb_bake.slang -target glsl -profile glsl_460 -entry main -stage compute -o reverb_bake.glsl
//   CPU: slangc reverb_bake.slang -target cpp -entry main -stage compute -o reverb_bake_cpu.cpp

static const uint NUM_BANDS = 9;
static const uint CLUSTER_SIZE = 32;
static const uint WORKGROUP_SIZE = 64;

struct BakeParams {
	uint ray_count;
	uint max_bounces;
	uint probe_count;
	uint triangle_count;
	uint ray_from;
	uint ray_to;
	float3 to_cell_offset;
	int grid_size;
	float3 to_cell_size;
	float bias;
};

struct Vertex {
	float3 position;
	float3 position_lo; // Double-float low bits for large-world precision.
};

struct Triangle {
	uint i0;
	uint i1;
	uint i2;
	uint material_index;
};

struct MaterialAbsorption {
	float coefficients[NUM_BANDS];
};

struct ClusterAABB {
	float3 min_bounds;
	float pad0;
	float3 max_bounds;
	float pad1;
};

struct ProbePosition {
	float3 position;
	float pad0;
	float3 position_lo;
	float pad1;
};

// Double-float arithmetic (matches Godot's scene_forward_clustered.glsl).
float3 df_two_sum(float3 a, float3 b, out float3 err) {
	float3 s = a + b;
	float3 v = s - a;
	err = (a - (s - v)) + (b - v);
	return s;
}

float3 df_add(float3 a_hi, float3 a_lo, float3 b_hi, float3 b_lo) {
	float3 se, te;
	float3 s = df_two_sum(a_hi, b_hi, se);
	float3 t = df_two_sum(a_lo, b_lo, te);
	se += t;
	float3 dummy;
	s = df_two_sum(s, se, dummy);
	return s;
}

float3 df_sub(float3 a_hi, float3 a_lo, float3 b_hi, float3 b_lo) {
	return df_add(a_hi, a_lo, -b_hi, -b_lo);
}

[[vk::binding(0, 0)]] ConstantBuffer<BakeParams> params;
[[vk::binding(1, 0)]] StructuredBuffer<Vertex> vertices;
[[vk::binding(2, 0)]] StructuredBuffer<Triangle> triangles;
[[vk::binding(3, 0)]] StructuredBuffer<ProbePosition> probe_positions;
[[vk::binding(4, 0)]] StructuredBuffer<MaterialAbsorption> materials;
[[vk::binding(5, 0)]] StructuredBuffer<uint2> grid_data;
[[vk::binding(6, 0)]] StructuredBuffer<uint2> cluster_indices;
[[vk::binding(7, 0)]] StructuredBuffer<ClusterAABB> cluster_aabbs;
[[vk::binding(8, 0)]] StructuredBuffer<uint> triangle_indices;
[[vk::binding(0, 1)]] RWStructuredBuffer<uint> output_accum;

groupshared float s_absorbed[NUM_BANDS][WORKGROUP_SIZE];
groupshared float s_hits[WORKGROUP_SIZE];

void atomicAddFloat(uint index, float value) {
	uint expected = output_accum[index];
	[loop] for (;;) {
		uint desired = asuint(asfloat(expected) + value);
		uint original;
		InterlockedCompareExchange(output_accum[index], expected, desired, original);
		if (original == expected)
			break;
		expected = original;
	}
}

uint pcg_hash(uint state) {
	uint s = state * 747796405u + 2891336453u;
	uint word = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
	return (word >> 22u) ^ word;
}

float rand_float(inout uint state) {
	state = pcg_hash(state);
	return float(state) / float(0xFFFFFFFFu);
}

float3 random_sphere_direction(inout uint rng) {
	float z = 2.0 * rand_float(rng) - 1.0;
	float phi = 2.0 * 3.14159265 * rand_float(rng);
	float r = sqrt(max(0.0, 1.0 - z * z));
	return float3(r * cos(phi), r * sin(phi), z);
}

float3 random_cosine_direction(float3 normal, inout uint rng) {
	float3 dir = random_sphere_direction(rng);
	return normalize(dir + normal);
}

bool ray_triangle_intersect(float3 ro, float3 rd, float3 v0, float3 v1, float3 v2,
		out float t, out float3 normal_out) {
	t = 0.0;
	normal_out = float3(0, 0, 0);
	float3 e1 = v1 - v0;
	float3 e2 = v2 - v0;
	float3 h = cross(rd, e2);
	float a = dot(e1, h);
	if (abs(a) < 1e-8)
		return false;
	float f = 1.0 / a;
	float3 s = ro - v0;
	float u = f * dot(s, h);
	if (u < 0.0 || u > 1.0)
		return false;
	float3 q = cross(s, e1);
	float v = f * dot(rd, q);
	if (v < 0.0 || u + v > 1.0)
		return false;
	t = f * dot(e2, q);
	if (t < 1e-5)
		return false;
	normal_out = normalize(cross(e1, e2));
	if (dot(normal_out, rd) > 0.0)
		normal_out = -normal_out;
	return true;
}

bool ray_box_test(float3 origin, float3 inv_dir, float3 box_min, float3 box_max) {
	float3 t0 = (box_min - origin) * inv_dir;
	float3 t1 = (box_max - origin) * inv_dir;
	float3 tmin = min(t0, t1);
	float3 tmax = max(t0, t1);
	float enter = max(max(tmin.x, tmin.y), tmin.z);
	float exit = min(min(tmax.x, tmax.y), tmax.z);
	return enter <= exit && exit >= 0.0;
}

// Trace a ray using DDA grid traversal with cluster AABBs.
// Returns hit normal directly to avoid redundant vertex re-reads in caller.
bool trace_ray_grid(float3 ray_origin, float3 ray_dir, out float hit_t, out uint hit_tri, out float3 hit_normal) {
	hit_t = 1e30;
	hit_tri = 0xFFFFFFFFu;
	hit_normal = float3(0, 0, 0);

	float3 inv_dir = 1.0 / ray_dir;
	int gs = params.grid_size;

	// Convert to cell coordinates.
	float3 from_cell = (ray_origin - params.to_cell_offset) * params.to_cell_size;
	int3 icell = int3(floor(from_cell));

	// DDA setup.
	int3 step = int3(sign(ray_dir));
	float3 dir_cell = normalize(ray_dir * (1.0 / params.to_cell_size));
	float3 delta = min(abs(1.0 / dir_cell), float(gs));
	float3 t_max_init = (float3(icell) + max(float3(0, 0, 0), float3(step)) - from_cell) / dir_cell;
	float3 t_max = t_max_init;

	// Avoid division by zero for axis-aligned rays.
	if (step.x == 0) t_max.x = 1e30;
	if (step.y == 0) t_max.y = 1e30;
	if (step.z == 0) t_max.z = 1e30;

	uint iters = 0;
	while (all(icell >= int3(0, 0, 0)) && all(icell < int3(gs, gs, gs)) && iters < 1000) {
		uint cell_idx = uint(icell.x + icell.y * gs + icell.z * gs * gs);
		uint2 cell_data = grid_data[cell_idx];
		uint tri_count = cell_data.x;

		if (tri_count > 0) {
			uint cell_solid_index = cell_data.y;
			uint cluster_start = cluster_indices[cell_solid_index].x;
			uint cell_tri_start = cluster_indices[cell_solid_index].y;
			uint cluster_count = (tri_count + CLUSTER_SIZE - 1) / CLUSTER_SIZE;

			for (uint ci = 0; ci < cluster_count; ci++) {
				ClusterAABB ca = cluster_aabbs[cluster_start + ci];
				if (!ray_box_test(ray_origin, inv_dir, ca.min_bounds, ca.max_bounds))
					continue;

				uint tri_base = ci * CLUSTER_SIZE;
				uint tri_test = min(CLUSTER_SIZE, tri_count - tri_base);
				for (uint ti = 0; ti < tri_test; ti++) {
					uint tri_idx = triangle_indices[cell_tri_start + tri_base + ti];
					Triangle tri = triangles[tri_idx];
					float3 v0 = vertices[tri.i0].position + vertices[tri.i0].position_lo;
					float3 v1 = vertices[tri.i1].position + vertices[tri.i1].position_lo;
					float3 v2 = vertices[tri.i2].position + vertices[tri.i2].position_lo;

					float t;
					float3 n;
					if (ray_triangle_intersect(ray_origin, ray_dir, v0, v1, v2, t, n) && t < hit_t) {
						hit_t = t;
						hit_tri = tri_idx;
						hit_normal = n;
					}
				}
			}
		}

		// DDA step.
		if (t_max.x < t_max.y) {
			if (t_max.x < t_max.z) {
				icell.x += step.x;
				t_max.x += delta.x;
			} else {
				icell.z += step.z;
				t_max.z += delta.z;
			}
		} else {
			if (t_max.y < t_max.z) {
				icell.y += step.y;
				t_max.y += delta.y;
			} else {
				icell.z += step.z;
				t_max.z += delta.z;
			}
		}
		iters++;

		// Early exit if we found a hit closer than the next cell boundary.
		if (hit_tri != 0xFFFFFFFFu) {
			float min_t_max = min(min(t_max.x, t_max.y), t_max.z);
			// Convert cell-space t to world-space.
			if (hit_t < min_t_max / length(params.to_cell_size))
				break;
		}
	}

	return hit_tri != 0xFFFFFFFFu;
}

[numthreads(WORKGROUP_SIZE, 1, 1)]
void main(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID) {
	uint probe_index = gid.y;
	uint local_id = gtid.x;
	uint ray_index = params.ray_from + gid.x * WORKGROUP_SIZE + local_id;

	if (probe_index >= params.probe_count)
		return;

	// Reconstruct probe position from hi+lo double-float pair.
	float3 position = probe_positions[probe_index].position + probe_positions[probe_index].position_lo;

	float local_absorbed[NUM_BANDS];
	for (uint b = 0; b < NUM_BANDS; b++)
		local_absorbed[b] = 0.0;
	float local_hits = 0.0;

	// Each thread traces exactly one ray (all bounces).
	if (ray_index < params.ray_to) {
		uint rng = pcg_hash(probe_index * 1337u + ray_index * 7919u);
		float3 ray_dir = random_sphere_direction(rng);
		float3 ray_origin = position;

		for (uint bounce = 0; bounce < params.max_bounces; bounce++) {
			float hit_t;
			uint hit_tri;
			float3 hit_normal;
			if (!trace_ray_grid(ray_origin, ray_dir, hit_t, hit_tri, hit_normal))
				break;

			uint mat_idx = triangles[hit_tri].material_index;
			float max_alpha = 0.0;
			for (uint b = 0; b < NUM_BANDS; b++) {
				float alpha = materials[mat_idx].coefficients[b];
				local_absorbed[b] += alpha;
				if (alpha > max_alpha) max_alpha = alpha;
			}
			local_hits += 1.0;

			// Russian roulette: after 5 bounces, probabilistically terminate.
			if (bounce > 4) {
				float survive = max(1.0 - max_alpha, 0.05);
				if (rand_float(rng) > survive)
					break;
			}

			ray_origin = ray_origin + ray_dir * hit_t + hit_normal * params.bias;
			ray_dir = random_cosine_direction(hit_normal, rng);
		}
	}

	// Store per-thread results in shared memory for reduction.
	for (uint b = 0; b < NUM_BANDS; b++)
		s_absorbed[b][local_id] = local_absorbed[b];
	s_hits[local_id] = local_hits;
	GroupMemoryBarrierWithGroupSync();

	// Parallel reduction across workgroup.
	for (uint stride = WORKGROUP_SIZE / 2; stride > 0; stride >>= 1) {
		if (local_id < stride) {
			for (uint b = 0; b < NUM_BANDS; b++)
				s_absorbed[b][local_id] += s_absorbed[b][local_id + stride];
			s_hits[local_id] += s_hits[local_id + stride];
		}
		GroupMemoryBarrierWithGroupSync();
	}

	// Thread 0 atomically adds the workgroup's partial sum to global output.
	if (local_id == 0) {
		uint base = probe_index * (NUM_BANDS + 1);
		for (uint b = 0; b < NUM_BANDS; b++)
			atomicAddFloat(base + b, s_absorbed[b][0]);
		atomicAddFloat(base + NUM_BANDS, s_hits[0]);
	}
}

/**************************************************************************/
/*  cassie_curvenet_extractor.cpp                                         */
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

#include "cassie_curvenet_extractor.h"

#include "../curves/cassie_curve_fit.h"
#include "cassie_final_stroke.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/pair.h"
#include "core/variant/typed_array.h"

#include <cstdint>

namespace {

// Pack (v0, v1) with v0 < v1 into a single uint64 key for hashing.
inline uint64_t edge_key(int p_a, int p_b) {
	const uint32_t lo = uint32_t(p_a < p_b ? p_a : p_b);
	const uint32_t hi = uint32_t(p_a < p_b ? p_b : p_a);
	return (uint64_t(hi) << 32) | uint64_t(lo);
}

// Per-edge metadata: vertex pair, score, indices of up to two incident
// triangles (-1 if absent — boundary edges have one).
struct EdgeRecord {
	int v0;
	int v1;
	int tri_a;
	int tri_b;
	real_t score;
};

// Comparator for LocalVector::sort_custom — descending by score so the
// top-K prefix is the highest-scoring edges. LocalVector's Iterator
// doesn't support operator- so std::sort isn't usable here.
struct EdgeScoreDesc {
	bool operator()(const EdgeRecord &p_a, const EdgeRecord &p_b) const {
		return p_a.score > p_b.score;
	}
};

// Walks a feature-graph chain starting from p_start, leaving through
// p_first_neighbor. Advances while the visited vertex has feature-valence
// 2. Marks each edge it crosses in p_visited_edges. Returns the chain as a
// vertex-index sequence.
PackedInt32Array walk_chain_from(int p_start, int p_first_neighbor,
		const LocalVector<LocalVector<int>> &p_feature_neighbors,
		HashMap<uint64_t, bool> &p_visited_edges) {
	PackedInt32Array chain;
	chain.push_back(p_start);
	chain.push_back(p_first_neighbor);
	p_visited_edges[edge_key(p_start, p_first_neighbor)] = true;
	int prev = p_start;
	int curr = p_first_neighbor;
	const int kMaxChainLen = 1 << 24; // runaway guard
	while (chain.size() < kMaxChainLen) {
		const LocalVector<int> &nbrs = p_feature_neighbors[curr];
		if (nbrs.size() != 2) {
			break; // hit an intersection or dangling vertex — chain ends here
		}
		const int next = (nbrs[0] == prev) ? nbrs[1] : nbrs[0];
		const uint64_t e = edge_key(curr, next);
		if (p_visited_edges.has(e)) {
			break;
		}
		p_visited_edges[e] = true;
		chain.push_back(next);
		prev = curr;
		curr = next;
	}
	return chain;
}

// Walks a closed cycle (every vertex has valence 2) starting from
// p_start. Returns the chain with first vertex == last vertex.
PackedInt32Array walk_cycle_from(int p_start,
		const LocalVector<LocalVector<int>> &p_feature_neighbors,
		HashMap<uint64_t, bool> &p_visited_edges) {
	PackedInt32Array chain;
	chain.push_back(p_start);
	int prev = -1;
	int curr = p_start;
	const int kMaxChainLen = 1 << 24;
	while (chain.size() < kMaxChainLen) {
		const LocalVector<int> &nbrs = p_feature_neighbors[curr];
		if (nbrs.size() != 2) {
			break; // not a pure cycle — bail
		}
		const int next = (prev < 0)
				? nbrs[0]
				: (nbrs[0] == prev ? nbrs[1] : nbrs[0]);
		const uint64_t e = edge_key(curr, next);
		if (p_visited_edges.has(e)) {
			break;
		}
		p_visited_edges[e] = true;
		chain.push_back(next);
		prev = curr;
		curr = next;
		if (curr == p_start) {
			break;
		}
	}
	return chain;
}

// Builds the feature-graph adjacency given a list of edges and the top-K
// kept count. Returns the per-vertex neighbor list.
LocalVector<LocalVector<int>> build_feature_neighbors(int p_vertex_count,
		const LocalVector<EdgeRecord> &p_sorted_edges, int p_keep_count) {
	LocalVector<LocalVector<int>> neighbors;
	neighbors.resize(p_vertex_count);
	const int keep = MIN(p_keep_count, int(p_sorted_edges.size()));
	for (int i = 0; i < keep; ++i) {
		const EdgeRecord &e = p_sorted_edges[i];
		neighbors[e.v0].push_back(e.v1);
		neighbors[e.v1].push_back(e.v0);
	}
	return neighbors;
}

// Counts the chains the simplification pass would produce for a given
// feature-graph. Same logic as the real extraction pass but skips the
// per-chain curve fit — used by the adaptive-threshold binary search.
int count_chains(const LocalVector<LocalVector<int>> &p_feature_neighbors) {
	HashMap<uint64_t, bool> visited;
	int count = 0;
	// Pass A — chains anchored at non-valence-2 vertices.
	for (uint32_t v = 0; v < p_feature_neighbors.size(); ++v) {
		const LocalVector<int> &nbrs = p_feature_neighbors[v];
		if (nbrs.size() == 2 || nbrs.is_empty()) {
			continue;
		}
		for (uint32_t j = 0; j < nbrs.size(); ++j) {
			const int u = nbrs[j];
			const uint64_t e = edge_key(int(v), u);
			if (visited.has(e)) {
				continue;
			}
			PackedInt32Array c = walk_chain_from(int(v), u,
					p_feature_neighbors, visited);
			if (c.size() >= 2) {
				++count;
			}
		}
	}
	// Pass B — pure cycles (every vertex valence 2).
	for (uint32_t v = 0; v < p_feature_neighbors.size(); ++v) {
		const LocalVector<int> &nbrs = p_feature_neighbors[v];
		if (nbrs.size() != 2) {
			continue;
		}
		const uint64_t e = edge_key(int(v), nbrs[0]);
		if (visited.has(e)) {
			continue;
		}
		PackedInt32Array c = walk_cycle_from(int(v),
				p_feature_neighbors, visited);
		if (c.size() >= 3) {
			++count;
		}
	}
	return count;
}

} // namespace

Dictionary CassieCurvenetExtractor::extract_graph_data(
		const Ref<CassieSurfacePatch> &p_patch,
		int p_target_curve_count, float p_rdp_error, float p_fit_error,
		float p_curvature_weight) {
	Dictionary out;
	TypedArray<CassieFinalStroke> empty_curves;
	out["curves"] = empty_curves;
	out["nodes"] = Array();
	if (p_patch.is_null() || p_patch->get_triangle_count() == 0) {
		return out;
	}

	const int tri_count = p_patch->get_triangle_count();
	const int vert_count = p_patch->get_vertex_count();
	if (vert_count == 0) {
		return out;
	}

	// ── Pass 1 — accumulate edge → triangle incidences ─────────────────────
	HashMap<uint64_t, Pair<int, int>> edge_tris; // key → (tri_a, tri_b), tri_b = -1 if boundary
	edge_tris.reserve(tri_count * 3);
	for (int t = 0; t < tri_count; ++t) {
		const Vector3i tri = p_patch->get_triangle_indices(t);
		const int verts[3] = { tri.x, tri.y, tri.z };
		for (int e = 0; e < 3; ++e) {
			const int a = verts[e];
			const int b = verts[(e + 1) % 3];
			if (a == b) {
				continue;
			}
			const uint64_t k = edge_key(a, b);
			HashMap<uint64_t, Pair<int, int>>::Iterator it = edge_tris.find(k);
			if (it == edge_tris.end()) {
				edge_tris.insert(k, Pair<int, int>(t, -1));
			} else {
				if (it->value.second < 0) {
					it->value.second = t;
				}
				// > 2 incident triangles (non-manifold) — keep first two.
			}
		}
	}

	// ── Pass 1b (ENG-56) — per-vertex discrete mean curvature ────────────
	// Cotangent Laplacian of vertex positions: Δp_i = Σ_j (cot α + cot β)
	// (p_j - p_i). Magnitude approximates mean curvature × area; we use it
	// as a relative ridge signal when p_curvature_weight > 0.
	LocalVector<Vector3> vert_laplacian;
	LocalVector<real_t> vert_curvature;
	if (p_curvature_weight > real_t(0.0)) {
		vert_laplacian.resize(vert_count);
		for (int v = 0; v < vert_count; ++v) {
			vert_laplacian[v] = Vector3();
		}
		auto cot_at = [](const Vector3 &p, const Vector3 &q, const Vector3 &r) -> real_t {
			const Vector3 u = q - p;
			const Vector3 v = r - p;
			const real_t dot = u.dot(v);
			const real_t cross = u.cross(v).length();
			return cross > real_t(1e-12) ? dot / cross : real_t(0.0);
		};
		for (int t = 0; t < tri_count; ++t) {
			const Vector3i tri = p_patch->get_triangle_indices(t);
			if (tri.x < 0) {
				continue;
			}
			const Vector3 a = p_patch->get_vertex_position(tri.x);
			const Vector3 b = p_patch->get_vertex_position(tri.y);
			const Vector3 c = p_patch->get_vertex_position(tri.z);
			// Half-cot weight on each edge from the opposite-angle cotangent.
			const real_t half_cot_a = real_t(0.5) * cot_at(a, b, c);
			const real_t half_cot_b = real_t(0.5) * cot_at(b, c, a);
			const real_t half_cot_c = real_t(0.5) * cot_at(c, a, b);
			vert_laplacian[tri.y] += half_cot_a * (c - b);
			vert_laplacian[tri.z] += half_cot_a * (b - c);
			vert_laplacian[tri.z] += half_cot_b * (a - c);
			vert_laplacian[tri.x] += half_cot_b * (c - a);
			vert_laplacian[tri.x] += half_cot_c * (b - a);
			vert_laplacian[tri.y] += half_cot_c * (a - b);
		}
		vert_curvature.resize(vert_count);
		for (int v = 0; v < vert_count; ++v) {
			vert_curvature[v] = vert_laplacian[v].length();
		}
	}

	// ── Pass 2 — score each edge ───────────────────────────────────────────
	// Minimum score below which an edge is never kept regardless of K.
	// 15° dihedral — keeps real ridges, drops scanner ripple.
	const real_t kMinDihedral = real_t(Math::PI) * real_t(15.0 / 180.0);
	LocalVector<EdgeRecord> edges;
	edges.reserve(edge_tris.size());
	for (const KeyValue<uint64_t, Pair<int, int>> &kv : edge_tris) {
		const uint64_t k = kv.key;
		const int v0 = int(uint32_t(k & 0xFFFFFFFFULL));
		const int v1 = int(uint32_t((k >> 32) & 0xFFFFFFFFULL));
		const int ta = kv.value.first;
		const int tb = kv.value.second;
		real_t score;
		if (tb < 0) {
			// Boundary edge — always wins.
			score = real_t(Math::PI);
		} else {
			const Vector3 na = p_patch->get_triangle_normal(ta);
			const Vector3 nb = p_patch->get_triangle_normal(tb);
			const real_t dot = CLAMP(na.dot(nb), real_t(-1.0), real_t(1.0));
			score = Math::acos(dot);
		}
		// Blend in the curvature signal when requested. Boundaries already
		// max out — the curvature lift only matters on interior edges.
		if (p_curvature_weight > real_t(0.0) && tb >= 0) {
			const real_t k_v0 = vert_curvature[v0];
			const real_t k_v1 = vert_curvature[v1];
			score += real_t(p_curvature_weight) * (k_v0 + k_v1) * real_t(0.5);
		}
		if (score < kMinDihedral) {
			continue;
		}
		EdgeRecord rec{ v0, v1, ta, tb, score };
		edges.push_back(rec);
	}
	if (edges.is_empty()) {
		return out; // smooth mesh, no boundary — empty curvenet
	}

	// Sort descending by score so the top-K subset is just the prefix.
	edges.sort_custom<EdgeScoreDesc>();

	// ── Pass 3 — adaptive thresholding (log-sweep over K) ──────────────────
	// Try K at 1×, 2×, ... up to the full candidate count plus the boundary
	// K = target_count. Pick K whose chain count is closest to target.
	const int total = int(edges.size());
	const int target = MAX(1, p_target_curve_count);
	int best_k = total;
	int best_diff = INT32_MAX;
	int best_count = 0;

	LocalVector<int> k_candidates;
	for (int k = 1; k <= total; k = MAX(k + 1, k * 2)) {
		k_candidates.push_back(k);
	}
	if (k_candidates.is_empty() || k_candidates[k_candidates.size() - 1] != total) {
		k_candidates.push_back(total);
	}
	// Always evaluate target as a candidate so target≈result is reachable.
	if (target <= total) {
		k_candidates.push_back(target);
	}

	for (uint32_t ki = 0; ki < k_candidates.size(); ++ki) {
		const int k = k_candidates[ki];
		LocalVector<LocalVector<int>> nbrs = build_feature_neighbors(vert_count, edges, k);
		const int count = count_chains(nbrs);
		const int diff = Math::abs(count - target);
		// Prefer counts ≥ target slightly over ≪ target so a 0-curve answer
		// doesn't win on smooth meshes when the user clearly asked for more.
		// But when ALL candidates undershoot, we still pick the closest.
		if (diff < best_diff || (diff == best_diff && count > best_count)) {
			best_diff = diff;
			best_k = k;
			best_count = count;
		}
	}

	// ── Pass 4 — build the chosen feature graph, walk chains, fit curves ──
	LocalVector<LocalVector<int>> feature_neighbors = build_feature_neighbors(
			vert_count, edges, best_k);

	// Walk chains identical to count_chains() but actually emit them.
	HashMap<uint64_t, bool> visited_edges;
	LocalVector<PackedInt32Array> chains;
	for (int v = 0; v < vert_count; ++v) {
		const LocalVector<int> &nbrs = feature_neighbors[v];
		if (nbrs.size() == 2 || nbrs.is_empty()) {
			continue;
		}
		for (uint32_t j = 0; j < nbrs.size(); ++j) {
			const int u = nbrs[j];
			const uint64_t e = edge_key(v, u);
			if (visited_edges.has(e)) {
				continue;
			}
			PackedInt32Array c = walk_chain_from(v, u,
					feature_neighbors, visited_edges);
			if (c.size() >= 2) {
				chains.push_back(c);
			}
		}
	}
	for (int v = 0; v < vert_count; ++v) {
		const LocalVector<int> &nbrs = feature_neighbors[v];
		if (nbrs.size() != 2) {
			continue;
		}
		const uint64_t e = edge_key(v, nbrs[0]);
		if (visited_edges.has(e)) {
			continue;
		}
		PackedInt32Array c = walk_cycle_from(v, feature_neighbors, visited_edges);
		if (c.size() >= 3) {
			chains.push_back(c);
		}
	}

	// Convert chains → CassieFinalStroke + collect intersection vertices.
	// chain_vertex_to_node_idx[v] = the node index that vertex v maps to,
	// only populated when v is a chain endpoint (intersection or dangling).
	HashMap<int, int> chain_vertex_to_node_idx;
	TypedArray<CassieFinalStroke> out_curves;
	Array out_nodes;
	for (uint32_t ci = 0; ci < chains.size(); ++ci) {
		const PackedInt32Array &chain = chains[ci];
		if (chain.size() < 2) {
			continue;
		}
		PackedVector3Array points;
		points.resize(chain.size());
		for (int i = 0; i < chain.size(); ++i) {
			points.write[i] = p_patch->get_vertex_position(chain[i]);
		}

		Ref<Curve3D> curve;
		if (points.size() == 2) {
			curve = cassie_fit_line(points[0], points[1]);
		} else {
			curve = cassie_fit_curve(points, p_fit_error, p_rdp_error);
			if (curve.is_null()) {
				// Fallback: degenerate fit — emit a straight line through
				// the endpoints rather than dropping the chain.
				curve = cassie_fit_line(points[0], points[points.size() - 1]);
			}
		}
		if (curve.is_null()) {
			continue;
		}

		const int curve_id = int(out_curves.size());
		Ref<CassieFinalStroke> stroke;
		stroke.instantiate();
		stroke->set_id(curve_id);
		const bool closed = (chain[0] == chain[chain.size() - 1]);
		stroke->set_curve(curve, closed);
		stroke->set_input_samples(points);
		out_curves.push_back(stroke);

		// Endpoint book-keeping. Pre-create node records for first/last chain
		// vertex and append this curve_id.
		const int v_start = chain[0];
		const int v_end = chain[chain.size() - 1];
		const int endpoints[2] = { v_start, v_end };
		for (int ei = 0; ei < 2; ++ei) {
			const int v = endpoints[ei];
			HashMap<int, int>::Iterator it = chain_vertex_to_node_idx.find(v);
			int node_idx;
			if (it == chain_vertex_to_node_idx.end()) {
				Dictionary node;
				node["id"] = int(out_nodes.size());
				node["position"] = p_patch->get_vertex_position(v);
				node["incident_curve_ids"] = PackedInt32Array();
				node_idx = int(out_nodes.size());
				out_nodes.push_back(node);
				chain_vertex_to_node_idx[v] = node_idx;
			} else {
				node_idx = it->value;
			}
			Dictionary node = out_nodes[node_idx];
			PackedInt32Array incident = node["incident_curve_ids"];
			incident.push_back(curve_id);
			node["incident_curve_ids"] = incident;
			out_nodes[node_idx] = node;
		}
	}

	out["curves"] = out_curves;
	out["nodes"] = out_nodes;
	return out;
}

Ref<CassieCurvenet> CassieCurvenetExtractor::extract(
		const Ref<CassieSurfacePatch> &p_patch,
		int p_target_curve_count, float p_rdp_error, float p_fit_error,
		float p_curvature_weight) {
	Ref<CassieCurvenet> cn;
	cn.instantiate();
	const Dictionary graph_data = extract_graph_data(p_patch,
			p_target_curve_count, p_rdp_error, p_fit_error,
			p_curvature_weight);
	cn->build_from_graph(graph_data);
	cn->set_bound_patch(p_patch);
	if (p_patch.is_valid()) {
		cn->update_rest_pose(p_patch);
		cn->compute_orientations();
	}
	return cn;
}

void CassieCurvenetExtractor::_bind_methods() {
	ClassDB::bind_static_method("CassieCurvenetExtractor",
			D_METHOD("extract_graph_data", "patch", "target_curve_count",
					"rdp_error", "fit_error", "curvature_weight"),
			&CassieCurvenetExtractor::extract_graph_data,
			DEFVAL(200), DEFVAL(1e-3f), DEFVAL(1e-2f), DEFVAL(0.0f));
	ClassDB::bind_static_method("CassieCurvenetExtractor",
			D_METHOD("extract", "patch", "target_curve_count",
					"rdp_error", "fit_error", "curvature_weight"),
			&CassieCurvenetExtractor::extract,
			DEFVAL(200), DEFVAL(1e-3f), DEFVAL(1e-2f), DEFVAL(0.0f));
}

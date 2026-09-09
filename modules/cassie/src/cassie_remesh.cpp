/**************************************************************************/
/*  cassie_remesh.cpp                                                     */
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

#include "cassie_remesh.h"

#include "core/math/aabb.h"
#include "core/math/dynamic_bvh.h"
#include "core/math/vector2.h"
#include "core/os/os.h"
#include "core/templates/local_vector.h"

#include <algorithm>
#include <cfloat> // FLT_MAX
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
CassieRefineProfile g_refine_profile;
}

const CassieRefineProfile &cassie_refine_last_profile() {
	return g_refine_profile;
}

namespace {

// ── Internal mesh representation ─────────────────────────────────────────

struct CMVertex {
	Vector3 pos;
	bool on_boundary = false;
};

struct CMTri {
	int v[3];
	bool valid = true;
};

static uint64_t edge_key(int a, int b) {
	if (a > b) {
		std::swap(a, b);
	}
	return (uint64_t(unsigned(a)) << 32) | uint64_t(unsigned(b));
}

struct CMMesh {
	std::vector<CMVertex> verts;
	std::vector<CMTri> tris;
	std::unordered_map<uint64_t, std::vector<int>> edge_tris;

	void rebuild_adjacency() {
		edge_tris.clear();
		for (int t = 0, n = int(tris.size()); t < n; ++t) {
			if (!tris[t].valid) {
				continue;
			}
			for (int e = 0; e < 3; ++e) {
				int a = tris[t].v[e];
				int b = tris[t].v[(e + 1) % 3];
				edge_tris[edge_key(a, b)].push_back(t);
			}
		}
	}

	void detect_boundary() {
		for (auto &v : verts) {
			v.on_boundary = false;
		}
		for (auto &[key, ts] : edge_tris) {
			if (ts.size() == 1) {
				int a = int(key >> 32);
				int b = int(key & 0xFFFFFFFFu);
				if (a < int(verts.size())) {
					verts[a].on_boundary = true;
				}
				if (b < int(verts.size())) {
					verts[b].on_boundary = true;
				}
			}
		}
	}
};

// ── Reference-mesh projection ─────────────────────────────────────────────
// Closest point on triangle abc to query point p.
// Ericson, "Real-Time Collision Detection", p. 141.
static Vector3 closest_point_on_triangle(Vector3 p, Vector3 a, Vector3 b, Vector3 c) {
	Vector3 ab = b - a, ac = c - a, ap = p - a;
	float d1 = ab.dot(ap), d2 = ac.dot(ap);
	if (d1 <= 0.0f && d2 <= 0.0f) {
		return a;
	}
	Vector3 bp = p - b;
	float d3 = ab.dot(bp), d4 = ac.dot(bp);
	if (d3 >= 0.0f && d4 <= d3) {
		return b;
	}
	float vc = d1 * d4 - d3 * d2;
	if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
		return a + ab * (d1 / (d1 - d3));
	}
	Vector3 cp = p - c;
	float d5 = ab.dot(cp), d6 = ac.dot(cp);
	if (d6 >= 0.0f && d5 <= d6) {
		return c;
	}
	float vb = d5 * d2 - d1 * d6;
	if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
		return a + ac * (d2 / (d2 - d6));
	}
	float va = d3 * d6 - d5 * d4;
	if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
		float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		return b + (c - b) * w;
	}
	float denom = 1.0f / (va + vb + vc);
	float v = vb * denom;
	float w = vc * denom;
	return a + ab * v + ac * w;
}

// DynamicBVH-accelerated nearest-surface query.
// Build once per refine_patch call, query O(log n) per vertex.
struct RefMeshBVH {
	DynamicBVH bvh;
	const std::vector<CMVertex> *verts = nullptr;
	const std::vector<CMTri> *tris = nullptr;
	float search_radius = 0.1f; // initial AABB half-extent for query

	void build(const std::vector<CMVertex> &rv, const std::vector<CMTri> &rt) {
		verts = &rv;
		tris = &rt;
		// Estimate search radius from mean triangle edge length.
		float total_len = 0.0f;
		int edge_count = 0;
		for (const CMTri &t : rt) {
			if (!t.valid) {
				continue;
			}
			for (int e = 0; e < 3; ++e) {
				total_len += rv[t.v[e]].pos.distance_to(rv[t.v[(e + 1) % 3]].pos);
				++edge_count;
			}
		}
		search_radius = edge_count > 0 ? (total_len / edge_count) * 2.0f : 0.1f;

		for (int i = 0; i < int(rt.size()); ++i) {
			if (!rt[i].valid) {
				continue;
			}
			Vector3 a = rv[rt[i].v[0]].pos;
			Vector3 b = rv[rt[i].v[1]].pos;
			Vector3 c = rv[rt[i].v[2]].pos;
			AABB box;
			box.position = Vector3(MIN(a.x, MIN(b.x, c.x)),
					MIN(a.y, MIN(b.y, c.y)),
					MIN(a.z, MIN(b.z, c.z)));
			Vector3 mx(MAX(a.x, MAX(b.x, c.x)),
					MAX(a.y, MAX(b.y, c.y)),
					MAX(a.z, MAX(b.z, c.z)));
			box.size = (mx - box.position).maxf(1e-4f); // avoid zero-size AABB
			bvh.insert(box, reinterpret_cast<void *>(uintptr_t(i)));
		}
	}

	Vector3 nearest(Vector3 query) const {
		// Functor: accumulate best candidate across BVH leaves.
		struct Collector {
			const RefMeshBVH *self;
			Vector3 query;
			Vector3 best;
			float best_d;
			bool operator()(void *p_data) {
				int ti = int(reinterpret_cast<uintptr_t>(p_data));
				const CMTri &t = self->tris->at(ti);
				if (!t.valid) {
					return true;
				}
				Vector3 p = closest_point_on_triangle(query,
						self->verts->at(t.v[0]).pos,
						self->verts->at(t.v[1]).pos,
						self->verts->at(t.v[2]).pos);
				float d = query.distance_squared_to(p);
				if (d < best_d) {
					best_d = d;
					best = p;
				}
				return true; // keep visiting
			}
		};
		Collector c{ this, query, query, FLT_MAX };
		// Expand search AABB until we get at least one candidate.
		float r = search_radius;
		for (int attempt = 0; attempt < 6 && c.best_d == FLT_MAX; ++attempt, r *= 4.0f) {
			AABB box(query - Vector3(r, r, r), Vector3(r, r, r) * 2.0f);
			const_cast<DynamicBVH &>(bvh).aabb_query(box, c);
		}
		return c.best;
	}
};

// ── Compact — remove orphaned vertices after collapse ─────────────────────

static void compact(CMMesh &m) {
	std::vector<bool> used(m.verts.size(), false);
	for (auto &t : m.tris) {
		if (!t.valid) {
			continue;
		}
		used[t.v[0]] = true;
		used[t.v[1]] = true;
		used[t.v[2]] = true;
	}
	std::vector<int> remap(m.verts.size(), -1);
	std::vector<CMVertex> new_verts;
	new_verts.reserve(m.verts.size());
	for (int i = 0; i < int(m.verts.size()); ++i) {
		if (used[i]) {
			remap[i] = int(new_verts.size());
			new_verts.push_back(m.verts[i]);
		}
	}
	std::vector<CMTri> new_tris;
	new_tris.reserve(m.tris.size());
	for (auto &t : m.tris) {
		if (!t.valid) {
			continue;
		}
		CMTri nt = { { remap[t.v[0]], remap[t.v[1]], remap[t.v[2]] }, true };
		new_tris.push_back(nt);
	}
	m.verts = std::move(new_verts);
	m.tris = std::move(new_tris);
}

// ── Edge split ────────────────────────────────────────────────────────────

// Split edge (va,vb) and update m.edge_tris incrementally — O(1) amortized.
// This avoids the full rebuild_adjacency that would otherwise be needed after
// each split to keep triangle-index references current.
//
// Old triangles (before split):
//   t0 = (va, vb, vc0)   t1 = (va, vb, vc1)  [t1 optional, -1 = boundary]
// New triangles (after split, vm = midpoint):
//   t0        → (va, vm, vc0)        new_from_t0 → (vm, vb, vc0)
//   t1        → (vb, vm, vc1)        new_from_t1 → (vm, va, vc1)
static int split_edge(CMMesh &m, int va, int vb, int t0, int t1,
		const RefMeshBVH *ref_bvh) {
	CMVertex mid;
	mid.pos = (m.verts[va].pos + m.verts[vb].pos) * 0.5f;
	// Boundary iff the edge itself is a boundary edge (only 1 adj. triangle).
	// Using endpoint on_boundary flags is wrong: a diagonal of the initial quad
	// has boundary-vertex endpoints but is an interior edge.
	mid.on_boundary = (t1 < 0);
	if (ref_bvh && !mid.on_boundary) {
		mid.pos = ref_bvh->nearest(mid.pos);
	}
	const int vm = int(m.verts.size());
	m.verts.push_back(mid);

	auto opposite_vertex = [&](int t) -> int {
		const int *v = m.tris[t].v;
		for (int k = 0; k < 3; ++k) {
			if (v[k] != va && v[k] != vb) {
				return v[k];
			}
		}
		return -1;
	};

	// Capture apex vertices BEFORE modifying any triangle.
	const int vc0 = opposite_vertex(t0);
	const int vc1 = (t1 >= 0) ? opposite_vertex(t1) : -1;

	// New triangle indices (computed before any push_back).
	const int new_from_t0 = int(m.tris.size());
	const int new_from_t1 = (t1 >= 0) ? int(m.tris.size()) + 1 : -1;

	// Modify triangles.
	m.tris[t0] = { { va, vm, vc0 }, true };
	m.tris.push_back({ { vm, vb, vc0 }, true }); // index = new_from_t0
	if (t1 >= 0) {
		m.tris[t1] = { { vb, vm, vc1 }, true };
		m.tris.push_back({ { vm, va, vc1 }, true }); // index = new_from_t1
	}

	// ── Incremental edge_tris update ────────────────────────────────────────
	// Helper: replace one triangle index inside an edge's list.
	auto replace_tri = [&](int a, int b, int from, int to) {
		auto it = m.edge_tris.find(edge_key(a, b));
		if (it == m.edge_tris.end()) {
			return;
		}
		for (auto &ti : it->second) {
			if (ti == from) {
				ti = to;
				return;
			}
		}
	};

	// Remove the split edge.
	m.edge_tris.erase(edge_key(va, vb));

	// (vb,vc0): was in t0, now in new_from_t0.
	if (vc0 >= 0) {
		replace_tri(vb, vc0, t0, new_from_t0);
	}
	// (vc0,va): still in t0 — reference unchanged. ✓
	// New edge (vm,vc0): in t0 and new_from_t0.
	if (vc0 >= 0) {
		m.edge_tris[edge_key(vm, vc0)] = { t0, new_from_t0 };
	}

	if (t1 >= 0) {
		// (va,vm): in t0 and new_from_t1.
		m.edge_tris[edge_key(va, vm)] = { t0, new_from_t1 };
		// (vm,vb): in new_from_t0 and t1.
		m.edge_tris[edge_key(vm, vb)] = { new_from_t0, t1 };
		// (vc1,va): was in t1, now in new_from_t1.
		if (vc1 >= 0) {
			replace_tri(vc1, va, t1, new_from_t1);
		}
		// (vb,vc1): still in t1 — reference unchanged. ✓
		// New edge (vm,vc1): in t1 and new_from_t1.
		if (vc1 >= 0) {
			m.edge_tris[edge_key(vm, vc1)] = { t1, new_from_t1 };
		}
	} else {
		// Boundary split: (va,vm) only in t0, (vm,vb) only in new_from_t0.
		m.edge_tris[edge_key(va, vm)] = { t0 };
		m.edge_tris[edge_key(vm, vb)] = { new_from_t0 };
	}

	return vm;
}

static void split_long_edges(CMMesh &m, float hi, const RefMeshBVH *ref_bvh) {
	struct Candidate {
		uint64_t key;
		float len_sq;
	};
	std::vector<Candidate> cands;
	const float hi_sq = hi * hi;
	for (auto &[key, ts] : m.edge_tris) {
		const int va = int(key >> 32), vb = int(key & 0xFFFFFFFFu);
		if (va >= int(m.verts.size()) || vb >= int(m.verts.size())) {
			continue;
		}
		const float lsq = m.verts[va].pos.distance_squared_to(m.verts[vb].pos);
		if (lsq > hi_sq) {
			cands.push_back({ key, lsq });
		}
	}
	std::sort(cands.begin(), cands.end(),
			[](const Candidate &a, const Candidate &b) { return a.len_sq > b.len_sq; });

	for (auto &c : cands) {
		// Fresh lookup: get the CURRENT t0/t1 from edge_tris, which has been
		// kept consistent by the incremental updates in split_edge.
		auto it = m.edge_tris.find(c.key);
		if (it == m.edge_tris.end()) {
			continue; // edge was removed by a prior split (shouldn't happen, but safe)
		}
		const int va = int(c.key >> 32), vb = int(c.key & 0xFFFFFFFFu);
		const int t0 = it->second[0];
		const int t1 = it->second.size() > 1 ? it->second[1] : -1;
		split_edge(m, va, vb, t0, t1, ref_bvh);
		// edge_tris is now consistent — no full rebuild needed.
	}
}

// ── Edge collapse ─────────────────────────────────────────────────────────
// Vertex one-ring index: maps each vertex to the set of its neighbors.
// Built once per collapse pass from m.edge_tris, then updated incrementally.
using VertexRings = std::unordered_map<int, std::unordered_set<int>>;

static VertexRings build_vertex_rings(const CMMesh &m) {
	VertexRings rings;
	for (auto &[key, ts] : m.edge_tris) {
		int a = int(key >> 32), b = int(key & 0xFFFFFFFFu);
		rings[a].insert(b);
		rings[b].insert(a);
	}
	return rings;
}

// Link condition via pre-built rings: O(min(deg_a, deg_b)) per call.
// For interior edges (the only kind we collapse) manifold safety requires
// exactly 2 common neighbors — the two apex vertices.
static bool can_collapse_with_rings(int va, int vb, const VertexRings &rings) {
	auto ra = rings.find(va);
	auto rb = rings.find(vb);
	if (ra == rings.end() || rb == rings.end()) {
		return false;
	}
	// Iterate the smaller ring for speed.
	const auto &small = ra->second.size() <= rb->second.size() ? ra->second : rb->second;
	const auto &large = ra->second.size() <= rb->second.size() ? rb->second : ra->second;
	int common = 0;
	for (int v : small) {
		if (v != va && v != vb && large.count(v)) {
			++common;
		}
	}
	return common == 2;
}

// Merge vb into va in the ring index: O(deg_vb) per collapse.
static void update_rings_after_collapse(VertexRings &rings, int va, int vb) {
	auto rb_it = rings.find(vb);
	if (rb_it == rings.end()) {
		return;
	}
	auto &ring_a = rings[va];
	for (int nb : rb_it->second) {
		if (nb == va) {
			continue;
		}
		auto nb_it = rings.find(nb);
		if (nb_it != rings.end()) {
			nb_it->second.erase(vb);
			nb_it->second.insert(va);
		}
		ring_a.insert(nb);
	}
	ring_a.erase(va); // no self-loop
	ring_a.erase(vb); // vb is absorbed
	rings.erase(vb);
}

static void collapse_short_edges(CMMesh &m, float lo) {
	// Build rings once from the current adjacency — O(E).
	VertexRings vrings = build_vertex_rings(m);

	struct Candidate {
		int va, vb;
		float len_sq;
	};
	const float lo_sq = lo * lo;
	std::vector<Candidate> cands;
	for (auto &[key, ts] : m.edge_tris) {
		const int va = int(key >> 32), vb = int(key & 0xFFFFFFFFu);
		if (va >= int(m.verts.size()) || vb >= int(m.verts.size())) {
			continue;
		}
		if (m.verts[va].on_boundary || m.verts[vb].on_boundary) {
			continue;
		}
		const float lsq = m.verts[va].pos.distance_squared_to(m.verts[vb].pos);
		if (lsq < lo_sq) {
			cands.push_back({ va, vb, lsq });
		}
	}
	std::sort(cands.begin(), cands.end(),
			[](const Candidate &a, const Candidate &b) { return a.len_sq < b.len_sq; });

	for (auto &c : cands) {
		// vb absent from rings means it was already merged by a prior collapse.
		if (!vrings.count(c.vb)) {
			continue;
		}
		if (m.verts[c.va].on_boundary || m.verts[c.vb].on_boundary) {
			continue;
		}
		// Re-check length after prior collapses may have moved va.
		const float lsq = m.verts[c.va].pos.distance_squared_to(m.verts[c.vb].pos);
		if (lsq >= lo_sq) {
			continue;
		}
		// Link condition via rings — no adjacency rebuild needed.
		if (!can_collapse_with_rings(c.va, c.vb, vrings)) {
			continue;
		}
		// Merge vb into va at the midpoint.
		m.verts[c.va].pos = (m.verts[c.va].pos + m.verts[c.vb].pos) * 0.5f;
		for (auto &t : m.tris) {
			if (!t.valid) {
				continue;
			}
			for (int k = 0; k < 3; ++k) {
				if (t.v[k] == c.vb) {
					t.v[k] = c.va;
				}
			}
			if (t.v[0] == t.v[1] || t.v[1] == t.v[2] || t.v[0] == t.v[2]) {
				t.valid = false;
			}
		}
		// Update rings incrementally — O(deg_vb), no full rebuild.
		update_rings_after_collapse(vrings, c.va, c.vb);
	}
	compact(m);
}

// ── Edge flip (Delaunay criterion) ────────────────────────────────────────

static bool in_circumcircle(Vector3 a, Vector3 b, Vector3 c, Vector3 p) {
	Vector3 n = (b - a).cross(c - a);
	const float nl = n.length();
	if (nl < 1e-10f) {
		return false;
	}
	n /= nl;
	auto proj = [&](Vector3 v) -> Vector2 {
		Vector3 u = (b - a).normalized();
		Vector3 w = n.cross(u);
		Vector3 r = v - a;
		return Vector2(r.dot(u), r.dot(w));
	};
	Vector2 A = proj(a), B = proj(b), C = proj(c), P = proj(p);
	float ax = A.x - P.x, ay = A.y - P.y;
	float bx = B.x - P.x, by = B.y - P.y;
	float cx = C.x - P.x, cy = C.y - P.y;
	float det = ax * (by * (cx * cx + cy * cy) - cy * (bx * bx + by * by)) -
			ay * (bx * (cx * cx + cy * cy) - cx * (bx * bx + by * by)) +
			(ax * ax + ay * ay) * (bx * cy - by * cx);
	return det > 0.0f;
}

// Out-parameters populated only when caller (cassie_remesh) is profiling.
// Avoids touching g_refine_profile from inside the hot loop on the no-profile
// path. The profile-on path pays a few atomic-style stores; the no-profile
// path is unchanged.
struct FlipCounters {
	int guard_iters = 0;
	int flips_committed = 0;
	int circumcircle_calls = 0;
};

static void flip_edges(CMMesh &m, FlipCounters *p_counters = nullptr) {
	// Caller has already rebuilt adjacency; skip the redundant first rebuild.
	// Iterate a flat LocalVector snapshot of (key, t0, t1) instead of
	// edge_tris directly — cache-friendly, no per-edge hash lookup. The
	// snapshot is rebuilt at the top of each pass; m.tris mutations during
	// the pass are tolerated by the existing opposite()/bounds checks.
	// ENG-87: ~70 % of refine wall time was here; this kills the hash-map
	// iteration cost and removes the redundant first/last rebuilds.
	struct EdgeRec {
		int va, vb, t0, t1;
	};
	LocalVector<EdgeRec> edges;
	for (int guard = 0; guard < 20; ++guard) {
		if (p_counters) {
			p_counters->guard_iters++;
		}
		edges.clear();
		edges.reserve(m.edge_tris.size());
		for (auto &[key, ts] : m.edge_tris) {
			if (ts.size() != 2) {
				continue;
			}
			EdgeRec r;
			r.va = int(key >> 32);
			r.vb = int(key & 0xFFFFFFFFu);
			r.t0 = ts[0];
			r.t1 = ts[1];
			edges.push_back(r);
		}
		bool flipped = false;
		const int nv = int(m.verts.size());
		const int nt = int(m.tris.size());
		for (uint32_t i = 0; i < edges.size(); ++i) {
			const EdgeRec &e = edges[i];
			if (e.va >= nv || e.vb >= nv || e.t0 >= nt || e.t1 >= nt) {
				continue;
			}
			auto opposite = [&](int t) -> int {
				for (int k = 0; k < 3; ++k) {
					if (m.tris[t].v[k] != e.va && m.tris[t].v[k] != e.vb) {
						return m.tris[t].v[k];
					}
				}
				return -1;
			};
			const int vc = opposite(e.t0);
			const int vd = opposite(e.t1);
			if (vc < 0 || vd < 0 || vc >= nv || vd >= nv) {
				continue;
			}
			if (p_counters) {
				p_counters->circumcircle_calls++;
			}
			if (in_circumcircle(m.verts[e.va].pos, m.verts[e.vb].pos,
						m.verts[vc].pos, m.verts[vd].pos)) {
				m.tris[e.t0] = { { e.va, vc, vd }, true };
				m.tris[e.t1] = { { e.vb, vd, vc }, true };
				flipped = true;
				if (p_counters) {
					p_counters->flips_committed++;
				}
			}
		}
		if (!flipped) {
			break;
		}
		// Only rebuild when a subsequent pass might still happen. The final
		// rebuild after the no-op pass is skipped — caller (cassie_remesh)
		// rebuilds before the next sub-stage anyway.
		if (guard + 1 < 20) {
			m.rebuild_adjacency();
		}
	}
}

// ── Tangential Laplacian smoothing ────────────────────────────────────────

static void smooth_vertices(CMMesh &m, const RefMeshBVH *ref_bvh) {
	const int nv = int(m.verts.size());
	std::vector<std::vector<int>> ring(nv);
	for (auto &tri : m.tris) {
		if (!tri.valid) {
			continue;
		}
		for (int k = 0; k < 3; ++k) {
			ring[tri.v[k]].push_back(tri.v[(k + 1) % 3]);
			ring[tri.v[k]].push_back(tri.v[(k + 2) % 3]);
		}
	}
	std::vector<Vector3> vnorm(nv, Vector3());
	for (auto &tri : m.tris) {
		if (!tri.valid) {
			continue;
		}
		Vector3 fn = (m.verts[tri.v[1]].pos - m.verts[tri.v[0]].pos)
							 .cross(m.verts[tri.v[2]].pos - m.verts[tri.v[0]].pos);
		vnorm[tri.v[0]] += fn;
		vnorm[tri.v[1]] += fn;
		vnorm[tri.v[2]] += fn;
	}
	std::vector<Vector3> new_pos(nv);
	for (int i = 0; i < nv; ++i) {
		new_pos[i] = m.verts[i].pos;
		if (m.verts[i].on_boundary || ring[i].empty()) {
			continue;
		}
		std::sort(ring[i].begin(), ring[i].end());
		ring[i].erase(std::unique(ring[i].begin(), ring[i].end()), ring[i].end());
		Vector3 centroid;
		for (int nb : ring[i]) {
			centroid += m.verts[nb].pos;
		}
		centroid /= float(ring[i].size());
		Vector3 disp = centroid - m.verts[i].pos;
		Vector3 n = vnorm[i];
		const float nl = n.length();
		if (nl > 1e-10f) {
			n /= nl;
			disp -= n * disp.dot(n);
		}
		new_pos[i] = m.verts[i].pos + disp;
	}
	for (int i = 0; i < nv; ++i) {
		m.verts[i].pos = new_pos[i];
	}
	// Re-project interior vertices back onto the original surface.
	if (ref_bvh) {
		for (int i = 0; i < nv; ++i) {
			if (!m.verts[i].on_boundary) {
				m.verts[i].pos = ref_bvh->nearest(m.verts[i].pos);
			}
		}
	}
}

// ── Convert PackedArray ↔ CMMesh ──────────────────────────────────────────

static CMMesh from_packed(const PackedVector3Array &verts, const PackedInt32Array &idx) {
	CMMesh m;
	m.verts.resize(verts.size());
	for (int i = 0; i < verts.size(); ++i) {
		m.verts[i].pos = verts[i];
	}
	const int nf = idx.size() / 3;
	m.tris.resize(nf);
	for (int i = 0; i < nf; ++i) {
		m.tris[i] = { { idx[i * 3], idx[i * 3 + 1], idx[i * 3 + 2] }, true };
	}
	return m;
}

static void to_packed(const CMMesh &m, PackedVector3Array &out_verts, PackedInt32Array &out_idx) {
	out_verts.resize(int(m.verts.size()));
	for (int i = 0; i < int(m.verts.size()); ++i) {
		out_verts.set(i, m.verts[i].pos);
	}
	out_idx.clear();
	for (auto &t : m.tris) {
		if (!t.valid) {
			continue;
		}
		out_idx.push_back(t.v[0]);
		out_idx.push_back(t.v[1]);
		out_idx.push_back(t.v[2]);
	}
}

} // namespace

// ── Public entry point ────────────────────────────────────────────────────

void cassie_remesh(PackedVector3Array &p_verts, PackedInt32Array &p_indices,
		float p_target_edge_length, int p_iterations,
		const PackedVector3Array &p_ref_verts,
		const PackedInt32Array &p_ref_indices) {
	if (p_verts.size() < 3 || p_indices.size() < 3) {
		return;
	}
	// ENG-87 sub-stage probe — env-gated, zero overhead when unset.
	const bool profile =
			OS::get_singleton() != nullptr &&
			OS::get_singleton()->has_environment("CASSIE_REFINE_PROFILE");
	if (profile) {
		g_refine_profile = CassieRefineProfile{};
	}
	auto stamp_us = [](const std::chrono::steady_clock::time_point &t0) {
		const auto t1 = std::chrono::steady_clock::now();
		return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
				t1 - t0)
						.count());
	};

	CMMesh m = from_packed(p_verts, p_indices);

	{
		const auto t0 = profile ? std::chrono::steady_clock::now()
								: std::chrono::steady_clock::time_point{};
		m.rebuild_adjacency();
		m.detect_boundary();
		if (profile) {
			g_refine_profile.adjacency_us += stamp_us(t0);
		}
	}

	// Build DynamicBVH over the reference DMWT surface (O(n log n) once).
	// All per-vertex projection queries during split and smooth are then O(log n).
	const bool has_ref = p_ref_verts.size() >= 3 && p_ref_indices.size() >= 3;
	CMMesh ref;
	RefMeshBVH ref_bvh;
	if (has_ref) {
		const auto t0 = profile ? std::chrono::steady_clock::now()
								: std::chrono::steady_clock::time_point{};
		ref = from_packed(p_ref_verts, p_ref_indices);
		ref_bvh.build(ref.verts, ref.tris);
		if (profile) {
			g_refine_profile.bvh_build_us += stamp_us(t0);
		}
	}
	const RefMeshBVH *bvh_ptr = has_ref ? &ref_bvh : nullptr;

	const float hi = (4.0f / 3.0f) * p_target_edge_length;
	const float lo = (4.0f / 5.0f) * p_target_edge_length;

	for (int iter = 0; iter < p_iterations; ++iter) {
		if (profile) {
			g_refine_profile.iterations++;
		}
		{
			const auto t0 = profile ? std::chrono::steady_clock::now()
									: std::chrono::steady_clock::time_point{};
			split_long_edges(m, hi, bvh_ptr);
			if (profile) {
				g_refine_profile.split_us += stamp_us(t0);
			}
		}
		{
			const auto t0 = profile ? std::chrono::steady_clock::now()
									: std::chrono::steady_clock::time_point{};
			m.rebuild_adjacency();
			m.detect_boundary();
			if (profile) {
				g_refine_profile.adjacency_us += stamp_us(t0);
			}
		}
		{
			const auto t0 = profile ? std::chrono::steady_clock::now()
									: std::chrono::steady_clock::time_point{};
			collapse_short_edges(m, lo);
			if (profile) {
				g_refine_profile.collapse_us += stamp_us(t0);
			}
		}
		{
			const auto t0 = profile ? std::chrono::steady_clock::now()
									: std::chrono::steady_clock::time_point{};
			m.rebuild_adjacency();
			m.detect_boundary();
			if (profile) {
				g_refine_profile.adjacency_us += stamp_us(t0);
			}
		}
		{
			const auto t0 = profile ? std::chrono::steady_clock::now()
									: std::chrono::steady_clock::time_point{};
			FlipCounters fc;
			flip_edges(m, profile ? &fc : nullptr);
			const uint64_t flip_dt = profile ? stamp_us(t0) : 0;
			if (profile) {
				g_refine_profile.flip_us += flip_dt;
				if (iter < 3) {
					g_refine_profile.flip_us_iter[iter] += flip_dt;
				}
				g_refine_profile.flip_calls++;
				g_refine_profile.flip_guard_iters += fc.guard_iters;
				g_refine_profile.flips_committed += fc.flips_committed;
				g_refine_profile.circumcircle_calls += fc.circumcircle_calls;
			}
		}
		{
			const auto t0 = profile ? std::chrono::steady_clock::now()
									: std::chrono::steady_clock::time_point{};
			m.rebuild_adjacency();
			m.detect_boundary();
			if (profile) {
				g_refine_profile.adjacency_us += stamp_us(t0);
			}
		}
		{
			const auto t0 = profile ? std::chrono::steady_clock::now()
									: std::chrono::steady_clock::time_point{};
			smooth_vertices(m, bvh_ptr);
			if (profile) {
				g_refine_profile.smooth_us += stamp_us(t0);
			}
		}
	}

	to_packed(m, p_verts, p_indices);
}

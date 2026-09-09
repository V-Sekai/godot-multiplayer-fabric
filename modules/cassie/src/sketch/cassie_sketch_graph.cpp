/**************************************************************************/
/*  cassie_sketch_graph.cpp                                               */
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

#include "cassie_sketch_graph.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/templates/local_vector.h"
#include "core/templates/sort_array.h"

void CassieSketchGraphNode::add_edge_id(int p_edge_id) {
	edge_ids.push_back(p_edge_id);
}

void CassieSketchGraphNode::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_id"), &CassieSketchGraphNode::get_id);
	ClassDB::bind_method(D_METHOD("get_position"), &CassieSketchGraphNode::get_position);
	ClassDB::bind_method(D_METHOD("get_normal"), &CassieSketchGraphNode::get_normal);
	ClassDB::bind_method(D_METHOD("get_edge_ids"), &CassieSketchGraphNode::get_edge_ids);
	ClassDB::bind_method(D_METHOD("get_degree"), &CassieSketchGraphNode::get_degree);
	ClassDB::bind_method(D_METHOD("get_is_sharp"), &CassieSketchGraphNode::get_is_sharp);
}

int CassieSketchGraphEdge::get_opposite(int node_id) const {
	return node_id == node_a_id ? node_b_id : node_a_id;
}

Vector3 CassieSketchGraphEdge::get_tangent_away_from(int node_id) const {
	const int n = points.size();
	if (n < 2) {
		return Vector3();
	}
	if (node_id == node_a_id) {
		return (points[1] - points[0]).normalized();
	}
	return (points[n - 2] - points[n - 1]).normalized();
}

void CassieSketchGraphEdge::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_id"), &CassieSketchGraphEdge::get_id);
	ClassDB::bind_method(D_METHOD("get_points"), &CassieSketchGraphEdge::get_points);
	ClassDB::bind_method(D_METHOD("get_normals"), &CassieSketchGraphEdge::get_normals);
	ClassDB::bind_method(D_METHOD("get_node_a_id"), &CassieSketchGraphEdge::get_node_a_id);
	ClassDB::bind_method(D_METHOD("get_node_b_id"), &CassieSketchGraphEdge::get_node_b_id);
	ClassDB::bind_method(D_METHOD("get_opposite", "node_id"),
			&CassieSketchGraphEdge::get_opposite);
	ClassDB::bind_method(D_METHOD("get_source_polyline_idx"),
			&CassieSketchGraphEdge::get_source_polyline_idx);
}

// ── Graph implementation ────────────────────────────────────────────────────

void CassieSketchGraph::clear() {
	nodes.clear();
	edges.clear();
	next_node_id = 0;
	next_edge_id = 0;
}

int CassieSketchGraph::_find_or_create_node(const Vector3 &p_pos,
		const Vector3 &p_normal) {
	// Linear scan over existing nodes within merge_epsilon. For the
	// modest node counts the editing demo produces (low hundreds) this
	// is fine; the GDScript scaffold has a spatial hash but the C++
	// path stays simple until profiling motivates one.
	for (const KeyValue<int, Ref<CassieSketchGraphNode>> &e : nodes) {
		if (e.value.is_valid() &&
				p_pos.distance_to(e.value->get_position()) <= merge_epsilon) {
			return e.key;
		}
	}
	const int id = next_node_id++;
	Ref<CassieSketchGraphNode> node;
	node.instantiate();
	node->set_id(id);
	node->set_position(p_pos);
	Vector3 n = p_normal;
	if (n.length_squared() > real_t(1e-20)) {
		n = n.normalized();
	} else {
		n = Vector3(0, 1, 0);
	}
	node->set_normal(n);
	nodes.insert(id, node);
	return id;
}

void CassieSketchGraph::_update_node_normal(int p_node_id) {
	HashMap<int, Ref<CassieSketchGraphNode>>::Iterator it = nodes.find(p_node_id);
	if (!it) {
		return;
	}
	Ref<CassieSketchGraphNode> node = it->value;
	const PackedInt32Array eids = node->get_edge_ids();
	// Average per-edge stored normal if any edge supplies one (drawn-with-
	// normals path).
	Vector3 acc;
	for (int i = 0; i < eids.size(); ++i) {
		HashMap<int, Ref<CassieSketchGraphEdge>>::Iterator eit = edges.find(eids[i]);
		if (!eit || eit->value.is_null()) {
			continue;
		}
		const PackedVector3Array nrms = eit->value->get_normals();
		if (nrms.is_empty()) {
			continue;
		}
		acc += eit->value->get_node_a_id() == p_node_id
				? nrms[0]
				: nrms[nrms.size() - 1];
	}
	if (acc.length_squared() > real_t(1e-10)) {
		node->set_normal(acc.normalized());
		return;
	}
	// Fallback for the raw-input path (no per-edge normals): best-fit plane
	// normal from the incident edge tangents. The tangent plane at a
	// curve-network vertex spans the plane that contains all incident
	// edges' departing tangents; its normal is what `_next_edge_at`
	// needs for a coherent angular sort. Yu 2021 §5.1 ("we obtain this
	// normal estimate at each intersection by assuming that the
	// intersecting curves lie on a smooth surface, whose tangent plane
	// is spanned by the curve tangents"). Build the 3×3 covariance of
	// tangents, take the eigenvector with the smallest eigenvalue.
	LocalVector<Vector3> tangents;
	for (int i = 0; i < eids.size(); ++i) {
		HashMap<int, Ref<CassieSketchGraphEdge>>::Iterator eit = edges.find(eids[i]);
		if (!eit || eit->value.is_null()) {
			continue;
		}
		const Vector3 t = eit->value->get_tangent_away_from(p_node_id);
		if (t.length_squared() > real_t(1e-10)) {
			tangents.push_back(t);
		}
	}
	if (tangents.size() < 2) {
		return;
	}
	// 3×3 covariance Σ tᵢ tᵢᵀ — laid out as 6 distinct entries.
	real_t cxx = 0, cyy = 0, czz = 0, cxy = 0, cxz = 0, cyz = 0;
	for (uint32_t i = 0; i < tangents.size(); ++i) {
		const Vector3 t = tangents[i];
		cxx += t.x * t.x;
		cyy += t.y * t.y;
		czz += t.z * t.z;
		cxy += t.x * t.y;
		cxz += t.x * t.z;
		cyz += t.y * t.z;
	}
	// Smallest-eigenvector via inverse-power iteration (a few sweeps).
	// Seed: cross of first two non-parallel tangents (good initial guess
	// of the plane normal direction). If degenerate, fall back to (0,1,0).
	Vector3 seed(0, 1, 0);
	for (uint32_t i = 1; i < tangents.size(); ++i) {
		const Vector3 c = tangents[0].cross(tangents[i]);
		if (c.length_squared() > real_t(1e-10)) {
			seed = c.normalized();
			break;
		}
	}
	// Project tangents out of seed: v ← seed − (covariance · seed).
	// For tangent plane, normal n satisfies Σ (tᵢ·n)² minimal — i.e. n
	// is the smallest-eigenvalue eigenvector of the covariance. One
	// step of explicit power-iteration on (I·trace − C) converges fast.
	Vector3 n = seed;
	const real_t trace = cxx + cyy + czz;
	for (int iter = 0; iter < 4; ++iter) {
		// Apply (trace·I − C) to n.
		const Vector3 cn(
				cxx * n.x + cxy * n.y + cxz * n.z,
				cxy * n.x + cyy * n.y + cyz * n.z,
				cxz * n.x + cyz * n.y + czz * n.z);
		Vector3 r = n * trace - cn;
		const real_t r2 = r.length_squared();
		if (r2 < real_t(1e-20)) {
			break;
		}
		n = r / Math::sqrt(r2);
	}
	if (n.length_squared() > real_t(1e-10)) {
		node->set_normal(n.normalized());
	}
}

void CassieSketchGraph::_update_node_sharpness(int p_node_id) {
	HashMap<int, Ref<CassieSketchGraphNode>>::Iterator it = nodes.find(p_node_id);
	if (!it) {
		return;
	}
	Ref<CassieSketchGraphNode> node = it->value;
	const int deg = node->get_degree();
	if (deg != 2) {
		node->set_is_sharp(deg > 2);
		return;
	}
	const PackedInt32Array eids = node->get_edge_ids();
	HashMap<int, Ref<CassieSketchGraphEdge>>::Iterator e0it = edges.find(eids[0]);
	HashMap<int, Ref<CassieSketchGraphEdge>>::Iterator e1it = edges.find(eids[1]);
	if (!e0it || !e1it) {
		node->set_is_sharp(false);
		return;
	}
	const Vector3 t0 = e0it->value->get_tangent_away_from(p_node_id);
	const Vector3 t1 = e1it->value->get_tangent_away_from(p_node_id);
	const real_t cosang = CLAMP(t0.dot(t1), real_t(-1.0), real_t(1.0));
	const real_t angle = Math::acos(cosang);
	// The two tangents both point AWAY from the node, so they're at the
	// supplementary of the curve's turning angle. A truly sharp corner
	// (small turning angle along the curve) gives `angle` close to π, a
	// smooth continuation gives `angle` close to π too. The simpler
	// reading: "sharp" iff the angular gap deviates from π by more than
	// the threshold (i.e., one tangent is not nearly opposite the other).
	node->set_is_sharp(Math::abs(real_t(Math::PI) - angle) > sharp_angle_threshold);
}

// Segment-segment closest pair in 3D. Returns the parameters s, t in [0, 1]
// on segments (a0→a1) and (b0→b1) of the closest points, plus the squared
// distance between them. Standard clamped-projection routine.
static void _segment_segment_closest(const Vector3 &a0, const Vector3 &a1,
		const Vector3 &b0, const Vector3 &b1, real_t &out_s, real_t &out_t,
		real_t &out_dist2) {
	const Vector3 d1 = a1 - a0;
	const Vector3 d2 = b1 - b0;
	const Vector3 r = a0 - b0;
	const real_t a = d1.dot(d1);
	const real_t e = d2.dot(d2);
	const real_t f = d2.dot(r);
	real_t s = 0;
	real_t t = 0;
	const real_t kEps = real_t(1e-20);
	if (a <= kEps && e <= kEps) {
		s = 0;
		t = 0;
	} else if (a <= kEps) {
		s = 0;
		t = CLAMP(f / e, real_t(0), real_t(1));
	} else {
		const real_t c = d1.dot(r);
		if (e <= kEps) {
			t = 0;
			s = CLAMP(-c / a, real_t(0), real_t(1));
		} else {
			const real_t b = d1.dot(d2);
			const real_t denom = a * e - b * b;
			if (denom > kEps) {
				s = CLAMP((b * f - c * e) / denom, real_t(0), real_t(1));
			} else {
				s = 0;
			}
			t = (b * s + f) / e;
			if (t < 0) {
				t = 0;
				s = CLAMP(-c / a, real_t(0), real_t(1));
			} else if (t > 1) {
				t = 1;
				s = CLAMP((b - c) / a, real_t(0), real_t(1));
			}
		}
	}
	out_s = s;
	out_t = t;
	const Vector3 cp_a = a0 + d1 * s;
	const Vector3 cp_b = b0 + d2 * t;
	out_dist2 = cp_a.distance_squared_to(cp_b);
}

int CassieSketchGraph::add_stroke(const PackedVector3Array &p_points,
		const PackedVector3Array &p_normals) {
	if (p_points.size() < 2) {
		return -1;
	}
	const Vector3 pa = p_points[0];
	const Vector3 pb = p_points[p_points.size() - 1];
	const Vector3 na = p_normals.is_empty() ? Vector3(0, 1, 0) : p_normals[0];
	const Vector3 nb = p_normals.is_empty() ? Vector3(0, 1, 0)
											: p_normals[p_normals.size() - 1];

	const int node_a = _find_or_create_node(pa, na);
	const int node_b = _find_or_create_node(pb, nb);

	const int eid = next_edge_id++;
	Ref<CassieSketchGraphEdge> edge;
	edge.instantiate();
	edge->set_id(eid);
	edge->set_points(p_points);
	edge->set_normals(p_normals);
	edge->set_node_a_id(node_a);
	edge->set_node_b_id(node_b);
	edges.insert(eid, edge);

	nodes[node_a]->add_edge_id(eid);
	nodes[node_b]->add_edge_id(eid);

	_update_node_sharpness(node_a);
	_update_node_sharpness(node_b);
	_update_node_normal(node_a);
	_update_node_normal(node_b);
	return eid;
}

// One-shot planar arrangement build for offline replay. See header comment.
//
// Strategy:
//  1. Snapshot every segment of every polyline as a candidate, tagged with
//     (polyline_idx, seg_idx, t0, t1) where t0/t1 are the parametric
//     positions along the WHOLE polyline (cumulative arc length normalized).
//  2. Pairwise segment-segment closest-pair tests with two cheap culls:
//     per-polyline AABB intersection and per-segment AABB intersection.
//     Each hit within p_proximity becomes a candidate crossing position.
//  3. Cluster crossing positions into unique node positions via the
//     existing _find_or_create_node merge (radius merge_epsilon).
//  4. For each polyline, collect all of its split parameters, dedupe,
//     sort, and slice the polyline at those parameters; the resulting
//     sub-polylines are added as edges via the simple endpoint-merge
//     path (which finds the shared nodes we just created).
//
// Cost: O(P² × S²) closest-pair tests in the worst case where P=polyline
// count and S=avg segments per polyline, with the AABB culls eliminating
// most pairs in practice. For hat (120 polylines × ~5 segs each) this
// runs in tens of ms.
int CassieSketchGraph::build_from_polylines(
		const TypedArray<PackedVector3Array> &p_polylines,
		real_t p_proximity) {
	clear();
	const int P = p_polylines.size();
	if (P == 0) {
		return 0;
	}
	const real_t prox2 = p_proximity * p_proximity;

	// Per-polyline copy + AABB.
	LocalVector<PackedVector3Array> polys;
	polys.resize(P);
	LocalVector<Vector3> poly_min;
	LocalVector<Vector3> poly_max;
	poly_min.resize(P);
	poly_max.resize(P);
	for (int i = 0; i < P; ++i) {
		polys[i] = p_polylines[i];
		const PackedVector3Array &poly = polys[i];
		Vector3 mn = poly[0];
		Vector3 mx = poly[0];
		for (int k = 1; k < poly.size(); ++k) {
			const Vector3 p = poly[k];
			if (p.x < mn.x) {
				mn.x = p.x;
			}
			if (p.x > mx.x) {
				mx.x = p.x;
			}
			if (p.y < mn.y) {
				mn.y = p.y;
			}
			if (p.y > mx.y) {
				mx.y = p.y;
			}
			if (p.z < mn.z) {
				mn.z = p.z;
			}
			if (p.z > mx.z) {
				mx.z = p.z;
			}
		}
		const Vector3 pad(p_proximity, p_proximity, p_proximity);
		poly_min[i] = mn - pad;
		poly_max[i] = mx + pad;
	}

	// Cumulative arc lengths per polyline — used to express crossing
	// positions as monotone parameters along the whole polyline.
	LocalVector<LocalVector<real_t>> cum_len;
	cum_len.resize(P);
	for (int i = 0; i < P; ++i) {
		const PackedVector3Array &poly = polys[i];
		LocalVector<real_t> &cl = cum_len[i];
		cl.resize(poly.size());
		cl[0] = 0;
		for (int k = 1; k < poly.size(); ++k) {
			cl[k] = cl[k - 1] + poly[k - 1].distance_to(poly[k]);
		}
	}

	// One split point per polyline. (t along whole polyline, world pos).
	struct SplitPt {
		real_t t; // arc-length along the polyline, monotonically increasing
		Vector3 pos;
	};
	LocalVector<LocalVector<SplitPt>> splits_per_poly;
	splits_per_poly.resize(P);

	// Phase 1 — pairwise crossing detection with AABB culls.
	for (int i = 0; i < P; ++i) {
		const PackedVector3Array &pi = polys[i];
		const int ni = pi.size();
		for (int j = i + 1; j < P; ++j) {
			// Polyline AABB cull.
			if (poly_max[i].x < poly_min[j].x ||
					poly_min[i].x > poly_max[j].x ||
					poly_max[i].y < poly_min[j].y ||
					poly_min[i].y > poly_max[j].y ||
					poly_max[i].z < poly_min[j].z ||
					poly_min[i].z > poly_max[j].z) {
				continue;
			}
			const PackedVector3Array &pj = polys[j];
			const int nj = pj.size();
			for (int a = 0; a < ni - 1; ++a) {
				const Vector3 a0 = pi[a];
				const Vector3 a1 = pi[a + 1];
				// Per-segment AABB cull.
				const real_t a_minx = MIN(a0.x, a1.x) - p_proximity;
				const real_t a_maxx = MAX(a0.x, a1.x) + p_proximity;
				const real_t a_miny = MIN(a0.y, a1.y) - p_proximity;
				const real_t a_maxy = MAX(a0.y, a1.y) + p_proximity;
				const real_t a_minz = MIN(a0.z, a1.z) - p_proximity;
				const real_t a_maxz = MAX(a0.z, a1.z) + p_proximity;
				for (int b = 0; b < nj - 1; ++b) {
					const Vector3 b0 = pj[b];
					const Vector3 b1 = pj[b + 1];
					if (a_maxx < MIN(b0.x, b1.x) ||
							a_minx > MAX(b0.x, b1.x) ||
							a_maxy < MIN(b0.y, b1.y) ||
							a_miny > MAX(b0.y, b1.y) ||
							a_maxz < MIN(b0.z, b1.z) ||
							a_minz > MAX(b0.z, b1.z)) {
						continue;
					}
					real_t s, t, d2;
					_segment_segment_closest(a0, a1, b0, b1, s, t, d2);
					if (d2 > prox2) {
						continue;
					}
					// Crossing position: midpoint of the closest pair. This
					// is the same node from both polylines' point of view;
					// merge_epsilon will collapse it to a single graph node.
					const Vector3 cp_a = a0 + (a1 - a0) * s;
					const Vector3 cp_b = b0 + (b1 - b0) * t;
					const Vector3 mid = (cp_a + cp_b) * real_t(0.5);
					// Arc-length parameter on each polyline.
					const real_t seg_len_a = cum_len[i][a + 1] - cum_len[i][a];
					const real_t seg_len_b = cum_len[j][b + 1] - cum_len[j][b];
					SplitPt sa = { cum_len[i][a] + s * seg_len_a, mid };
					SplitPt sb = { cum_len[j][b] + t * seg_len_b, mid };
					splits_per_poly[i].push_back(sa);
					splits_per_poly[j].push_back(sb);
				}
			}
		}
	}

	// Phase 2 — for each polyline, sort split positions by arc length,
	// dedupe near-coincident ones (within merge_epsilon spatially), then
	// emit edges between consecutive splits (and from each end of the
	// polyline). The endpoint-merge inside add_stroke's _find_or_create
	// snaps the slice endpoints to the unique node positions.
	const PackedVector3Array empty_normals;
	int total_edges = 0;
	for (int i = 0; i < P; ++i) {
		const PackedVector3Array &poly = polys[i];
		const int n = poly.size();
		LocalVector<SplitPt> &sp = splits_per_poly[i];
		// Sort by arc-length t. Tiny lists — insertion sort.
		for (uint32_t a = 1; a < sp.size(); ++a) {
			for (uint32_t b = a; b > 0; --b) {
				if (sp[b].t >= sp[b - 1].t) {
					break;
				}
				const SplitPt tmp = sp[b];
				sp[b] = sp[b - 1];
				sp[b - 1] = tmp;
			}
		}
		// Dedupe within merge_epsilon spatially.
		LocalVector<SplitPt> uniq;
		for (uint32_t k = 0; k < sp.size(); ++k) {
			if (!uniq.is_empty() &&
					sp[k].pos.distance_to(uniq[uniq.size() - 1].pos) <=
							merge_epsilon) {
				continue;
			}
			uniq.push_back(sp[k]);
		}

		// Slice the polyline at each split's arc-length parameter, emit
		// each slice as an edge. The cumulative-length array lets us find
		// the source segment for each split position quickly.
		const LocalVector<real_t> &cl = cum_len[i];
		PackedVector3Array current;
		current.push_back(poly[0]);
		uint32_t s_ix = 0;
		for (int k = 0; k < n - 1; ++k) {
			const real_t seg_end_t = cl[k + 1];
			while (s_ix < uniq.size() && uniq[s_ix].t <= seg_end_t) {
				const Vector3 cut = uniq[s_ix].pos;
				// If cut is past the current tail, include it as the
				// boundary vertex. If it's coincident (within
				// merge_epsilon), the existing tail already represents
				// the same shared node, so we don't add a duplicate.
				if (current.size() > 0 &&
						cut.distance_to(current[current.size() - 1]) >
								merge_epsilon) {
					current.push_back(cut);
				}
				// Emit the sub-polyline up to (and including) this cut.
				if (current.size() >= 2) {
					const int new_eid = add_stroke(current, empty_normals);
					if (new_eid >= 0) {
						edges[new_eid]->set_source_polyline_idx(i);
						total_edges++;
					}
				}
				current = PackedVector3Array();
				current.push_back(cut);
				s_ix++;
			}
			if (k + 1 < n) {
				const Vector3 next = poly[k + 1];
				if (current.size() == 0 ||
						next.distance_to(current[current.size() - 1]) >
								merge_epsilon) {
					current.push_back(next);
				}
			}
		}
		if (current.size() >= 2) {
			const int new_eid = add_stroke(current, empty_normals);
			if (new_eid >= 0) {
				edges[new_eid]->set_source_polyline_idx(i);
				total_edges++;
			}
		}
	}
	return total_edges;
}

Ref<CassieSketchGraphNode> CassieSketchGraph::get_node(int p_id) const {
	HashMap<int, Ref<CassieSketchGraphNode>>::ConstIterator it = nodes.find(p_id);
	return it ? it->value : Ref<CassieSketchGraphNode>();
}

Ref<CassieSketchGraphEdge> CassieSketchGraph::get_edge(int p_id) const {
	HashMap<int, Ref<CassieSketchGraphEdge>>::ConstIterator it = edges.find(p_id);
	return it ? it->value : Ref<CassieSketchGraphEdge>();
}

TypedArray<CassieSketchGraphNode> CassieSketchGraph::get_all_nodes() const {
	TypedArray<CassieSketchGraphNode> result;
	for (const KeyValue<int, Ref<CassieSketchGraphNode>> &e : nodes) {
		result.push_back(e.value);
	}
	return result;
}

TypedArray<CassieSketchGraphEdge> CassieSketchGraph::get_all_edges() const {
	TypedArray<CassieSketchGraphEdge> result;
	for (const KeyValue<int, Ref<CassieSketchGraphEdge>> &e : edges) {
		result.push_back(e.value);
	}
	return result;
}

// Discrete parallel transport along the polyline. At each interior vertex
// of the polyline, the local tangent changes; rotate `v` by the same
// smallest-angle rotation that takes the previous tangent to the next.
// The result keeps v perpendicular-to-tangent to leading order, which is
// the property upstream's Stroke.ParallelTransport relies on.
Vector3 CassieSketchGraphEdge::parallel_transport(const Vector3 &p_v,
		int p_from_node_id) const {
	if (points.size() < 2) {
		return p_v;
	}
	const bool forward = (p_from_node_id == node_a_id);
	Vector3 v = p_v;
	const int n = points.size();
	const Vector3 p0 = forward ? points[0] : points[n - 1];
	const Vector3 p1 = forward ? points[1] : points[n - 2];
	Vector3 prev_t = p1 - p0;
	if (prev_t.length_squared() < real_t(1e-20)) {
		return v;
	}
	prev_t.normalize();
	for (int i = 1; i < n - 1; ++i) {
		const Vector3 pa = forward ? points[i] : points[n - 1 - i];
		const Vector3 pb = forward ? points[i + 1] : points[n - 2 - i];
		Vector3 next_t = pb - pa;
		if (next_t.length_squared() < real_t(1e-20)) {
			continue;
		}
		next_t.normalize();
		const Vector3 axis_unnorm = prev_t.cross(next_t);
		const real_t l2 = axis_unnorm.length_squared();
		if (l2 < real_t(1e-12)) {
			prev_t = next_t;
			continue; // collinear — no rotation
		}
		const real_t cos_a = CLAMP(prev_t.dot(next_t), real_t(-1.0), real_t(1.0));
		const real_t angle = Math::acos(cos_a);
		const Vector3 axis = axis_unnorm / Math::sqrt(l2);
		v = v.rotated(axis, angle);
		prev_t = next_t;
	}
	return v;
}

int CassieSketchGraph::_next_edge_at(int p_node_id, int p_incoming_edge,
		const Vector3 &p_normal, bool p_want_next) const {
	HashMap<int, Ref<CassieSketchGraphNode>>::ConstIterator it =
			nodes.find(p_node_id);
	if (!it) {
		return -1;
	}
	const PackedInt32Array eids = it->value->get_edge_ids();
	if (eids.size() < 2) {
		return -1;
	}
	if (eids.size() == 2) {
		return eids[0] == p_incoming_edge ? eids[1] : eids[0];
	}
	HashMap<int, Ref<CassieSketchGraphEdge>>::ConstIterator inc =
			edges.find(p_incoming_edge);
	if (!inc) {
		return -1;
	}
	const Vector3 t_in = inc->value->get_tangent_away_from(p_node_id);
	const Vector3 ref_unnorm = t_in - p_normal * t_in.dot(p_normal);
	const Vector3 ref = ref_unnorm.normalized();
	if (ref.length_squared() < real_t(1e-10)) {
		const int idx = eids.find(p_incoming_edge);
		return idx >= 0 ? eids[(idx + 1) % eids.size()] : eids[0];
	}
	int best_eid = -1;
	// want_next=true picks smallest CCW angle in (0, 2π).
	// want_next=false picks largest CCW angle in (0, 2π) = smallest CW.
	real_t best_angle = p_want_next ? real_t(2.0) * real_t(Math::PI) + real_t(1.0)
									: real_t(-1.0);
	// Out-of-plane exclusion per Yu 2021 §5.1: "take care of excluding the
	// segments that do not lie close to the plane defined by this normal".
	// A tangent is "in plane" if its component along p_normal is small
	// compared to its in-plane projection. The threshold matches the
	// paper's sharp-feature angular threshold (30°), so a tangent within
	// 60° of the tangent plane counts as in-plane and within 30° of the
	// normal counts as out-of-plane.
	const real_t in_plane_cos_threshold = Math::cos(real_t(Math::PI / 3.0));
	for (int i = 0; i < eids.size(); ++i) {
		const int eid = eids[i];
		if (eid == p_incoming_edge) {
			continue;
		}
		HashMap<int, Ref<CassieSketchGraphEdge>>::ConstIterator e_it = edges.find(eid);
		if (!e_it) {
			continue;
		}
		const Vector3 t = e_it->value->get_tangent_away_from(p_node_id);
		// Exclude tangents that are nearly parallel to p_normal (out of
		// plane). |t · n| > cos(60°) → tangent within 30° of the normal.
		if (Math::abs(t.dot(p_normal)) > in_plane_cos_threshold) {
			continue;
		}
		const Vector3 proj_unnorm = t - p_normal * t.dot(p_normal);
		const Vector3 proj = proj_unnorm.normalized();
		if (proj.length_squared() < real_t(1e-10)) {
			continue;
		}
		const Vector3 cross = ref.cross(proj);
		const real_t sin_a = cross.length() *
				(cross.dot(p_normal) >= 0 ? real_t(1.0) : real_t(-1.0));
		const real_t cos_a = CLAMP(ref.dot(proj), real_t(-1.0), real_t(1.0));
		real_t ang = Math::atan2(sin_a, cos_a);
		if (ang < 0) {
			ang += real_t(2.0) * real_t(Math::PI);
		}
		if (p_want_next) {
			if (ang < best_angle) {
				best_angle = ang;
				best_eid = eid;
			}
		} else {
			if (ang > best_angle) {
				best_angle = ang;
				best_eid = eid;
			}
		}
	}
	return best_eid;
}

int CassieSketchGraph::_next_edge_planar(int p_node_id, int p_incoming_edge,
		const Vector3 &p_plane_normal) const {
	HashMap<int, Ref<CassieSketchGraphNode>>::ConstIterator it = nodes.find(p_node_id);
	if (!it) {
		return -1;
	}
	const Ref<CassieSketchGraphNode> node = it->value;
	const PackedInt32Array eids = node->get_edge_ids();
	if (eids.size() < 2) {
		return -1;
	}
	// Smooth (deg 2) fast path: return the other edge regardless of
	// plane orientation.
	if (eids.size() == 2) {
		return eids[0] == p_incoming_edge ? eids[1] : eids[0];
	}
	// Project each incident edge's tangent into the plane perpendicular
	// to p_plane_normal, then sort by signed angle. Pick the edge whose
	// projected tangent is the next CW after the incoming-reversed.
	HashMap<int, Ref<CassieSketchGraphEdge>>::ConstIterator inc =
			edges.find(p_incoming_edge);
	if (!inc) {
		return -1;
	}
	const Vector3 t_in = inc->value->get_tangent_away_from(p_node_id);
	const Vector3 ref_unnorm = t_in - p_plane_normal * t_in.dot(p_plane_normal);
	const Vector3 ref = ref_unnorm.normalized();
	if (ref.length_squared() < real_t(1e-10)) {
		// Degenerate — fall back to next index.
		const int idx = eids.find(p_incoming_edge);
		return idx >= 0 ? eids[(idx + 1) % eids.size()] : eids[0];
	}
	int best_eid = -1;
	real_t best_angle = real_t(2.0) * real_t(Math::PI) + real_t(1.0);
	for (int i = 0; i < eids.size(); ++i) {
		const int eid = eids[i];
		if (eid == p_incoming_edge) {
			continue;
		}
		HashMap<int, Ref<CassieSketchGraphEdge>>::ConstIterator e_it = edges.find(eid);
		if (!e_it) {
			continue;
		}
		const Vector3 t = e_it->value->get_tangent_away_from(p_node_id);
		const Vector3 proj_unnorm = t - p_plane_normal * t.dot(p_plane_normal);
		const Vector3 proj = proj_unnorm.normalized();
		if (proj.length_squared() < real_t(1e-10)) {
			continue;
		}
		// Signed angle CCW from ref to proj, in [0, 2π).
		const Vector3 cross = ref.cross(proj);
		const real_t sin_a = cross.length() *
				(cross.dot(p_plane_normal) >= 0 ? real_t(1.0) : real_t(-1.0));
		const real_t cos_a = CLAMP(ref.dot(proj), real_t(-1.0), real_t(1.0));
		real_t ang = Math::atan2(sin_a, cos_a);
		if (ang < 0) {
			ang += real_t(2.0) * real_t(Math::PI);
		}
		if (ang < best_angle) {
			best_angle = ang;
			best_eid = eid;
		}
	}
	return best_eid;
}

// CycleDetection.cs port — see Assets/Scripts/Data/Graph/CycleDetection.cs
// in upstream Unity CASSIE. Maintains a `current_normal` propagated by
// parallel transport along each segment; selects the next edge at each
// node via CCW (next) or CW (previous) angular ordering relative to that
// transported normal; flips the `reversed` flag when the next node's
// stored normal opposes the transported one.
//
// Adds an explicit manifold cycle counter per edge — caps cycles per
// segment at 2 (one per side), which prevents the outer-face walk from
// being enumerated and stops redundant re-walks of inner faces.
Array CassieSketchGraph::find_cycles() const {
	Array out;
	const int edge_count = edges.size();
	if (edge_count < 3) {
		return out;
	}

	// Compute the GRAPH's global best-fit plane normal once for the entire
	// pass. PCA on node positions: subtract the centroid, accumulate the
	// 3×3 outer-product covariance, take the smallest-eigenvalue
	// eigenvector. Falls back to Y-up if the node cloud is degenerate
	// (all colinear). The angular selector reuses this normal at every
	// node so the CCW direction is consistent across the network — this
	// is the bench's "the sketch lives roughly on a 2D canvas" assumption,
	// but it's derived from the captured data instead of hardcoded to
	// world up, so it works for canvases at any orientation.
	Vector3 centroid;
	{
		int n = 0;
		for (const KeyValue<int, Ref<CassieSketchGraphNode>> &kv : nodes) {
			centroid += kv.value->get_position();
			++n;
		}
		if (n > 0) {
			centroid = centroid / real_t(n);
		}
	}
	real_t cxx = 0, cyy = 0, czz = 0, cxy = 0, cxz = 0, cyz = 0;
	for (const KeyValue<int, Ref<CassieSketchGraphNode>> &kv : nodes) {
		const Vector3 d = kv.value->get_position() - centroid;
		cxx += d.x * d.x;
		cyy += d.y * d.y;
		czz += d.z * d.z;
		cxy += d.x * d.y;
		cxz += d.x * d.z;
		cyz += d.y * d.z;
	}
	Vector3 graph_plane_normal(0, 1, 0);
	{
		const real_t trace = cxx + cyy + czz;
		Vector3 n = graph_plane_normal;
		for (int it = 0; it < 8; ++it) {
			const Vector3 cn(
					cxx * n.x + cxy * n.y + cxz * n.z,
					cxy * n.x + cyy * n.y + cyz * n.z,
					cxz * n.x + cyz * n.y + czz * n.z);
			Vector3 r = n * trace - cn;
			const real_t r2 = r.length_squared();
			if (r2 < real_t(1e-20)) {
				break;
			}
			n = r / Math::sqrt(r2);
		}
		if (n.length_squared() > real_t(1e-10)) {
			graph_plane_normal = n.normalized();
		}
	}
	// Half-edge visit set: (edge_id << 32) | start_node_id.
	HashSet<uint64_t> visited;
	// Per-edge cycle count for the manifold constraint.
	HashMap<int, int> cycle_count;
	for (const KeyValue<int, Ref<CassieSketchGraphEdge>> &kv : edges) {
		cycle_count[kv.key] = 0;
	}
	for (const KeyValue<int, Ref<CassieSketchGraphEdge>> &kv : edges) {
		const int eid = kv.key;
		const int starts[2] = { kv.value->get_node_a_id(),
			kv.value->get_node_b_id() };
		for (int side = 0; side < 2; ++side) {
			const int start_nid = starts[side];
			const uint64_t key0 =
					(uint64_t(uint32_t(eid)) << 32) | uint64_t(uint32_t(start_nid));
			if (visited.has(key0)) {
				continue;
			}
			// Manifold check at entry: segment already in 2 cycles → outer
			// face / non-planar reuse; skip per upstream's
			// `g.ExistingCyclesCount(currentSegment) >= 2` break.
			if (cycle_count[eid] >= 2) {
				continue;
			}
			Ref<CassieSketchGraphNode> seed_node = get_node(start_nid);
			if (seed_node.is_null()) {
				continue;
			}
			// Seed with the graph-derived plane normal (PCA above). For a
			// canvas sketch this is the canvas normal regardless of world
			// orientation; for fully 3D networks it's the best single
			// plane and downstream walks override it from the same source.
			Vector3 current_normal = graph_plane_normal;
			bool reversed = false;

			LocalVector<int> path;
			HashSet<int> path_set;
			int current_eid = eid;
			int current_nid = start_nid;
			const int max_steps = edge_count + 2;
			bool closed = false;
			// Mark only the SEED half-edge as tried — not every step. If the
			// walk dead-ends mid-flight, the intermediate half-edges are
			// still valid starting points for some other cycle.
			visited.insert(key0);

			for (int step = 0; step < max_steps; ++step) {
				if (cycle_count[current_eid] >= 2) {
					break;
				}
				path.push_back(current_eid);
				path_set.insert(current_eid);

				Ref<CassieSketchGraphEdge> cur_edge = get_edge(current_eid);
				if (cur_edge.is_null()) {
					break;
				}
				const int next_nid = cur_edge->get_opposite(current_nid);
				// Parallel-transport the walking normal through this
				// segment — the rotation that takes the segment's start
				// tangent to its end tangent is applied to current_normal.
				// This is the paper's mechanism for keeping a consistent
				// "this side of the surface" direction as the walk crosses
				// from one tangent plane to the next.
				Vector3 transported = cur_edge->parallel_transport(
						current_normal, current_nid);
				if (transported.length_squared() > real_t(1e-10)) {
					transported = transported.normalized();
				} else {
					transported = current_normal;
				}
				// Sign-align the next node's stored plane normal to the
				// transported one. When they oppose, the local CCW
				// convention has flipped, so we flip `reversed` and the
				// angular selector picks the opposite-direction next edge.
				Ref<CassieSketchGraphNode> nnode = get_node(next_nid);
				if (nnode.is_valid()) {
					const Vector3 nn = nnode->get_normal();
					if (nn.length_squared() > real_t(1e-10) &&
							nn.dot(transported) < 0) {
						reversed = !reversed;
					}
				}
				current_normal = transported;
				const int next_eid = _next_edge_at(next_nid, current_eid,
						current_normal, /*p_want_next=*/!reversed);
				if (next_eid < 0) {
					break;
				}
				// reversed flag stays put — we already sign-aligned plane_n
				// above, so we don't need a normal-flip detection step.
				if (next_eid == eid && next_nid == start_nid) {
					if (path.size() >= 3) {
						PackedInt32Array cycle;
						cycle.resize(int(path.size()));
						int *w = cycle.ptrw();
						for (uint32_t i = 0; i < path.size(); ++i) {
							w[i] = path[i];
						}
						out.push_back(cycle);
						// Mark each traversed half-edge as consumed so a
						// rotated version of this cycle isn't re-found from
						// a different seed.
						int prev_nid = start_nid;
						for (uint32_t i = 0; i < path.size(); ++i) {
							const int peid = path[i];
							cycle_count[peid]++;
							const uint64_t kused =
									(uint64_t(uint32_t(peid)) << 32) |
									uint64_t(uint32_t(prev_nid));
							visited.insert(kused);
							Ref<CassieSketchGraphEdge> pe = get_edge(peid);
							if (pe.is_valid()) {
								prev_nid = pe->get_opposite(prev_nid);
							}
						}
						closed = true;
					}
					break;
				}
				if (path_set.has(next_eid)) {
					break;
				}
				current_eid = next_eid;
				current_nid = next_nid;
			}
			(void)closed;
		}
	}

	// Determinism guard: although Godot's HashMap/HashSet iterate in
	// insertion order — so this loop is already insertion-stable — we
	// further canonicalize the emitted list by sorting cycles
	// lexicographically by their SORTED edge-id signature. This keeps the
	// list order invariant under stroke-insertion-order changes (e.g. a
	// session replayed with strokes committed in a different order across
	// peers) and makes multi-peer comparisons trivial. Per-cycle traversal
	// order is preserved so sample_cycle_boundary still picks the correct
	// edge orientation.
	struct CycleEntry {
		PackedInt32Array cycle;
		LocalVector<int> sig;
	};
	LocalVector<CycleEntry> entries;
	entries.resize(out.size());
	for (int i = 0; i < out.size(); ++i) {
		CycleEntry &e = entries[i];
		e.cycle = out[i];
		e.sig.resize(e.cycle.size());
		for (int j = 0; j < e.cycle.size(); ++j) {
			e.sig[j] = e.cycle[j];
		}
		SortArray<int> sorter;
		sorter.sort(e.sig.ptr(), e.sig.size());
	}
	struct CycleLess {
		bool operator()(const CycleEntry &a, const CycleEntry &b) const {
			const int n = MIN(int(a.sig.size()), int(b.sig.size()));
			for (int i = 0; i < n; ++i) {
				if (a.sig[i] != b.sig[i]) {
					return a.sig[i] < b.sig[i];
				}
			}
			return a.sig.size() < b.sig.size();
		}
	};
	SortArray<CycleEntry, CycleLess> entries_sorter;
	entries_sorter.sort(entries.ptr(), entries.size());
	out.clear();
	for (uint32_t i = 0; i < entries.size(); ++i) {
		out.push_back(entries[i].cycle);
	}
	return out;
}

PackedVector3Array CassieSketchGraph::sample_cycle_boundary(
		const PackedInt32Array &p_cycle_edge_ids,
		real_t p_target_edge_length) const {
	PackedVector3Array out;
	const int N = p_cycle_edge_ids.size();
	if (N < 3 || p_target_edge_length <= 0) {
		return out;
	}
	for (int i = 0; i < N; ++i) {
		const int eid = p_cycle_edge_ids[i];
		Ref<CassieSketchGraphEdge> edge = get_edge(eid);
		if (edge.is_null()) {
			continue;
		}
		// The exit node from edge `eid` is the node it shares with the
		// next edge in the cycle (wraparound at the end).
		const int next_eid = p_cycle_edge_ids[(i + 1) % N];
		Ref<CassieSketchGraphEdge> next_edge = get_edge(next_eid);
		if (next_edge.is_null()) {
			continue;
		}
		const int a = edge->get_node_a_id();
		const int b = edge->get_node_b_id();
		const int na = next_edge->get_node_a_id();
		const int nb = next_edge->get_node_b_id();
		int exit_nid = -1;
		if (a == na || a == nb) {
			exit_nid = a;
		} else if (b == na || b == nb) {
			exit_nid = b;
		} else {
			continue;
		}
		const int entry_nid = edge->get_opposite(exit_nid);
		const bool reversed = (entry_nid == b);

		const PackedVector3Array pts = edge->get_points();
		const int M = pts.size();
		if (M < 2) {
			continue;
		}
		real_t seg_len = 0;
		for (int j = 0; j < M - 1; ++j) {
			seg_len += pts[j].distance_to(pts[j + 1]);
		}
		const int samples = MAX(2,
				int(Math::round(seg_len / p_target_edge_length)));

		// Walk edge points from entry → exit. When `reversed`, mirror
		// the parametric lookup so the output stays in CCW cycle order.
		for (int s = 0; s < samples; ++s) {
			const real_t t = real_t(s) / real_t(samples);
			const real_t idx_f = t * real_t(M - 1);
			const int idx0 = CLAMP(int(Math::floor(idx_f)), 0, M - 2);
			const real_t frac = idx_f - real_t(idx0);
			Vector3 p;
			if (!reversed) {
				p = pts[idx0].lerp(pts[idx0 + 1], frac);
			} else {
				p = pts[M - 1 - idx0].lerp(pts[M - 2 - idx0], frac);
			}
			out.push_back(p);
		}
	}
	return out;
}

void CassieSketchGraph::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_merge_epsilon", "value"),
			&CassieSketchGraph::set_merge_epsilon);
	ClassDB::bind_method(D_METHOD("get_merge_epsilon"),
			&CassieSketchGraph::get_merge_epsilon);
	ClassDB::bind_method(D_METHOD("clear"), &CassieSketchGraph::clear);
	ClassDB::bind_method(D_METHOD("add_stroke", "points", "normals"),
			&CassieSketchGraph::add_stroke);
	ClassDB::bind_method(D_METHOD("build_from_polylines", "polylines", "proximity"),
			&CassieSketchGraph::build_from_polylines);
	ClassDB::bind_method(D_METHOD("get_edge_count"),
			&CassieSketchGraph::get_edge_count);
	ClassDB::bind_method(D_METHOD("get_node_count"),
			&CassieSketchGraph::get_node_count);
	ClassDB::bind_method(D_METHOD("get_node", "id"),
			&CassieSketchGraph::get_node);
	ClassDB::bind_method(D_METHOD("get_edge", "id"),
			&CassieSketchGraph::get_edge);
	ClassDB::bind_method(D_METHOD("get_all_nodes"),
			&CassieSketchGraph::get_all_nodes);
	ClassDB::bind_method(D_METHOD("get_all_edges"),
			&CassieSketchGraph::get_all_edges);
	ClassDB::bind_method(D_METHOD("find_cycles"),
			&CassieSketchGraph::find_cycles);
	ClassDB::bind_method(D_METHOD("sample_cycle_boundary",
								 "cycle_edge_ids", "target_edge_length"),
			&CassieSketchGraph::sample_cycle_boundary);
}

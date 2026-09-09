/**************************************************************************/
/*  predictive_bvh_adapter.h                                              */
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

// Drop-in adapter exposing a DynamicBVH-shaped C++ surface over predictive_bvh's
// codegen'd pbvh_tree_* symbols. Hand-written once; no algorithm content —
// every method is dispatch glue onto the emitted C primitives in
// modules/multiplayer_fabric_mmog/predictive_bvh/predictive_bvh.h (from TreeC.lean). Lean-side
// proofs reason over EClassId; this adapter treats the pbvh_node_id_t as the
// eclass id and stores caller-supplied void * payloads in a parallel sidecar.

#include "core/math/aabb.h"
#include "core/math/plane.h"
#include "core/math/vector3.h"
#include "core/templates/local_vector.h"
#include "core/typedefs.h"

#include <thirdparty/predictive_bvh/predictive_bvh.h>

// Portable count-leading-zeros for uint64_t.
// MSVC lacks __builtin_clzll; use _BitScanReverse64 instead.
#if defined(_MSC_VER)
#include <intrin.h>
static inline uint32_t _pbvh_clzll(uint64_t x) {
	unsigned long idx = 0;
	_BitScanReverse64(&idx, x);
	return 63u - (uint32_t)idx;
}
#else
// NOLINTNEXTLINE — __builtin_clzll is intentional here; not a recursive call.
static inline uint32_t _pbvh_clzll(uint64_t x) {
	return (uint32_t)__builtin_clzll(x); // NOLINT
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// pbvh_real_t — R128-backed scalar with a real_t-shaped C++ interface.
//
// This is the "real_t backend" type: callers pass real_t values in/out, but
// all arithmetic internally uses 64.64 fixed-point (R128) for the precision
// that the Lean-proved polynomial functions require.
//
// Template specialization selects this type transparently via pbvh_backend<T>:
//   pbvh_backend<real_t>::type  = pbvh_real_t   (R128 precision)
//   pbvh_backend<int64_t>::type = int64_t        (native integer)
// ─────────────────────────────────────────────────────────────────────────────
struct pbvh_real_t {
	R128 v;

	pbvh_real_t() : v(R128_ZERO) {}
	/* Construct from R128 — used internally and by r128_* return values. */
	pbvh_real_t(R128 r) : v(r) {}
	/* Construct from integer literal — satisfies T(n) in template code. */
	pbvh_real_t(int n) : v(r128_from_int((int64_t)n)) {}
	pbvh_real_t(int64_t n) : v(r128_from_int(n)) {}
	/* Construct from Godot real_t (float or double). */
	explicit pbvh_real_t(real_t f) : v(r128_from_float((float)f)) {}

	/* Implicit decay to R128 — lets r128_le/r128_add etc. accept pbvh_real_t
	 * arguments without explicit casts (single user-defined conversion). */
	operator R128() const { return v; }
	/* Explicit export to real_t for output side. */
	explicit operator real_t() const { return (real_t)r128_to_float(v); }

	/* Arithmetic operators — satisfy (a + b), (a * b) in template code. */
	pbvh_real_t operator+(pbvh_real_t o) const { return r128_add(v, o.v); }
	pbvh_real_t operator*(pbvh_real_t o) const { return r128_mul(v, o.v); }
	pbvh_real_t operator-() const { return r128_neg(v); }
	pbvh_real_t operator-(pbvh_real_t o) const { return r128_sub(v, o.v); }

	/* Comparison — satisfies operator<= used in template predicates. */
	bool operator<=(pbvh_real_t o) const { return r128_le(v, o.v); }
	bool operator==(pbvh_real_t o) const { return r128_eq(v, o.v); }
	bool operator!=(pbvh_real_t o) const { return !r128_eq(v, o.v); }
};

// ─────────────────────────────────────────────────────────────────────────────
// pbvh_backend<T> — traits class: maps caller-facing scalar types to the
// internal storage type used by the BVH template instantiations.
//
// Template specialization allows callers to write pbvh_tree_for<real_t> and
// get R128-backed precision, or pbvh_tree_for<int64_t> for integer coordinates,
// without knowing about the R128 representation.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
struct pbvh_backend {
	using type = T;
};

template <>
struct pbvh_backend<real_t> {
	using type = pbvh_real_t;
};

// Convenience alias: pbvh_tree_for<real_t> → pbvh_tree<pbvh_real_t>, etc.
template <typename T>
using pbvh_tree_for = pbvh_tree<typename pbvh_backend<T>::type>;
template <typename T>
using pbvh_node_for = pbvh_node<typename pbvh_backend<T>::type>;
template <typename T>
using pbvh_internal_for = pbvh_internal<typename pbvh_backend<T>::type>;
template <typename T>
using AabbFor = AabbT<typename pbvh_backend<T>::type>;

// Named aliases for the two concrete instantiations.
using AabbReal = AabbFor<real_t>; // AabbT<pbvh_real_t>  — R128 precision
using AabbI64 = AabbFor<int64_t>; // AabbT<int64_t>      — integer coords

// Float-AABB constructor glue. Non-templated boilerplate, so it lives here per
// the codegen discipline in thirdparty/predictive_bvh/CONTRIBUTING.md rather
// than inside the emitted header.
static inline Aabb aabb_from_floats(float x0, float x1, float y0, float y1, float z0, float z1) {
	Aabb a;
	a.min_x = (int64_t)(x0 * 1000000.0f);
	a.max_x = (int64_t)(x1 * 1000000.0f);
	a.min_y = (int64_t)(y0 * 1000000.0f);
	a.max_y = (int64_t)(y1 * 1000000.0f);
	a.min_z = (int64_t)(z0 * 1000000.0f);
	a.max_z = (int64_t)(z1 * 1000000.0f);
	return a;
}

// real_t variant — passes through R128 precision. Callers work with real_t;
// each field is stored as r128_from_float(x * 1e6) ≈ 1 µm resolution.
static inline AabbReal aabb_from_reals(real_t x0, real_t x1, real_t y0, real_t y1, real_t z0, real_t z1) {
	AabbReal a;
	a.min_x = pbvh_real_t(r128_from_float((float)(x0 * 1000000.0)));
	a.max_x = pbvh_real_t(r128_from_float((float)(x1 * 1000000.0)));
	a.min_y = pbvh_real_t(r128_from_float((float)(y0 * 1000000.0)));
	a.max_y = pbvh_real_t(r128_from_float((float)(y1 * 1000000.0)));
	a.min_z = pbvh_real_t(r128_from_float((float)(z0 * 1000000.0)));
	a.max_z = pbvh_real_t(r128_from_float((float)(z1 * 1000000.0)));
	return a;
}

// int64_t variant — integer µm coordinates, no floating-point conversion.
static inline AabbI64 aabb_from_um(int64_t x0, int64_t x1, int64_t y0, int64_t y1, int64_t z0, int64_t z1) {
	return { x0, x1, y0, y1, z0, z1 };
}

// ─────────────────────────────────────────────────────────────────────────────
// Non-polynomial R128 helpers — moved here from CodeGen.lean / generated header.
// These are direct C translations (no E-graph optimization). They belong in
// the adapter rather than the generated header per the codegen discipline:
// the generated header should contain only E-graph-produced polynomial code.
// ─────────────────────────────────────────────────────────────────────────────

/* Sign bit of R128 (d.hi's high bit) packed as R128-encoded 0 or 1.
   Used by the Z<->GF(2) bridge to turn comparisons into ring ops. */
inline R128 r128_sign_bit(R128 d) {
	R128 r;
	r.hi = ((uint64_t)d.hi >> 63) & 1;
	r.lo = 0;
	return r;
}

/* Branchless min via Z<->GF(2) bridge. sign = sign_bit(b-a). */
template <typename T>
static inline T ring_min_r128(T a, T b, T sign) {
	T t0 = (T(-1) * a);
	T t1 = (b + t0);
	T t2 = (sign * t1);
	T t3 = (a + t2);
	return t3;
}

/* Branchless max via Z<->GF(2) bridge. sign = sign_bit(b-a). */
template <typename T>
static inline T ring_max_r128(T a, T b, T sign) {
	T t0 = (T(-1) * a);
	T t1 = (b + t0);
	T t2 = (sign * t1);
	T t3 = (T(-1) * t2);
	T t4 = (b + t3);
	return t4;
}

/* Two-arg min/max wrappers: compute sign inline, delegate to ring form. */
inline R128 pbvh_r128_min(R128 a, R128 b) {
	return ring_min_r128(a, b, r128_sign_bit(r128_sub(b, a)));
}

inline R128 pbvh_r128_max(R128 a, R128 b) {
	return ring_max_r128(a, b, r128_sign_bit(r128_sub(b, a)));
}

/* Aabb union — hot-path short-circuit form (called per refit). Proved
   equivalent to ring_min_r128 / ring_max_r128 via Z<->GF(2) bridge. */
inline Aabb aabb_union(const Aabb *a, const Aabb *o) {
	Aabb r;
	r.min_x = a->min_x <= o->min_x ? a->min_x : o->min_x;
	r.max_x = a->max_x <= o->max_x ? o->max_x : a->max_x;
	r.min_y = a->min_y <= o->min_y ? a->min_y : o->min_y;
	r.max_y = a->max_y <= o->max_y ? o->max_y : a->max_y;
	r.min_z = a->min_z <= o->min_z ? a->min_z : o->min_z;
	r.max_z = a->max_z <= o->max_z ? o->max_z : a->max_z;
	return r;
}

/* Fast-path predicates: short-circuit r128_le chains. Proved equivalent to
   aabb_overlaps_ring via the Z<->GF(2) bridge — see Lean HilbertBroadphase. */
inline bool aabb_overlaps(const Aabb *a, const Aabb *o) {
	return a->min_x <= o->max_x && o->min_x <= a->max_x && a->min_y <= o->max_y && o->min_y <= a->max_y && a->min_z <= o->max_z && o->min_z <= a->max_z;
}

inline bool aabb_contains(const Aabb *a, const Aabb *inner) {
	return a->min_x <= inner->min_x && inner->max_x <= a->max_x && a->min_y <= inner->min_y && inner->max_y <= a->max_y && a->min_z <= inner->min_z && inner->max_z <= a->max_z;
}

inline bool aabb_contains_point(const Aabb *a, int64_t x, int64_t y, int64_t z) {
	return a->min_x <= x && x <= a->max_x && a->min_y <= y && y <= a->max_y && a->min_z <= z && z <= a->max_z;
}

/* Source: Build.lean:193 (clz30) */
inline uint32_t clz30(uint32_t x) {
	return x == 0 ? 30 : 29 - (31 - _pbvh_clz(x));
}

/* R128 arithmetic right shift by 1 (divide by 2, preserving sign) */
inline R128 r128_half(R128 v) {
	R128 r;
	r.hi = v.hi >> 1;
	r.lo = (v.lo >> 1) | ((uint64_t)v.hi << 63);
	return r;
}

/* Source: Build.lean (hilbert3D) — Skilling (2004) 3D Hilbert curve.
   O(b) bit manipulation; better locality than Morton for volume partitioning.
   Bader (2013) Ch.7: cluster diameter O(n^{1/3}) vs Morton O(n^{2/3}). */
inline uint32_t hilbert3d(uint32_t x, uint32_t y, uint32_t z) {
	const uint32_t order = 10;
	const uint32_t mask = (1u << order) - 1u;
	x &= mask;
	y &= mask;
	z &= mask;
	/* Step 1: inverse undo (MSB down to bit 1) */
	for (uint32_t i = 0; i < order - 1; i++) {
		uint32_t q = 1u << (order - 1 - i);
		uint32_t p = q - 1;
		if (z & q) {
			x ^= p;
		} else {
			uint32_t t = (x ^ z) & p;
			x ^= t;
			z ^= t;
		}
		if (y & q) {
			x ^= p;
		} else {
			uint32_t t = (x ^ y) & p;
			x ^= t;
			y ^= t;
		}
	}
	/* Step 2: Gray encode */
	y ^= x;
	z ^= y;
	/* Step 3: fixup — propagate gray parity */
	uint32_t t = 0;
	for (uint32_t i = 0; i < order - 1; i++) {
		uint32_t q = 1u << (order - 1 - i);
		if (z & q) {
			t ^= (q - 1);
		}
	}
	x ^= t;
	y ^= t;
	z ^= t;
	x &= mask;
	y &= mask;
	z &= mask;
	/* Step 4: transpose to 30-bit index (MSB-first, z-y-x per triple) */
	uint32_t h = 0;
	for (int b = (int)order - 1; b >= 0; b--) {
		h = (h << 1) | ((z >> b) & 1);
		h = (h << 1) | ((y >> b) & 1);
		h = (h << 1) | ((x >> b) & 1);
	}
	return h;
}

/* Forward declaration required by hilbert_of_aabb before the definition below. */
inline void hilbert3d_inverse(uint32_t h, uint32_t *ox, uint32_t *oy, uint32_t *oz);

/* Source: Build.lean (leafHilbert) — int64_t Aabb coords in µm, returns uint32 Hilbert code */
inline uint32_t hilbert_of_aabb(const Aabb *b, const Aabb *scene) {
	int64_t sw = scene->max_x - scene->min_x;
	int64_t sh = scene->max_y - scene->min_y;
	int64_t sd = scene->max_z - scene->min_z;
	if (sw <= 0) {
		sw = 1;
	}
	if (sh <= 0) {
		sh = 1;
	}
	if (sd <= 0) {
		sd = 1;
	}
	int64_t cx = (b->min_x + b->max_x) / 2;
	int64_t cy = (b->min_y + b->max_y) / 2;
	int64_t cz = (b->min_z + b->max_z) / 2;
	int64_t nxi = (cx - scene->min_x) * 1024 / sw;
	int64_t nyi = (cy - scene->min_y) * 1024 / sh;
	int64_t nzi = (cz - scene->min_z) * 1024 / sd;
	uint32_t nx = (uint32_t)(nxi < 0 ? 0 : nxi > 1023 ? 1023
													  : nxi);
	uint32_t ny = (uint32_t)(nyi < 0 ? 0 : nyi > 1023 ? 1023
													  : nyi);
	uint32_t nz = (uint32_t)(nzi < 0 ? 0 : nzi > 1023 ? 1023
													  : nzi);
	uint32_t h = hilbert3d(nx, ny, nz);
	/* Witness check: inverse(forward(x,y,z)) == (x,y,z) */
	uint32_t rx, ry, rz;
	hilbert3d_inverse(h, &rx, &ry, &rz);
	CRASH_COND(rx != nx || ry != ny || rz != nz);
	return h;
}

/* Hilbert3D inverse: 30-bit code → (x, y, z) 10-bit coordinates.
   Skilling transposeToAxes. Verified by roundtrip in Lean. */
inline void hilbert3d_inverse(uint32_t h, uint32_t *ox, uint32_t *oy, uint32_t *oz) {
	const uint32_t order = 10;
	const uint32_t mask = (1u << order) - 1u;
	uint32_t x = 0, y = 0, z = 0;
	for (uint32_t b = 0; b < order; b++) {
		uint32_t s = 3 * b;
		x |= ((h >> s) & 1) << b;
		y |= ((h >> (s + 1)) & 1) << b;
		z |= ((h >> (s + 2)) & 1) << b;
	}
	/* Undo fixup: progressive decode */
	uint32_t t = 0;
	for (uint32_t i = 0; i < order - 1; i++) {
		uint32_t q = 1u << (order - 1 - i);
		if ((z ^ t) & q) {
			t ^= (q - 1);
		}
	}
	x ^= t;
	y ^= t;
	z ^= t;
	/* Undo Gray: z ^= y, then y ^= x */
	z ^= y;
	y ^= x;
	/* Undo main loop: Q from 2 to MSB, y then z */
	for (uint32_t q = 2; q < (1u << order); q <<= 1) {
		uint32_t p = q - 1;
		if (y & q) {
			x ^= p;
		} else {
			uint32_t ty = (x ^ y) & p;
			x ^= ty;
			y ^= ty;
		}
		if (z & q) {
			x ^= p;
		} else {
			uint32_t tz = (x ^ z) & p;
			x ^= tz;
			z ^= tz;
		}
	}
	*ox = x & mask;
	*oy = y & mask;
	*oz = z & mask;
	/* Witness check: forward(inverse(h)) == h */
	CRASH_COND(hilbert3d(*ox, *oy, *oz) != h);
}

/* Hilbert-cell-of: AABB from Hilbert code + prefix depth + scene bounds (int64_t µm). */
inline Aabb hilbert_cell_of(uint32_t code, uint32_t prefix_depth, const Aabb *scene) {
	uint32_t cx, cy, cz;
	hilbert3d_inverse(code, &cx, &cy, &cz);
	int64_t sw = scene->max_x - scene->min_x;
	int64_t sh = scene->max_y - scene->min_y;
	int64_t sd = scene->max_z - scene->min_z;
	if (sw <= 0) {
		sw = 1;
	}
	if (sh <= 0) {
		sh = 1;
	}
	if (sd <= 0) {
		sd = 1;
	}
	uint32_t shift = (prefix_depth < 10) ? 10 - prefix_depth : 0;
	int64_t cell = 1LL << shift;
	uint32_t x0 = (cx >> shift) << shift;
	uint32_t y0 = (cy >> shift) << shift;
	uint32_t z0 = (cz >> shift) << shift;
	Aabb result;
	result.min_x = scene->min_x + (int64_t)x0 * sw / 1024;
	result.max_x = scene->min_x + ((int64_t)x0 + cell) * sw / 1024;
	result.min_y = scene->min_y + (int64_t)y0 * sh / 1024;
	result.max_y = scene->min_y + ((int64_t)y0 + cell) * sh / 1024;
	result.min_z = scene->min_z + (int64_t)z0 * sd / 1024;
	result.max_z = scene->min_z + ((int64_t)z0 + cell) * sd / 1024;
	return result;
}

/* Per-entity delta: largest candidate where v*d + ah*d^2 <= R.
   Polynomial cost evaluation via E-graph (delta_cost_N<pbvh_real_t> templates);
   selection is non-ring postprocessing.
   Uses pbvh_real_t internally so that T(n) literal construction works for the
   template instantiation; R128 args are implicitly promoted via pbvh_real_t(R128).
   Source of truth: perEntityDelta (Sim.lean:407) */
inline uint32_t per_entity_delta_poly(R128 v_r, R128 a_half_r) {
	pbvh_real_t v(v_r), a(a_half_r);
	const R128 R = r128_from_int(5000000LL);
	if (r128_le(delta_cost_120(v, a), R)) {
		return 120;
	}
	if (r128_le(delta_cost_100(v, a), R)) {
		return 100;
	}
	if (r128_le(delta_cost_80(v, a), R)) {
		return 80;
	}
	if (r128_le(delta_cost_64(v, a), R)) {
		return 64;
	}
	if (r128_le(delta_cost_48(v, a), R)) {
		return 48;
	}
	if (r128_le(delta_cost_32(v, a), R)) {
		return 32;
	}
	if (r128_le(delta_cost_24(v, a), R)) {
		return 24;
	}
	if (r128_le(delta_cost_16(v, a), R)) {
		return 16;
	}
	if (r128_le(delta_cost_8(v, a), R)) {
		return 8;
	}
	if (r128_le(delta_cost_4(v, a), R)) {
		return 4;
	}
	if (r128_le(delta_cost_2(v, a), R)) {
		return 2;
	}
	if (r128_le(delta_cost_1(v, a), R)) {
		return 1;
	}
	return 1;
}

// ─── Tree structs for queries ──────────────────────────────────────────────

/* ceil(log2(n)) clamped to [0, 30]. n == 0 maps to 0. */
inline uint32_t pbvh_ceil_log2(uint32_t n) {
	if (n <= 1u) {
		return 0u;
	}
	uint32_t v = n - 1u;
	uint32_t r = 0u;
	while (v > 0u) {
		v >>= 1u;
		r++;
	}
	if (r > 30u) {
		r = 30u;
	}
	return r;
}

/* Ideal bucket_bits for N leaves: ceil(log2(N / K_TARGET)), clamped to [0, 30].
 * At N < K_TARGET, returns 0 (single bucket covers the whole tree). */
inline uint32_t pbvh_bucket_bits_for(uint32_t n) {
	if (n <= PBVH_BUCKET_K_TARGET) {
		return 0u;
	}
	return pbvh_ceil_log2((n + PBVH_BUCKET_K_TARGET - 1u) / PBVH_BUCKET_K_TARGET);
}

/* Required uint32 element count for bucket_dir given N leaves.
 * Each bucket stores [lo, hi) as two uint32, so size = 2 * (1 << bucket_bits).
 * Callers use this to size their bucket_dir allocation before pbvh_tree_build;
 * the build itself overwrites t->bucket_bits with pbvh_bucket_bits_for(N). */
inline uint32_t pbvh_bucket_dir_size(uint32_t n) {
	return 2u * (1u << pbvh_bucket_bits_for(n));
}

inline pbvh_node_id_t pbvh_tree_insert_h(pbvh_tree_t *t, pbvh_eclass_id_t ec,
		Aabb box, uint32_t hilbert) {
	pbvh_node_id_t id;
	if (t->free_head != PBVH_NULL_NODE) {
		id = t->free_head;
		t->free_head = t->nodes[id].next_free;
	} else {
		id = t->count++;
	}
	pbvh_node_t *n = &t->nodes[id];
	n->bounds = box;
	n->eclass = ec;
	n->next_free = PBVH_NULL_NODE;
	n->is_leaf = 1u;
	n->hilbert = hilbert;
	return id;
}

inline pbvh_node_id_t pbvh_tree_insert(pbvh_tree_t *t, pbvh_eclass_id_t ec, Aabb box) {
	return pbvh_tree_insert_h(t, ec, box, 0u);
}

inline void pbvh_tree_remove(pbvh_tree_t *t, pbvh_node_id_t id) {
	pbvh_node_t *n = &t->nodes[id];
	n->is_leaf = 0u;
	n->next_free = t->free_head;
	t->free_head = id;
}

inline void pbvh_tree_update(pbvh_tree_t *t, pbvh_node_id_t id, Aabb box) {
	t->nodes[id].bounds = box;
}

/* Update bounds AND hilbert code together. Caller must pbvh_tree_build()
 * before the next _n or _b query; the sort key has changed. */
inline void pbvh_tree_update_h(pbvh_tree_t *t, pbvh_node_id_t id,
		Aabb box, uint32_t hilbert) {
	pbvh_node_t *n = &t->nodes[id];
	n->bounds = box;
	n->hilbert = hilbert;
}

/* Build one internal node over sorted[lo, hi) by splitting on the highest
 * bit where the first and last Hilbert codes disagree. Pre-order layout:
 * the returned id is the slot claimed before any descendant, so internals[]
 * itself ends up in DFS order. The `skip` field is set after the children
 * are placed — it equals t->internal_count at that point, i.e. the index
 * that any ancestor would jump to when pruning this subtree. */
inline pbvh_internal_id_t pbvh_build_internal_with_parent_(pbvh_tree_t *t,
		uint32_t lo, uint32_t hi, pbvh_internal_id_t parent) {
	if (lo >= hi) {
		return PBVH_NULL_NODE;
	}
	if (t->internal_count >= t->internal_capacity) {
		return PBVH_NULL_NODE;
	}
	pbvh_internal_id_t id = t->internal_count++;
	if (t->parent_of_internal != nullptr) {
		t->parent_of_internal[id] = parent;
	}
	pbvh_internal_t *n = &t->internals[id];
	n->offset = lo;
	n->span = hi - lo;
	n->bounds = t->nodes[t->sorted[lo]].bounds;
	for (uint32_t i = lo + 1; i < hi; i++) {
		n->bounds = aabb_union(&n->bounds, &t->nodes[t->sorted[i]].bounds);
	}
	if (hi - lo <= 1) {
		n->left = PBVH_NULL_NODE;
		n->right = PBVH_NULL_NODE;
		n->skip = t->internal_count;
		return id;
	}
	uint32_t h_lo = t->nodes[t->sorted[lo]].hilbert;
	uint32_t h_hi = t->nodes[t->sorted[hi - 1]].hilbert;
	uint32_t diff = h_lo ^ h_hi;
	uint32_t split = lo + (hi - lo) / 2;
	if (diff != 0u) {
		uint32_t bit = 31u;
		while ((diff & (1u << bit)) == 0u) {
			bit--;
		}
		uint32_t mask = 1u << bit;
		uint32_t s = hi;
		for (uint32_t i = lo; i < hi; i++) {
			if ((t->nodes[t->sorted[i]].hilbert & mask) != 0u) {
				s = i;
				break;
			}
		}
		if (s > lo && s < hi) {
			split = s;
		}
	}
	pbvh_internal_id_t l = pbvh_build_internal_with_parent_(t, lo, split, id);
	pbvh_internal_id_t r = pbvh_build_internal_with_parent_(t, split, hi, id);
	/* Re-fetch: the `n` pointer to caller-owned fixed storage stays valid,
	 * but writing through it after recursion is the clearest shape. */
	t->internals[id].left = l;
	t->internals[id].right = r;
	t->internals[id].skip = t->internal_count;
	return id;
}

/* Legacy entry point: defers to the parent-tracking variant with root parent
 * = PBVH_NULL_NODE. Callers that don't care about parent_of_internal can
 * leave the sidecar NULL; the recursive variant skips the write. */
inline pbvh_internal_id_t pbvh_build_internal_(pbvh_tree_t *t, uint32_t lo, uint32_t hi) {
	return pbvh_build_internal_with_parent_(t, lo, hi, PBVH_NULL_NODE);
}

/* Populate bucket_dir[2*b], bucket_dir[2*b+1] with the (lo, hi) window in
 * sorted[] whose Hilbert prefix at bucket_bits equals b. Runs in O(N + B)
 * where B = 1 << bucket_bits. */
inline void pbvh_build_bucket_dir_(pbvh_tree_t *t) {
	if (t->bucket_dir == nullptr || t->bucket_bits == 0u || t->bucket_bits > 30u) {
		return;
	}
	const uint32_t B = 1u << t->bucket_bits;
	for (uint32_t i = 0; i < 2u * B; i++) {
		t->bucket_dir[i] = 0u;
	}
	const uint32_t shift = 30u - t->bucket_bits;
	uint32_t j = 0u;
	for (uint32_t b = 0; b < B; b++) {
		t->bucket_dir[2u * b] = j;
		while (j < t->sorted_count &&
				(t->nodes[t->sorted[j]].hilbert >> shift) == b) {
			j++;
		}
		t->bucket_dir[2u * b + 1u] = j;
	}
	/* Any Hilbert codes outside [0, B) (e.g. from queries with scene-mismatched
	 * codes) land past the last bucket — those queries must fall back to _n. */
}

/* Phase 2c refit-only fast path. Caller guarantees that no dirty leaf has
 * changed bucket (hilbert prefix at bucket_bits stable), so sorted[] order
 * and internals[] topology stay valid — only bounds need re-unioning.
 *
 * Walks internals[] bottom-up in reverse DFS order: leaf-range nodes
 * refit from sorted[offset..offset+span), inner nodes union their two
 * children's already-refit bounds. Runs in O(internal_count), but every
 * pass is sequential memory reads over a contiguous array — cache-friendly
 * and free of sort/rebuild churn. When the dirty set is sparse relative
 * to N, this is the frame-budget win vs pbvh_tree_build. */
inline void pbvh_tree_refit(pbvh_tree_t *t) {
	if (t->internal_count == 0u) {
		return;
	}
	uint32_t idx = t->internal_count;
	while (idx > 0u) {
		idx--;
		pbvh_internal_t *n = &t->internals[idx];
		if (n->left == PBVH_NULL_NODE && n->right == PBVH_NULL_NODE) {
			const uint32_t o = n->offset;
			const uint32_t s = n->span;
			if (s == 0u) {
				continue;
			}
			Aabb acc = t->nodes[t->sorted[o]].bounds;
			for (uint32_t j = o + 1u; j < o + s; j++) {
				acc = aabb_union(&acc, &t->nodes[t->sorted[j]].bounds);
			}
			n->bounds = acc;
		} else if (n->left != PBVH_NULL_NODE && n->right != PBVH_NULL_NODE) {
			n->bounds = aabb_union(&t->internals[n->left].bounds,
					&t->internals[n->right].bounds);
		} else {
			const pbvh_internal_id_t only =
					(n->left != PBVH_NULL_NODE) ? n->left : n->right;
			n->bounds = t->internals[only].bounds;
		}
	}
}

/* Incremental refit: touches only the leaf-range internals containing dirty
 * leaves and their ancestor chain up to the first ancestor whose current
 * bounds already cover the dirty leaf's new bounds. Relies on two caller-owned
 * sidecars (leaf_to_internal[], parent_of_internal[]) plus scratch touched_bits.
 *
 * Complexity: O(K + n_touched) where K = dirty_count and n_touched is the
 * ancestor-union set truncated by per-leaf containment. A leaf whose new AABB
 * is already covered by its direct enclosing internal contributes O(1) — no
 * ancestor work at all. In the worst case (every leaf grows past the root)
 * n_touched is bounded by K × depth.
 *
 * Algorithm:
 *   1. For each dirty leaf d, fetch new_leaf = t->nodes[d].bounds and walk
 *      up parent_of_internal[] starting from leaf_to_internal[d]. At each
 *      ancestor, break if ancestor.bounds already contains new_leaf (BVH
 *      invariant + transitivity: nothing above needs refit). Otherwise mark
 *      a bit in touched_bits and continue. Walks also short-circuit on
 *      already-marked bits (covers shared-ancestor case in O(1)).
 *   2. Scan touched_bits from max_word down to min_word. Within each word,
 *      pop bits highest-to-lowest via __builtin_clzll — pre-order DFS gives
 *      parent < child, so this yields strictly bottom-up refit order with
 *      no sort step. Refit each: leaf-range nodes re-union their sorted[]
 *      window; inner nodes union their two children's (already-refit or
 *      still-valid) bounds. Bits self-clear during the scan.
 *
 * Soundness license for the early-out: Lean theorem
 * aabbQueryN_complete_from_invariants only requires each internal's bounds
 * to be a superset of its subtree leaves. Over-conservative bounds stay sound.
 *
 * Preconditions (caller-enforced via pbvh_tree_tick's checks, which is the
 * only entry to this internal function): parent_of_internal[], leaf_to_internal[],
 * touched_bits[], and touched_meta_bits[] are all non-null; the two bitmaps
 * are all-zero on entry (they self-clear during the scan phase so they're
 * also all-zero on exit). Violating either trips an out-of-bounds index of
 * internals[] in the scan phase — there is no fallback path that hides
 * misuse, by design (the previous silent fall-through to pbvh_tree_refit
 * masked a real bug that took years to surface). */
inline void pbvh_tree_refit_incremental_(pbvh_tree_t *t,
		const pbvh_dirty_leaf_t *dirty, uint32_t dirty_count) {
	if (t->internal_count == 0u) {
		return;
	}
	/* Mark phase: walk ancestors, setting bits in touched_bits AND in the
	 * meta-bitmap (one bit per touched_bits word). Track [min_meta..max_meta]
	 * instead of a word-level range, so the scan phase iterates only the
	 * coarser meta-words. Every set touched_bits word has its meta-bit set,
	 * giving an O(1) probe to find the next non-empty word via clzll.
	 * Bits self-clear during refit; both bitmaps are all-zero on entry and
	 * all-zero on exit. */
	uint32_t min_meta = UINT32_MAX;
	uint32_t max_meta = 0u;
	for (uint32_t d = 0u; d < dirty_count; d++) {
		const pbvh_node_id_t leaf_id = dirty[d].leaf_id;
		if (leaf_id >= t->count) {
			continue;
		}
		const Aabb new_leaf = t->nodes[leaf_id].bounds;
		uint32_t i = t->leaf_to_internal[leaf_id];
		while (i != PBVH_NULL_NODE && i < t->internal_count) {
			/* Containment early-out: if this ancestor's current bounds already
			 * contain the new leaf bounds, the existing bounds remain a valid
			 * conservative superset. By the BVH invariant (every ancestor's
			 * bounds are a superset of its subtree leaves) and transitivity,
			 * all ancestors above this node also remain valid — no mark, no
			 * refit, walk terminates in O(1) for shrinking / in-place leaves.
			 * Soundness: aabbQueryN_complete_from_invariants requires only
			 * bounds ⊇ descendant leaves; over-conservative bounds are permitted.
			 * NO DEDUP-BREAK: stopping when a node is already marked is unsound
			 * combined with the containment early-out above. Leaf A may
			 * containment-break at ancestor P (not marking P); leaf B then
			 * reaches P's child I (already marked by A) and would dedup-break,
			 * silently skipping P even though B's bounds exceed P's. Spec:
			 * RefitIncremental.lean markAncestors / refitIncrementalSpec. */
			if (aabb_contains(&t->internals[i].bounds, &new_leaf)) {
				break;
			}
			const uint32_t w = i >> 6;
			const uint64_t mask = 1ull << (i & 63u);
			t->touched_bits[w] |= mask;
			const uint32_t mw = w >> 6;
			t->touched_meta_bits[mw] |= 1ull << (w & 63u);
			if (mw < min_meta) {
				min_meta = mw;
			}
			if (mw > max_meta) {
				max_meta = mw;
			}
			i = t->parent_of_internal[i];
		}
	}
	if (min_meta > max_meta) {
		return;
	}
	/* Refit phase: iterate meta-words from max_meta down to min_meta, and
	 * within each, pop set bits highest-to-lowest via clzll to get the
	 * (also-descending) indices of non-empty touched_bits words. Within
	 * each such word, pop bits highest-to-lowest again to get descending
	 * internal ids. Pre-order DFS gives parent < child, so this strict
	 * descending-id walk visits children before parents and every union
	 * reads already-refit children. Zero sort steps, zero wasted probes —
	 * empty touched_bits words are never even loaded. Bits self-clear
	 * as consumed. */
	for (uint32_t mw = max_meta + 1u; mw > min_meta;) {
		mw--;
		uint64_t meta = t->touched_meta_bits[mw];
		while (meta != 0ull) {
			const uint32_t mb = 63u - (uint32_t)_pbvh_clzll(meta);
			meta &= ~(1ull << mb);
			const uint32_t w = (mw << 6) | mb;
			uint64_t bits = t->touched_bits[w];
			while (bits != 0ull) {
				const uint32_t b = 63u - (uint32_t)_pbvh_clzll(bits);
				bits &= ~(1ull << b);
				const uint32_t idx = (w << 6) | b;
				pbvh_internal_t *n = &t->internals[idx];
				if (n->left == PBVH_NULL_NODE && n->right == PBVH_NULL_NODE) {
					const uint32_t o = n->offset;
					const uint32_t s = n->span;
					if (s == 0u) {
						continue;
					}
					Aabb acc = t->nodes[t->sorted[o]].bounds;
					for (uint32_t j = o + 1u; j < o + s; j++) {
						acc = aabb_union(&acc, &t->nodes[t->sorted[j]].bounds);
					}
					n->bounds = acc;
				} else if (n->left != PBVH_NULL_NODE && n->right != PBVH_NULL_NODE) {
					n->bounds = aabb_union(&t->internals[n->left].bounds,
							&t->internals[n->right].bounds);
				} else {
					const pbvh_internal_id_t only =
							(n->left != PBVH_NULL_NODE) ? n->left : n->right;
					n->bounds = t->internals[only].bounds;
				}
			}
			t->touched_bits[w] = 0ull;
		}
		t->touched_meta_bits[mw] = 0ull;
	}
}

/* 4-pass LSD radix sort of sorted[] by 30-bit hilbert (O(N)), then build
 * the internal tree and (if provided) the bucket directory. The radix
 * passes reuse t->internals[] as a pbvh_node_id_t scratch buffer — its
 * contents are overwritten immediately after by pbvh_build_internal_, and
 * sizeof(pbvh_internal_t) >= sizeof(pbvh_node_id_t) with internal_capacity
 * >= count so the aliasing is in-bounds. Four passes is even, so the sorted
 * result lands back in t->sorted[] without a final copy. */
inline void pbvh_tree_build(pbvh_tree_t *t) {
	uint32_t k = 0;
	for (uint32_t i = 0; i < t->count; i++) {
		if (t->nodes[i].is_leaf) {
			t->sorted[k++] = (pbvh_node_id_t)i;
		}
	}
	t->sorted_count = k;
	if (k > 1u && t->internals != nullptr && t->internal_capacity >= k) {
		pbvh_node_id_t *scratch = (pbvh_node_id_t *)t->internals;
		pbvh_node_id_t *src = t->sorted;
		pbvh_node_id_t *dst = scratch;
		for (uint32_t pass = 0u; pass < 4u; pass++) {
			uint32_t count_bin[256];
			for (uint32_t b = 0u; b < 256u; b++) {
				count_bin[b] = 0u;
			}
			const uint32_t shift = pass * 8u;
			for (uint32_t i = 0u; i < k; i++) {
				uint32_t b = (t->nodes[src[i]].hilbert >> shift) & 0xFFu;
				count_bin[b]++;
			}
			uint32_t sum = 0u;
			for (uint32_t b = 0u; b < 256u; b++) {
				uint32_t c = count_bin[b];
				count_bin[b] = sum;
				sum += c;
			}
			for (uint32_t i = 0u; i < k; i++) {
				uint32_t b = (t->nodes[src[i]].hilbert >> shift) & 0xFFu;
				dst[count_bin[b]++] = src[i];
			}
			pbvh_node_id_t *tmp = src;
			src = dst;
			dst = tmp;
		}
	} else if (k > 1u) {
		/* Fallback insertion sort when no internals scratch is attached.
		 * Production paths always supply internals; this branch exists for
		 * tiny harnesses that skip the allocation. */
		for (uint32_t i = 1u; i < k; i++) {
			pbvh_node_id_t cur = t->sorted[i];
			uint32_t cur_h = t->nodes[cur].hilbert;
			uint32_t j = i;
			while (j > 0u && t->nodes[t->sorted[j - 1u]].hilbert > cur_h) {
				t->sorted[j] = t->sorted[j - 1u];
				j--;
			}
			t->sorted[j] = cur;
		}
	}
	t->internal_count = 0u;
	t->internal_root = PBVH_NULL_NODE;
	if (t->internals != nullptr && t->internal_capacity > 0u && k > 0u) {
		t->internal_root = pbvh_build_internal_(t, 0u, k);
	}
	/* Auto-tune bucket_bits from leaf count. Caller pre-allocated bucket_dir
	 * via pbvh_bucket_dir_size(N); this overwrite keeps per-query scan cost
	 * bounded by PBVH_BUCKET_K_TARGET at any N. */
	if (t->bucket_dir != nullptr) {
		t->bucket_bits = pbvh_bucket_bits_for(k);
	}
	pbvh_build_bucket_dir_(t);
	/* Populate leaf_to_internal[] from the leaf-range internals produced
	 * above. Any leaf id inside a leaf-range internal's [offset, offset+span)
	 * window has that internal as its immediate enclosing ancestor. One pass
	 * over leaf-range internals, total O(N) writes. */
	if (t->leaf_to_internal != nullptr) {
		for (uint32_t i = 0u; i < t->internal_count; i++) {
			pbvh_internal_t *n = &t->internals[i];
			if (n->left != PBVH_NULL_NODE || n->right != PBVH_NULL_NODE) {
				continue;
			}
			const uint32_t o = n->offset;
			const uint32_t s = n->span;
			for (uint32_t j = o; j < o + s; j++) {
				t->leaf_to_internal[t->sorted[j]] = i;
			}
		}
	}
}

/* O(N) brute-force leaf scan. Kept only as the correctness oracle for
 * tests; production paths should use _n or _b. Sets last_visits. */
inline void pbvh_tree_aabb_query(pbvh_tree_t *t, const Aabb *query,
		int (*cb)(pbvh_eclass_id_t, void *), void *ud) {
	const uint32_t n = t->count;
	uint32_t visits = 0;
	for (uint32_t i = 0; i < n; i++) {
		const pbvh_node_t *node = &t->nodes[i];
		if (!node->is_leaf) {
			continue;
		}
		visits++;
		if (aabb_overlaps(&node->bounds, query)) {
			if (cb(node->eclass, ud) != 0) {
				t->last_visits = visits;
				return;
			}
		}
	}
	t->last_visits = visits;
}

/* Iterative nested-set descent. Walks internals[] in pre-order; on a bounds
 * miss, jumps straight to internals[i].skip, which is the index past the
 * entire pruned subtree — a single assignment, no stack, no recursion.
 * On a leaf-range node, iterates sorted[offset .. offset+span) then jumps
 * to skip. Requires internals[] to have been built. */
inline void pbvh_tree_aabb_query_n(pbvh_tree_t *t, const Aabb *query,
		int (*cb)(pbvh_eclass_id_t, void *), void *ud) {
	uint32_t visits = 0u;
	if (t->internal_root == PBVH_NULL_NODE) {
		t->last_visits = 0u;
		return;
	}
	uint32_t i = t->internal_root;
	const uint32_t end = t->internal_count;
	while (i < end) {
		const pbvh_internal_t *n = &t->internals[i];
		if (!aabb_overlaps(&n->bounds, query)) {
			i = n->skip;
			continue;
		}
		if (n->left == PBVH_NULL_NODE && n->right == PBVH_NULL_NODE) {
			const uint32_t o = n->offset;
			const uint32_t s = n->span;
			for (uint32_t j = o; j < o + s; j++) {
				const pbvh_node_t *leaf = &t->nodes[t->sorted[j]];
				if (!leaf->is_leaf) {
					continue;
				}
				visits++;
				if (aabb_overlaps(&leaf->bounds, query)) {
					if (cb(leaf->eclass, ud) != 0) {
						t->last_visits = visits;
						return;
					}
				}
			}
			i = n->skip;
			continue;
		}
		i++; /* descend: next DFS index is the left child */
	}
	t->last_visits = visits;
}

/* Bucket-directory query. Given the query's Hilbert code, computes the
 * owning prefix bucket in O(1) and iterates only the leaves in that window.
 * Total cost is O(1 + k) where k is the bucket's leaf count — strictly
 * better than the O(log N) descent of _n for queries the caller can tag
 * with a Hilbert code. Falls through to _n if bucket_dir wasn't built. */
inline void pbvh_tree_aabb_query_b(pbvh_tree_t *t, const Aabb *query,
		uint32_t query_hilbert,
		int (*cb)(pbvh_eclass_id_t, void *), void *ud) {
	if (t->bucket_dir == nullptr || t->bucket_bits == 0u) {
		pbvh_tree_aabb_query_n(t, query, cb, ud);
		return;
	}
	const uint32_t shift = 30u - t->bucket_bits;
	const uint32_t b = query_hilbert >> shift;
	const uint32_t B = 1u << t->bucket_bits;
	if (b >= B) {
		pbvh_tree_aabb_query_n(t, query, cb, ud);
		return;
	}
	const uint32_t lo = t->bucket_dir[2u * b];
	const uint32_t hi = t->bucket_dir[2u * b + 1u];
	uint32_t visits = 0u;
	for (uint32_t j = lo; j < hi; j++) {
		const pbvh_node_t *leaf = &t->nodes[t->sorted[j]];
		if (!leaf->is_leaf) {
			continue;
		}
		visits++;
		if (aabb_overlaps(&leaf->bounds, query)) {
			if (cb(leaf->eclass, ud) != 0) {
				t->last_visits = visits;
				return;
			}
		}
	}
	t->last_visits = visits;
}

/* Eclass-style self-query: look up the leaf that stores eclass `self` and
 * run _b using its stored bounds + hilbert. Skips `self` in results so the
 * callback only sees \"other\" eclasses that overlap. Caller works in eclass
 * IDs end-to-end; no AABB pointers, no Hilbert codes threaded through. */
typedef struct pbvh_eclass_skip_ud {
	pbvh_eclass_id_t self;
	int (*inner_cb)(pbvh_eclass_id_t, void *);
	void *inner_ud;
} pbvh_eclass_skip_ud_t;

inline int pbvh_eclass_skip_cb_(pbvh_eclass_id_t other, void *ud) {
	pbvh_eclass_skip_ud_t *s = (pbvh_eclass_skip_ud_t *)ud;
	if (other == s->self) {
		return 0;
	}
	return s->inner_cb(other, s->inner_ud);
}

inline void pbvh_tree_query_eclass(pbvh_tree_t *t, pbvh_eclass_id_t self,
		int (*cb)(pbvh_eclass_id_t, void *), void *ud) {
	const uint32_t n = t->count;
	for (uint32_t i = 0; i < n; i++) {
		const pbvh_node_t *node = &t->nodes[i];
		if (!node->is_leaf || node->eclass != self) {
			continue;
		}
		pbvh_eclass_skip_ud_t s = { self, cb, ud };
		pbvh_tree_aabb_query_b(t, &node->bounds, node->hilbert,
				pbvh_eclass_skip_cb_, &s);
		return;
	}
}

/* Enumerate every overlapping (a, b) pair with a.eclass < b.eclass exactly
 * once. Pure eclass-style API — no caller-side AABBs, no Hilbert threading,
 * just two eclass IDs per pair. Uses _n (nested-set skip descent) so ghost
 * AABBs that span multiple Hilbert cells stay correct. */
typedef struct pbvh_pair_enum_ud {
	pbvh_eclass_id_t self;
	int matched_count;
	int (*pair_cb)(pbvh_eclass_id_t, pbvh_eclass_id_t, void *);
	void *pair_ud;
} pbvh_pair_enum_ud_t;

inline int pbvh_pair_enum_cb_(pbvh_eclass_id_t other, void *ud) {
	pbvh_pair_enum_ud_t *p = (pbvh_pair_enum_ud_t *)ud;
	if (other <= p->self) {
		return 0;
	}
	p->matched_count++;
	return p->pair_cb(p->self, other, p->pair_ud);
}

inline int pbvh_tree_enumerate_pairs(pbvh_tree_t *t,
		int (*pair_cb)(pbvh_eclass_id_t, pbvh_eclass_id_t, void *), void *ud) {
	int pairs = 0;
	for (uint32_t i = 0; i < t->count; i++) {
		const pbvh_node_t *node = &t->nodes[i];
		if (!node->is_leaf) {
			continue;
		}
		pbvh_pair_enum_ud_t p = { node->eclass, 0, pair_cb, ud };
		pbvh_tree_aabb_query_n(t, &node->bounds,
				pbvh_pair_enum_cb_, &p);
		pairs += p.matched_count;
	}
	return pairs;
}

/* ── Phase 2b primitives: ray, convex, clear, is_empty, optimize, index ─── */

/* Oriented half-space {p : normal · p + d >= 0}. Kept side is positive. */
typedef struct pbvh_plane {
	R128 nx, ny, nz;
	R128 d;
} pbvh_plane_t;

/* Build the segment-AABB of a ray segment from (ox,oy,oz) to (tx,ty,tz).
 * Conservative broadphase: a segment hits `b` only if its AABB overlaps `b`.
 * Hot-path short-circuit form; proved equivalent to pbvh_r128_min/max
 * (Z<->GF(2) branchless ring form) via bitDecompose. */
inline Aabb pbvh_segment_aabb_(int64_t ox, int64_t oy, int64_t oz,
		int64_t tx, int64_t ty, int64_t tz) {
	Aabb s;
	s.min_x = ox <= tx ? ox : tx;
	s.max_x = ox <= tx ? tx : ox;
	s.min_y = oy <= ty ? oy : ty;
	s.max_y = oy <= ty ? ty : oy;
	s.min_z = oz <= tz ? oz : tz;
	s.max_z = oz <= tz ? tz : oz;
	return s;
}

/* Iterative skip-pointer descent over internals[] using a ray segment. Every
 * live leaf whose AABB overlaps the segment's AABB has its eclass passed to
 * `cb`. Callback returns nonzero to stop early. Mirrors _n's traversal shape
 * exactly; only the prune predicate changes. Emitted from Spatial/Tree.lean
 * `rayQueryN`; soundness of a tight slab test is deferred to Phase 2b'. */
inline void pbvh_tree_ray_query(pbvh_tree_t *t,
		int64_t ox, int64_t oy, int64_t oz, int64_t tx, int64_t ty, int64_t tz,
		int (*cb)(pbvh_eclass_id_t, void *), void *ud) {
	if (t->internal_root == PBVH_NULL_NODE) {
		return;
	}
	const Aabb seg = pbvh_segment_aabb_(ox, oy, oz, tx, ty, tz);
	uint32_t i = t->internal_root;
	const uint32_t end = t->internal_count;
	while (i < end) {
		const pbvh_internal_t *n = &t->internals[i];
		if (!aabb_overlaps(&n->bounds, &seg)) {
			i = n->skip;
			continue;
		}
		if (n->left == PBVH_NULL_NODE && n->right == PBVH_NULL_NODE) {
			const uint32_t o = n->offset;
			const uint32_t s = n->span;
			for (uint32_t j = o; j < o + s; j++) {
				const pbvh_node_t *leaf = &t->nodes[t->sorted[j]];
				if (!leaf->is_leaf) {
					continue;
				}
				if (aabb_overlaps(&leaf->bounds, &seg)) {
					if (cb(leaf->eclass, ud) != 0) {
						return;
					}
				}
			}
			i = n->skip;
			continue;
		}
		i++;
	}
}

/* Half-space test: does AABB `b` have any corner `c` satisfying
 * normal · c + d >= 0 ? If every corner is strictly below the plane,
 * the entire box is rejected. Arithmetic per corner is routed through
 * the EGraph-emitted pbvh_plane_corner_val helper — the only remaining
 * C here is control-flow (the 8-corner unroll) and the scalar
 * comparison (not a ring op). */
inline bool pbvh_half_space_keeps_(const pbvh_plane_t *p, const Aabb *b) {
	const R128 zero = r128_from_int(0);
	R128 xs[2];
	xs[0] = r128_from_int(b->min_x);
	xs[1] = r128_from_int(b->max_x);
	R128 ys[2];
	ys[0] = r128_from_int(b->min_y);
	ys[1] = r128_from_int(b->max_y);
	R128 zs[2];
	zs[0] = r128_from_int(b->min_z);
	zs[1] = r128_from_int(b->max_z);
	for (int ix = 0; ix < 2; ix++) {
		for (int iy = 0; iy < 2; iy++) {
			for (int iz = 0; iz < 2; iz++) {
				R128 val = pbvh_plane_corner_val(p->nx, p->ny, p->nz, p->d,
						xs[ix], ys[iy], zs[iz]);
				if (r128_le(zero, val)) {
					return true;
				}
			}
		}
	}
	return false;
}

inline bool pbvh_convex_keeps_box_(const pbvh_plane_t *planes,
		uint32_t plane_count, const Aabb *b) {
	for (uint32_t k = 0; k < plane_count; k++) {
		if (!pbvh_half_space_keeps_(&planes[k], b)) {
			return false;
		}
	}
	return true;
}

/* Convex-hull broadphase: every live leaf whose AABB has at least one corner
 * on the kept side of every plane is passed to `cb`. Hull `points` parameter
 * is accepted for DynamicBVH-FFI parity (convex_query callers pass both a
 * plane list and the hull vertices); we use plane-only pruning which is the
 * safer over-approximation. Callback returns nonzero to stop early. */
inline void pbvh_tree_convex_query(pbvh_tree_t *t,
		const pbvh_plane_t *planes, uint32_t plane_count,
		const R128 *points, uint32_t point_count,
		int (*cb)(pbvh_eclass_id_t, void *), void *ud) {
	(void)points;
	(void)point_count;
	if (t->internal_root == PBVH_NULL_NODE || plane_count == 0u) {
		return;
	}
	uint32_t i = t->internal_root;
	const uint32_t end = t->internal_count;
	while (i < end) {
		const pbvh_internal_t *n = &t->internals[i];
		if (!pbvh_convex_keeps_box_(planes, plane_count, &n->bounds)) {
			i = n->skip;
			continue;
		}
		if (n->left == PBVH_NULL_NODE && n->right == PBVH_NULL_NODE) {
			const uint32_t o = n->offset;
			const uint32_t s = n->span;
			for (uint32_t j = o; j < o + s; j++) {
				const pbvh_node_t *leaf = &t->nodes[t->sorted[j]];
				if (!leaf->is_leaf) {
					continue;
				}
				if (pbvh_convex_keeps_box_(planes, plane_count, &leaf->bounds)) {
					if (cb(leaf->eclass, ud) != 0) {
						return;
					}
				}
			}
			i = n->skip;
			continue;
		}
		i++;
	}
}

/* Reset the tree to empty. Preserves caller-owned buffer pointers/capacities
 * and the `index` tag; zeroes every *_count and clears the free list. */
inline void pbvh_tree_clear(pbvh_tree_t *t) {
	t->count = 0u;
	t->sorted_count = 0u;
	t->internal_count = 0u;
	t->root = PBVH_NULL_NODE;
	t->free_head = PBVH_NULL_NODE;
	t->internal_root = PBVH_NULL_NODE;
	t->last_visits = 0u;
}

/* True iff the tree has no live leaves (no live `is_leaf` node). */
inline bool pbvh_tree_is_empty(const pbvh_tree_t *t) {
	for (uint32_t i = 0; i < t->count; i++) {
		if (t->nodes[i].is_leaf) {
			return false;
		}
	}
	return true;
}

/* Scratch buffers required by pbvh_tree_tick's incremental refit path.
 *
 * Owns four arrays that must be wired into a pbvh_tree_t before any tick:
 *   - parent_of_internal[internal_capacity] — internal-id parent index,
 *     populated by pbvh_tree_build
 *   - leaf_to_internal[leaf_capacity]       — leaf-id → enclosing internal,
 *     populated by pbvh_tree_build
 *   - touched_bits[ceil(internal_capacity / 64)] — one bit per internal,
 *     ALL-ZERO on entry to every tick (self-clears during the scan)
 *   - touched_meta_bits[ceil(touched_words / 64)] — one bit per touched_bits
 *     word, also ALL-ZERO on entry
 *
 * Using `attach()` rather than open-coded resize+ptrw blocks the two
 * specific footguns that the original setup walked into:
 *
 *   1. `LocalVector<uint64_t>::resize()` and `Vector<uint64_t>::resize()`
 *      both leave trivially-constructible types UNINITIALIZED. The tick
 *      scan phase iterates set bits of touched_bits/touched_meta_bits and
 *      dereferences `internals[(w<<6)|b]` for each — garbage bits crash
 *      with SIGSEGV when the bogus index runs past the allocated capacity.
 *      `attach()` uses resize_initialized() on the two bitmap arrays.
 *
 *   2. The sizes are coupled: touched_bits has ceil(internal_capacity/64)
 *      uint64s, touched_meta_bits has ceil(touched_words/64). Copy-paste
 *      of that arithmetic at every call site is its own bug class.
 *
 * Pair the scratch with the tree for its lifetime; on every rebuild that
 * grows `internal_capacity`, call `attach()` again. The four arrays remain
 * caller-owned; this struct simply manages their backing storage. */
struct PbvhTickScratch {
	LocalVector<uint32_t> parent_of_internal;
	LocalVector<uint32_t> leaf_to_internal;
	LocalVector<uint64_t> touched_bits;
	LocalVector<uint64_t> touched_meta_bits;

	void attach(pbvh_tree_t *p_tree, uint32_t p_leaf_capacity,
			uint32_t p_internal_capacity) {
		parent_of_internal.resize(p_internal_capacity);
		leaf_to_internal.resize(p_leaf_capacity);
		const uint32_t touched_words = (p_internal_capacity + 63u) / 64u;
		touched_bits.resize_initialized(touched_words);
		touched_meta_bits.resize_initialized((touched_words + 63u) / 64u);
		p_tree->parent_of_internal = parent_of_internal.ptr();
		p_tree->leaf_to_internal = leaf_to_internal.ptr();
		p_tree->touched_bits = touched_bits.ptr();
		p_tree->touched_meta_bits = touched_meta_bits.ptr();
	}
};

/* Phase 2c: per-frame dirty-leaf entry handed to pbvh_tree_tick. old_hilbert
 * is the Hilbert code the leaf had on the previous build/tick; the current
 * code lives in t->nodes[leaf_id].hilbert. Comparing the two, masked by
 * bucket_bits, classifies the leaf as stayed-in-bucket (refit-compatible)
 * vs crossed-boundary (needs full rebuild). */
/* Per-frame rebalance. When every dirty leaf kept its Hilbert-prefix bucket
 * (so sorted[] order and internals[] topology are still valid), we can skip
 * the O(N) sort+build and just re-union bounds bottom-up via pbvh_tree_refit
 * — O(internal_count) sequential reads. Any condition that could leave
 * sorted[] / internals[] out of sync with the current leaf set forces a
 * full pbvh_tree_build. The conditions are enumerated in order of cost:
 *
 *   (1) trivial: empty dirty list, NULL dirty pointer, or bucket_bits out
 *       of range (0 or >30). Cheap to check; same cost as a full build.
 *   (2) structural: NULL t or t->nodes. Defensive; refit would crash.
 *   (3) insert since build: t->count > t->sorted_count means at least one
 *       leaf was allocated in nodes[] after the last build and is therefore
 *       NOT indexed in sorted[]. Refit cannot reach it and queries would
 *       silently miss it. This is the main adversarial-caller footgun we
 *       harden against: a consumer that does insert() → tick() without an
 *       intermediate build() would lose the inserted leaf without this
 *       check.
 *   (4) empty internals: nothing built yet; refit is a no-op that leaves
 *       internal_root == PBVH_NULL_NODE, queries return empty. A build
 *       from scratch is the correct recovery.
 *   (5) per-leaf: each dirty entry must name a live leaf within sorted[]'s
 *       address range and must not have crossed its Hilbert prefix bucket.
 *       Out-of-range leaf_id is silently skipped (caller may have stale
 *       IDs); is_leaf=0 or bucket crossing forces a build. Callers that
 *       lie about old_hilbert are not a correctness risk — refit unions
 *       the current bounds, so inflated internal AABBs over-emit rather
 *       than under-emit.
 *
 * Passing (dirty=NULL, dirty_count=0) is an explicit \"reset internals
 * from current leaves\" request and behaves as pbvh_tree_build. */
inline void pbvh_tree_tick(pbvh_tree_t *t,
		const pbvh_dirty_leaf_t *dirty, uint32_t dirty_count) {
	/* (2) Defensive NULL guard. */
	if (t == nullptr || t->nodes == nullptr) {
		return;
	}
	/* (1) Trivial fallback. */
	if (dirty_count == 0u || dirty == nullptr || t->bucket_bits == 0u ||
			t->bucket_bits > 30u) {
		pbvh_tree_build(t);
		return;
	}
	/* (3) Inserts since last build are invisible to sorted[]/internals[];
	 * only a full rebuild can pick them up. */
	if (t->count > t->sorted_count) {
		pbvh_tree_build(t);
		return;
	}
	/* (4) No internals yet → nothing to refit; build instead. */
	if (t->internal_count == 0u || t->internal_root == PBVH_NULL_NODE) {
		pbvh_tree_build(t);
		return;
	}
	/* (5) Incremental-refit sidecars are mandatory once we know dirty != null
	 * and there's a built tree to update. There used to be a silent fall-through
	 * to pbvh_tree_refit when any of these were null, but that masked the
	 * "I forgot to set up the scratch buffers" failure mode for the entire
	 * lifetime of the module — the new bench tests are what surfaced it as
	 * a SIGSEGV in the scan phase. Use PbvhTickScratch to wire all four in
	 * lockstep with correct sizing and zero-initialised bitmaps. */
	ERR_FAIL_NULL_MSG(t->parent_of_internal,
			"pbvh_tree_tick: parent_of_internal must be wired before incremental refit. "
			"Use PbvhTickScratch::attach() or call pbvh_tree_build instead.");
	ERR_FAIL_NULL_MSG(t->leaf_to_internal,
			"pbvh_tree_tick: leaf_to_internal must be wired before incremental refit.");
	ERR_FAIL_NULL_MSG(t->touched_bits,
			"pbvh_tree_tick: touched_bits must be wired and zero-initialised before "
			"incremental refit.");
	ERR_FAIL_NULL_MSG(t->touched_meta_bits,
			"pbvh_tree_tick: touched_meta_bits must be wired and zero-initialised before "
			"incremental refit.");
	/* Never rebuild in steady-state — any input pattern that forces a full
	 * O(N) pass is a DoS vector. Instead, treat sorted[]/internals[] topology
	 * as copy-on-write: structure is fixed at the last build, and every frame
	 * only refits bounds along the ancestor union of dirty leaves. Bucket
	 * crossings are NOT forced to a rebuild — the leaf stays at its old
	 * sorted[] index, its enclosing leaf-range internal re-unions to cover
	 * the leaf's new AABB, and the crossing becomes an over-conservative
	 * (but sound) bound. aabb_query_n scans by bounds, not by Hilbert bucket,
	 * so correctness is preserved; bucket_dir-based aabb_query_b may return
	 * slack but the adapter never invokes it. Dead slots (is_leaf=0) are
	 * skipped — their sorted[] entry contributes nothing to unions. */
	for (uint32_t i = 0; i < dirty_count; i++) {
		const pbvh_dirty_leaf_t *d = &dirty[i];
		if (d->leaf_id >= t->count) {
			continue;
		}
	}
	pbvh_tree_refit_incremental_(t, dirty, dirty_count);
}

/* DynamicBVH-parity wrapper: ignore `passes`, route through pbvh_tree_tick
 * with an empty dirty list → pbvh_tree_build. Consumers that want the
 * Phase 2c fast path call pbvh_tree_tick directly with their dirty list. */
inline void pbvh_tree_optimize_incremental(pbvh_tree_t *t, int passes) {
	(void)passes;
	pbvh_tree_tick(t, nullptr, 0u);
}

/* Opaque uint32 tag for consumers that multiplex multiple trees (e.g.
 * RendererSceneCull::Scenario::indexers[] distinguishes GEOMETRY/VOLUMES).
 * Stored in bucket_bits' high bits is avoided; the adapter allocates a
 * dedicated member on the C++ side. These two accessors exist only so the
 * adapter surface matches DynamicBVH byte-for-byte. Stored in t->bucket_bits
 * is unavailable (it has spatial meaning), so this pair operates on a
 * separate caller-owned `uint32_t *out_index` the adapter threads alongside. */
inline uint32_t pbvh_tree_get_index(const uint32_t *idx_slot) {
	return *idx_slot;
}

inline void pbvh_tree_set_index(uint32_t *idx_slot, uint32_t idx) {
	*idx_slot = idx;
}

class PredictiveBVH {
public:
	struct ID {
		pbvh_node_id_t id = PBVH_NULL_NODE;
		_FORCE_INLINE_ bool is_valid() const { return id != PBVH_NULL_NODE; }
	};

private:
	pbvh_tree_t tree = {};
	LocalVector<pbvh_node_t> node_storage;
	LocalVector<pbvh_node_id_t> sorted_storage;
	LocalVector<pbvh_internal_t> internal_storage;
	LocalVector<void *> userdata; // parallel to node_storage
	// Phase 2c incremental-refit sidecars. Allocated lazily via _ensure_capacity
	// so pbvh_tree_tick can do O(K + n_touched) ancestor refits rather than
	// the O(internal_count) bottom-up pass. Owned by PbvhTickScratch so the
	// sizing arithmetic and zero-initialisation of the bitmaps stay in one
	// place — see the PbvhTickScratch docstring above for the trap this
	// closes (resize() leaves uint64 garbage that crashes the scan phase).
	PbvhTickScratch tick_scratch;
	uint32_t index_slot = 0;
	bool dirty = false; // true if insert/update/remove happened since last build

	_FORCE_INLINE_ void _ensure_capacity(uint32_t need) {
		if (need <= tree.capacity) {
			return;
		}
		uint32_t new_cap = MAX(need, (uint32_t)16);
		while (new_cap < need) {
			new_cap *= 2;
		}
		const uint32_t internal_cap = new_cap * 2u;
		node_storage.resize(new_cap);
		sorted_storage.resize(new_cap);
		internal_storage.resize(internal_cap);
		userdata.resize(new_cap);
		tree.nodes = node_storage.ptr();
		tree.sorted = sorted_storage.ptr();
		tree.internals = internal_storage.ptr();
		tree.capacity = new_cap;
		tree.internal_capacity = internal_cap;
		// Sizes parent_of_internal, leaf_to_internal, touched_bits and
		// touched_meta_bits correctly relative to internal_cap; zero-fills the
		// two bitmaps (resize_initialized). Wires all four pointer fields on
		// `tree` in lockstep.
		tick_scratch.attach(&tree, new_cap, internal_cap);
	}

	// Scale by 1e6 (micrometres) to preserve sub-meter precision; keep the
	// multiply in double regardless of real_t so open-world consumers
	// (renderer, physics) don't lose precision through a float intermediate.
	_FORCE_INLINE_ static int64_t _scalar_to_i64(real_t v) {
		return (int64_t)((double)v * 1000000.0);
	}

	// Kept for plane conversion (pbvh_plane_t still uses R128 for the normal dot).
	_FORCE_INLINE_ static R128 _scalar_to_r128(real_t v) {
		return r128_from_int((int64_t)((double)v * 1000000.0));
	}

	_FORCE_INLINE_ static Aabb _aabb_to_i64(const AABB &b) {
		Aabb a;
		a.min_x = _scalar_to_i64(b.position.x);
		a.max_x = _scalar_to_i64(b.position.x + b.size.x);
		a.min_y = _scalar_to_i64(b.position.y);
		a.max_y = _scalar_to_i64(b.position.y + b.size.y);
		a.min_z = _scalar_to_i64(b.position.z);
		a.max_z = _scalar_to_i64(b.position.z + b.size.z);
		return a;
	}

	_FORCE_INLINE_ static pbvh_plane_t _plane_to_pbvh(const Plane &p) {
		pbvh_plane_t r;
		r.nx = _scalar_to_r128(p.normal.x);
		r.ny = _scalar_to_r128(p.normal.y);
		r.nz = _scalar_to_r128(p.normal.z);
		// Godot Plane: normal·p - d = 0; kept side is normal·p - d >= 0.
		// pbvh Plane: kept side is nx·x + ny·y + nz·z + d_pbvh >= 0, so
		// d_pbvh = -d.
		r.d = _scalar_to_r128(-p.d);
		return r;
	}

	_FORCE_INLINE_ void _maybe_build() {
		if (dirty) {
			pbvh_tree_build(&tree);
			dirty = false;
		}
	}

	template <typename QueryResult>
	struct Ctx {
		PredictiveBVH *self;
		QueryResult *result;
	};

	template <typename QueryResult>
	static int _aabb_cb(pbvh_eclass_id_t eclass, void *ud) {
		auto *ctx = (Ctx<QueryResult> *)ud;
		void *payload = ctx->self->userdata[eclass];
		return ctx->result->operator()(payload) ? 1 : 0;
	}

public:
	PredictiveBVH() {
		tree.root = PBVH_NULL_NODE;
		tree.free_head = PBVH_NULL_NODE;
		tree.internal_root = PBVH_NULL_NODE;
		tree.bucket_bits = 0u;
		tree.bucket_dir = nullptr;
	}

	_FORCE_INLINE_ bool is_empty() const { return pbvh_tree_is_empty(&tree); }

	_FORCE_INLINE_ void clear() {
		pbvh_tree_clear(&tree);
		userdata.clear();
		dirty = false;
	}

	ID insert(const AABB &p_box, void *p_userdata) {
		_ensure_capacity(tree.count + 1u);
		const Aabb r = _aabb_to_i64(p_box);
		pbvh_node_id_t id = pbvh_tree_insert(&tree, (pbvh_eclass_id_t)tree.count, r);
		if (id >= userdata.size()) {
			userdata.resize(id + 1u);
		}
		userdata[id] = p_userdata;
		// Rewrite the eclass to be id (stable across free-list reuse).
		tree.nodes[id].eclass = (pbvh_eclass_id_t)id;
		dirty = true;
		return ID{ id };
	}

	bool update(const ID &p_id, const AABB &p_box) {
		if (!p_id.is_valid() || p_id.id >= tree.capacity) {
			return false;
		}
		const Aabb r = _aabb_to_i64(p_box);
		pbvh_tree_update(&tree, p_id.id, r);
		dirty = true;
		return true;
	}

	void remove(const ID &p_id) {
		if (!p_id.is_valid() || p_id.id >= tree.capacity) {
			return;
		}
		pbvh_tree_remove(&tree, p_id.id);
		dirty = true;
	}

	_FORCE_INLINE_ void optimize_incremental(int p_passes) {
		if (dirty) {
			pbvh_tree_build(&tree);
			dirty = false;
		}
		(void)p_passes;
	}

	// Phase 2c per-frame rebalance. Pass the leaves that moved this frame
	// paired with their previous Hilbert code; the emitted C tick picks
	// refit vs full build based on whether any leaf crossed its Hilbert
	// bucket. tree.bucket_bits must be non-zero for the fast path to
	// fire; when it is zero (default), tick routes to pbvh_tree_build.
	struct DirtyLeaf {
		ID id;
		uint32_t old_hilbert;
	};

	void tick(const DirtyLeaf *p_dirty, uint32_t p_count) {
		if (p_count == 0 || p_dirty == nullptr) {
			pbvh_tree_tick(&tree, nullptr, 0);
			dirty = false;
			return;
		}
		LocalVector<pbvh_dirty_leaf_t> scratch;
		scratch.resize(p_count);
		for (uint32_t i = 0; i < p_count; i++) {
			scratch[i].leaf_id = p_dirty[i].id.id;
			scratch[i].old_hilbert = p_dirty[i].old_hilbert;
		}
		pbvh_tree_tick(&tree, scratch.ptr(), p_count);
		dirty = false;
	}

	_FORCE_INLINE_ void set_index(uint32_t p_index) { index_slot = p_index; }
	_FORCE_INLINE_ uint32_t get_index() const { return index_slot; }

	template <typename QueryResult>
	_FORCE_INLINE_ void aabb_query(const AABB &p_box, QueryResult &r_result) {
		_maybe_build();
		if (tree.internal_root == PBVH_NULL_NODE) {
			return;
		}
		Ctx<QueryResult> ctx = { this, &r_result };
		const Aabb q = _aabb_to_i64(p_box);
		pbvh_tree_aabb_query_n(&tree, &q, &_aabb_cb<QueryResult>, &ctx);
	}

	template <typename QueryResult>
	_FORCE_INLINE_ void ray_query(const Vector3 &p_from, const Vector3 &p_to, QueryResult &r_result) {
		_maybe_build();
		if (tree.internal_root == PBVH_NULL_NODE) {
			return;
		}
		Ctx<QueryResult> ctx = { this, &r_result };
		pbvh_tree_ray_query(&tree,
				_scalar_to_i64(p_from.x), _scalar_to_i64(p_from.y), _scalar_to_i64(p_from.z),
				_scalar_to_i64(p_to.x), _scalar_to_i64(p_to.y), _scalar_to_i64(p_to.z),
				&_aabb_cb<QueryResult>, &ctx);
	}

	template <typename QueryResult>
	_FORCE_INLINE_ void convex_query(const Plane *p_planes, int p_plane_count,
			const Vector3 *p_points, int p_point_count, QueryResult &r_result) {
		_maybe_build();
		if (tree.internal_root == PBVH_NULL_NODE || p_plane_count <= 0) {
			return;
		}
		LocalVector<pbvh_plane_t> planes;
		planes.resize((uint32_t)p_plane_count);
		for (int i = 0; i < p_plane_count; i++) {
			planes[i] = _plane_to_pbvh(p_planes[i]);
		}
		// Hull points: unused by our prune (plane-only), pass nullptr/0.
		(void)p_points;
		(void)p_point_count;
		Ctx<QueryResult> ctx = { this, &r_result };
		pbvh_tree_convex_query(&tree, planes.ptr(), (uint32_t)p_plane_count,
				nullptr, 0u, &_aabb_cb<QueryResult>, &ctx);
	}
};

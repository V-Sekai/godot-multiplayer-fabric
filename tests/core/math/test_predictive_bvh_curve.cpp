/**************************************************************************/
/*  test_predictive_bvh_curve.cpp                                         */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_predictive_bvh_curve)

#include "core/math/aabb.h"
#include "core/math/predictive_bvh_adapter.h"
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"

// The predictive BVH is ordered by a space-filling code: every node stores one and
// pbvh_tree_build radix-sorts over it. Nothing asserted that the codes were ever
// distinct, and they were not — PredictiveBVH::insert passed 0u for every node, so
// the sort ordered a constant and the tree fell back to insertion order. The index
// still returned correct results, which is why it went unnoticed.
//
// These cases assert the properties that make the structure the structure, rather
// than the ones that merely happen to hold. Every previous check on this code path
// was of the second kind: the Hilbert round-trip closed, the bijection held, and
// the curve was still wrong.

namespace TestPredictiveBVHCurve {

TEST_CASE("[PredictiveBVH] Morton code round-trips") {
	for (uint32_t x = 0; x < 1024; x += 37) {
		for (uint32_t y = 0; y < 1024; y += 53) {
			for (uint32_t z = 0; z < 1024; z += 71) {
				uint32_t a = 0, b = 0, c = 0;
				pbvh_morton3d_inverse(pbvh_morton3d(x, y, z), &a, &b, &c);
				CHECK(a == x);
				CHECK(b == y);
				CHECK(c == z);
			}
		}
	}
}

TEST_CASE("[PredictiveBVH] Morton prefix is the octree cell") {
	// The partitioning depends on this and only this: the top 3d bits of a code
	// name the octree cell of side 2^(10-d). For a bit interleave it is the
	// definition; for a rotation-based curve it is a consequence that has to be
	// implemented correctly, which is the part that failed twice.
	for (uint32_t depth = 1; depth <= 10; depth++) {
		const uint32_t shift = 10 - depth;
		for (uint32_t x = 3; x < 1024; x += 101) {
			for (uint32_t y = 7; y < 1024; y += 103) {
				const uint32_t code = pbvh_morton3d(x, y, x ^ y);
				const uint32_t origin = pbvh_morton3d((x >> shift) << shift,
						(y >> shift) << shift, ((x ^ y) >> shift) << shift);
				CHECK((code >> (3 * shift)) == (origin >> (3 * shift)));
			}
		}
	}
}

TEST_CASE("[PredictiveBVH] insert assigns distinct codes to distinct positions") {
	// THE REGRESSION. Before the fix every node carried 0, so this collapsed to a
	// single distinct code however far apart the boxes were placed.
	PredictiveBVH bvh;
	const int N = 64;
	LocalVector<PredictiveBVH::ID> ids;
	for (int i = 0; i < N; i++) {
		// One cell is WORLD_BOUND_UM*2 / 1024 ~= 1.95 m, so boxes must be spaced wider
		// than that to be guaranteed distinct codes. At 1 m apart, 64 boxes landed in 53
		// cells -- correct quantisation, not a defect, but it makes a poor assertion.
		const float f = (float)i * 8.0f;
		ids.push_back(bvh.insert(AABB(Vector3(f, f * 0.5f, -f), Vector3(0.5f, 0.5f, 0.5f)), nullptr));
	}

	HashSet<uint32_t> seen;
	uint32_t nonzero = 0;
	for (int i = 0; i < N; i++) {
		const uint32_t code = bvh.debug_code(ids[i]);
		seen.insert(code);
		if (code != 0u) {
			nonzero++;
		}
	}
	CHECK_MESSAGE(nonzero == (uint32_t)N, "every inserted node must carry a spatial code");
	CHECK_MESSAGE(seen.size() == (uint32_t)N, "distinct positions must not share one code");
}

TEST_CASE("[PredictiveBVH] update keeps the code in step with the box") {
	PredictiveBVH bvh;
	PredictiveBVH::ID id = bvh.insert(AABB(Vector3(0, 0, 0), Vector3(1, 1, 1)), nullptr);
	const uint32_t before = bvh.debug_code(id);
	bvh.update(id, AABB(Vector3(120, -40, 75), Vector3(1, 1, 1)));
	CHECK_MESSAGE(bvh.debug_code(id) != before, "moving a node must move its code");
}

} // namespace TestPredictiveBVHCurve

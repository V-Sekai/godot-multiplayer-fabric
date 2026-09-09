/**************************************************************************/
/*  cassie_remesh.h                                                       */
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

#include "core/variant/variant.h"

#include <cstdint>

// Uniform remeshing with edge collapse and surface projection.
//
// p_ref_verts / p_ref_indices: original DMWT surface to project onto after
// each smooth pass. Pass empty arrays to skip projection.
void cassie_remesh(PackedVector3Array &p_verts, PackedInt32Array &p_indices,
		float p_target_edge_length, int p_iterations = 3,
		const PackedVector3Array &p_ref_verts = PackedVector3Array(),
		const PackedInt32Array &p_ref_indices = PackedInt32Array());

// ENG-87 sub-stage probe. cassie_remesh populates these accumulators when
// CASSIE_REFINE_PROFILE is set in the environment. Values are summed across
// all iterations of a single cassie_remesh() call and reset at the start of
// every call. Zero overhead when the env var is unset.
struct CassieRefineProfile {
	uint64_t bvh_build_us = 0;
	uint64_t split_us = 0;
	uint64_t collapse_us = 0;
	uint64_t flip_us = 0;
	uint64_t smooth_us = 0;
	uint64_t adjacency_us = 0; // sum of rebuild_adjacency + detect_boundary
	int iterations = 0;

	// flip_edges sub-counters (populated when CASSIE_REFINE_PROFILE is set)
	uint64_t flip_us_iter[3] = { 0, 0, 0 }; // per cassie_remesh outer iter
	int flip_guard_iters = 0; // total inner passes
	int flip_calls = 0; // # of flip_edges invocations
	int flips_committed = 0; // # of edges actually flipped
	int circumcircle_calls = 0; // # of in_circumcircle invocations
};

const CassieRefineProfile &cassie_refine_last_profile();

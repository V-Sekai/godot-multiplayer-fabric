/**************************************************************************/
/*  cassie_sketcher.h                                                     */
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

#include "cassie_beautifier.h"
#include "cassie_beautifier_params.h"
#include "sketch/cassie_final_stroke.h"
#include "sketch/cassie_input_stroke.h"
#include "sketch/cassie_sketch_graph.h"
#include "sketch/cassie_surface_manager.h"

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "scene/3d/node_3d.h"

// CassieSketcher — single-node entry point that fuses sample buffering,
// beautify, FinalStroke commit + intersection split, planar-graph
// arrangement, and reactive cycle → patch mesh creation. Lives in the
// engine module so xr-grid just instantiates one node and wires it to
// hand input + a fabric channel.
//
// Implements the FITTING → SOLVING → commit → triangulate state machine, run
// synchronously inside commit_stroke() instead of one phase per frame.
// Synchronous because the multiplayer determinism contract requires
// that all peers run the same chain on the same bits in the same order.
//
// API contract for callers:
//   var sid = sketcher.begin_stroke(local_pos, pressure)
//   for each sample frame:
//       sketcher.add_sample(sid, local_pos, pressure)
//   var result = sketcher.commit_stroke(sid)  # synchronous
//   var packet_bytes = sketcher.encode_stroke_packet(sid)
//   # broadcast packet_bytes over the fabric reliable channel
//
// Remote peers:
//   sketcher.apply_remote_samples(packet_bytes)
//   # runs the same chain on the decoded samples
//
// Determinism rules embedded here:
//   - creation_time = sample_index * sample_dt (no wall clock)
//   - CassieSurfaceManager.async_triangulation = false by default
//   - all per-stroke state keyed by int stroke_id, not pointer identity

class CassieSketcher : public Node3D {
	GDCLASS(CassieSketcher, Node3D);

	struct InFlightStroke {
		Ref<CassieInputStroke> input;
		int sample_index = 0;
	};

	HashMap<int, InFlightStroke> in_flight;
	int next_stroke_id = 1;

	Ref<CassieBeautifier> beautifier;
	Ref<CassieBeautifierParams> beautifier_params;
	Ref<CassieSketchContext> sketch_context;
	Ref<CassieSketchGraph> sketch_graph;
	Ref<CassieSurfaceManager> surface_manager;

	TypedArray<CassieFinalStroke> committed_strokes;

	float sample_dt = 1.0f / 60.0f;
	int64_t peer_id = 0;
	uint16_t broadcast_seq = 0;

	// Last encoded packet, keyed by stroke_id. Kept so the
	// caller can pull bytes for fabric broadcast after commit. Cleared
	// when the stroke_id is committed and broadcast.
	HashMap<int, PackedByteArray> last_encoded_packet;

	void _ensure_owned_state();

	Dictionary _run_chain_locally(const Ref<CassieInputStroke> &p_input,
			bool p_emit_signals);

	Dictionary _drain_patches();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	CassieSketcher();

	void set_beautifier_params(const Ref<CassieBeautifierParams> &p_params);
	Ref<CassieBeautifierParams> get_beautifier_params() const { return beautifier_params; }

	void set_sample_dt(float p_dt) { sample_dt = MAX(p_dt, 1e-6f); }
	float get_sample_dt() const { return sample_dt; }

	void set_peer_id(int64_t p_id) { peer_id = p_id; }
	int64_t get_peer_id() const { return peer_id; }

	void set_async_triangulation(bool p_enable);
	bool get_async_triangulation() const;

	Ref<CassieSketchGraph> get_sketch_graph() const { return sketch_graph; }
	Ref<CassieSurfaceManager> get_surface_manager() const { return surface_manager; }
	Ref<CassieSketchContext> get_sketch_context() const { return sketch_context; }
	TypedArray<CassieFinalStroke> get_committed_strokes() const { return committed_strokes; }

	// Sample lifecycle ----------------------------------------------------
	int begin_stroke(const Vector3 &p_local_position, float p_pressure);
	void add_sample(int p_stroke_id, const Vector3 &p_local_position, float p_pressure);

	// Synchronously runs Beautify → FinalStroke commit → graph add → patch
	// update. Returns the same shape as _run_chain_locally:
	//   ok              : bool
	//   is_valid        : bool  (from beautifier)
	//   final_stroke    : Ref<CassieFinalStroke> or null
	//   new_patches     : TypedArray<CassieSurfacePatch>
	//   removed_patches : TypedArray<CassieSurfacePatch>
	Dictionary commit_stroke(int p_stroke_id);

	// Decode a packet produced by encode_stroke_packet on another peer
	// and run the same local chain. The packet's creation_time is
	// reconstructed from sample_index * sample_dt to match the
	// originating peer.
	Dictionary apply_remote_samples(const PackedByteArray &p_packet);

	// Returns the byte packet for the most recently committed stroke
	// matching p_stroke_id, or an empty PackedByteArray if none.
	PackedByteArray encode_stroke_packet(int p_stroke_id);

	void clear();
};

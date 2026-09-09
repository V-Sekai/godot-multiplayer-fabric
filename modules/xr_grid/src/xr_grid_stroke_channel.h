/**************************************************************************/
/*  xr_grid_stroke_channel.h                                              */
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

#include "core/object/ref_counted.h"
#include "core/templates/local_vector.h"
#include "core/variant/typed_array.h"
#include "scene/main/multiplayer_peer.h"

// XRGridStrokeChannel — engine-native bridge between CassieSketcher
// (modules/cassie/) and xr-grid's WebTransport-based fabric
// (modules/xr_grid/project/addons/procedural_3d_grid/core/fabric/).
//
// The xr-grid GDScript FabricManager exposes a single send_entity()
// path that uses the unreliable transfer mode (UDP-equivalent, fixed
// 100-byte entity packets, 30 Hz). Stroke packets are variable-length
// (~36 to ~2 KB depending on sample count) and must be reliable — a
// dropped stroke means a missing piece of the shared canvas.
//
// This class:
//   - holds a Ref to a MultiplayerPeer the GDScript layer can supply
//     (typically the same WebTransportPeer that FabricManager already
//     owns; we use a fresh transfer_channel index so reliable strokes
//     don't fight with the unreliable transform fan-out)
//   - exposes send_stroke(packet) which sets transfer_mode = RELIABLE,
//     transfer_channel = stroke_channel, target_peer = 0 (broadcast),
//     and puts the packet
//   - drains incoming packets each tick, emits stroke_packet_received
//     for those with the 'CSP1' magic (which CassieSketcher decodes
//     via apply_remote_samples)
//
// Keeping this in C++ means the determinism contract — that committed
// stroke packets go out the wire as the same bytes every peer
// receives — stays free of GDScript-side coercion / reordering.

class XRGridStrokeChannel : public RefCounted {
	GDCLASS(XRGridStrokeChannel, RefCounted);

	Ref<MultiplayerPeer> peer;
	int stroke_channel = 1;
	int reliable_send_count = 0;
	int reliable_receive_count = 0;

protected:
	static void _bind_methods();

public:
	XRGridStrokeChannel() = default;

	// Same wire magic as CassieStrokePacket. Duplicated rather than
	// included to keep modules/xr_grid free of a hard dependency on
	// modules/cassie/ at the link level — sniffing 4 bytes is cheap.
	static constexpr uint32_t MAGIC = 0x31'50'53'43u; // 'CSP1' LE

	void set_peer(const Ref<MultiplayerPeer> &p_peer) { peer = p_peer; }
	Ref<MultiplayerPeer> get_peer() const { return peer; }

	void set_stroke_channel(int p_ch) { stroke_channel = MAX(1, p_ch); }
	int get_stroke_channel() const { return stroke_channel; }

	int get_reliable_send_count() const { return reliable_send_count; }
	int get_reliable_receive_count() const { return reliable_receive_count; }

	// Send a stroke packet over the reliable channel. Returns OK on
	// success, ERR_UNAVAILABLE if no peer is set or the connection
	// isn't open. Configures the peer's transfer mode just for this
	// put_packet then leaves the mode untouched (peer-level toggles
	// are sticky on most MultiplayerPeer implementations).
	Error send_stroke(const PackedByteArray &p_packet);

	// Drain all pending packets from the peer. Packets with the 'CSP1'
	// magic emit stroke_packet_received; everything else is passed
	// through entity_packet_received so the existing FabricManager
	// dispatch isn't disrupted.
	void poll();
};

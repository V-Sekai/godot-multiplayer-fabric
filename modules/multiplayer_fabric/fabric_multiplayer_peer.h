/**************************************************************************/
/*  fabric_multiplayer_peer.h                                             */
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

#include "core/templates/hash_map.h"
#include "core/variant/callable.h"
#include "scene/main/multiplayer_peer.h"

// Three independent peer connections per neighbor, one per channel, each on
// its own port (base+1/+2/+3). Channel identity is known from which peer
// received the packet — no channel byte in the payload, no HOL blocking.
static constexpr int CH_MIGRATION = 1; // reliable   — STAGING intents
static constexpr int CH_INTEREST = 2; // unreliable — entity snapshots
static constexpr int CH_PLAYER = 3; // unreliable — player state

/// Zone-fabric multiplayer peer driven by user-supplied peer factories.
/// Works with any MultiplayerPeer backend (WebTransport, ENet, WebRTC …).
///
/// Setup (WebTransport example):
///   var fab = FabricMultiplayerPeer.new()
///   fab.frame_channels = true          # WT has no native channel concept
///   fab.server_factory = func(port):
///       var p = WebTransportPeer.new(); p.create_server(port, "/wt", cert, key); return p
///   fab.client_factory = func(host, port):
///       var p = WebTransportPeer.new(); p.create_client(host, port, "/wt"); return p
///   fab.create_server(7000)
///   # Binds port 7000 (game clients) + 7001/7002/7003 (per-channel neighbor links)
///
/// Setup (ENet example):
///   fab.frame_channels = false         # ENet carries channel natively
///   fab.server_factory = func(port):
///       var p = ENetMultiplayerPeer.new(); p.create_server(port); return p
///   fab.client_factory = func(host, port):
///       var p = ENetMultiplayerPeer.new(); p.create_client(host, port); return p
///   fab.create_server(7000)
///
/// Setup (WebSocket example — caveat: CH_INTEREST and CH_PLAYER lose their
/// unreliable semantics and become reliable because WebSocket (TCP) cannot
/// drop packets. Each zone-to-zone channel still gets its own connection so
/// there is no cross-channel HOL, but a stalled CH_INTEREST stream will
/// back-pressure that connection rather than silently dropping stale snapshots):
///   fab.frame_channels = true          # WebSocket has no native channels
///   fab.server_factory = func(port):
///       var p = WebSocketMultiplayerPeer.new(); p.create_server(port); return p
///   fab.client_factory = func(host, port):
///       var p = WebSocketMultiplayerPeer.new()
///       p.create_client("ws://%s:%d" % [host, port]); return p
///   fab.create_server(7000)
///
/// HOL-free guarantee: each channel gets an independent connection on its own
/// port, so no channel can stall another. For reliable delivery, the underlying
/// peer must open independent streams per packet (WebTransport does; ENet has
/// per-connection in-order delivery, which is acceptable for low-traffic CH_MIGRATION).
class FabricMultiplayerPeer : public MultiplayerPeer {
	GDCLASS(FabricMultiplayerPeer, MultiplayerPeer);

private:
	String game_id;

	// Factory callables — must be set before create_server() / create_client().
	// server_factory: (port: int) -> MultiplayerPeer
	// client_factory: (host: String, port: int) -> MultiplayerPeer
	Callable server_factory;
	Callable client_factory;

	// When true the server_peer (game-client side) uses wtd frame encoding to
	// carry the logical channel, because the underlying transport has no native
	// channel concept (WebTransport). When false uses set_transfer_channel /
	// get_packet_channel (ENet and similar native-channel transports).
	bool frame_channels = false;

	// Game-client server peer (base port).
	Ref<MultiplayerPeer> server_peer;
	// Inbound per-channel servers for zone-to-zone neighbor links (port+1/+2/+3).
	Ref<MultiplayerPeer> channel_servers[3];

	struct NeighborConn {
		// channel_peers[0]=CH_MIGRATION(port+1), [1]=CH_INTEREST(port+2), [2]=CH_PLAYER(port+3).
		Ref<MultiplayerPeer> channel_peers[3];
		bool connected[3] = { false, false, false };
	};
	HashMap<int, NeighborConn> neighbors;

	// Channel-sorted inboxes filled during poll(). One per logical channel so
	// drain_channel_raw(CH_PLAYER) never returns CH_INTEREST packets and vice
	// versa — otherwise 100-byte entity snapshots get parsed as player upserts
	// and phantom-fill the zone to capacity.
	LocalVector<Vector<uint8_t>> migration_inbox; // CH_MIGRATION (reliable)
	LocalVector<Vector<uint8_t>> interest_inbox; // CH_INTEREST  (unreliable)
	LocalVector<Vector<uint8_t>> player_inbox; // CH_PLAYER    (unreliable)

	// Current packet state for MultiplayerPeer interface.
	Vector<uint8_t> current_packet_data;
	int32_t current_packet_peer = 0;
	int32_t current_packet_channel = 0;
	TransferMode current_packet_mode = TRANSFER_MODE_RELIABLE;

	uint16_t base_port = 0;

	// p_known_channel > 0: all packets go to that inbox (neighbor channel_peers).
	// p_known_channel == 0: derive channel per-packet:
	//   frame_channels=true  → wtd frame decode (WT server_peer)
	//   frame_channels=false → get_packet_channel() (ENet server_peer)
	void _poll_peer(Ref<MultiplayerPeer> p_peer, int p_known_channel);

	// Send to a neighbor channel_peer — channel is implicit from peer slot.
	void _send_to_channel_peer(Ref<MultiplayerPeer> p_peer, int p_channel, const uint8_t *p_data, int p_size);

	// Send to server_peer (game clients), encoding channel appropriately.
	void _send_to_server_peer(int p_channel, const uint8_t *p_data, int p_size);

	// Instantiate a peer via factory callable with error checking.
	Ref<MultiplayerPeer> _make_server_peer(int p_port);
	Ref<MultiplayerPeer> _make_client_peer(const String &p_host, int p_port);

protected:
	static void _bind_methods();

public:
	Error create_server(int p_port);
	Error create_client(const String &p_address, int p_port);

	void set_server_factory(const Callable &p_factory);
	Callable get_server_factory() const;

	void set_client_factory(const Callable &p_factory);
	Callable get_client_factory() const;

	void set_frame_channels(bool p_enabled);
	bool get_frame_channels() const;

	void set_game_id(const String &p_id);
	String get_game_id() const;

	// Zone-fabric API (GDScript-callable).
	void connect_to_zone(int p_target_zone_id);
	// Internal: connect to a neighbor by explicit port (avoids base_port arithmetic).
	void connect_to_zone_at(int p_target_zone_id, int p_target_port);
	bool is_zone_connected(int p_zone_id) const;
	void send_to_zone(int p_target_zone_id, int p_channel, const PackedByteArray &p_data);
	void broadcast_to_zones(int p_channel, const PackedByteArray &p_data);
	Array drain_channel(int p_channel);

	// Raw-pointer overloads for internal C++ callers (avoids PackedByteArray alloc).
	void send_to_zone_raw(int p_target_zone_id, int p_channel, const uint8_t *p_data, int p_size);
	void broadcast_raw(int p_channel, const uint8_t *p_data, int p_size);
	// Local-only fanout: writes once to the server peer with target_peer=0 so
	// attached clients receive the packet, but does NOT forward to neighbor
	// zones. Used by the CH_INTEREST relay to duplicate a neighbor row to local
	// clients without re-fanning it back across the link it arrived on.
	void local_broadcast_raw(int p_channel, const uint8_t *p_data, int p_size);
	LocalVector<Vector<uint8_t>> drain_channel_raw(int p_channel);

	// MultiplayerPeer interface.
	virtual void set_target_peer(int p_peer_id) override;
	virtual int get_packet_peer() const override;
	virtual TransferMode get_packet_mode() const override;
	virtual int get_packet_channel() const override;
	virtual void disconnect_peer(int p_peer, bool p_force = false) override;
	virtual bool is_server() const override;
	virtual void poll() override;
	virtual void close() override;
	virtual int get_unique_id() const override;
	virtual ConnectionStatus get_connection_status() const override;
	virtual bool is_server_relay_supported() const override;

	// PacketPeer interface.
	virtual Error get_packet(const uint8_t **r_buffer, int &r_buffer_size) override;
	virtual Error put_packet(const uint8_t *p_buffer, int p_buffer_size) override;
	virtual int get_available_packet_count() const override;
	virtual int get_max_packet_size() const override;

	FabricMultiplayerPeer() = default;
	~FabricMultiplayerPeer() = default;
};

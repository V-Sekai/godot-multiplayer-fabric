/**************************************************************************/
/*  web_transport_peer.h                                                  */
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

#include "quic_client.h"

#include "core/crypto/crypto.h"
#include "core/os/mutex.h"
#include "core/templates/hash_set.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"
#include "scene/main/multiplayer_peer.h"

// MultiplayerPeer driven by one QUIC connection. Maps Godot's transfer modes
// onto QUIC features:
//   TRANSFER_MODE_UNRELIABLE / UNRELIABLE_ORDERED  -> QUIC DATAGRAMs
//   TRANSFER_MODE_RELIABLE                          -> per-packet bidi streams
// Each reliable packet opens a fresh bidirectional stream, writes the payload
// + FIN in one go, and the peer's stream_read drains the whole packet at once.
// Unreliable packets go through the datagram channel directly.
//
// Peer addressing: this scaffold handles the single-connection case, which is
// what a client or the listen side of a one-server-one-client session looks
// like. Peer id 1 is the remote; peer id of the local side is generated.
class WebTransportPeer : public MultiplayerPeer {
	GDCLASS(WebTransportPeer, MultiplayerPeer);

public:
	enum Mode {
		MODE_NONE,
		MODE_CLIENT,
		MODE_SERVER,
	};

	// Explicit WT session lifecycle — not derived from cnx_state polling.
	// Driven by picohttp callback events in _wt_app_callback.
	enum SessionState {
		SESSION_DISCONNECTED,
		SESSION_QUIC_HANDSHAKING,
		SESSION_H3_SETTINGS,
		SESSION_WT_CONNECTING, // Extended CONNECT sent, waiting for 200
		SESSION_OPEN, // connect_accepted — datagrams + streams allowed
		SESSION_CLOSED,
	};

	WebTransportPeer();
	~WebTransportPeer();

	// Client-side dial. When the WT-session backend is registered, routes
	// Client: dials a WebTransport server (spec-correct H3/picowt path).
	// Falls back to raw QUIC ALPN "webtransport" if no session backend registered.
	Error create_client(const String &p_host, int p_port, const String &p_path = "/");

	// Server: starts the QUIC/WT server with a caller-provided cert and key.
	// Build cert+key in GDScript via Crypto.generate_ecdsa() +
	// Crypto.generate_self_signed_certificate_san().
	Error create_server(int p_port, const String &p_path, Ref<X509Certificate> p_cert, Ref<CryptoKey> p_key);

	// Server backend contract.
	//   cert_der / cert_der_len: raw DER bytes (from X509Certificate::get_der())
	//   key_pem:  NUL-terminated PEM private key (picoquic's ptls parses PEM natively)
	static Error (*server_listen_func)(WebTransportPeer *peer, int port, const String &path,
			const uint8_t *cert_der, size_t cert_der_len, const char *key_pem);
	static void (*server_close_func)();
	// target <= 0 broadcasts to every session; > 0 routes to that peer id.
	static Error (*server_send_datagram_func)(int target, const uint8_t *bytes, size_t len);
	static Error (*server_send_stream_func)(int target, const uint8_t *bytes, size_t len);
	// Drops one session. There is deliberately no broadcast form: closing everybody is
	// `close()`, and a `disconnect_peer(0)` that stopped the listener is the bug this exists
	// to remove.
	static Error (*server_disconnect_peer_func)(int target);

	// Called by the backend when a WT session opens or ends, from the picoquic network
	// thread. They record the peer and leave the announcement to poll(): a signal emitted
	// off the main thread reaches script and the SceneTree from the wrong one, and the
	// crash it causes lands nowhere near the network code that caused it.
	void _server_peer_joined(int p_peer_id);
	void _server_peer_left(int p_peer_id);

	// Whether any session is open. Derived from the roster rather than assigned, because a
	// bool cannot count: with two clients, the first to leave used to clear it while the
	// second was still connected, and the server stopped sending to a peer that was there.
	bool has_sessions() const;

	// WT-session backend contract (meshoptimizer pattern). Populated by
	// the picoquic module on initialize; null on platforms without picoquic.
	//  create_session_backend_func: picowt_prepare_client_cnx + picowt_connect
	//  destroy_session_backend_func: picowt_send_close_session_message + free
	static void *(*create_session_backend_func)(WebTransportPeer *peer,
			const char *host, int port, const char *path);
	static void (*destroy_session_backend_func)(void *ctx);
	// Session state query — returns SessionState enum value from WTSessionCtx.
	static int (*session_state_func)(void *ctx);
	// Set session state from the backend (callback thread → main thread).
	static void (*set_session_state_func)(void *ctx, int state);
	bool _has_session_backend() const { return session_ctx != nullptr; }
	void *_get_session_ctx() const { return session_ctx; }
	SessionState get_session_state() const;

	// Send a WT datagram through the session.
	static Error (*send_wt_datagram_func)(void *ctx, const uint8_t *bytes, size_t len);
	// Send reliable data on a new WT bidi stream (open + send + FIN).
	static Error (*send_wt_stream_func)(void *ctx, const uint8_t *bytes, size_t len);

	// Called by the backend when a WT datagram arrives via the session.
	void _push_wt_incoming_datagram(const uint8_t *p_bytes, size_t p_len, int p_from = 1);
	// Called by the backend when WT stream data arrives (reliable).
	void _push_wt_incoming_stream(const uint8_t *p_bytes, size_t p_len, int p_from = 1, int p_channel = 0);

	// In-process echo server for loopback testing.
	static Error (*start_echo_server_func)(int port);
	static void (*stop_echo_server_func)();

	Ref<QUICClient> get_quic() const { return quic; }
	void _bind_quic(const Ref<QUICClient> &p_quic, Mode p_mode);

	// PacketPeer overrides.
	virtual int get_available_packet_count() const override;
	virtual Error get_packet(const uint8_t **r_buffer, int &r_buffer_size) override;
	virtual Error put_packet(const uint8_t *p_buffer, int p_buffer_size) override;
	virtual int get_max_packet_size() const override;

	// MultiplayerPeer overrides.
	virtual void set_target_peer(int p_peer) override;
	virtual int get_packet_peer() const override;
	virtual TransferMode get_packet_mode() const override;
	virtual int get_packet_channel() const override;

	virtual void poll() override;
	virtual void close() override;
	virtual void disconnect_peer(int p_peer, bool p_force = false) override;

	virtual bool is_server() const override;
	virtual int get_unique_id() const override;
	virtual ConnectionStatus get_connection_status() const override;

protected:
	static void _bind_methods();

private:
	struct IncomingPacket {
		Vector<uint8_t> bytes;
		TransferMode mode = TRANSFER_MODE_UNRELIABLE;
		int channel = 0;
		int from = 1;
	};

	Ref<QUICClient> quic;
	void *session_ctx = nullptr; // set when connect_to_host uses the WT-session backend
	Mode mode = MODE_NONE;
	int target_peer = 0;
	int unique_id = 0;

	// Peer-initiated streams we haven't yet fully drained.
	List<int64_t> pending_peer_streams;

	// Parsed packets ready for get_packet(). Written from the picoquic network
	// thread (_push_wt_incoming_*) and read from the main thread (get_packet),
	// so every touch of `incoming` and its cached size MUST hold this mutex —
	// otherwise the unguarded read-modify-write on the list's size strands the
	// newest session's packets (proven in the http3_queue Lean/Plausible model).
	// `mutable` so the const get_available_packet_count() can lock.
	mutable Mutex incoming_mutex;
	List<IncomingPacket> incoming;

	// Set on get_packet, read back for get_packet_mode / _channel / _peer.
	PackedByteArray current_packet_bytes; // owns the buffer returned via r_buffer
	TransferMode current_mode = TRANSFER_MODE_UNRELIABLE;
	int current_channel = 0;
	int current_peer = 1;

	// Who is connected, and who changed since the last poll.
	//
	// `connected_peers` is the roster a server answers from. `pending_*` are written on the
	// network thread and drained on the main one, which is the only place a signal may be
	// emitted from. Both are guarded: `has_sessions()` is called from put_packet on the main
	// thread while the backend is adding sessions on its own.
	mutable Mutex roster_mutex;
	HashSet<int> connected_peers;
	Vector<int> pending_joins;
	Vector<int> pending_leaves;

	// A client has exactly one peer to announce — the server, which is always id 1 — and it
	// must be announced once. Without this, `MultiplayerAPI` never learns the server exists
	// and every RPC to it is dropped with no error at either end.
	bool announced_server = false;

	void _emit_roster_changes();

	void _ingest_datagrams();
	void _ingest_peer_streams();
};

VARIANT_ENUM_CAST(WebTransportPeer::Mode);
VARIANT_ENUM_CAST(WebTransportPeer::SessionState);

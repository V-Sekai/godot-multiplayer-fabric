/**************************************************************************/
/*  web_transport_peer.cpp                                                */
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

#include "web_transport_peer.h"

#include "core/object/class_db.h"

// Default: no WT-session backend registered. The picoquic module installs
// real implementations in initialize_quic_module.
void *(*WebTransportPeer::create_session_backend_func)(WebTransportPeer *, const char *, int, const char *) = nullptr;
void (*WebTransportPeer::destroy_session_backend_func)(void *) = nullptr;
int (*WebTransportPeer::session_state_func)(void *) = nullptr;
void (*WebTransportPeer::set_session_state_func)(void *, int) = nullptr;
Error (*WebTransportPeer::send_wt_datagram_func)(void *, const uint8_t *, size_t) = nullptr;
Error (*WebTransportPeer::send_wt_stream_func)(void *, const uint8_t *, size_t) = nullptr;
Error (*WebTransportPeer::start_echo_server_func)(int) = nullptr;
void (*WebTransportPeer::stop_echo_server_func)() = nullptr;
Error (*WebTransportPeer::server_listen_func)(WebTransportPeer *, int, const String &, const uint8_t *, size_t, const char *) = nullptr;
void (*WebTransportPeer::server_close_func)() = nullptr;
Error (*WebTransportPeer::server_send_datagram_func)(int, const uint8_t *, size_t) = nullptr;
Error (*WebTransportPeer::server_send_stream_func)(int, const uint8_t *, size_t) = nullptr;
Error (*WebTransportPeer::server_disconnect_peer_func)(int) = nullptr;

WebTransportPeer::WebTransportPeer() {
	unique_id = generate_unique_id();
}

WebTransportPeer::~WebTransportPeer() {
	close();
}

Error WebTransportPeer::create_client(const String &p_host, int p_port, const String &p_path) {
	close();
	if (create_session_backend_func != nullptr) {
		// Spec-correct path: picowt_prepare_client_cnx + picowt_connect.
		// The backend owns its own picoquic_quic_t + picoquic_cnx_t +
		// h3zero_callback_ctx_t; we don't share the QUICClient path here.
		session_ctx = create_session_backend_func(
				this, p_host.utf8().get_data(), p_port, p_path.utf8().get_data());
		if (!session_ctx) {
			return ERR_CANT_CONNECT;
		}
		mode = MODE_CLIENT;
		target_peer = 1;
		return OK;
	}
	// Fallback: raw QUIC with a bespoke ALPN for in-process loopback tests.
	quic.instantiate();
	quic->set_alpn("webtransport");
	Error err = quic->connect_to_host(p_host, p_port);
	if (err != OK) {
		quic.unref();
		return err;
	}
	mode = MODE_CLIENT;
	target_peer = 1;
	return OK;
}

Error WebTransportPeer::create_server(int p_port, const String &p_path,
		Ref<X509Certificate> p_cert, Ref<CryptoKey> p_key) {
	ERR_FAIL_COND_V_MSG(!server_listen_func, ERR_UNAVAILABLE,
			"WebTransportPeer: no server backend registered (picoquic not compiled in?)");
	ERR_FAIL_COND_V_MSG(p_cert.is_null(), ERR_INVALID_PARAMETER, "WebTransportPeer: cert is null");
	ERR_FAIL_COND_V_MSG(p_key.is_null(), ERR_INVALID_PARAMETER, "WebTransportPeer: key is null");

	PackedByteArray der = p_cert->get_der();
	ERR_FAIL_COND_V_MSG(der.is_empty(), ERR_CANT_CREATE, "WebTransportPeer: cert has no DER (not loaded?)");

	String key_pem = p_key->save_to_string(false);
	ERR_FAIL_COND_V_MSG(key_pem.is_empty(), ERR_CANT_CREATE, "WebTransportPeer: failed to serialize key");

	mode = MODE_SERVER;
	unique_id = 1;
	Error err = server_listen_func(this, p_port, p_path,
			der.ptr(), (size_t)der.size(), key_pem.utf8().get_data());
	if (err != OK) {
		mode = MODE_NONE;
	}
	return err;
}

void WebTransportPeer::_bind_quic(const Ref<QUICClient> &p_quic, Mode p_mode) {
	quic = p_quic;
	mode = p_mode;
	target_peer = 1;
}

int WebTransportPeer::get_available_packet_count() const {
	MutexLock lock(incoming_mutex);
	return incoming.size();
}

Error WebTransportPeer::get_packet(const uint8_t **r_buffer, int &r_buffer_size) {
	MutexLock lock(incoming_mutex);
	if (incoming.is_empty()) {
		return ERR_UNAVAILABLE;
	}
	IncomingPacket pkt = incoming.front()->get();
	incoming.pop_front();

	current_packet_bytes.resize(pkt.bytes.size());
	if (pkt.bytes.size() > 0) {
		memcpy(current_packet_bytes.ptrw(), pkt.bytes.ptr(), pkt.bytes.size());
	}
	current_mode = pkt.mode;
	current_channel = pkt.channel;
	current_peer = pkt.from;

	*r_buffer = current_packet_bytes.ptr();
	r_buffer_size = current_packet_bytes.size();
	return OK;
}

void WebTransportPeer::_push_wt_incoming_datagram(const uint8_t *p_bytes, size_t p_len, int p_from) {
	IncomingPacket pkt;
	if (p_len > 0 && p_bytes) {
		pkt.bytes.resize(p_len);
		memcpy(pkt.bytes.ptrw(), p_bytes, p_len);
	}
	pkt.mode = TRANSFER_MODE_UNRELIABLE;
	pkt.channel = 0;
	pkt.from = p_from;
	MutexLock lock(incoming_mutex);
	incoming.push_back(pkt);
}

void WebTransportPeer::_push_wt_incoming_stream(const uint8_t *p_bytes, size_t p_len, int p_from, int p_channel) {
	IncomingPacket pkt;
	if (p_len > 0 && p_bytes) {
		pkt.bytes.resize(p_len);
		memcpy(pkt.bytes.ptrw(), p_bytes, p_len);
	}
	pkt.mode = TRANSFER_MODE_RELIABLE;
	pkt.channel = p_channel; // decoded from the wtd frame flag (0-7)
	pkt.from = p_from;
	MutexLock lock(incoming_mutex);
	incoming.push_back(pkt);
}

Error WebTransportPeer::put_packet(const uint8_t *p_buffer, int p_buffer_size) {
	// Server mode: route through server send funcs.
	if (mode == MODE_SERVER) {
		if (!has_sessions()) {
			return ERR_UNCONFIGURED;
		}
		TransferMode m = get_transfer_mode();
		if (m == TRANSFER_MODE_RELIABLE) {
			ERR_FAIL_COND_V(!server_send_stream_func, ERR_UNCONFIGURED);
			return server_send_stream_func(target_peer,
					reinterpret_cast<const uint8_t *>(p_buffer), p_buffer_size);
		}
		ERR_FAIL_COND_V(!server_send_datagram_func, ERR_UNCONFIGURED);
		return server_send_datagram_func(target_peer,
				reinterpret_cast<const uint8_t *>(p_buffer), p_buffer_size);
	}
	// Session-backend path: route through WT-session-aware APIs.
	if (session_ctx) {
		if (get_session_state() != SESSION_OPEN) {
			return ERR_UNCONFIGURED; // Session not yet established.
		}
		TransferMode m = get_transfer_mode();
		if (m == TRANSFER_MODE_RELIABLE) {
			if (send_wt_stream_func == nullptr) {
				return ERR_UNCONFIGURED;
			}
			return send_wt_stream_func(session_ctx,
					reinterpret_cast<const uint8_t *>(p_buffer), p_buffer_size);
		}
		if (send_wt_datagram_func == nullptr) {
			return ERR_UNCONFIGURED;
		}
		return send_wt_datagram_func(session_ctx, reinterpret_cast<const uint8_t *>(p_buffer), p_buffer_size);
	}
	// Fallback raw-QUIC path.
	if (quic.is_null() || quic->get_status() != QUICClient::STATUS_CONNECTED) {
		return ERR_UNCONFIGURED;
	}
	PackedByteArray bytes;
	if (p_buffer_size > 0) {
		bytes.resize(p_buffer_size);
		memcpy(bytes.ptrw(), p_buffer, p_buffer_size);
	}
	TransferMode m = get_transfer_mode();
	if (m == TRANSFER_MODE_RELIABLE) {
		// Each reliable packet gets a fresh bidi stream, FIN'd after the
		// payload so the peer can detect the boundary cleanly.
		int64_t sid = quic->open_stream(true);
		if (sid == QUICClient::INVALID_STREAM_ID) {
			return ERR_CANT_CREATE;
		}
		Error se = quic->stream_send(sid, bytes);
		if (se != OK) {
			return se;
		}
		return quic->stream_close(sid);
	}
	// Both UNRELIABLE and UNRELIABLE_ORDERED map to datagrams for now;
	// adding per-packet sequence numbers for UNRELIABLE_ORDERED is a
	// follow-up that needs a reorder buffer on the receive side.
	return quic->send_datagram(bytes);
}

int WebTransportPeer::get_max_packet_size() const {
	// QUIC datagram frame size practical ceiling. Streams are effectively
	// unlimited, but Godot's multiplayer stack treats a peer's max the
	// same for both channels, so report the tighter datagram bound.
	return 1200;
}

void WebTransportPeer::set_target_peer(int p_peer) {
	target_peer = p_peer;
}

// get_packet_peer/mode/channel describe the NEXT packet (the MultiplayerPeer
// contract: peek the front BEFORE get_packet pops it), matching ENetMultiplayerPeer.
// Returning the last-popped `current_*` mis-attributes interleaved WT sessions: a
// peer's packet carries the PREVIOUS packet's session, so a late client's join
// credits another peer and that client never receives its welcome. The fallback to
// current_* applies only when the queue is empty (no next packet to peek).
int WebTransportPeer::get_packet_peer() const {
	MutexLock lock(incoming_mutex);
	return incoming.is_empty() ? current_peer : incoming.front()->get().from;
}

MultiplayerPeer::TransferMode WebTransportPeer::get_packet_mode() const {
	MutexLock lock(incoming_mutex);
	return incoming.is_empty() ? current_mode : incoming.front()->get().mode;
}

int WebTransportPeer::get_packet_channel() const {
	MutexLock lock(incoming_mutex);
	return incoming.is_empty() ? current_channel : incoming.front()->get().channel;
}

void WebTransportPeer::_ingest_datagrams() {
	if (quic.is_null()) {
		return;
	}
	while (true) {
		PackedByteArray d = quic->receive_datagram();
		if (d.is_empty()) {
			break;
		}
		IncomingPacket pkt;
		pkt.bytes.resize(d.size());
		memcpy(pkt.bytes.ptrw(), d.ptr(), d.size());
		pkt.mode = TRANSFER_MODE_UNRELIABLE;
		pkt.channel = 0;
		pkt.from = 1;
		MutexLock lock(incoming_mutex);
		incoming.push_back(pkt);
	}
}

void WebTransportPeer::_ingest_peer_streams() {
	// Check streams we've already seen, then discover new ones by probing
	// a small range of peer-initiated bidi ids. This is a scaffold — real
	// impl needs QUICClient to expose a "new streams this tick" list.
	if (quic.is_null()) {
		return;
	}
	// Drain data from known peer streams; when the peer FINs, finalize
	// the packet and remove from pending.
	List<int64_t>::Element *it = pending_peer_streams.front();
	while (it) {
		int64_t sid = it->get();
		List<int64_t>::Element *next = it->next();
		PackedByteArray chunk = quic->stream_read(sid);
		// Build up the packet in the current_packet cache keyed by sid —
		// for this scaffold, one stream = one packet, so we require FIN
		// before surfacing to the app.
		if (chunk.size() > 0) {
			IncomingPacket pkt;
			pkt.bytes.resize(chunk.size());
			memcpy(pkt.bytes.ptrw(), chunk.ptr(), chunk.size());
			pkt.mode = TRANSFER_MODE_RELIABLE;
			pkt.channel = 0;
			pkt.from = 1;
			{
				MutexLock lock(incoming_mutex);
				incoming.push_back(pkt);
			}
		}
		if (quic->is_stream_peer_closed(sid)) {
			pending_peer_streams.erase(it);
		}
		it = next;
	}
}

void WebTransportPeer::_server_peer_joined(int p_peer_id) {
	MutexLock lock(roster_mutex);
	if (connected_peers.has(p_peer_id)) {
		return;
	}
	connected_peers.insert(p_peer_id);
	pending_joins.push_back(p_peer_id);
}

void WebTransportPeer::_server_peer_left(int p_peer_id) {
	MutexLock lock(roster_mutex);
	if (!connected_peers.has(p_peer_id)) {
		return; // a second deregister for the same session, which picoquic does send
	}
	connected_peers.erase(p_peer_id);
	pending_leaves.push_back(p_peer_id);
}

bool WebTransportPeer::has_sessions() const {
	MutexLock lock(roster_mutex);
	return !connected_peers.is_empty();
}

// Announce, on the main thread, what the network thread recorded.
//
// The lists are taken under the lock and emitted outside it. A signal handler is script: it
// can send a packet, disconnect a peer, or free this object, and every one of those wants the
// same mutex. Emitting while holding it deadlocks on the first handler that answers.
void WebTransportPeer::_emit_roster_changes() {
	Vector<int> joins;
	Vector<int> leaves;
	{
		MutexLock lock(roster_mutex);
		joins = pending_joins;
		leaves = pending_leaves;
		pending_joins.clear();
		pending_leaves.clear();
	}
	for (int id : joins) {
		emit_signal(SNAME("peer_connected"), id);
	}
	for (int id : leaves) {
		emit_signal(SNAME("peer_disconnected"), id);
	}
}

void WebTransportPeer::poll() {
	if (mode == MODE_SERVER) {
		// picoquic runs the server on its own thread, so there is no I/O to drive here — but
		// there is still the roster to announce, and this is the main thread.
		_emit_roster_changes();
		return;
	}
	if (session_ctx) {
		// A client has one peer, and it is the server. `MultiplayerAPI` will not route an RPC
		// to a peer it was never told about, so without this the session opens, packets flow,
		// and every RPC is dropped in silence at both ends.
		const bool open = get_session_state() == SESSION_OPEN;
		if (open && !announced_server) {
			announced_server = true;
			emit_signal(SNAME("peer_connected"), 1);
		} else if (!open && announced_server) {
			announced_server = false;
			emit_signal(SNAME("peer_disconnected"), 1);
		}
		return;
	}
	if (quic.is_null()) {
		return;
	}
	quic->poll();
	_ingest_datagrams();
	_ingest_peer_streams();
}

void WebTransportPeer::close() {
	if (mode == MODE_SERVER && server_close_func) {
		server_close_func();
	}
	if (session_ctx && destroy_session_backend_func) {
		destroy_session_backend_func(session_ctx);
		session_ctx = nullptr;
	}
	if (quic.is_valid()) {
		quic->close();
		quic.unref();
	}
	mode = MODE_NONE;
	announced_server = false;
	{
		// Closing is not a disconnection to announce. Whoever called `close()` already knows,
		// and a peer_disconnected storm during teardown reaches handlers that are being torn
		// down themselves. The roster is dropped, not walked.
		MutexLock lock(roster_mutex);
		connected_peers.clear();
		pending_joins.clear();
		pending_leaves.clear();
	}
	{
		MutexLock lock(incoming_mutex);
		incoming.clear();
	}
	pending_peer_streams.clear();
	current_packet_bytes.clear();
}

// Drop one client. This used to call `close()`, which stops the listener — so kicking one
// player, or one player leaving, took every other player with them and the server too.
//
// `p_force` is not honoured yet and says so rather than being silently ignored: a graceful
// close still sends the WebTransport close message first, which is what tells the client it was
// disconnected instead of leaving it to time out.
void WebTransportPeer::disconnect_peer(int p_peer, bool p_force) {
	(void)p_force;
	if (mode != MODE_SERVER) {
		// A client has one peer and it is the server, so disconnecting it is closing.
		close();
		return;
	}
	ERR_FAIL_COND_MSG(!server_disconnect_peer_func,
			"WebTransportPeer: no server backend registered");
	ERR_FAIL_COND_MSG(p_peer <= 0,
			"WebTransportPeer: disconnect_peer needs a peer id; use close() to stop the server");
	// The roster entry is not removed here. picoquic answers the close with a deregister
	// callback and that path reports the peer gone, so removing it now would announce the
	// disconnection twice — once optimistically and once when it actually happened.
	server_disconnect_peer_func(p_peer);
}

bool WebTransportPeer::is_server() const {
	return mode == MODE_SERVER;
}

int WebTransportPeer::get_unique_id() const {
	return unique_id;
}

WebTransportPeer::SessionState WebTransportPeer::get_session_state() const {
	if (session_ctx && session_state_func) {
		return static_cast<SessionState>(session_state_func(session_ctx));
	}
	return SESSION_DISCONNECTED;
}

MultiplayerPeer::ConnectionStatus WebTransportPeer::get_connection_status() const {
	if (mode == MODE_SERVER) {
		return has_sessions() ? CONNECTION_CONNECTED : CONNECTION_CONNECTING;
	}
	if (session_ctx) {
		SessionState ss = get_session_state();
		switch (ss) {
			case SESSION_OPEN:
				return CONNECTION_CONNECTED;
			case SESSION_DISCONNECTED:
			case SESSION_CLOSED:
				return CONNECTION_DISCONNECTED;
			default:
				return CONNECTION_CONNECTING;
		}
	}
	if (quic.is_null()) {
		return CONNECTION_DISCONNECTED;
	}
	switch (quic->get_status()) {
		case QUICClient::STATUS_DISCONNECTED:
		case QUICClient::STATUS_CONNECTION_ERROR:
			return CONNECTION_DISCONNECTED;
		case QUICClient::STATUS_CONNECTED:
			return CONNECTION_CONNECTED;
		default:
			return CONNECTION_CONNECTING;
	}
}

void WebTransportPeer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("create_client", "host", "port", "path"), &WebTransportPeer::create_client, DEFVAL("/"));
	ClassDB::bind_method(D_METHOD("create_server", "port", "path", "cert", "key"), &WebTransportPeer::create_server);
	ClassDB::bind_method(D_METHOD("get_quic"), &WebTransportPeer::get_quic);
	// close / poll / disconnect_peer / get_* are bound by MultiplayerPeer base.

	BIND_ENUM_CONSTANT(MODE_NONE);
	BIND_ENUM_CONSTANT(MODE_CLIENT);
	BIND_ENUM_CONSTANT(MODE_SERVER);
}

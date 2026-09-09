/**************************************************************************/
/*  quic_client.h                                                         */
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

#include "core/crypto/crypto.h"
#include "core/object/class_db.h"
#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/list.h"
#include "core/templates/safe_refcount.h"
#include "core/variant/type_info.h"

class QUICClient : public RefCounted {
	GDCLASS(QUICClient, RefCounted);

public:
	enum Status {
		STATUS_DISCONNECTED,
		STATUS_RESOLVING,
		STATUS_CONNECTING,
		STATUS_HANDSHAKING,
		STATUS_CONNECTED,
		STATUS_BUSY,
		STATUS_CONNECTION_ERROR,
	};

	// picoquic callback event codes exposed so tests and GDScript can reference
	// them by name instead of magic numbers. Values mirror
	// picoquic_call_back_event_t in picoquic.h.
	enum CallbackEvent {
		CALLBACK_STREAM_DATA = 0,
		CALLBACK_STREAM_FIN = 1,
		CALLBACK_CLOSE = 5,
		CALLBACK_APPLICATION_CLOSE = 6,
		CALLBACK_ALMOST_READY = 9,
		CALLBACK_READY = 10,
		CALLBACK_DATAGRAM = 11,
	};

	// Sentinel returned by open_stream() when the client cannot allocate one
	// (disconnected, flow-controlled, etc.). Matches the convention in
	// core/io/http_client.h where -1 signals "no resource available yet".
	static constexpr int64_t INVALID_STREAM_ID = -1;

	// Transport backend contract — meshoptimizer pattern (see
	// modules/meshoptimizer/register_types.cpp). A module assigns these at
	// init to picoquic's implementation (native) or browser WebTransport JS
	// glue (web). When unset, the client operates in pure in-memory mode —
	// useful for tests and for GDScript code that uses _push/_pop hooks
	// directly.
	static void *(*create_backend_func)(QUICClient *client, const char *host, int port, const char *alpn);
	static void (*destroy_backend_func)(void *ctx);
	// Diagnostic: number of picoquic connections held inside the backend ctx.
	// Primarily for tests; real user code wouldn't need this.
	static int (*backend_cnx_count_func)(void *ctx);
	// picoquic connection state (picoquic_state_enum value). 0 means
	// client_init (pre-start); > 0 means the handshake has begun.
	static int (*backend_cnx_state_func)(void *ctx);

	// Dispatches a picoquic_callback_* event onto a client. The backend's
	// real picoquic callback (on the cnx hotpath) calls through this bridge
	// so tests can verify routing without touching a picoquic_cnx_t*.
	// Event codes map 1:1 to picoquic_call_back_event_t (see picoquic.h):
	//   0 stream_data, 1 stream_fin, 3 close, 5 datagram, 6 almost_ready, 7 ready.
	static void (*dispatch_cnx_event_func)(QUICClient *client, uint64_t stream_id,
			const uint8_t *bytes, size_t length, int event);

	// Emit a minimal HTTP/3 GET on stream 0 (client bidi). The backend
	// builds the h3 SETTINGS/HEADERS frames and queues them via picoquic.
	// Returns OK if the request was queued, ERR_* otherwise.
	static Error (*backend_send_h3_get_func)(void *ctx, const char *path, const char *host);

	bool _has_backend_context() const { return backend_ctx != nullptr; }
	void *_get_backend_ctx() const { return backend_ctx; }

	Status get_status() const { return static_cast<Status>(status.get()); }
	String get_connected_host() const { return host; }
	int get_connected_port() const { return port; }

	void set_blocking_mode(bool p_enabled) { blocking_mode = p_enabled; }
	bool is_blocking_mode_enabled() const { return blocking_mode; }

	void set_read_chunk_size(int p_size) {
		if (p_size > 0) {
			read_chunk_size = p_size;
		}
	}
	int get_read_chunk_size() const { return read_chunk_size; }

	void set_alpn(const String &p_alpn) {
		// ALPN must be non-empty — picoquic aborts the handshake otherwise.
		if (!p_alpn.is_empty()) {
			alpn = p_alpn;
		}
	}
	String get_alpn() const { return alpn; }

	void set_tls_options(const Ref<TLSOptions> &p_options) { tls_options = p_options; }
	Ref<TLSOptions> get_tls_options() const { return tls_options; }

	void close() {
		if (backend_ctx && destroy_backend_func) {
			destroy_backend_func(backend_ctx);
		}
		backend_ctx = nullptr;
		status.set(STATUS_DISCONNECTED);
		host = String();
		port = 0;
		tls_options.unref();
		streams.clear();
		incoming_datagrams.clear();
		outgoing_datagrams.clear();
		next_client_bidi_id = 0;
		next_client_uni_id = 2;
	}

	~QUICClient() {
		if (backend_ctx && destroy_backend_func) {
			destroy_backend_func(backend_ctx);
			backend_ctx = nullptr;
		}
	}

	Error poll() {
		if (status.get() == STATUS_DISCONNECTED) {
			return ERR_UNCONFIGURED;
		}
		// TODO: drive picoquic_incoming_packet / outgoing loop one tick.
		return OK;
	}

	Error connect_to_host(const String &p_host, int p_port) {
		if (p_host.is_empty()) {
			return ERR_INVALID_PARAMETER;
		}
		if (p_port <= 0 || p_port > 65535) {
			return ERR_INVALID_PARAMETER;
		}
		host = p_host;
		port = p_port;
		status.set(STATUS_RESOLVING);
		if (create_backend_func) {
			backend_ctx = create_backend_func(
					this,
					p_host.utf8().get_data(),
					p_port,
					alpn.utf8().get_data());
			if (!backend_ctx) {
				// Backend rejected the dial — commonly DNS failure. Leave
				// host/port recorded so callers can inspect, but unwind the
				// status machine.
				status.set(STATUS_DISCONNECTED);
				return ERR_CANT_RESOLVE;
			}
		}
		return OK;
	}

	Error send_datagram(const PackedByteArray &p_payload) {
		if (status.get() != STATUS_CONNECTED) {
			return ERR_UNCONFIGURED;
		}
		MutexLock lock(queues_mutex);
		outgoing_datagrams.push_back(p_payload);
		return OK;
	}

	// picoquic pulls the next queued datagram via this drain when it has
	// room in its next packet. Empty means "none queued".
	PackedByteArray _pop_outgoing_datagram() {
		MutexLock lock(queues_mutex);
		if (outgoing_datagrams.is_empty()) {
			return PackedByteArray();
		}
		PackedByteArray front = outgoing_datagrams.front()->get();
		outgoing_datagrams.pop_front();
		return front;
	}

	PackedByteArray receive_datagram() {
		MutexLock lock(queues_mutex);
		if (incoming_datagrams.is_empty()) {
			return PackedByteArray();
		}
		PackedByteArray front = incoming_datagrams.front()->get();
		incoming_datagrams.pop_front();
		return front;
	}

	// Entry point for the picoquic datagram-frame callback. Not bound to
	// GDScript — the underscore prefix marks it as engine-internal.
	void _push_incoming_datagram(const PackedByteArray &p_payload) {
		MutexLock lock(queues_mutex);
		incoming_datagrams.push_back(p_payload);
	}

	int64_t open_stream(bool p_bidirectional) {
		if (status.get() != STATUS_CONNECTED) {
			return INVALID_STREAM_ID;
		}
		MutexLock lock(queues_mutex);
		// QUIC stream id low-2-bits convention (RFC 9000 §2.1):
		//   0b00 client-init bidi, 0b10 client-init uni — step by 4.
		int64_t &counter = p_bidirectional ? next_client_bidi_id : next_client_uni_id;
		int64_t id = counter;
		counter += 4;
		streams.insert(id, StreamState());
		return id;
	}

	Error stream_send(int64_t p_stream_id, const PackedByteArray &p_payload) {
		MutexLock lock(queues_mutex);
		HashMap<int64_t, StreamState>::Iterator it = streams.find(p_stream_id);
		if (!it) {
			return ERR_DOES_NOT_EXIST;
		}
		if (it->value.tx_closed) {
			return ERR_UNAVAILABLE;
		}
		it->value.tx.append_array(p_payload);
		return OK;
	}

	// QUIC half-close: FIN the local send direction. rx side keeps working,
	// so pending stream_read() calls still drain buffered data.
	Error stream_close(int64_t p_stream_id) {
		MutexLock lock(queues_mutex);
		HashMap<int64_t, StreamState>::Iterator it = streams.find(p_stream_id);
		if (!it) {
			return ERR_DOES_NOT_EXIST;
		}
		it->value.tx_closed = true;
		return OK;
	}

	PackedByteArray stream_read(int64_t p_stream_id) {
		MutexLock lock(queues_mutex);
		HashMap<int64_t, StreamState>::Iterator it = streams.find(p_stream_id);
		if (!it) {
			return PackedByteArray();
		}
		PackedByteArray out = it->value.rx;
		it->value.rx = PackedByteArray();
		return out;
	}

	// Entry point for the picoquic stream-data callback. If the stream id is
	// new this implicitly opens it (peer-initiated stream in QUIC terms).
	void _push_incoming_stream_data(int64_t p_stream_id, const PackedByteArray &p_payload) {
		MutexLock lock(queues_mutex);
		streams[p_stream_id].rx.append_array(p_payload);
	}

	// Peer FIN'd their send direction. Stream entry persists so buffered rx
	// can still be drained. Implicitly opens for zero-byte streams.
	void _push_incoming_stream_fin(int64_t p_stream_id) {
		MutexLock lock(queues_mutex);
		streams[p_stream_id].rx_closed = true;
	}

	bool is_stream_peer_closed(int64_t p_stream_id) const {
		MutexLock lock(queues_mutex);
		HashMap<int64_t, StreamState>::ConstIterator it = streams.find(p_stream_id);
		return it && it->value.rx_closed;
	}

	// Entry point for the picoquic handshake-state callbacks. Tests also drive
	// this directly to simulate status transitions without a real network.
	void _set_status(Status p_status) { status.set(p_status); }

	// picoquic pulls ready-to-send bytes via this drain. Empty means
	// "no bytes queued" — for unknown streams, also empty.
	PackedByteArray _pop_outgoing_stream_data(int64_t p_stream_id) {
		MutexLock lock(queues_mutex);
		HashMap<int64_t, StreamState>::Iterator it = streams.find(p_stream_id);
		if (!it) {
			return PackedByteArray();
		}
		PackedByteArray out = it->value.tx;
		it->value.tx = PackedByteArray();
		return out;
	}

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("connect_to_host", "host", "port"), &QUICClient::connect_to_host);
		ClassDB::bind_method(D_METHOD("close"), &QUICClient::close);
		ClassDB::bind_method(D_METHOD("poll"), &QUICClient::poll);
		ClassDB::bind_method(D_METHOD("get_status"), &QUICClient::get_status);
		ClassDB::bind_method(D_METHOD("get_connected_host"), &QUICClient::get_connected_host);
		ClassDB::bind_method(D_METHOD("get_connected_port"), &QUICClient::get_connected_port);
		ClassDB::bind_method(D_METHOD("set_blocking_mode", "enabled"), &QUICClient::set_blocking_mode);
		ClassDB::bind_method(D_METHOD("is_blocking_mode_enabled"), &QUICClient::is_blocking_mode_enabled);
		ClassDB::bind_method(D_METHOD("set_read_chunk_size", "size"), &QUICClient::set_read_chunk_size);
		ClassDB::bind_method(D_METHOD("get_read_chunk_size"), &QUICClient::get_read_chunk_size);
		ClassDB::bind_method(D_METHOD("set_alpn", "alpn"), &QUICClient::set_alpn);
		ClassDB::bind_method(D_METHOD("get_alpn"), &QUICClient::get_alpn);
		ClassDB::bind_method(D_METHOD("set_tls_options", "options"), &QUICClient::set_tls_options);
		ClassDB::bind_method(D_METHOD("get_tls_options"), &QUICClient::get_tls_options);
		ClassDB::bind_method(D_METHOD("send_datagram", "payload"), &QUICClient::send_datagram);
		ClassDB::bind_method(D_METHOD("receive_datagram"), &QUICClient::receive_datagram);
		ClassDB::bind_method(D_METHOD("open_stream", "bidirectional"), &QUICClient::open_stream);
		ClassDB::bind_method(D_METHOD("stream_send", "stream_id", "payload"), &QUICClient::stream_send);
		ClassDB::bind_method(D_METHOD("stream_read", "stream_id"), &QUICClient::stream_read);
		ClassDB::bind_method(D_METHOD("stream_close", "stream_id"), &QUICClient::stream_close);

		BIND_ENUM_CONSTANT(STATUS_DISCONNECTED);
		BIND_ENUM_CONSTANT(STATUS_RESOLVING);
		BIND_ENUM_CONSTANT(STATUS_CONNECTING);
		BIND_ENUM_CONSTANT(STATUS_HANDSHAKING);
		BIND_ENUM_CONSTANT(STATUS_CONNECTED);
		BIND_ENUM_CONSTANT(STATUS_BUSY);
		BIND_ENUM_CONSTANT(STATUS_CONNECTION_ERROR);
	}

private:
	SafeNumeric<int> status{ STATUS_DISCONNECTED };
	String host;
	int port = 0;
	bool blocking_mode = false;
	int read_chunk_size = 65536;
	String alpn = "h3";
	Ref<TLSOptions> tls_options;

	struct StreamState {
		PackedByteArray rx; // bytes received from peer, awaiting stream_read()
		PackedByteArray tx; // bytes queued by stream_send(), awaiting picoquic drain
		bool tx_closed = false; // local FIN sent; no more stream_send allowed
		bool rx_closed = false; // peer FIN received; no more bytes will arrive
	};
	// Streams + datagram queues are mutated by the picoquic network thread
	// (via _push_*) and read/written by the main thread (via stream_read,
	// receive_datagram, etc.). One mutex guards all four containers.
	mutable Mutex queues_mutex;
	HashMap<int64_t, StreamState> streams;
	int64_t next_client_bidi_id = 0; // 0, 4, 8, ...
	int64_t next_client_uni_id = 2; // 2, 6, 10, ...
	void *backend_ctx = nullptr;
	List<PackedByteArray> incoming_datagrams;
	List<PackedByteArray> outgoing_datagrams;
};

VARIANT_ENUM_CAST(QUICClient::Status);

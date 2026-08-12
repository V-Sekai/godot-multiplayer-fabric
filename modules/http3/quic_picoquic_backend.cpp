/**************************************************************************/
/*  quic_picoquic_backend.cpp                                             */
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

#include "quic_picoquic_backend.h"

#ifdef GODOT_QUIC_NATIVE_BACKEND

#ifdef _MSC_VER
#define strdup _strdup
#pragma warning(disable : 4996)
#endif

#include "quic_client.h"
#include "web_transport_peer.h"

#include "core/config/project_settings.h"
#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/templates/local_vector.h"
#include "core/templates/safe_refcount.h"

// clang-format off
// picotls.h MUST come before picoquic headers that use ptls types.
// Suppress GCC/Clang warnings for MSVC-only #pragma warning in picotls.h.
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#endif
#include <picotls.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
// clang-format on

#ifdef _WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#include "frame.h" // wtd_frame_encode/decode — persistent-stream WT framing

#include <demoserver.h>
#include <h3zero.h>
#include <h3zero_common.h>
#include <pico_webtransport.h>
#include <picoquic.h>
#include <picoquic_crypto_provider_api.h>
#include <picoquic_internal.h>
#include <picoquic_logger.h>
#include <picoquic_packet_loop.h>
#include <picoquic_utils.h>
#include <ptls_mbedtls.h>
#include <tls_api.h>

#include <cstdio>
#include <cstring>

// Authority and interest are split (FabricMultiplayerPeer channels): one
// single-threaded server is authoritative for the players, but interest fans out
// across up to ~1400 entities, so the QUIC connection table sizes for the
// ENTITY/interest workload, not the player count. It comfortably exceeds the
// entity budget (the target is ≥150 players and 1400 entities, and more where the
// transport allows). A table of one slot collapses every concurrent session onto
// that slot and stalls the server under load.
static constexpr uint32_t WT_SERVER_MAX_CONNECTIONS = 4096; // ≥ 1400 entities + players + headroom

// picoquic_ptls_minicrypto.c is excluded (it needs picotls minicrypto/uecc
// symbols we don't compile). tls_api.c still has a link-time reference; we
// provide a no-op stub and disable the provider at runtime via
// TLS_API_INIT_FLAGS_NO_MINICRYPTO.
extern "C" void picoquic_ptls_minicrypto_load(int unload) {
	(void)unload;
}

#ifdef _WINDOWS
// picotls needs gettimeofday (shimmed as wintimeofday via wincompat.h's
// #define gettimeofday wintimeofday). Provide the implementation here.
// Cannot include <sys/time.h> on MinGW because the gettimeofday macro
// would rename MinGW's declaration and cause a type conflict.
extern "C" int wintimeofday(struct timeval *tv, struct timezone *tz) {
	(void)tz;
	if (tv) {
		FILETIME ft;
		GetSystemTimePreciseAsFileTime(&ft);
		uint64_t now = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
		now /= 10;
		now -= 11644473600000000ULL;
		tv->tv_sec = (long)(now / 1000000);
		tv->tv_usec = (long)(now % 1000000);
	}
	return 0;
}
#endif // _WINDOWS

namespace {

// Work items posted from foreign threads to the network thread. picoquic
// is not thread-safe: only the loop thread may touch cnx / stream state.
// Foreign threads enqueue here and call picoquic_wake_up_network_thread;
// the loop drains the queue in _loop_callback (wake_up event).
enum WorkKind {
	WORK_SEND_STREAM, // picoquic_add_to_stream on an existing stream id
	WORK_SEND_DATAGRAM, // picoquic_queue_datagram_frame
	WORK_OPEN_WT_STREAM, // picowt_create_local_stream + add_to_stream + FIN
	WORK_CLOSE_SESSION, // picowt_send_close_session_message + picoquic_close, one session
};

struct WTServerSessionCtx;

struct WorkItem {
	WorkKind kind = WORK_SEND_STREAM;
	uint64_t stream_id = 0;
	LocalVector<uint8_t> bytes;
	bool fin = false;
	WTServerSessionCtx *session = nullptr; // server side: the target session
};

struct WorkQueue {
	Mutex mutex;
	LocalVector<WorkItem> items;

	void push(WorkItem &&p_item) {
		MutexLock lock(mutex);
		items.push_back(std::move(p_item));
	}

	void drain(LocalVector<WorkItem> &r_out) {
		MutexLock lock(mutex);
		r_out = std::move(items);
		items.clear();
	}
};

// Frame a reliable payload as a length-prefixed wtd frame (channel 0, reliable)
// for appending to the session's persistent bidi stream.
static void _frame_reliable(const uint8_t *p_bytes, size_t p_len, LocalVector<uint8_t> &r_out) {
	size_t cap = p_len + 9; // flag(1) + varint(<=8) + payload
	r_out.resize(cap);
	size_t flen = 0;
	if (wtd_frame_encode(WTD_FRAME_FLAG(0, true), p_bytes, p_len, r_out.ptr(), cap, &flen) == WTD_FRAME_OK) {
		r_out.resize(flen);
	} else {
		r_out.clear();
	}
}

// Append received stream bytes to a per-session accumulator.
static void _reliable_accumulate(LocalVector<uint8_t> &r_acc, const uint8_t *p_bytes, size_t p_len) {
	size_t old = r_acc.size();
	r_acc.resize(old + p_len);
	memcpy(r_acc.ptr() + old, p_bytes, p_len);
}

// Decode every complete frame out of the accumulator, pushing each payload to the
// peer; keep the unconsumed tail for the next chunk.
static void _reliable_decode(LocalVector<uint8_t> &r_acc, WebTransportPeer *p_peer, int p_peer_id) {
	size_t off = 0;
	while (off < r_acc.size()) {
		size_t consumed = 0, plen = 0;
		uint8_t flag = 0;
		const uint8_t *payload = nullptr;
		wtd_frame_status_t st = wtd_frame_decode(r_acc.ptr() + off, r_acc.size() - off,
				&consumed, &flag, &payload, &plen);
		if (st == WTD_FRAME_OK) {
			if (p_peer) {
				p_peer->_push_wt_incoming_stream(payload, plen, p_peer_id, WTD_FRAME_GET_CHANNEL(flag));
			}
			off += consumed;
		} else if (st == WTD_FRAME_INCOMPLETE) {
			break;
		} else {
			off = r_acc.size(); // protocol error: drop the buffer
			break;
		}
	}
	if (off > 0) {
		size_t remain = r_acc.size() - off;
		if (remain > 0) {
			memmove(r_acc.ptr(), r_acc.ptr() + off, remain);
		}
		r_acc.resize(remain);
	}
}

// Per-client backend state — opaque void* exposed to QUICClient.
struct BackendCtx {
	picoquic_quic_t *quic = nullptr;
	picoquic_cnx_t *cnx = nullptr;
	picoquic_network_thread_ctx_t *thread_ctx = nullptr;
	picoquic_packet_loop_param_t loop_param{};
	WorkQueue work;
	SafeNumeric<int> cached_cnx_state{ -1 };
};

// Drain a work queue on the network thread, applying each item directly
// to picoquic. cnx may be null if the connection is gone.
void _drain_work_queue(WorkQueue &p_queue, picoquic_cnx_t *p_cnx,
		h3zero_stream_ctx_t *p_control_stream, int (*p_app_cb)(picoquic_cnx_t *, uint8_t *, size_t, picohttp_call_back_event_t, h3zero_stream_ctx_t *, void *), void *p_app_ctx,
		int64_t *p_persistent_stream = nullptr) {
	LocalVector<WorkItem> items;
	p_queue.drain(items);
	if (!p_cnx) {
		return;
	}
	for (WorkItem &item : items) {
		switch (item.kind) {
			case WORK_SEND_STREAM: {
				picoquic_add_to_stream(p_cnx, item.stream_id,
						item.bytes.ptr(), item.bytes.size(), item.fin ? 1 : 0);
			} break;
			case WORK_SEND_DATAGRAM: {
				picoquic_queue_datagram_frame(p_cnx, item.bytes.size(), item.bytes.ptr());
			} break;
			case WORK_OPEN_WT_STREAM: {
				if (!p_control_stream) {
					break;
				}
				// Open the persistent reliable stream once, then append framed bytes
				// to it (no FIN) — not a fresh stream per message.
				uint64_t sid;
				if (p_persistent_stream && *p_persistent_stream >= 0) {
					sid = (uint64_t)*p_persistent_stream;
				} else {
					h3zero_callback_ctx_t *h3 = (h3zero_callback_ctx_t *)picoquic_get_callback_context(p_cnx);
					if (!h3) {
						break;
					}
					h3zero_stream_ctx_t *stream = picowt_create_local_stream(
							p_cnx, /*is_bidir=*/1, h3, p_control_stream->stream_id);
					if (!stream) {
						break;
					}
					stream->path_callback = p_app_cb;
					stream->path_callback_ctx = p_app_ctx;
					sid = stream->stream_id;
					if (p_persistent_stream) {
						*p_persistent_stream = (int64_t)sid;
					}
				}
				picoquic_add_to_stream(p_cnx, sid, item.bytes.ptr(), item.bytes.size(), 0);
			} break;
		}
	}
}

struct WTSessionCtx;

// Forward declare _wt_app_callback so _drain_work_queue can pass it by name.
int _wt_app_callback(picoquic_cnx_t *p_cnx, uint8_t *p_bytes, size_t p_length,
		picohttp_call_back_event_t p_event, struct st_h3zero_stream_ctx_t *p_stream_ctx,
		void *p_path_app_ctx);

// Per-owner packet-loop callbacks. Each type knows how to drain its own
// work queue and refresh its cached cnx state. picoquic invokes these on
// the network thread.
int _backend_loop_callback(picoquic_quic_t *p_quic, picoquic_packet_loop_cb_enum p_mode,
		void *p_callback_ctx, void *p_callback_argv) {
	(void)p_callback_argv;
	BackendCtx *bctx = static_cast<BackendCtx *>(p_callback_ctx);
	if (!bctx) {
		return 0;
	}
	// Drain on every callback, not only wake_up (see _server_loop_cb): a lost or
	// coalesced wake would otherwise strand the client's queued sends forever.
	_drain_work_queue(bctx->work, bctx->cnx, nullptr, nullptr, nullptr);
	// Refresh the cached state for foreign readers on every loop tick.
	picoquic_cnx_t *cnx = picoquic_get_first_cnx(p_quic);
	bctx->cached_cnx_state.set(cnx ? static_cast<int>(picoquic_get_cnx_state(cnx)) : -1);
	return 0;
}

// Defined after WTSessionCtx is fully declared (see WT section below).
int _wt_loop_callback(picoquic_quic_t *p_quic, picoquic_packet_loop_cb_enum p_mode,
		void *p_callback_ctx, void *p_callback_argv);

// Echo server has no foreign-thread queue (all work happens in callbacks),
// so its loop callback is a no-op.
int _echo_loop_callback(picoquic_quic_t *p_quic, picoquic_packet_loop_cb_enum p_mode,
		void *p_callback_ctx, void *p_callback_argv) {
	(void)p_quic;
	(void)p_mode;
	(void)p_callback_ctx;
	(void)p_callback_argv;
	return 0;
}

void _dispatch_cnx_event(QUICClient *p_client, uint64_t p_stream_id,
		const uint8_t *p_bytes, size_t p_length, int p_event) {
	if (!p_client) {
		return;
	}
	switch (p_event) {
		case picoquic_callback_stream_data: {
			PackedByteArray buf;
			if (p_length > 0 && p_bytes) {
				buf.resize(p_length);
				memcpy(buf.ptrw(), p_bytes, p_length);
			}
			p_client->_push_incoming_stream_data(static_cast<int64_t>(p_stream_id), buf);
		} break;
		case picoquic_callback_stream_fin: {
			// Any trailing bytes arrive on the same callback invocation in
			// picoquic's API — push them then flag the FIN.
			if (p_length > 0 && p_bytes) {
				PackedByteArray buf;
				buf.resize(p_length);
				memcpy(buf.ptrw(), p_bytes, p_length);
				p_client->_push_incoming_stream_data(static_cast<int64_t>(p_stream_id), buf);
			}
			p_client->_push_incoming_stream_fin(static_cast<int64_t>(p_stream_id));
		} break;
		case picoquic_callback_datagram: {
			PackedByteArray buf;
			if (p_length > 0 && p_bytes) {
				buf.resize(p_length);
				memcpy(buf.ptrw(), p_bytes, p_length);
			}
			p_client->_push_incoming_datagram(buf);
		} break;
		case picoquic_callback_almost_ready:
		case picoquic_callback_ready:
			p_client->_set_status(QUICClient::STATUS_CONNECTED);
			break;
		case picoquic_callback_close:
		case picoquic_callback_application_close:
		case picoquic_callback_stateless_reset:
			p_client->_set_status(QUICClient::STATUS_DISCONNECTED);
			break;
		default:
			// Unknown / not routed yet — picoquic has ~20 event types,
			// we cover the ones that touch our in-memory contract.
			break;
	}
}

int _default_stream_data_cb(picoquic_cnx_t *p_cnx, uint64_t p_stream_id,
		uint8_t *p_bytes, size_t p_length, picoquic_call_back_event_t p_event,
		void *p_callback_ctx, void *p_stream_ctx) {
	(void)p_cnx;
	(void)p_stream_ctx;
	QUICClient *client = static_cast<QUICClient *>(p_callback_ctx);
	_dispatch_cnx_event(client, p_stream_id, p_bytes, p_length, static_cast<int>(p_event));
	return 0;
}

void *_create_backend(QUICClient *p_client, const char *p_host, int p_port, const char *p_alpn) {
	// TLS providers initialized once at module registration time.
	picoquic_quic_t *quic = picoquic_create(
			/*max_nb_connections=*/1,
			/*cert_file_name=*/nullptr,
			/*key_file_name=*/nullptr,
			/*cert_root_file_name=*/nullptr,
			/*default_alpn=*/p_alpn,
			/*default_callback_fn=*/_default_stream_data_cb,
			/*default_callback_ctx=*/nullptr,
			/*cnx_id_callback=*/nullptr,
			/*cnx_id_callback_data=*/nullptr,
			/*reset_seed=*/nullptr,
			/*current_time=*/picoquic_current_time(),
			/*p_simulated_time=*/nullptr,
			/*ticket_file_name=*/nullptr,
			/*ticket_encryption_key=*/nullptr,
			/*ticket_encryption_key_length=*/0);
	if (!quic) {
		ERR_PRINT("picoquic_create returned null (alpn=" + String(p_alpn) + ")");
		return nullptr;
	}

	// Resolve the peer address via getaddrinfo (handles IPv4 numeric, IPv6
	// numeric, and DNS hostnames uniformly). We use a sockaddr_storage so
	// the sockaddr we pass to picoquic is large enough for either family.
	sockaddr_storage peer_addr{};
	{
		char port_str[8];
		snprintf(port_str, sizeof(port_str), "%d", p_port);
		addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;
		addrinfo *result = nullptr;
		int gai_ret = getaddrinfo(p_host, port_str, &hints, &result);
		if (gai_ret != 0 || !result) {
			// Hostname did not resolve — surface as null ctx so
			// connect_to_host can return ERR_CANT_RESOLVE.
			picoquic_free(quic);
			return nullptr;
		}
		memcpy(&peer_addr, result->ai_addr, result->ai_addrlen);
		freeaddrinfo(result);
	}

	picoquic_cnx_t *cnx = picoquic_create_client_cnx(
			quic,
			reinterpret_cast<sockaddr *>(&peer_addr),
			picoquic_current_time(),
			/*preferred_version=*/0,
			/*sni=*/p_host,
			/*alpn=*/p_alpn,
			_default_stream_data_cb,
			/*callback_ctx=*/p_client);
	if (!cnx) {
		ERR_PRINT("picoquic_create_client_cnx returned null");
		picoquic_free(quic);
		return nullptr;
	}

	// picoquic_create_client_cnx already invokes picoquic_start_client_cnx
	// internally, so the handshake state machine is already armed. The
	// ClientHello bytes sit in cnx's TLS stream send queue until the next
	// integration step drives a UDP packet loop.

	BackendCtx *bctx = new BackendCtx();
	bctx->quic = quic;
	bctx->cnx = cnx;
	// Seed the cached state before the network thread starts so foreign
	// readers get something other than -1 (the "no cnx" sentinel) until
	// the first loop tick refreshes it.
	bctx->cached_cnx_state.set(static_cast<int>(picoquic_get_cnx_state(cnx)));

	// Start the background packet loop. picoquic owns the UDP socket inside
	// the thread; events arrive via _default_stream_data_cb on the cnx.
	// local_port=0 lets the OS pick an ephemeral client port.
	bctx->loop_param.local_port = 0;
	bctx->loop_param.local_af = peer_addr.ss_family;
	int start_ret = 0;
	bctx->thread_ctx = picoquic_start_network_thread(
			quic, &bctx->loop_param, &_backend_loop_callback, bctx, &start_ret);
	if (!bctx->thread_ctx) {
		ERR_PRINT(vformat("picoquic_start_network_thread failed: %d", start_ret));
		// cnx is owned by quic; picoquic_free will clean it up.
		picoquic_free(quic);
		delete bctx;
		return nullptr;
	}
	return bctx;
}

void _destroy_backend(void *p_ctx) {
	if (!p_ctx) {
		return;
	}
	BackendCtx *bctx = static_cast<BackendCtx *>(p_ctx);
	// Stop the network thread first — it dereferences quic.
	if (bctx->thread_ctx) {
		picoquic_delete_network_thread(bctx->thread_ctx);
		bctx->thread_ctx = nullptr;
	}
	// picoquic_free(quic) walks the cnx list and cleans them up, so we
	// don't call picoquic_delete_cnx first.
	if (bctx->quic) {
		picoquic_free(bctx->quic);
	}
	delete bctx;
}

int _backend_cnx_count(void *p_ctx) {
	if (!p_ctx) {
		return 0;
	}
	BackendCtx *bctx = static_cast<BackendCtx *>(p_ctx);
	return bctx->cnx != nullptr ? 1 : 0;
}

Error _backend_send_h3_get(void *p_ctx, const char *p_path, const char *p_host) {
	if (!p_ctx || !p_path || !p_host) {
		return ERR_INVALID_PARAMETER;
	}
	BackendCtx *bctx = static_cast<BackendCtx *>(p_ctx);
	if (!bctx->cnx) {
		return ERR_UNCONFIGURED;
	}

	// Control stream (client uni, id 2): one-byte stream-type 0x00 + SETTINGS
	// frame with no parameters (type 0x04, length 0).
	{
		WorkItem ctrl;
		ctrl.kind = WORK_SEND_STREAM;
		ctrl.stream_id = 2;
		ctrl.bytes.resize(3);
		ctrl.bytes[0] = 0x00;
		ctrl.bytes[1] = 0x04;
		ctrl.bytes[2] = 0x00;
		ctrl.fin = false;
		bctx->work.push(std::move(ctrl));
	}

	// Request on client bidi stream 0: a proper h3 HEADERS frame envelope
	// (type 0x01 + varint length) wrapping the QPACK-encoded field section.
	// h3zero_create_request_header_frame writes the payload only; we prepend
	// the frame header manually (mirrors picohttp/h3zero_client.c:50–73).
	uint8_t buf[1024];
	buf[0] = 0x01; // h3zero_frame_header = HEADERS
	// Reserve 2 bytes for the length varint; write payload starting at buf[3].
	uint8_t *payload_end = h3zero_create_request_header_frame(
			buf + 3, buf + sizeof(buf),
			reinterpret_cast<const uint8_t *>(p_path), strlen(p_path),
			p_host);
	if (!payload_end) {
		return ERR_OUT_OF_MEMORY;
	}
	size_t payload_len = payload_end - (buf + 3);
	size_t hdr_len;
	if (payload_len < 64) {
		// 1-byte varint fits in one byte with the 0-prefix.
		buf[1] = static_cast<uint8_t>(payload_len);
		memmove(buf + 2, buf + 3, payload_len);
		hdr_len = 1 + 1 + payload_len;
	} else {
		// 2-byte varint with 0b01 prefix.
		buf[1] = static_cast<uint8_t>((payload_len >> 8) | 0x40);
		buf[2] = static_cast<uint8_t>(payload_len & 0xFF);
		hdr_len = 1 + 2 + payload_len;
	}
	{
		WorkItem req;
		req.kind = WORK_SEND_STREAM;
		req.stream_id = 0;
		req.bytes.resize(hdr_len);
		memcpy(req.bytes.ptr(), buf, hdr_len);
		req.fin = true;
		bctx->work.push(std::move(req));
	}
	// Kick the network thread so it drains the work queue.
	if (bctx->thread_ctx) {
		picoquic_wake_up_network_thread(bctx->thread_ctx);
	}
	return OK;
}

int _backend_cnx_state(void *p_ctx) {
	if (!p_ctx) {
		return -1;
	}
	BackendCtx *bctx = static_cast<BackendCtx *>(p_ctx);
	// The network thread caches the cnx state in _loop_callback. Foreign
	// threads read the cached value rather than dereferencing cnx directly.
	return bctx->cached_cnx_state.get();
}

} // namespace

// ---- WebTransport session backend -----------------------------------------
// Uses picoquic's own setup helpers (RFC 9220 via picowt_*). The WT ctx owns
// a full h3 stack: picoquic_quic_t + picoquic_cnx_t + h3zero_callback_ctx_t +
// h3zero_stream_ctx_t (the control stream) + network thread.

namespace {

struct WTSessionCtx {
	picoquic_quic_t *quic = nullptr;
	picoquic_cnx_t *cnx = nullptr;
	h3zero_callback_ctx_t *h3_ctx = nullptr;
	h3zero_stream_ctx_t *control_stream = nullptr;
	picoquic_network_thread_ctx_t *thread_ctx = nullptr;
	picoquic_packet_loop_param_t loop_param{};
	WebTransportPeer *peer = nullptr;
	// Session state updated on the network thread and read from anywhere.
	SafeNumeric<int> session_state{ WebTransportPeer::SESSION_DISCONNECTED };
	// Deferred CONNECT: stored here at create time, fired from
	// callback_almost_ready when 1-RTT keys are available.
	char *connect_host = nullptr;
	char *connect_path = nullptr;
	bool connect_pending = false;
	// Foreign-thread send requests drain into picoquic on the network thread.
	WorkQueue work;
	// One persistent bidi data stream carrying reliable messages as length-prefixed
	// wtd frames; reliable_rx accumulates inbound bytes for incremental decode.
	int64_t reliable_stream_id = -1;
	LocalVector<uint8_t> reliable_rx;
};

// Drain the WT work queue from the network thread.
int _wt_loop_callback(picoquic_quic_t *p_quic, picoquic_packet_loop_cb_enum p_mode,
		void *p_callback_ctx, void *p_callback_argv) {
	(void)p_quic;
	(void)p_callback_argv;
	WTSessionCtx *wctx = static_cast<WTSessionCtx *>(p_callback_ctx);
	if (!wctx) {
		return 0;
	}
	// Drain on EVERY loop callback, not only wake_up (see _server_loop_cb): a lost
	// or coalesced wake otherwise strands the WebTransport CLIENT's queued sends
	// forever, so the client stops transmitting a few seconds into load while the
	// thread stays alive in poll(). This is the client-side half of the wedge.
	_drain_work_queue(wctx->work, wctx->cnx, wctx->control_stream,
			&_wt_app_callback, wctx, &wctx->reliable_stream_id);
	return 0;
}

// Wrapper around h3zero_callback that fires the deferred picowt_connect
// when the client reaches almost_ready (1-RTT keys available).
int _wt_client_callback(picoquic_cnx_t *p_cnx,
		uint64_t p_stream_id, uint8_t *p_bytes, size_t p_length,
		picoquic_call_back_event_t p_event, void *p_callback_ctx, void *p_stream_ctx) {
	// Let h3zero handle everything first.
	int ret = h3zero_callback(p_cnx, p_stream_id, p_bytes, p_length, p_event, p_callback_ctx, p_stream_ctx);
	if (ret != 0) {
		return ret;
	}
	// After h3zero handles almost_ready (sends SETTINGS), fire the deferred
	// CONNECT if pending.
	if (p_event == picoquic_callback_almost_ready || p_event == picoquic_callback_ready) {
		// Walk the callback_ctx to find our WTSessionCtx. h3zero_callback
		// replaces the ctx on first call, so we stored a back-pointer.
		// For now, use the quic's app context pointer.
		WTSessionCtx *wctx = static_cast<WTSessionCtx *>(
				picoquic_get_default_callback_context(p_cnx->quic));
		if (wctx != nullptr && wctx->connect_pending) {
			wctx->connect_pending = false;
			h3zero_callback_ctx_t *h3_ctx = (h3zero_callback_ctx_t *)picoquic_get_callback_context(p_cnx);
			int cr = picowt_connect(p_cnx, h3_ctx, wctx->control_stream,
					wctx->connect_host, wctx->connect_path,
					_wt_app_callback, wctx, nullptr);
			if (cr != 0) {
				return -1;
			}
		}
	}
	return 0;
}

int _wt_app_callback(picoquic_cnx_t *p_cnx, uint8_t *p_bytes, size_t p_length,
		picohttp_call_back_event_t p_event, struct st_h3zero_stream_ctx_t *p_stream_ctx,
		void *p_path_app_ctx) {
	(void)p_stream_ctx;
	// p_path_app_ctx is the WTSessionCtx* we passed as wt_ctx to picowt_connect.
	WTSessionCtx *wctx = static_cast<WTSessionCtx *>(p_path_app_ctx);
	WebTransportPeer *peer = wctx ? wctx->peer : nullptr;

	// Elixir-style state machine: explicit {current_state, event} → next_state
	// transitions. Only listed pairs are valid; anything else is a no-op.
	// Terminal state (SESSION_CLOSED) is reachable from any state.
	using SS = WebTransportPeer::SessionState;
	if (wctx) {
		SS cur = static_cast<SS>(wctx->session_state.get());
		SS next = cur;
		switch (p_event) {
			case picohttp_callback_connecting:
				// {QUIC_HANDSHAKING, connecting} → WT_CONNECTING
				if (cur == SS::SESSION_QUIC_HANDSHAKING || cur == SS::SESSION_H3_SETTINGS) {
					next = SS::SESSION_WT_CONNECTING;
				}
				break;
			case picohttp_callback_connect_accepted:
				// {WT_CONNECTING, connect_accepted} → SESSION_OPEN
				if (cur == SS::SESSION_WT_CONNECTING) {
					next = SS::SESSION_OPEN;
				}
				break;
			case picohttp_callback_post_datagram:
				// {SESSION_OPEN, post_datagram} → SESSION_OPEN (data event, no transition)
				if (cur == SS::SESSION_OPEN && peer && p_bytes && p_length > 0) {
					peer->_push_wt_incoming_datagram(p_bytes, p_length);
				}
				break;
			case picohttp_callback_post_data:
			case picohttp_callback_post_fin:
				// The server's persistent stream carries length-prefixed wtd frames:
				// accumulate and decode each complete frame into a RELIABLE packet.
				if (cur == SS::SESSION_OPEN && peer && p_bytes && p_length > 0) {
					_reliable_accumulate(wctx->reliable_rx, p_bytes, p_length);
					_reliable_decode(wctx->reliable_rx, peer, 1);
				}
				break;
			case picohttp_callback_connect_refused:
			case picohttp_callback_reset:
			case picohttp_callback_deregister:
			case picohttp_callback_free:
				// {ANY, close-like} → SESSION_CLOSED
				next = SS::SESSION_CLOSED;
				break;
			default:
				break;
		}
		if (next != cur) {
			wctx->session_state.set(next);
		}
	}
	return 0;
}

void *_create_wt_session_backend(WebTransportPeer *p_peer, const char *p_host, int p_port, const char *p_path) {
	(void)p_peer;
	if (!p_host) {
		return nullptr;
	}

	// TLS providers initialized once at module registration time.

	// Per-process-shared quic_t that serves WT connections. picowt sets
	// WT-friendly default transport parameters on it. The first argument is
	// `max_nb_connections`: the server's QUIC connection-table size. A size of one
	// collides concurrent sessions onto a single slot and stalls the server under
	// multi-player load. This is the TRANSPORT budget (authority sessions +
	// interest fanout), distinct from the 150-player AUTHORITY cap the application
	// enforces — see WT_SERVER_MAX_CONNECTIONS above.
	picoquic_quic_t *quic = picoquic_create(
			WT_SERVER_MAX_CONNECTIONS, nullptr, nullptr, nullptr,
			"h3", nullptr, nullptr,
			nullptr, nullptr, nullptr,
			picoquic_current_time(),
			nullptr, nullptr, nullptr, 0);
	if (!quic) {
		ERR_PRINT("picoquic_create (WT) returned null");
		return nullptr;
	}
	// For loopback testing with self-signed certs: skip cert verification.
	// Production will need proper CA trust store wiring.
	picoquic_set_null_verifier(quic);
	// NB: the picoquic textlog writes a full per-packet trace to stdout
	// synchronously; under multi-session load the flush to stdout/journald blocks
	// the picoquic network thread and the whole server stalls (every client goes
	// silent at once). Gate it behind the engine's verbose flag (`--verbose`),
	// the same convention other modules use, so it's off by default.
	if (OS::get_singleton()->is_stdout_verbose()) {
		picoquic_set_textlog(quic, "-");
		picoquic_set_log_level(quic, 1);
	}
	picowt_set_default_transport_parameters(quic);

	// Resolve peer.
	sockaddr_storage peer_addr{};
	{
		char port_str[8];
		snprintf(port_str, sizeof(port_str), "%d", p_port);
		addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;
		addrinfo *result = nullptr;
		if (getaddrinfo(p_host, port_str, &hints, &result) != 0 || !result) {
			picoquic_free(quic);
			return nullptr;
		}
		memcpy(&peer_addr, result->ai_addr, result->ai_addrlen);
		freeaddrinfo(result);
	}

	// Ensure server can see our datagram support.
	picoquic_set_default_tp_value(quic,
			picoquic_tp_max_datagram_frame_size, PICOQUIC_MAX_PACKET_SIZE);

	picoquic_cnx_t *cnx = nullptr;
	h3zero_callback_ctx_t *h3_ctx = nullptr;
	h3zero_stream_ctx_t *stream_ctx = nullptr;
	int prep_ret = picowt_prepare_client_cnx(
			quic,
			reinterpret_cast<sockaddr *>(&peer_addr),
			&cnx, &h3_ctx, &stream_ctx,
			picoquic_current_time(),
			p_host);
	if (prep_ret != 0 || !cnx || !h3_ctx || !stream_ctx) {
		ERR_PRINT(vformat("picowt_prepare_client_cnx failed: %d", prep_ret));
		if (h3_ctx) {
			h3zero_callback_delete_context(cnx, h3_ctx);
		}
		picoquic_free(quic);
		return nullptr;
	}

	// Allocate WTSessionCtx BEFORE picowt_connect so the callback
	// (picohttp_callback_connecting) can access it. The wctx is passed
	// as wt_ctx to picowt_connect, so p_path_app_ctx in _wt_app_callback
	// is the wctx — not the peer directly.
	WTSessionCtx *wctx = new WTSessionCtx();
	wctx->quic = quic;
	wctx->cnx = cnx;
	wctx->h3_ctx = h3_ctx;
	wctx->control_stream = stream_ctx;
	wctx->peer = p_peer;
	wctx->session_state.set(WebTransportPeer::SESSION_QUIC_HANDSHAKING);
	// Defer the Extended CONNECT until callback_almost_ready, when 1-RTT
	// keys are available. Queuing stream data before the handshake causes
	// INTERNAL_ERROR (picoquic tries to send 1-RTT data without keys).
	wctx->connect_host = (char *)memalloc(strlen(p_host ? p_host : "") + 1);
	strcpy(wctx->connect_host, p_host ? p_host : "");
	wctx->connect_path = (char *)memalloc(strlen(p_path ? p_path : "") + 1);
	strcpy(wctx->connect_path, p_path ? p_path : "");
	wctx->connect_pending = true;

	// Replace h3zero_callback (set by picowt_prepare) with our wrapper
	// that fires the deferred CONNECT on almost_ready.
	picoquic_set_callback(cnx, _wt_client_callback, picoquic_get_callback_context(cnx));
	// Store wctx as the quic's default callback context so the wrapper can
	// find it (h3zero replaces the per-cnx ctx on first call).
	// Note: this is safe because the client quic has only one connection.
	quic->default_callback_ctx = wctx;

	// picowt_prepare_client_cnx uses picoquic_create_cnx (not _client_cnx),
	// so the TLS handshake is armed but not started. Kick it off now.
	int start_ret = picoquic_start_client_cnx(cnx);
	if (start_ret != 0) {
		ERR_PRINT(vformat("picoquic_start_client_cnx (WT) failed: %d", start_ret));
		h3zero_callback_delete_context(cnx, h3_ctx);
		picoquic_free(quic);
		delete wctx;
		return nullptr;
	}

	// wctx exists before picowt_connect; this sets the remaining fields.
	wctx->loop_param.local_port = 0;
	wctx->loop_param.local_af = peer_addr.ss_family;
	int loop_start_ret = 0;
	wctx->thread_ctx = picoquic_start_network_thread(
			quic, &wctx->loop_param, &_wt_loop_callback, wctx, &loop_start_ret);
	if (!wctx->thread_ctx) {
		ERR_PRINT(vformat("picoquic_start_network_thread (WT) failed: %d", loop_start_ret));
		h3zero_callback_delete_context(cnx, h3_ctx);
		picoquic_free(quic);
		delete wctx;
		return nullptr;
	}
	return wctx;
}

void _destroy_wt_session_backend(void *p_ctx) {
	if (!p_ctx) {
		return;
	}
	WTSessionCtx *wctx = static_cast<WTSessionCtx *>(p_ctx);
	if (wctx->thread_ctx) {
		picoquic_delete_network_thread(wctx->thread_ctx);
	}
	// h3zero_callback only frees the h3 context on close in server mode.
	// For client mode, we must free it manually. Order matters:
	//   1) Clear the cnx callback so picoquic_free's disconnect event
	//      doesn't read h3_ctx after we free it.
	//   2) Free h3_ctx while cnx is still alive (prefix deletion walks it).
	//   3) picoquic_free destroys the cnx + quic.
	if (wctx->cnx) {
		picoquic_set_callback(wctx->cnx, nullptr, nullptr);
	}
	if (wctx->h3_ctx && wctx->cnx) {
		h3zero_callback_delete_context(wctx->cnx, wctx->h3_ctx);
		wctx->h3_ctx = nullptr;
	}
	if (wctx->quic) {
		picoquic_free(wctx->quic);
	}
	if (wctx->connect_host) {
		memfree(wctx->connect_host);
	}
	if (wctx->connect_path) {
		memfree(wctx->connect_path);
	}
	delete wctx;
}

Error _send_wt_datagram(void *p_ctx, const uint8_t *p_bytes, size_t p_len) {
	if (!p_ctx) {
		return ERR_UNCONFIGURED;
	}
	WTSessionCtx *wctx = static_cast<WTSessionCtx *>(p_ctx);
	if (!wctx->cnx || !wctx->control_stream) {
		return ERR_UNCONFIGURED;
	}
	// RFC 9297: WT datagrams are QUIC DATAGRAM frames with a quarter-stream-id
	// varint prefix (session CONNECT stream_id / 4) followed by app payload.
	// Build the full on-wire frame here, then hand it off to the network
	// thread via the work queue — picoquic must only be touched from there.
	uint64_t quarter_id = wctx->control_stream->stream_id / 4;
	size_t prefix_len;
	uint8_t prefix[2];
	if (quarter_id < 64) {
		prefix[0] = (uint8_t)quarter_id;
		prefix_len = 1;
	} else if (quarter_id < 16384) {
		prefix[0] = (uint8_t)((quarter_id >> 8) | 0x40);
		prefix[1] = (uint8_t)(quarter_id & 0xFF);
		prefix_len = 2;
	} else {
		return ERR_INVALID_PARAMETER; // session id too large for this scaffold
	}
	if (prefix_len + p_len > PICOQUIC_MAX_PACKET_SIZE) {
		return ERR_OUT_OF_MEMORY;
	}
	WorkItem dg;
	dg.kind = WORK_SEND_DATAGRAM;
	dg.bytes.resize(prefix_len + p_len);
	memcpy(dg.bytes.ptr(), prefix, prefix_len);
	memcpy(dg.bytes.ptr() + prefix_len, p_bytes, p_len);
	wctx->work.push(std::move(dg));
	if (wctx->thread_ctx) {
		picoquic_wake_up_network_thread(wctx->thread_ctx);
	}
	return OK;
}

Error _send_wt_stream(void *p_ctx, const uint8_t *p_bytes, size_t p_len) {
	if (!p_ctx) {
		return ERR_UNCONFIGURED;
	}
	WTSessionCtx *wctx = static_cast<WTSessionCtx *>(p_ctx);
	if (!wctx->cnx || !wctx->control_stream) {
		return ERR_UNCONFIGURED;
	}
	// Stream work touches the h3zero context tree, which is only safe on the
	// network thread. Frame the payload (length-prefixed) and enqueue; the loop
	// opens the persistent reliable stream once and appends (no FIN per message).
	WorkItem w;
	w.kind = WORK_OPEN_WT_STREAM;
	_frame_reliable(p_bytes, p_len, w.bytes);
	w.fin = false;
	wctx->work.push(std::move(w));
	if (wctx->thread_ctx) {
		picoquic_wake_up_network_thread(wctx->thread_ctx);
	}
	return OK;
}

int _wt_session_state(void *p_ctx) {
	if (!p_ctx) {
		return WebTransportPeer::SESSION_DISCONNECTED;
	}
	return static_cast<WTSessionCtx *>(p_ctx)->session_state.get();
}

void _set_wt_session_state(void *p_ctx, int p_state) {
	if (p_ctx) {
		static_cast<WTSessionCtx *>(p_ctx)->session_state.set(p_state);
	}
}

// ---- In-process WT echo server for loopback tests -------------------------

// Hardcoded test cert/key from thirdparty/picoquic/certs/ so tests don't
// depend on CWD-relative file paths. Production code will use Godot's
// Crypto class to generate certs.
// ECDSA test cert/key from thirdparty/picoquic/certs/ecdsa/. ECDSA is the
// standard key type for QUIC (lighter than RSA, universally supported by
// TLS 1.3 cipher suites including the ones mbedtls registers).
static const char _TEST_CERT_PEM[] =
		"-----BEGIN CERTIFICATE-----\n"
		"MIICYDCCAUigAwIBAgIBATANBgkqhkiG9w0BAQsFADAaMRgwFgYDVQQDEw9waWNv\n"
		"dGxzIHRlc3QgY2EwHhcNMTgwMjIzMDUzMTA0WhcNMjgwMjIxMDUzMTA0WjAbMRkw\n"
		"FwYDVQQDExB0ZXN0LmV4YW1wbGUuY29tMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcD\n"
		"QgAE2silQFS6M9oYqUF/SVPfYOamPbaOUzqf3RkUXqsDz7z7NpgWJI8HKW0V2E8w\n"
		"6Alk+xT8hnzUBsL9neiZP0iMK6N7MHkwCQYDVR0TBAIwADAsBglghkgBhvhCAQ0E\n"
		"HxYdT3BlblNTTCBHZW5lcmF0ZWQgQ2VydGlmaWNhdGUwHQYDVR0OBBYEFO4whhah\n"
		"0mmtZOTXd2uy/VxPAaK1MB8GA1UdIwQYMBaAFL95ypeyYHgglqpGV5zfp7Ij9SVj\n"
		"MA0GCSqGSIb3DQEBCwUAA4IBAQCPrJwBbYGqjK5dtRZ06ujrJluxZtVr1E15DW2H\n"
		"qba/dC3Bsi5StkvKDQFFOFga0mptIJhaUbBvLD8PEojtfAmldAAhPUvSLVSqU4tk\n"
		"+R7qpYrnYV5WklI2PqBoWZx9s+hcS3du3ijtGJGpnDnSlsyYBYx03B4SWzi9Vsuj\n"
		"6OEqWivSMkXBEIUgbGs06maRDi64ZIefB7wjTyOtvonfCphH6WMC00H0LaTO3ePY\n"
		"QQj+30fA52OOH/BLxa6rwLo4PuOQnAi9dRy5uFRDHZlC4KK3dbsUA3ma9gfYpasr\n"
		"OnCLd4Vwipg4mzUJ9mJrKUqnp/k73tjIkFfydiojCwFoxpry\n"
		"-----END CERTIFICATE-----\n";

static const char _TEST_KEY_PEM[] =
		"-----BEGIN PRIVATE KEY-----\n"
		"MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgwXS0+V7+egEOvkro\n"
		"M7I2E/xl6WWRqDmemoD7q9H/ujqhRANCAATayKVAVLoz2hipQX9JU99g5qY9to5T\n"
		"Op/dGRReqwPPvPs2mBYkjwcpbRXYTzDoCWT7FPyGfNQGwv2d6Jk/SIwr\n"
		"-----END PRIVATE KEY-----\n";

// Load cert chain + private key from hardcoded PEM buffers into a
// picoquic_quic_t. No temp files — uses picoquic_set_tls_certificate_chain

struct WTEchoServerSessionCtx {
	uint8_t *pending_echo = nullptr;
	size_t pending_echo_len = 0;
	uint64_t session_stream_id = 0;
};

struct WTEchoServer {
	picoquic_quic_t *quic = nullptr;
	picoquic_network_thread_ctx_t *thread_ctx = nullptr;
	picoquic_packet_loop_param_t loop_param{};
	picohttp_server_parameters_t server_param{};
	picohttp_server_path_item_t path_table[1]{};
	// Session contexts are owned by the server, not by h3zero stream nodes.
	// h3zero calls picohttp_callback_free on each stream node during teardown
	// but does not expect the app to free shared context there (see wt_baton.c).
	Vector<WTEchoServerSessionCtx *> session_contexts;
};

static WTEchoServer *_echo_server = nullptr;

int _echo_path_callback(picoquic_cnx_t *p_cnx, uint8_t *p_bytes, size_t p_length,
		picohttp_call_back_event_t p_event, struct st_h3zero_stream_ctx_t *p_stream_ctx,
		void *p_path_app_ctx) {
	WTEchoServerSessionCtx *sctx = static_cast<WTEchoServerSessionCtx *>(p_path_app_ctx);
	switch (p_event) {
		case picohttp_callback_connect: {
			WTEchoServerSessionCtx *ctx = new WTEchoServerSessionCtx();
			if (_echo_server) {
				_echo_server->session_contexts.push_back(ctx);
			}
			if (p_stream_ctx) {
				ctx->session_stream_id = p_stream_ctx->stream_id;
				p_stream_ctx->path_callback_ctx = ctx;
				// Register stream prefix so h3zero routes datagrams to us.
				// Without this, h3zero_callback_datagram can't find the session.
				h3zero_callback_ctx_t *h3_ctx = (h3zero_callback_ctx_t *)picoquic_get_callback_context(p_cnx);
				if (h3_ctx) {
					h3zero_declare_stream_prefix(h3_ctx, p_stream_ctx->stream_id,
							_echo_path_callback, ctx);
				}
			}
			return 0;
		}
		case picohttp_callback_post_datagram:
			if (!sctx || !p_cnx || !p_bytes || p_length == 0) {
				return -1;
			}
			{
				uint64_t qid = sctx->session_stream_id / 4;
				uint8_t echo_frame[PICOQUIC_MAX_PACKET_SIZE];
				size_t plen = (qid < 64) ? 1 : 2;
				if (plen == 1) {
					echo_frame[0] = (uint8_t)qid;
				} else {
					echo_frame[0] = (uint8_t)((qid >> 8) | 0x40);
					echo_frame[1] = (uint8_t)(qid & 0xFF);
				}
				if (plen + p_length <= sizeof(echo_frame)) {
					memcpy(echo_frame + plen, p_bytes, p_length);
					picoquic_queue_datagram_frame(p_cnx, plen + p_length, echo_frame);
				}
			}
			return 0;
		case picohttp_callback_post_data:
		case picohttp_callback_post_fin:
			if (p_cnx && p_stream_ctx && p_bytes && p_length > 0) {
				int fin = (p_event == picohttp_callback_post_fin) ? 1 : 0;
				picoquic_add_to_stream(p_cnx, p_stream_ctx->stream_id, p_bytes, p_length, fin);
			} else if (p_event == picohttp_callback_post_fin && p_cnx && p_stream_ctx) {
				picoquic_add_to_stream(p_cnx, p_stream_ctx->stream_id, nullptr, 0, 1);
			}
			return 0;
		case picohttp_callback_deregister:
		case picohttp_callback_free:
			// Do not free sctx here — h3zero calls this for every stream
			// node during teardown, and multiple nodes share the same ctx.
			// The server owns session contexts and frees them in
			// _stop_echo_server (same pattern as wt_baton.c).
			return 0;
		default:
			return 0;
	}
}

Error _start_echo_server(int p_port) {
	if (_echo_server) {
		return ERR_ALREADY_IN_USE;
	}

	// TLS providers initialized once at module registration time.
	_echo_server = new WTEchoServer();
	_echo_server->path_table[0].path = "/echo";
	_echo_server->path_table[0].path_length = 5;
	_echo_server->path_table[0].path_callback = _echo_path_callback;
	_echo_server->path_table[0].path_app_ctx = nullptr;

	_echo_server->server_param.path_table = _echo_server->path_table;
	_echo_server->server_param.path_table_nb = 1;

	// h3zero_callback expects a picohttp_server_parameters_t* as the
	// default callback context — it creates the h3 context lazily on the
	// first incoming connection (see h3zero_common.c line ~1870). A
	// pre-created h3_ctx casts as server_params, so the path table does
	// not register.
	// Use picoquic's bundled ECDSA test certs (known compatible with mbedtls
	// TLS 1.3). Write from embedded PEM constants to temp files for
	// picoquic_create's file-based loading path.
	{
		Ref<FileAccess> fc = FileAccess::open("user://godot_quic_test_cert.pem", FileAccess::WRITE);
		if (fc.is_valid()) {
			fc->store_string(_TEST_CERT_PEM);
		}
		Ref<FileAccess> fk = FileAccess::open("user://godot_quic_test_key.pem", FileAccess::WRITE);
		if (fk.is_valid()) {
			fk->store_string(_TEST_KEY_PEM);
		}
	}

	// Open the actual paths from user:// for picoquic_create.
	// We need actual OS paths for picoquic to load.
	String cert_pwd = ProjectSettings::get_singleton()->globalize_path("user://godot_quic_test_cert.pem");
	String key_pwd = ProjectSettings::get_singleton()->globalize_path("user://godot_quic_test_key.pem");

	_echo_server->quic = picoquic_create(
			8,
			cert_pwd.utf8().get_data(),
			key_pwd.utf8().get_data(),
			nullptr,
			"h3",
			h3zero_callback,
			&_echo_server->server_param,
			nullptr, nullptr, nullptr,
			picoquic_current_time(),
			nullptr, nullptr, nullptr, 0);
	if (!_echo_server->quic) {
		delete _echo_server;
		_echo_server = nullptr;
		return ERR_CANT_CREATE;
	}

	// Fix 2: ALPN selection callback — without this, picoquic doesn't
	// transition incoming connections to the h3zero protocol layer.
	picoquic_set_alpn_select_fn_v2(_echo_server->quic, picoquic_demo_server_callback_select_alpn);

	picowt_set_default_transport_parameters(_echo_server->quic);
	picoquic_set_default_tp_value(_echo_server->quic,
			picoquic_tp_max_datagram_frame_size, PICOQUIC_MAX_PACKET_SIZE);

	_echo_server->loop_param.local_port = static_cast<uint16_t>(p_port);
	_echo_server->loop_param.local_af = 0; // 0 = dual-stack (AF_INET + AF_INET6)
	int start_ret = 0;
	_echo_server->thread_ctx = picoquic_start_network_thread(
			_echo_server->quic, &_echo_server->loop_param,
			&_echo_loop_callback, _echo_server, &start_ret);
	if (!_echo_server->thread_ctx) {
		picoquic_free(_echo_server->quic);
		delete _echo_server;
		_echo_server = nullptr;
		return ERR_CANT_CREATE;
	}
	return OK;
}

void _stop_echo_server() {
	if (!_echo_server) {
		return;
	}
	if (_echo_server->thread_ctx) {
		// picoquic_delete_network_thread signals the loop to stop, joins
		// the thread, and frees the thread context. It does NOT close
		// connections or free the quic context.
		picoquic_delete_network_thread(_echo_server->thread_ctx);
		_echo_server->thread_ctx = nullptr;
	}
	// picoquic_free walks the cnx list and cleans up each connection
	// (which triggers h3zero cleanup callbacks), then frees the context.
	// Same pattern as _destroy_backend / _destroy_wt_session_backend.
	if (_echo_server->quic) {
		picoquic_free(_echo_server->quic);
		_echo_server->quic = nullptr;
	}
	// Free session contexts after picoquic_free — the h3zero teardown
	// callbacks don't free them (see wt_baton.c pattern).
	for (int i = 0; i < _echo_server->session_contexts.size(); i++) {
		WTEchoServerSessionCtx *sctx = _echo_server->session_contexts[i];
		if (sctx->pending_echo) {
			memfree(sctx->pending_echo);
		}
		delete sctx;
	}
	_echo_server->session_contexts.clear();
	delete _echo_server;
	_echo_server = nullptr;
}

// ---- WT server backend -------------------------------------------------------
// Cert/key are generated by WebTransportPeer::listen() via Godot's Crypto API
// and passed here as NUL-terminated PEM strings. picoquic copies DER/key data.

struct WTServerSessionCtx {
	WebTransportPeer *peer = nullptr;
	uint64_t session_stream_id = UINT64_MAX;
	picoquic_cnx_t *cnx = nullptr;
	h3zero_stream_ctx_t *control_stream_ctx = nullptr;
	int peer_id = 0; // MultiplayerPeer id this session carries (2, 3, ...)
	// ONE persistent bidi data stream per session carries all reliable messages as
	// length-prefixed wtd frames (vs a fresh stream per message, which exhausts
	// stream credit and starves the connect handshake). reliable_rx accumulates
	// inbound bytes for incremental frame decoding.
	int64_t reliable_stream_id = -1;
	LocalVector<uint8_t> reliable_rx;
};

struct WTServerCtx {
	picoquic_quic_t *quic = nullptr;
	picoquic_network_thread_ctx_t *thread_ctx = nullptr;
	picoquic_packet_loop_param_t loop_param{};
	picohttp_server_parameters_t server_param{};
	picohttp_server_path_item_t path_table[1]{};
	WebTransportPeer *peer = nullptr;
	Mutex sessions_mutex;
	LocalVector<WTServerSessionCtx *> sessions;
	int next_peer_id = 2; // 1 is the server itself
	WorkQueue wq;
	CharString path_cs;
};

static WTServerCtx *_wt_server = nullptr;

static int _server_path_callback(picoquic_cnx_t *p_cnx, uint8_t *p_bytes, size_t p_length,
		picohttp_call_back_event_t p_event, struct st_h3zero_stream_ctx_t *p_stream_ctx,
		void *p_path_app_ctx) {
	WTServerSessionCtx *sctx = static_cast<WTServerSessionCtx *>(
			p_stream_ctx ? p_stream_ctx->path_callback_ctx : nullptr);

	switch (p_event) {
		case picohttp_callback_connect: {
			WTServerSessionCtx *ctx = new WTServerSessionCtx();
			ctx->peer = _wt_server ? _wt_server->peer : nullptr;
			ctx->cnx = p_cnx;
			if (p_stream_ctx) {
				ctx->session_stream_id = p_stream_ctx->stream_id;
				ctx->control_stream_ctx = p_stream_ctx;
				p_stream_ctx->path_callback_ctx = ctx;
				h3zero_callback_ctx_t *h3_ctx = (h3zero_callback_ctx_t *)picoquic_get_callback_context(p_cnx);
				if (h3_ctx) {
					h3zero_declare_stream_prefix(h3_ctx, p_stream_ctx->stream_id,
							_server_path_callback, ctx);
				}
			}
			if (_wt_server) {
				MutexLock lock(_wt_server->sessions_mutex);
				ctx->peer_id = _wt_server->next_peer_id++;
				_wt_server->sessions.push_back(ctx);
				if (_wt_server->peer) {
					// Record the peer; the announcement is poll()'s, on the main thread. This
					// is the picoquic network thread and a signal emitted from here reaches
					// script from the wrong one.
					_wt_server->peer->_server_peer_joined(ctx->peer_id);
				}
			}
			return 0;
		}
		case picohttp_callback_post_datagram:
			if (!sctx || !sctx->peer || !p_bytes || p_length == 0) {
				return 0;
			}
			sctx->peer->_push_wt_incoming_datagram(p_bytes, p_length, sctx->peer_id);
			return 0;
		case picohttp_callback_post_data:
		case picohttp_callback_post_fin:
			if (!sctx || !sctx->peer) {
				return 0;
			}
			// The client's persistent stream carries length-prefixed wtd frames:
			// accumulate and decode every complete frame.
			if (p_bytes && p_length > 0) {
				_reliable_accumulate(sctx->reliable_rx, p_bytes, p_length);
				_reliable_decode(sctx->reliable_rx, sctx->peer, sctx->peer_id);
			}
			if (p_event == picohttp_callback_post_fin && p_stream_ctx) {
				picoquic_add_to_stream(p_cnx, p_stream_ctx->stream_id, nullptr, 0, 1);
			}
			return 0;
		case picohttp_callback_deregister:
		case picohttp_callback_free:
			if (sctx) {
				bool owned = false;
				WebTransportPeer *leaving_peer = nullptr;
				int leaving_id = 0;
				if (_wt_server) {
					MutexLock lock(_wt_server->sessions_mutex);
					int64_t idx = _wt_server->sessions.find(sctx);
					if (idx >= 0) {
						_wt_server->sessions.remove_at(idx);
						owned = true;
						leaving_peer = _wt_server->peer;
						leaving_id = sctx->peer_id;
					}
				}
				// Outside the sessions lock. The peer takes its own roster lock, and the send
				// path takes them the other way round — roster first, then sessions — so
				// nesting them here would be the one ordering that can deadlock.
				//
				// Only the session actually removed above is reported gone. The line this
				// replaces recomputed a single bool from the session count, so with two
				// clients the first to leave cleared it while the second was still connected,
				// and the server stopped sending to a peer that was there.
				if (owned && leaving_peer) {
					leaving_peer->_server_peer_left(leaving_id);
				}
				if (owned) {
					delete sctx; // erase-then-delete: a second deregister/free finds nothing
				}
				if (p_stream_ctx) {
					p_stream_ctx->path_callback_ctx = nullptr;
				}
			}
			return 0;
		default:
			return 0;
	}
}

static int _server_loop_cb(picoquic_quic_t *p_quic, picoquic_packet_loop_cb_enum p_mode,
		void *p_callback_ctx, void *p_callback_argv) {
	(void)p_quic;
	(void)p_callback_argv;
	WTServerCtx *sctx = static_cast<WTServerCtx *>(p_callback_ctx);
	if (!sctx) {
		return 0;
	}
	// Drain the work queue on EVERY loop callback, not only `wake_up`. picoquic is
	// only touchable from this thread, and a wake-up can be lost or coalesced under
	// load (the wake pipe saturates, write() returns short and the byte is dropped).
	// If the drain were gated on wake_up, that one lost wake would strand every
	// queued send forever — the server stays alive but goes permanently silent.
	// Draining on wake_up / after_receive / after_send / time_check guarantees the
	// queue is applied on whatever callback fires next (≤ the 10s poll timeout even
	// when fully idle). Pairs with the inclusive sockloop iteration so a timeout
	// also reaches the send pass. Modeled in http3_queue (WakeLoop).
	{
		LocalVector<WorkItem> items;
		sctx->wq.drain(items);
		MutexLock lock(sctx->sessions_mutex);
		for (WorkItem &item : items) {
			// Validate the target session still lives; it can close between
			// queueing on the main thread and draining here.
			if (!item.session || sctx->sessions.find(item.session) < 0) {
				continue;
			}
			WTServerSessionCtx *target = item.session;
			switch (item.kind) {
				case WORK_SEND_DATAGRAM: {
					picoquic_queue_datagram_frame(target->cnx, item.bytes.size(), item.bytes.ptr());
				} break;
				case WORK_CLOSE_SESSION: {
					// Close the WebTransport session first, then the QUIC connection under it.
					// Dropping the connection alone leaves the client waiting out an idle
					// timeout to discover it was disconnected, because nothing told it.
					//
					// The session is not removed from `sessions` here. picoquic answers the
					// close with a deregister callback, and that path is the one that erases
					// and deletes — doing it twice is a double free, and doing it here would
					// leave the callback holding a pointer to freed memory.
					if (target->control_stream_ctx) {
						picowt_send_close_session_message(target->cnx, target->control_stream_ctx,
								0, nullptr);
					}
					picoquic_close(target->cnx, 0);
				} break;
				case WORK_OPEN_WT_STREAM: {
					// Open the session's persistent reliable stream once, then append
					// framed bytes to it (no FIN). A fresh stream per message exhausts
					// stream credit and starves the connect handshake.
					if (target->reliable_stream_id < 0) {
						if (!target->control_stream_ctx) {
							break;
						}
						h3zero_callback_ctx_t *h3 = (h3zero_callback_ctx_t *)picoquic_get_callback_context(target->cnx);
						if (!h3) {
							break;
						}
						h3zero_stream_ctx_t *stream = picowt_create_local_stream(
								target->cnx, /*is_bidir=*/1, h3, target->control_stream_ctx->stream_id);
						if (!stream) {
							break;
						}
						stream->path_callback = _server_path_callback;
						stream->path_callback_ctx = target;
						target->reliable_stream_id = (int64_t)stream->stream_id;
					}
					picoquic_add_to_stream(target->cnx, (uint64_t)target->reliable_stream_id,
							item.bytes.ptr(), item.bytes.size(), 0);
				} break;
				default: {
					picoquic_add_to_stream(target->cnx, item.stream_id,
							item.bytes.ptr(), item.bytes.size(), item.fin ? 1 : 0);
				} break;
			}
		}
	}
	return 0;
}

static Error _wt_server_listen(WebTransportPeer *p_peer, int p_port, const String &p_path,
		const uint8_t *cert_der, size_t cert_der_len, const char *key_pem) {
	if (_wt_server) {
		ERR_PRINT("WebTransportPeer: server already listening");
		return ERR_ALREADY_IN_USE;
	}
	_wt_server = new WTServerCtx();
	_wt_server->peer = p_peer;

	_wt_server->path_cs = p_path.utf8();
	_wt_server->path_table[0].path = _wt_server->path_cs.get_data();
	_wt_server->path_table[0].path_length = strlen(_wt_server->path_table[0].path);
	_wt_server->path_table[0].path_callback = _server_path_callback;
	_wt_server->path_table[0].path_app_ctx = nullptr;

	_wt_server->server_param.path_table = _wt_server->path_table;
	_wt_server->server_param.path_table_nb = 1;

	_wt_server->quic = picoquic_create(8, nullptr, nullptr, nullptr,
			"h3", h3zero_callback, &_wt_server->server_param,
			nullptr, nullptr, nullptr, picoquic_current_time(), nullptr, nullptr, nullptr, 0);
	if (!_wt_server->quic) {
		ERR_PRINT("WebTransportPeer: picoquic_create failed");
		delete _wt_server;
		_wt_server = nullptr;
		return ERR_CANT_CREATE;
	}
	picoquic_enforce_client_only(_wt_server->quic, 0);

	// Install cert chain. picoquic's free_certificates_list calls free() on
	// certs[i].base and free() on the array itself, so both must be malloc'd.
	{
		ptls_iovec_t *chain = (ptls_iovec_t *)malloc(sizeof(ptls_iovec_t));
		uint8_t *der_copy = (uint8_t *)malloc(cert_der_len);
		if (!chain || !der_copy) {
			free(chain);
			free(der_copy);
			ERR_PRINT("WebTransportPeer: malloc failed for cert chain");
			picoquic_free(_wt_server->quic);
			delete _wt_server;
			_wt_server = nullptr;
			return ERR_OUT_OF_MEMORY;
		}
		memcpy(der_copy, cert_der, cert_der_len);
		chain[0].base = der_copy;
		chain[0].len = cert_der_len;
		picoquic_set_tls_certificate_chain(_wt_server->quic, chain, 1);
	}

	// Install key (PEM; picoquic's ptls layer parses PEM natively).
	int key_ret = picoquic_set_tls_key(_wt_server->quic,
			(const uint8_t *)key_pem, strlen(key_pem));
	if (key_ret != 0) {
		ERR_PRINT(vformat("WebTransportPeer: picoquic_set_tls_key failed: %d", key_ret));
		picoquic_free(_wt_server->quic);
		delete _wt_server;
		_wt_server = nullptr;
		return ERR_CANT_CREATE;
	}

	picoquic_set_alpn_select_fn_v2(_wt_server->quic, picoquic_demo_server_callback_select_alpn);
	picowt_set_default_transport_parameters(_wt_server->quic);
	picoquic_set_default_tp_value(_wt_server->quic,
			picoquic_tp_max_datagram_frame_size, PICOQUIC_MAX_PACKET_SIZE);

	_wt_server->loop_param.local_af = 0; // dual-stack
	_wt_server->loop_param.local_port = static_cast<uint16_t>(p_port);

	int start_ret = 0;
	_wt_server->thread_ctx = picoquic_start_network_thread(
			_wt_server->quic, &_wt_server->loop_param,
			&_server_loop_cb, _wt_server, &start_ret);
	if (!_wt_server->thread_ctx) {
		ERR_PRINT(vformat("WebTransportPeer: picoquic_start_network_thread failed: %d", start_ret));
		picoquic_free(_wt_server->quic);
		delete _wt_server;
		_wt_server = nullptr;
		return ERR_CANT_CREATE;
	}
	return OK;
}

static void _wt_server_close() {
	if (!_wt_server) {
		return;
	}
	if (_wt_server->thread_ctx) {
		picoquic_delete_network_thread(_wt_server->thread_ctx);
		_wt_server->thread_ctx = nullptr;
	}
	if (_wt_server->quic) {
		picoquic_free(_wt_server->quic);
		_wt_server->quic = nullptr;
	}
	{
		MutexLock lock(_wt_server->sessions_mutex);
		_wt_server->sessions.clear(); // ctxs freed by h3zero teardown
	}
	delete _wt_server;
	_wt_server = nullptr;
}

static Error _wt_server_queue_datagram(WTServerSessionCtx *p_session, const uint8_t *p_bytes, size_t p_len) {
	uint64_t qid = p_session->session_stream_id / 4;
	size_t prefix_len;
	uint8_t prefix[2];
	if (qid < 64) {
		prefix[0] = (uint8_t)qid;
		prefix_len = 1;
	} else if (qid < 16384) {
		prefix[0] = (uint8_t)((qid >> 8) | 0x40);
		prefix[1] = (uint8_t)(qid & 0xFF);
		prefix_len = 2;
	} else {
		return ERR_INVALID_PARAMETER;
	}
	if (prefix_len + p_len > PICOQUIC_MAX_PACKET_SIZE) {
		return ERR_OUT_OF_MEMORY;
	}
	WorkItem dg;
	dg.kind = WORK_SEND_DATAGRAM;
	dg.session = p_session;
	dg.bytes.resize(prefix_len + p_len);
	memcpy(dg.bytes.ptr(), prefix, prefix_len);
	if (p_len > 0) {
		memcpy(dg.bytes.ptr() + prefix_len, p_bytes, p_len);
	}
	_wt_server->wq.push(std::move(dg));
	return OK;
}

static Error _wt_server_send_datagram(int p_target, const uint8_t *p_bytes, size_t p_len) {
	if (!_wt_server) {
		return ERR_UNCONFIGURED;
	}
	Error err = ERR_UNCONFIGURED;
	{
		MutexLock lock(_wt_server->sessions_mutex);
		for (WTServerSessionCtx *session : _wt_server->sessions) {
			if (p_target > 0 && session->peer_id != p_target) {
				continue;
			}
			err = _wt_server_queue_datagram(session, p_bytes, p_len);
			if (err != OK) {
				return err;
			}
		}
	}
	if (err == OK && _wt_server->thread_ctx) {
		picoquic_wake_up_network_thread(_wt_server->thread_ctx);
	}
	return err;
}

// Drop one client without closing the server.
//
// `disconnect_peer` used to call `close()`, which stops the listener: one client leaving, or
// being kicked, took every other client with it and the server too.
static Error _wt_server_disconnect_peer(int p_target) {
	if (!_wt_server) {
		return ERR_UNCONFIGURED;
	}
	if (p_target <= 0) {
		return ERR_INVALID_PARAMETER; // there is no "disconnect everybody"; that is close()
	}
	bool found = false;
	{
		MutexLock lock(_wt_server->sessions_mutex);
		for (WTServerSessionCtx *session : _wt_server->sessions) {
			if (session->peer_id != p_target) {
				continue;
			}
			// Queued rather than closed here: this is the main thread and picoquic owns the
			// connection from its own. The network thread drains the queue and does the close.
			WorkItem w;
			w.kind = WORK_CLOSE_SESSION;
			w.session = session;
			_wt_server->wq.push(std::move(w));
			found = true;
			break;
		}
	}
	if (!found) {
		return ERR_DOES_NOT_EXIST;
	}
	if (_wt_server->thread_ctx) {
		picoquic_wake_up_network_thread(_wt_server->thread_ctx);
	}
	return OK;
}

static Error _wt_server_send_stream(int p_target, const uint8_t *p_bytes, size_t p_len) {
	if (!_wt_server) {
		return ERR_UNCONFIGURED;
	}
	Error err = ERR_UNCONFIGURED;
	{
		MutexLock lock(_wt_server->sessions_mutex);
		for (WTServerSessionCtx *session : _wt_server->sessions) {
			if (p_target > 0 && session->peer_id != p_target) {
				continue;
			}
			WorkItem w;
			w.kind = WORK_OPEN_WT_STREAM; // append framed bytes to the persistent stream
			w.session = session;
			_frame_reliable(p_bytes, p_len, w.bytes);
			w.fin = false; // never FIN: the stream persists for the session lifetime
			_wt_server->wq.push(std::move(w));
			err = OK;
		}
	}
	if (err == OK && _wt_server->thread_ctx) {
		picoquic_wake_up_network_thread(_wt_server->thread_ctx);
	}
	return err;
}

} // namespace

void register_quic_picoquic_backend() {
	// Initialize TLS ONCE at module registration — before any picoquic_create.
	// Calling picoquic_tls_api_reset later (from per-connection code) would
	// unload+reload mbedtls, invalidating cipher suite pointers held by
	// earlier-created picoquic contexts. This is the root cause of
	// PTLS_ERROR_INCOMPATIBLE_KEY (0x204) in the loopback test.
	if (ptls_mbedtls_init() == 0) {
		picoquic_tls_api_reset(
				TLS_API_INIT_FLAGS_NO_MINICRYPTO |
				TLS_API_INIT_FLAGS_NO_OPENSSL |
				TLS_API_INIT_FLAGS_NO_FUSION);
	}
	QUICClient::create_backend_func = &_create_backend;
	QUICClient::destroy_backend_func = &_destroy_backend;
	QUICClient::backend_cnx_count_func = &_backend_cnx_count;
	QUICClient::backend_cnx_state_func = &_backend_cnx_state;
	QUICClient::dispatch_cnx_event_func = &_dispatch_cnx_event;
	QUICClient::backend_send_h3_get_func = &_backend_send_h3_get;
	WebTransportPeer::create_session_backend_func = &_create_wt_session_backend;
	WebTransportPeer::destroy_session_backend_func = &_destroy_wt_session_backend;
	WebTransportPeer::session_state_func = &_wt_session_state;
	WebTransportPeer::set_session_state_func = &_set_wt_session_state;
	WebTransportPeer::send_wt_datagram_func = &_send_wt_datagram;
	WebTransportPeer::send_wt_stream_func = &_send_wt_stream;
	WebTransportPeer::start_echo_server_func = &_start_echo_server;
	WebTransportPeer::stop_echo_server_func = &_stop_echo_server;
	WebTransportPeer::server_listen_func = &_wt_server_listen;
	WebTransportPeer::server_close_func = &_wt_server_close;
	WebTransportPeer::server_send_datagram_func = &_wt_server_send_datagram;
	WebTransportPeer::server_send_stream_func = &_wt_server_send_stream;
	WebTransportPeer::server_disconnect_peer_func = &_wt_server_disconnect_peer;
}

void unregister_quic_picoquic_backend() {
	QUICClient::create_backend_func = nullptr;
	QUICClient::destroy_backend_func = nullptr;
	QUICClient::backend_cnx_count_func = nullptr;
	QUICClient::backend_cnx_state_func = nullptr;
	QUICClient::dispatch_cnx_event_func = nullptr;
	QUICClient::backend_send_h3_get_func = nullptr;
	WebTransportPeer::create_session_backend_func = nullptr;
	WebTransportPeer::destroy_session_backend_func = nullptr;
	WebTransportPeer::session_state_func = nullptr;
	WebTransportPeer::set_session_state_func = nullptr;
	WebTransportPeer::send_wt_datagram_func = nullptr;
	WebTransportPeer::send_wt_stream_func = nullptr;
	WebTransportPeer::start_echo_server_func = nullptr;
	WebTransportPeer::stop_echo_server_func = nullptr;
	WebTransportPeer::server_listen_func = nullptr;
	WebTransportPeer::server_close_func = nullptr;
	WebTransportPeer::server_send_datagram_func = nullptr;
	WebTransportPeer::server_send_stream_func = nullptr;
	WebTransportPeer::server_disconnect_peer_func = nullptr;
}

#else // !GODOT_QUIC_NATIVE_BACKEND

void register_quic_picoquic_backend() {}
void unregister_quic_picoquic_backend() {}

#endif

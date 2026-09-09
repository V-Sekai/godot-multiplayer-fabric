/**************************************************************************/
/*  quic_web_backend.cpp                                                  */
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

#ifdef GODOT_QUIC_WEB_BACKEND

#include "web_transport_peer.h"

// Browser WebTransport JS glue (quic_web_glue.js).
extern "C" {
extern int godot_wt_connect(const char *url, int url_len);
extern int godot_wt_is_connected(int id);
extern int godot_wt_is_closed(int id);
extern void godot_wt_send_datagram(int id, const uint8_t *data, int len);
extern int godot_wt_recv_datagram(int id, uint8_t *buf, int buf_max);
extern void godot_wt_close(int id);
}

namespace {

struct WebSessionCtx {
	int js_id = 0;
	WebTransportPeer *peer = nullptr;
	volatile int session_state = WebTransportPeer::SESSION_DISCONNECTED;
};

void *_create_web_session(WebTransportPeer *p_peer, const char *p_host, int p_port, const char *p_path) {
	// Build the WebTransport URL: https://host:port/path
	String url = vformat("https://%s:%d%s", String(p_host), p_port, String(p_path));
	CharString url_utf8 = url.utf8();
	int js_id = godot_wt_connect(url_utf8.get_data(), url_utf8.length());
	if (js_id <= 0) {
		return nullptr;
	}
	WebSessionCtx *ctx = new WebSessionCtx();
	ctx->js_id = js_id;
	ctx->peer = p_peer;
	ctx->session_state = WebTransportPeer::SESSION_QUIC_HANDSHAKING;
	return ctx;
}

void _destroy_web_session(void *p_ctx) {
	if (!p_ctx) {
		return;
	}
	WebSessionCtx *ctx = static_cast<WebSessionCtx *>(p_ctx);
	if (ctx->js_id > 0) {
		godot_wt_close(ctx->js_id);
	}
	delete ctx;
}

int _web_session_state(void *p_ctx) {
	if (!p_ctx) {
		return WebTransportPeer::SESSION_DISCONNECTED;
	}
	WebSessionCtx *ctx = static_cast<WebSessionCtx *>(p_ctx);
	// Poll the JS side for state transitions.
	if (ctx->session_state == WebTransportPeer::SESSION_QUIC_HANDSHAKING ||
			ctx->session_state == WebTransportPeer::SESSION_WT_CONNECTING) {
		if (godot_wt_is_connected(ctx->js_id)) {
			ctx->session_state = WebTransportPeer::SESSION_OPEN;
		} else if (godot_wt_is_closed(ctx->js_id)) {
			ctx->session_state = WebTransportPeer::SESSION_CLOSED;
		}
	}
	return ctx->session_state;
}

void _set_web_session_state(void *p_ctx, int p_state) {
	if (p_ctx) {
		static_cast<WebSessionCtx *>(p_ctx)->session_state = p_state;
	}
}

Error _send_web_datagram(void *p_ctx, const uint8_t *p_bytes, size_t p_len) {
	if (!p_ctx) {
		return ERR_UNCONFIGURED;
	}
	WebSessionCtx *ctx = static_cast<WebSessionCtx *>(p_ctx);
	if (ctx->session_state != WebTransportPeer::SESSION_OPEN) {
		return ERR_UNCONFIGURED;
	}
	godot_wt_send_datagram(ctx->js_id, p_bytes, (int)p_len);
	return OK;
}

Error _send_web_stream(void *p_ctx, const uint8_t *p_bytes, size_t p_len) {
	// TODO: open a bidi stream via JS and send data.
	// Browser WebTransport streams need async JS — deferred to a follow-up.
	(void)p_ctx;
	(void)p_bytes;
	(void)p_len;
	return ERR_UNAVAILABLE;
}

} // namespace

void register_quic_picoquic_backend() {
	// Web backend: browser-native WebTransport via JS glue.
	// No picoquic, no TLS init — the browser handles everything.
	WebTransportPeer::create_session_backend_func = &_create_web_session;
	WebTransportPeer::destroy_session_backend_func = &_destroy_web_session;
	WebTransportPeer::session_state_func = &_web_session_state;
	WebTransportPeer::set_session_state_func = &_set_web_session_state;
	WebTransportPeer::send_wt_datagram_func = &_send_web_datagram;
	WebTransportPeer::send_wt_stream_func = &_send_web_stream;
	// QUICClient/HTTP3Client backend pointers stay null on web — those
	// classes aren't usable from the browser (no raw QUIC).
}

void unregister_quic_picoquic_backend() {
	WebTransportPeer::create_session_backend_func = nullptr;
	WebTransportPeer::destroy_session_backend_func = nullptr;
	WebTransportPeer::session_state_func = nullptr;
	WebTransportPeer::set_session_state_func = nullptr;
	WebTransportPeer::send_wt_datagram_func = nullptr;
	WebTransportPeer::send_wt_stream_func = nullptr;
}

#endif // GODOT_QUIC_WEB_BACKEND

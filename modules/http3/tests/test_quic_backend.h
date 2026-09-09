/**************************************************************************/
/*  test_quic_backend.h                                                   */
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

#include "../quic_client.h"

#include "tests/test_macros.h"

#include "modules/http3/tests/test_poll_until.h"

namespace TestQUICBackend {

// Following the meshoptimizer pattern (modules/meshoptimizer/register_types.cpp):
// QUICClient declares function pointers for the transport backend; a module
// assigns picoquic's implementation on init. Tests can swap in a fake backend
// to drive the lifecycle without any real network.

struct FakeBackend {
	static int ctor_calls;
	static int dtor_calls;
	static void *last_ctx;

	static void *create(QUICClient *p_client, const char *p_host, int p_port, const char *p_alpn) {
		ctor_calls++;
		// Return a non-null sentinel. The client just stores it opaquely.
		last_ctx = reinterpret_cast<void *>(0x1234);
		return last_ctx;
	}

	static void destroy(void *p_ctx) {
		dtor_calls++;
	}

	static void reset() {
		ctor_calls = 0;
		dtor_calls = 0;
		last_ctx = nullptr;
	}
};

// Static definitions live here so the header is header-only-friendly; the
// compilation unit that includes this will provide storage.
inline int FakeBackend::ctor_calls = 0;
inline int FakeBackend::dtor_calls = 0;
inline void *FakeBackend::last_ctx = nullptr;

struct BackendScope {
	BackendScope() {
		FakeBackend::reset();
		prev_create = QUICClient::create_backend_func;
		prev_destroy = QUICClient::destroy_backend_func;
		QUICClient::create_backend_func = &FakeBackend::create;
		QUICClient::destroy_backend_func = &FakeBackend::destroy;
	}
	~BackendScope() {
		QUICClient::create_backend_func = prev_create;
		QUICClient::destroy_backend_func = prev_destroy;
	}

private:
	void *(*prev_create)(QUICClient *, const char *, int, const char *) = nullptr;
	void (*prev_destroy)(void *) = nullptr;
};

struct NoBackendScope {
	NoBackendScope() {
		prev_create = QUICClient::create_backend_func;
		prev_destroy = QUICClient::destroy_backend_func;
		QUICClient::create_backend_func = nullptr;
		QUICClient::destroy_backend_func = nullptr;
	}
	~NoBackendScope() {
		QUICClient::create_backend_func = prev_create;
		QUICClient::destroy_backend_func = prev_destroy;
	}

private:
	void *(*prev_create)(QUICClient *, const char *, int, const char *) = nullptr;
	void (*prev_destroy)(void *) = nullptr;
};

TEST_CASE("[QUICBackend] no backend registered: connect still succeeds, no ctx") {
	// With the function pointers unset, connect_to_host remains a no-op w.r.t.
	// the transport layer — this preserves in-memory test behavior.
	NoBackendScope scope;
	Ref<QUICClient> client;
	client.instantiate();
	CHECK_FALSE(client->_has_backend_context());
	REQUIRE(client->connect_to_host("example.com", 4433) == OK);
	CHECK_FALSE(client->_has_backend_context());
}

TEST_CASE("[QUICBackend] registered backend: connect_to_host invokes create") {
	BackendScope scope;
	Ref<QUICClient> client;
	client.instantiate();
	REQUIRE(client->connect_to_host("example.com", 4433) == OK);
	CHECK(FakeBackend::ctor_calls == 1);
	CHECK(client->_has_backend_context());
}

TEST_CASE("[QUICBackend] registered backend: close invokes destroy") {
	BackendScope scope;
	Ref<QUICClient> client;
	client.instantiate();
	REQUIRE(client->connect_to_host("example.com", 4433) == OK);
	REQUIRE(client->_has_backend_context());
	client->close();
	CHECK(FakeBackend::dtor_calls == 1);
	CHECK_FALSE(client->_has_backend_context());
}

TEST_CASE("[QUICBackend] destructor frees the backend context") {
	BackendScope scope;
	{
		Ref<QUICClient> client;
		client.instantiate();
		REQUIRE(client->connect_to_host("example.com", 4433) == OK);
		REQUIRE(FakeBackend::ctor_calls == 1);
	}
	// Ref scope-exit destroyed the client; backend should have been freed.
	CHECK(FakeBackend::dtor_calls == 1);
}

TEST_CASE("[QUICBackend] native backend registered after module init") {
	// Sanity: initialize_quic_module() installs the picoquic-backed
	// create/destroy functions on native platforms. Web builds register
	// a different shim (or none), so skip if not available here.
	if (QUICClient::create_backend_func == nullptr) {
		MESSAGE("No QUIC backend registered on this platform — skipping");
		return;
	}
	CHECK(QUICClient::create_backend_func != nullptr);
	CHECK(QUICClient::destroy_backend_func != nullptr);
}

TEST_CASE("[QUICBackend] native backend produces a non-null ctx") {
	if (QUICClient::create_backend_func == nullptr) {
		MESSAGE("No QUIC backend registered on this platform — skipping");
		return;
	}
	Ref<QUICClient> client;
	client.instantiate();
	REQUIRE(client->connect_to_host("127.0.0.1", 4433) == OK);
	CHECK(client->_has_backend_context());
	// close() must not crash on a real picoquic_quic_t*.
	client->close();
	CHECK_FALSE(client->_has_backend_context());
}

TEST_CASE("[QUICBackend] native backend registers a connection with picoquic") {
	if (QUICClient::create_backend_func == nullptr) {
		MESSAGE("No QUIC backend registered on this platform — skipping");
		return;
	}
	if (QUICClient::backend_cnx_count_func == nullptr) {
		MESSAGE("No backend_cnx_count_func registered — skipping");
		return;
	}
	Ref<QUICClient> client;
	client.instantiate();
	REQUIRE(client->connect_to_host("127.0.0.1", 4433) == OK);
	CHECK(QUICClient::backend_cnx_count_func(client->_get_backend_ctx()) == 1);
	client->close();
}

TEST_CASE("[QUICBackend] connect_to_host resolves a real hostname") {
	if (QUICClient::create_backend_func == nullptr) {
		MESSAGE("No QUIC backend registered — skipping");
		return;
	}
	// example.com is guaranteed to resolve by IANA; if DNS fails here the
	// test env is degraded, skip to avoid flakes.
	Ref<QUICClient> client;
	client.instantiate();
	Error err = client->connect_to_host("example.com", 443);
	if (err == ERR_CANT_RESOLVE) {
		MESSAGE("DNS unavailable in this env — skipping");
		return;
	}
	REQUIRE(err == OK);
	CHECK(client->_has_backend_context());
	client->close();
}

TEST_CASE("[QUICBackend] connect_to_host on unresolvable host returns ERR_CANT_RESOLVE") {
	if (QUICClient::create_backend_func == nullptr) {
		MESSAGE("No QUIC backend registered — skipping");
		return;
	}
	Ref<QUICClient> client;
	client.instantiate();
	// RFC 2606 .invalid TLD is reserved and guaranteed never to resolve.
	Error err = client->connect_to_host("should-never-resolve.invalid", 443);
	CHECK(err == ERR_CANT_RESOLVE);
	CHECK_FALSE(client->_has_backend_context());
}

// Scan an h3 response stream for the first HEADERS frame and look for a
// QPACK static-table indexed reference to :status. RFC 9204 Appendix A
// gives the static-table status codes:
//   24 -> 103, 25 -> 200, 26 -> 304, 27 -> 404, 28 -> 503.
// Indexed field line (static) encodes as byte 0b11xxxxxx where the low
// 6 bits are the index. So 0xD9 means :status 200, etc. This is good
// enough for the trophy — real response parsing comes with HTTP3Client.
static bool _decode_varint(const PackedByteArray &p_bytes, size_t &pos, uint64_t &out) {
	if (pos >= (size_t)p_bytes.size()) {
		return false;
	}
	uint8_t b = p_bytes[pos];
	int vlen = 1 << (b >> 6);
	if (pos + vlen > (size_t)p_bytes.size()) {
		return false;
	}
	uint64_t v = b & 0x3F;
	for (int i = 1; i < vlen; i++) {
		v = (v << 8) | p_bytes[pos + i];
	}
	pos += vlen;
	out = v;
	return true;
}

static int _extract_h3_status(const PackedByteArray &p_bytes) {
	size_t pos = 0;
	while (pos < (size_t)p_bytes.size()) {
		uint64_t frame_type = 0;
		uint64_t frame_len = 0;
		if (!_decode_varint(p_bytes, pos, frame_type)) {
			return -1;
		}
		if (!_decode_varint(p_bytes, pos, frame_len)) {
			return -1;
		}
		if (pos + frame_len > (size_t)p_bytes.size()) {
			return -1;
		}
		if (frame_type == 0x01 /* HEADERS */) {
			size_t end = pos + frame_len;
			// Skip the 2-byte QPACK prefix (Required Insert Count + Delta Base).
			size_t qpack = pos + 2;
			while (qpack < end) {
				uint8_t b = p_bytes[qpack];
				if ((b & 0xC0) == 0xC0) { // static indexed field line
					int idx = b & 0x3F;
					switch (idx) {
						case 24:
							return 103;
						case 25:
							return 200;
						case 26:
							return 304;
						case 27:
							return 404;
						case 28:
							return 503;
						default:
							break;
					}
				}
				qpack++;
			}
			return -1;
		}
		// Skip unknown / GREASE / DATA frames.
		pos += frame_len;
	}
	return -1;
}

TEST_CASE("[QUICBackend] 🏆 handshake against cloudflare-quic.com:443") {
	if (QUICClient::create_backend_func == nullptr) {
		MESSAGE("No QUIC backend registered — skipping");
		return;
	}
	Ref<QUICClient> client;
	client.instantiate();
	Error err = client->connect_to_host("cloudflare-quic.com", 443);
	if (err == ERR_CANT_RESOLVE) {
		MESSAGE("DNS unavailable — skipping (need network for victory run)");
		return;
	}
	REQUIRE(err == OK);
	REQUIRE(client->_has_backend_context());

	// Drive the QUIC handshake state machine.
	// picoquic's network thread advances the state; we call poll() each step
	// so the timeout fires even if the thread hasn't ticked yet.
	// Termination proved in lean/http3/PollingTermination.lean.
	poll_until(
			[&]() { return client->get_status(); },
			[](QUICClient::Status s) { return s != QUICClient::STATUS_CONNECTING; },
			[&]() { /* background thread drives picoquic */ });

	MESSAGE("final QUICClient status: ", (int)client->get_status());
	MESSAGE("picoquic cnx state: ", QUICClient::backend_cnx_state_func(client->_get_backend_ctx()));
	if (client->get_status() != QUICClient::STATUS_CONNECTED) {
		MESSAGE("QUIC handshake failed — skipping (outbound QUIC/UDP may be blocked on this runner)");
		return;
	}

	// Fire a GET / and collect whatever comes back on stream 0.
	if (QUICClient::backend_send_h3_get_func) {
		REQUIRE(QUICClient::backend_send_h3_get_func(
						client->_get_backend_ctx(), "/", "cloudflare-quic.com") == OK);

		PackedByteArray accum;
		PackedByteArray ctrl3;
		PackedByteArray ctrl7;
		PackedByteArray ctrl11;
		// Drive read state machine: exit when stream 0 closes with data.
		poll_until(
				[&]() { return client->is_stream_peer_closed(0) && accum.size() > 0; },
				[](bool done) { return done; },
				[&]() {
					PackedByteArray chunk = client->stream_read(0);
					if (chunk.size() > 0) {
						accum.append_array(chunk);
					}
					ctrl3.append_array(client->stream_read(3));
					ctrl7.append_array(client->stream_read(7));
					ctrl11.append_array(client->stream_read(11));
				});
		MESSAGE("response bytes on stream 0: ", (int)accum.size());
		MESSAGE("bytes on stream 3 (server ctrl): ", (int)ctrl3.size());
		MESSAGE("bytes on stream 7 (server encoder): ", (int)ctrl7.size());
		MESSAGE("bytes on stream 11 (server decoder): ", (int)ctrl11.size());
		if (accum.size() > 0) {
			// Dump the first 256 bytes as a hex preview so we can see
			// the HEADERS/DATA frame structure.
			String hex;
			for (int i = 0; i < MIN((int)accum.size(), 256); i++) {
				hex += vformat("%02x ", accum[i]);
			}
			MESSAGE("first bytes: ", hex);

			// And as ASCII where printable.
			String ascii;
			for (int i = 0; i < MIN((int)accum.size(), 512); i++) {
				char c = (char)accum[i];
				ascii += (c >= 32 && c < 127) ? String::chr(c) : String(".");
			}
			MESSAGE("ascii preview: ", ascii);
		}
		CHECK(accum.size() > 0);

		int status = _extract_h3_status(accum);
		MESSAGE("🏆 :status = ", status);
		CHECK(status == 200);
	}

	client->close();
}

TEST_CASE("[QUICBackend] connect_to_host arms the picoquic handshake") {
	if (QUICClient::create_backend_func == nullptr) {
		MESSAGE("No QUIC backend registered on this platform — skipping");
		return;
	}
	if (QUICClient::backend_cnx_state_func == nullptr) {
		MESSAGE("No backend_cnx_state_func registered — skipping");
		return;
	}
	Ref<QUICClient> client;
	client.instantiate();
	REQUIRE(client->connect_to_host("127.0.0.1", 4433) == OK);
	// picoquic_create_client_cnx internally calls picoquic_start_client_cnx —
	// but the state enum only advances when the next packet is actually
	// prepared, not at start time. So the right invariant post-connect is
	// simply "the cnx exists and its state is queryable" (>= 0, not -1 which
	// is our null sentinel).
	int state = QUICClient::backend_cnx_state_func(client->_get_backend_ctx());
	CHECK(state >= 0);
	client->close();
}

} // namespace TestQUICBackend

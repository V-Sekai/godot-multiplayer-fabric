/**************************************************************************/
/*  test_quic_client.h                                                    */
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

#include "core/crypto/crypto.h"
#include "tests/test_macros.h"

namespace TestQUICClient {

TEST_CASE("[QUICClient] Fresh instance is DISCONNECTED") {
	Ref<QUICClient> client;
	client.instantiate();
	REQUIRE(client.is_valid());
	CHECK(client->get_status() == QUICClient::STATUS_DISCONNECTED);
}

TEST_CASE("[QUICClient] Status enum mirrors HTTPClient shape") {
	// Muscle-memory parity with HTTPClient — users who know HTTPClient
	// should recognize these states. See core/io/http_client.h.
	CHECK(QUICClient::STATUS_DISCONNECTED == 0);
	CHECK(QUICClient::STATUS_RESOLVING > QUICClient::STATUS_DISCONNECTED);
	CHECK(QUICClient::STATUS_CONNECTING > QUICClient::STATUS_RESOLVING);
	CHECK(QUICClient::STATUS_CONNECTED > QUICClient::STATUS_CONNECTING);
}

TEST_CASE("[QUICClient] close() on fresh instance is a no-op") {
	Ref<QUICClient> client;
	client.instantiate();
	client->close();
	CHECK(client->get_status() == QUICClient::STATUS_DISCONNECTED);
}

TEST_CASE("[QUICClient] send_datagram before connect is ERR_UNCONFIGURED") {
	Ref<QUICClient> client;
	client.instantiate();
	PackedByteArray payload;
	payload.push_back(0x42);
	CHECK(client->send_datagram(payload) == ERR_UNCONFIGURED);
}

TEST_CASE("[QUICClient] connect_to_host rejects empty host") {
	Ref<QUICClient> client;
	client.instantiate();
	CHECK(client->connect_to_host(String(), 4433) == ERR_INVALID_PARAMETER);
	CHECK(client->get_status() == QUICClient::STATUS_DISCONNECTED);
}

TEST_CASE("[QUICClient] connect_to_host rejects invalid port") {
	Ref<QUICClient> client;
	client.instantiate();
	CHECK(client->connect_to_host("example.com", 0) == ERR_INVALID_PARAMETER);
	CHECK(client->connect_to_host("example.com", -1) == ERR_INVALID_PARAMETER);
	CHECK(client->connect_to_host("example.com", 65536) == ERR_INVALID_PARAMETER);
	CHECK(client->get_status() == QUICClient::STATUS_DISCONNECTED);
}

TEST_CASE("[QUICClient] open_stream before connect returns invalid id") {
	Ref<QUICClient> client;
	client.instantiate();
	CHECK(client->open_stream(true) == QUICClient::INVALID_STREAM_ID);
	CHECK(client->open_stream(false) == QUICClient::INVALID_STREAM_ID);
}

TEST_CASE("[QUICClient] stream_send on unknown stream is ERR_DOES_NOT_EXIST") {
	Ref<QUICClient> client;
	client.instantiate();
	PackedByteArray payload;
	payload.push_back(0xAB);
	CHECK(client->stream_send(12345, payload) == ERR_DOES_NOT_EXIST);
}

TEST_CASE("[QUICClient] stream_read on unknown stream returns empty") {
	Ref<QUICClient> client;
	client.instantiate();
	PackedByteArray out = client->stream_read(999);
	CHECK(out.is_empty());
}

TEST_CASE("[QUICClient] receive_datagram on disconnected returns empty") {
	Ref<QUICClient> client;
	client.instantiate();
	PackedByteArray out = client->receive_datagram();
	CHECK(out.is_empty());
}

TEST_CASE("[QUICClient] receive_datagram drains injected payloads in FIFO order") {
	Ref<QUICClient> client;
	client.instantiate();

	PackedByteArray a;
	a.push_back(0xAA);
	PackedByteArray b;
	b.push_back(0xBB);
	b.push_back(0xCC);

	// _push_incoming_datagram is the entry point the picoquic callback will
	// feed; tests drive it directly to assert the queue contract.
	client->_push_incoming_datagram(a);
	client->_push_incoming_datagram(b);

	CHECK(client->receive_datagram() == a);
	CHECK(client->receive_datagram() == b);
	CHECK(client->receive_datagram().is_empty());
}

TEST_CASE("[QUICClient] _push_incoming_stream_data implicitly opens the stream") {
	Ref<QUICClient> client;
	client.instantiate();
	PackedByteArray a;
	a.push_back(0xDE);
	a.push_back(0xAD);

	// The peer opening a stream surfaces as bytes arriving for a new id.
	client->_push_incoming_stream_data(7, a);

	PackedByteArray out = client->stream_read(7);
	CHECK(out == a);
	// After draining the stream buffer is empty.
	CHECK(client->stream_read(7).is_empty());
}

TEST_CASE("[QUICClient] incoming stream data concatenates (byte stream semantics)") {
	Ref<QUICClient> client;
	client.instantiate();
	PackedByteArray first;
	first.push_back(0x01);
	first.push_back(0x02);
	PackedByteArray second;
	second.push_back(0x03);

	client->_push_incoming_stream_data(42, first);
	client->_push_incoming_stream_data(42, second);

	PackedByteArray combined;
	combined.push_back(0x01);
	combined.push_back(0x02);
	combined.push_back(0x03);
	CHECK(client->stream_read(42) == combined);
}

TEST_CASE("[QUICClient] stream_send on open stream buffers for drain") {
	Ref<QUICClient> client;
	client.instantiate();

	// Peer-initiated open: push zero bytes of rx to create the stream.
	client->_push_incoming_stream_data(11, PackedByteArray());

	PackedByteArray payload;
	payload.push_back(0xF0);
	payload.push_back(0x0D);
	CHECK(client->stream_send(11, payload) == OK);

	CHECK(client->_pop_outgoing_stream_data(11) == payload);
	CHECK(client->_pop_outgoing_stream_data(11).is_empty());
}

TEST_CASE("[QUICClient] stream_send concatenates across calls (byte stream)") {
	Ref<QUICClient> client;
	client.instantiate();
	client->_push_incoming_stream_data(3, PackedByteArray());

	PackedByteArray a;
	a.push_back(0x10);
	PackedByteArray b;
	b.push_back(0x20);
	b.push_back(0x30);
	CHECK(client->stream_send(3, a) == OK);
	CHECK(client->stream_send(3, b) == OK);

	PackedByteArray combined;
	combined.push_back(0x10);
	combined.push_back(0x20);
	combined.push_back(0x30);
	CHECK(client->_pop_outgoing_stream_data(3) == combined);
}

TEST_CASE("[QUICClient] _pop_outgoing_stream_data on unknown stream is empty") {
	Ref<QUICClient> client;
	client.instantiate();
	CHECK(client->_pop_outgoing_stream_data(999).is_empty());
}

TEST_CASE("[QUICClient] _set_status transitions and unlocks send_datagram") {
	Ref<QUICClient> client;
	client.instantiate();
	client->_set_status(QUICClient::STATUS_CONNECTED);
	CHECK(client->get_status() == QUICClient::STATUS_CONNECTED);
	PackedByteArray payload;
	payload.push_back(0x77);
	CHECK(client->send_datagram(payload) == OK);
}

TEST_CASE("[QUICClient] open_stream allocates distinct ids when connected") {
	Ref<QUICClient> client;
	client.instantiate();
	client->_set_status(QUICClient::STATUS_CONNECTED);
	int64_t a = client->open_stream(true);
	int64_t b = client->open_stream(true);
	CHECK(a != QUICClient::INVALID_STREAM_ID);
	CHECK(b != QUICClient::INVALID_STREAM_ID);
	CHECK(a != b);
}

TEST_CASE("[QUICClient] open_stream id is usable with stream_send") {
	Ref<QUICClient> client;
	client.instantiate();
	client->_set_status(QUICClient::STATUS_CONNECTED);
	int64_t id = client->open_stream(true);
	REQUIRE(id != QUICClient::INVALID_STREAM_ID);
	PackedByteArray payload;
	payload.push_back(0xAB);
	CHECK(client->stream_send(id, payload) == OK);
	CHECK(client->_pop_outgoing_stream_data(id) == payload);
}

TEST_CASE("[QUICClient] send_datagram queues for drain in FIFO order") {
	Ref<QUICClient> client;
	client.instantiate();
	client->_set_status(QUICClient::STATUS_CONNECTED);

	PackedByteArray a;
	a.push_back(0x11);
	PackedByteArray b;
	b.push_back(0x22);
	b.push_back(0x33);

	REQUIRE(client->send_datagram(a) == OK);
	REQUIRE(client->send_datagram(b) == OK);

	CHECK(client->_pop_outgoing_datagram() == a);
	CHECK(client->_pop_outgoing_datagram() == b);
	CHECK(client->_pop_outgoing_datagram().is_empty());
}

TEST_CASE("[QUICClient] _pop_outgoing_datagram on fresh client is empty") {
	Ref<QUICClient> client;
	client.instantiate();
	CHECK(client->_pop_outgoing_datagram().is_empty());
}

TEST_CASE("[QUICClient] close() drops pending outgoing datagrams") {
	Ref<QUICClient> client;
	client.instantiate();
	client->_set_status(QUICClient::STATUS_CONNECTED);
	PackedByteArray a;
	a.push_back(0x44);
	REQUIRE(client->send_datagram(a) == OK);
	client->close();
	CHECK(client->_pop_outgoing_datagram().is_empty());
}

TEST_CASE("[QUICClient] stream_close on unknown stream is ERR_DOES_NOT_EXIST") {
	Ref<QUICClient> client;
	client.instantiate();
	CHECK(client->stream_close(4242) == ERR_DOES_NOT_EXIST);
}

TEST_CASE("[QUICClient] stream_close on open stream succeeds and blocks further send") {
	Ref<QUICClient> client;
	client.instantiate();
	client->_set_status(QUICClient::STATUS_CONNECTED);
	int64_t id = client->open_stream(true);
	REQUIRE(id != QUICClient::INVALID_STREAM_ID);

	CHECK(client->stream_close(id) == OK);

	PackedByteArray payload;
	payload.push_back(0x99);
	// Half-close: the send side is FIN'd, but the stream entry remains for rx.
	CHECK(client->stream_send(id, payload) == ERR_UNAVAILABLE);
}

TEST_CASE("[QUICClient] stream_read still works after stream_close") {
	Ref<QUICClient> client;
	client.instantiate();
	PackedByteArray rx;
	rx.push_back(0xA1);
	rx.push_back(0xA2);
	client->_push_incoming_stream_data(9, rx);

	REQUIRE(client->stream_close(9) == OK);
	CHECK(client->stream_read(9) == rx);
}

TEST_CASE("[QUICClient] is_stream_peer_closed default is false") {
	Ref<QUICClient> client;
	client.instantiate();
	// Unknown stream: nothing to report, treat as not closed.
	CHECK_FALSE(client->is_stream_peer_closed(99));

	client->_push_incoming_stream_data(12, PackedByteArray());
	CHECK_FALSE(client->is_stream_peer_closed(12));
}

TEST_CASE("[QUICClient] _push_incoming_stream_fin flips peer-closed flag") {
	Ref<QUICClient> client;
	client.instantiate();
	PackedByteArray data;
	data.push_back(0x55);
	client->_push_incoming_stream_data(7, data);
	CHECK_FALSE(client->is_stream_peer_closed(7));

	client->_push_incoming_stream_fin(7);
	CHECK(client->is_stream_peer_closed(7));
	// Pending rx is still drainable after the FIN.
	CHECK(client->stream_read(7) == data);
}

TEST_CASE("[QUICClient] _push_incoming_stream_fin on unknown stream implicitly opens") {
	Ref<QUICClient> client;
	client.instantiate();
	// Peer can send FIN with no data (zero-byte stream) — must still register.
	client->_push_incoming_stream_fin(15);
	CHECK(client->is_stream_peer_closed(15));
}

TEST_CASE("[QUICClient] stream_close is idempotent") {
	Ref<QUICClient> client;
	client.instantiate();
	client->_push_incoming_stream_data(3, PackedByteArray());
	REQUIRE(client->stream_close(3) == OK);
	CHECK(client->stream_close(3) == OK);
}

TEST_CASE("[QUICClient] close() resets status to DISCONNECTED") {
	Ref<QUICClient> client;
	client.instantiate();
	client->_set_status(QUICClient::STATUS_CONNECTED);
	client->close();
	CHECK(client->get_status() == QUICClient::STATUS_DISCONNECTED);
}

TEST_CASE("[QUICClient] close() drops pending streams") {
	Ref<QUICClient> client;
	client.instantiate();
	PackedByteArray a;
	a.push_back(0x10);
	client->_push_incoming_stream_data(5, a);
	client->close();
	CHECK(client->stream_read(5).is_empty());
}

TEST_CASE("[QUICClient] close() drops pending datagrams") {
	Ref<QUICClient> client;
	client.instantiate();
	PackedByteArray a;
	a.push_back(0x01);
	client->_push_incoming_datagram(a);
	client->close();
	CHECK(client->receive_datagram().is_empty());
}

TEST_CASE("[QUICClient] poll on disconnected is ERR_UNCONFIGURED") {
	Ref<QUICClient> client;
	client.instantiate();
	CHECK(client->poll() == ERR_UNCONFIGURED);
}

TEST_CASE("[QUICClient] connected_host/port are empty before connect") {
	Ref<QUICClient> client;
	client.instantiate();
	CHECK(client->get_connected_host().is_empty());
	CHECK(client->get_connected_port() == 0);
}

TEST_CASE("[QUICClient] connect_to_host records host and port") {
	Ref<QUICClient> client;
	client.instantiate();
	REQUIRE(client->connect_to_host("example.com", 4433) == OK);
	CHECK(client->get_connected_host() == "example.com");
	CHECK(client->get_connected_port() == 4433);
}

TEST_CASE("[QUICClient] successful connect_to_host transitions to RESOLVING") {
	Ref<QUICClient> client;
	client.instantiate();
	// Fresh instance is DISCONNECTED; successful dial kicks off DNS/handshake
	// which lands in RESOLVING first (picoquic drives further via poll()).
	REQUIRE(client->connect_to_host("example.com", 4433) == OK);
	CHECK(client->get_status() == QUICClient::STATUS_RESOLVING);
}

TEST_CASE("[QUICClient] failed connect_to_host keeps status DISCONNECTED") {
	Ref<QUICClient> client;
	client.instantiate();
	// Validation failure must not move the state machine.
	CHECK(client->connect_to_host(String(), 4433) == ERR_INVALID_PARAMETER);
	CHECK(client->get_status() == QUICClient::STATUS_DISCONNECTED);
	CHECK(client->connect_to_host("example.com", 0) == ERR_INVALID_PARAMETER);
	CHECK(client->get_status() == QUICClient::STATUS_DISCONNECTED);
}

TEST_CASE("[QUICClient] poll after connect_to_host no longer returns ERR_UNCONFIGURED") {
	Ref<QUICClient> client;
	client.instantiate();
	REQUIRE(client->connect_to_host("example.com", 4433) == OK);
	CHECK(client->poll() == OK);
}

TEST_CASE("[QUICClient] close() clears host and port") {
	Ref<QUICClient> client;
	client.instantiate();
	REQUIRE(client->connect_to_host("example.com", 4433) == OK);
	client->close();
	CHECK(client->get_connected_host().is_empty());
	CHECK(client->get_connected_port() == 0);
}

TEST_CASE("[QUICClient] blocking mode defaults to false and toggles") {
	Ref<QUICClient> client;
	client.instantiate();
	// Godot networking defaults to non-blocking (see HTTPClient); QUIC follows suit.
	CHECK_FALSE(client->is_blocking_mode_enabled());
	client->set_blocking_mode(true);
	CHECK(client->is_blocking_mode_enabled());
	client->set_blocking_mode(false);
	CHECK_FALSE(client->is_blocking_mode_enabled());
}

TEST_CASE("[QUICClient] tls_options defaults to null and round-trips") {
	Ref<QUICClient> client;
	client.instantiate();
	CHECK(client->get_tls_options().is_null());
	Ref<TLSOptions> opts = TLSOptions::client();
	REQUIRE(opts.is_valid());
	client->set_tls_options(opts);
	CHECK(client->get_tls_options() == opts);
}

TEST_CASE("[QUICClient] close() clears tls_options") {
	Ref<QUICClient> client;
	client.instantiate();
	client->set_tls_options(TLSOptions::client());
	REQUIRE(client->get_tls_options().is_valid());
	client->close();
	CHECK(client->get_tls_options().is_null());
}

TEST_CASE("[QUICClient] alpn defaults to h3 and can be overridden") {
	Ref<QUICClient> client;
	client.instantiate();
	// QUIC requires ALPN negotiation (RFC 9001 §8.1). h3 is the common
	// default; WebTransport/QUIC uses "h3" too then upgrades via CONNECT-webtransport.
	CHECK(client->get_alpn() == "h3");
	client->set_alpn("webtransport");
	CHECK(client->get_alpn() == "webtransport");
	// Empty ALPN is rejected silently — keeping prior value prevents the
	// client from landing in an un-dialable state.
	client->set_alpn(String());
	CHECK(client->get_alpn() == "webtransport");
}

TEST_CASE("[QUICClient] read_chunk_size has sane default and clamps") {
	Ref<QUICClient> client;
	client.instantiate();
	// 64 KiB matches HTTPClient's default and is one-ish QUIC packet payload.
	CHECK(client->get_read_chunk_size() == 65536);
	client->set_read_chunk_size(4096);
	CHECK(client->get_read_chunk_size() == 4096);
	// Non-positive values are rejected silently rather than poisoning state.
	client->set_read_chunk_size(0);
	CHECK(client->get_read_chunk_size() == 4096);
	client->set_read_chunk_size(-1);
	CHECK(client->get_read_chunk_size() == 4096);
}

} // namespace TestQUICClient

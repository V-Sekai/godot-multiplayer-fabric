/**************************************************************************/
/*  test_quic_loopback.h                                                  */
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

namespace TestQUICLoopback {

// These tests wire two QUICClient instances through their engine-internal
// push/pop hooks without a real network. picoquic's outgoing callback feeds
// _pop_outgoing_datagram / _pop_outgoing_stream_data; the peer's incoming
// callback fires _push_incoming_datagram / _push_incoming_stream_data. A
// successful loopback here means our in-memory model upholds the contract
// the real transport will be expected to uphold.

TEST_CASE("[QUICLoopback] datagram a -> b delivers byte-identical payload") {
	Ref<QUICClient> a;
	a.instantiate();
	a->_set_status(QUICClient::STATUS_CONNECTED);
	Ref<QUICClient> b;
	b.instantiate();
	b->_set_status(QUICClient::STATUS_CONNECTED);

	PackedByteArray payload;
	payload.push_back(0xCA);
	payload.push_back(0xFE);
	REQUIRE(a->send_datagram(payload) == OK);

	PackedByteArray on_wire = a->_pop_outgoing_datagram();
	REQUIRE_FALSE(on_wire.is_empty());
	b->_push_incoming_datagram(on_wire);

	CHECK(b->receive_datagram() == payload);
}

TEST_CASE("[QUICLoopback] datagrams arrive FIFO after three hops") {
	Ref<QUICClient> a;
	a.instantiate();
	a->_set_status(QUICClient::STATUS_CONNECTED);
	Ref<QUICClient> b;
	b.instantiate();
	b->_set_status(QUICClient::STATUS_CONNECTED);

	PackedByteArray p1;
	p1.push_back(0x01);
	PackedByteArray p2;
	p2.push_back(0x02);
	PackedByteArray p3;
	p3.push_back(0x03);
	REQUIRE(a->send_datagram(p1) == OK);
	REQUIRE(a->send_datagram(p2) == OK);
	REQUIRE(a->send_datagram(p3) == OK);

	// Drain all three off A and feed B in order.
	for (int i = 0; i < 3; i++) {
		b->_push_incoming_datagram(a->_pop_outgoing_datagram());
	}

	CHECK(b->receive_datagram() == p1);
	CHECK(b->receive_datagram() == p2);
	CHECK(b->receive_datagram() == p3);
}

TEST_CASE("[QUICLoopback] bidirectional stream a -> b delivers byte-identical data") {
	Ref<QUICClient> a;
	a.instantiate();
	a->_set_status(QUICClient::STATUS_CONNECTED);
	Ref<QUICClient> b;
	b.instantiate();
	b->_set_status(QUICClient::STATUS_CONNECTED);

	int64_t stream_id = a->open_stream(true);
	REQUIRE(stream_id != QUICClient::INVALID_STREAM_ID);

	PackedByteArray payload;
	payload.push_back(0xDE);
	payload.push_back(0xAD);
	payload.push_back(0xBE);
	payload.push_back(0xEF);
	REQUIRE(a->stream_send(stream_id, payload) == OK);

	// Drain A's tx, deliver to B preserving the stream id.
	PackedByteArray on_wire = a->_pop_outgoing_stream_data(stream_id);
	REQUIRE(on_wire == payload);
	b->_push_incoming_stream_data(stream_id, on_wire);

	CHECK(b->stream_read(stream_id) == payload);
}

TEST_CASE("[QUICLoopback] stream bytes concatenate across multiple sends") {
	Ref<QUICClient> a;
	a.instantiate();
	a->_set_status(QUICClient::STATUS_CONNECTED);
	Ref<QUICClient> b;
	b.instantiate();
	b->_set_status(QUICClient::STATUS_CONNECTED);

	int64_t id = a->open_stream(true);
	REQUIRE(id != QUICClient::INVALID_STREAM_ID);

	PackedByteArray first;
	first.push_back(0xAA);
	PackedByteArray second;
	second.push_back(0xBB);
	second.push_back(0xCC);

	REQUIRE(a->stream_send(id, first) == OK);
	// Drain + forward before the second send — simulates partial flush.
	b->_push_incoming_stream_data(id, a->_pop_outgoing_stream_data(id));

	REQUIRE(a->stream_send(id, second) == OK);
	b->_push_incoming_stream_data(id, a->_pop_outgoing_stream_data(id));

	PackedByteArray combined;
	combined.push_back(0xAA);
	combined.push_back(0xBB);
	combined.push_back(0xCC);
	CHECK(b->stream_read(id) == combined);
}

} // namespace TestQUICLoopback

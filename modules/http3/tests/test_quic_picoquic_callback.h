/**************************************************************************/
/*  test_quic_picoquic_callback.h                                         */
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

// We own _default_stream_data_cb (the picoquic callback) and call it directly
// here — no need to mock a picoquic_cnx_t*. The callback only dereferences
// callback_ctx and the byte args.

namespace TestQUICPicoquicCallback {

TEST_CASE("[QUICPicoquicCallback] stream_data event pushes rx bytes") {
	if (QUICClient::dispatch_cnx_event_func == nullptr) {
		MESSAGE("Dispatch bridge not registered — skipping");
		return;
	}
	Ref<QUICClient> client;
	client.instantiate();

	uint8_t payload[3] = { 0xDE, 0xAD, 0xBE };
	QUICClient::dispatch_cnx_event_func(client.ptr(), /*stream_id=*/7,
			payload, sizeof(payload), QUICClient::CALLBACK_STREAM_DATA);

	PackedByteArray out = client->stream_read(7);
	REQUIRE(out.size() == 3);
	CHECK(out[0] == 0xDE);
	CHECK(out[1] == 0xAD);
	CHECK(out[2] == 0xBE);
}

TEST_CASE("[QUICPicoquicCallback] stream_fin event sets peer-closed flag") {
	if (QUICClient::dispatch_cnx_event_func == nullptr) {
		MESSAGE("Dispatch bridge not registered — skipping");
		return;
	}
	Ref<QUICClient> client;
	client.instantiate();

	QUICClient::dispatch_cnx_event_func(client.ptr(), /*stream_id=*/12,
			nullptr, 0, QUICClient::CALLBACK_STREAM_FIN);
	CHECK(client->is_stream_peer_closed(12));
}

TEST_CASE("[QUICPicoquicCallback] datagram event queues bytes for receive") {
	if (QUICClient::dispatch_cnx_event_func == nullptr) {
		MESSAGE("Dispatch bridge not registered — skipping");
		return;
	}
	Ref<QUICClient> client;
	client.instantiate();

	uint8_t datagram[2] = { 0x42, 0x99 };
	QUICClient::dispatch_cnx_event_func(client.ptr(), /*stream_id=*/0,
			datagram, sizeof(datagram), QUICClient::CALLBACK_DATAGRAM);

	PackedByteArray out = client->receive_datagram();
	REQUIRE(out.size() == 2);
	CHECK(out[0] == 0x42);
	CHECK(out[1] == 0x99);
}

TEST_CASE("[QUICPicoquicCallback] almost_ready event flips status to CONNECTED") {
	if (QUICClient::dispatch_cnx_event_func == nullptr) {
		MESSAGE("Dispatch bridge not registered — skipping");
		return;
	}
	Ref<QUICClient> client;
	client.instantiate();
	QUICClient::dispatch_cnx_event_func(client.ptr(), 0, nullptr, 0,
			QUICClient::CALLBACK_ALMOST_READY);
	CHECK(client->get_status() == QUICClient::STATUS_CONNECTED);
}

TEST_CASE("[QUICPicoquicCallback] ready event flips status to CONNECTED") {
	if (QUICClient::dispatch_cnx_event_func == nullptr) {
		MESSAGE("Dispatch bridge not registered — skipping");
		return;
	}
	Ref<QUICClient> client;
	client.instantiate();
	QUICClient::dispatch_cnx_event_func(client.ptr(), 0, nullptr, 0,
			QUICClient::CALLBACK_READY);
	CHECK(client->get_status() == QUICClient::STATUS_CONNECTED);
}

TEST_CASE("[QUICPicoquicCallback] close event flips status to DISCONNECTED") {
	if (QUICClient::dispatch_cnx_event_func == nullptr) {
		MESSAGE("Dispatch bridge not registered — skipping");
		return;
	}
	Ref<QUICClient> client;
	client.instantiate();
	client->_set_status(QUICClient::STATUS_CONNECTED);
	QUICClient::dispatch_cnx_event_func(client.ptr(), 0, nullptr, 0,
			QUICClient::CALLBACK_CLOSE);
	CHECK(client->get_status() == QUICClient::STATUS_DISCONNECTED);
}

} // namespace TestQUICPicoquicCallback

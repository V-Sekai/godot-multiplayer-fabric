/**************************************************************************/
/*  test_quic_server.h                                                    */
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

#include "../quic_server.h"

#include "tests/test_macros.h"

namespace TestQUICServer {

TEST_CASE("[QUICServer] Fresh instance is not listening") {
	Ref<QUICServer> server;
	server.instantiate();
	REQUIRE(server.is_valid());
	CHECK_FALSE(server->is_listening());
	CHECK(server->get_port() == 0);
}

TEST_CASE("[QUICServer] listen rejects invalid ports") {
	Ref<QUICServer> server;
	server.instantiate();
	CHECK(server->listen(0, "cert.pem", "key.pem") == ERR_INVALID_PARAMETER);
	CHECK(server->listen(-1, "cert.pem", "key.pem") == ERR_INVALID_PARAMETER);
	CHECK(server->listen(65536, "cert.pem", "key.pem") == ERR_INVALID_PARAMETER);
	CHECK_FALSE(server->is_listening());
}

TEST_CASE("[QUICServer] listen rejects empty cert or key paths") {
	Ref<QUICServer> server;
	server.instantiate();
	CHECK(server->listen(4433, String(), "key.pem") == ERR_INVALID_PARAMETER);
	CHECK(server->listen(4433, "cert.pem", String()) == ERR_INVALID_PARAMETER);
	CHECK_FALSE(server->is_listening());
}

TEST_CASE("[QUICServer] alpn defaults to h3 and can be set") {
	Ref<QUICServer> server;
	server.instantiate();
	CHECK(server->get_alpn() == "h3");
	server->set_alpn("webtransport");
	CHECK(server->get_alpn() == "webtransport");
	server->set_alpn(String());
	CHECK(server->get_alpn() == "webtransport"); // empty silently rejected
}

TEST_CASE("[QUICServer] close() on fresh instance is a no-op") {
	Ref<QUICServer> server;
	server.instantiate();
	server->close();
	CHECK_FALSE(server->is_listening());
}

TEST_CASE("[QUICServer] take_connection returns null when idle") {
	Ref<QUICServer> server;
	server.instantiate();
	Ref<QUICClient> c = server->take_connection();
	CHECK(c.is_null());
}

TEST_CASE("[QUICServer] accept queue drains FIFO via take_connection") {
	Ref<QUICServer> server;
	server.instantiate();

	Ref<QUICClient> c1;
	c1.instantiate();
	Ref<QUICClient> c2;
	c2.instantiate();

	// _push_accepted_connection is the picoquic accept-callback target.
	server->_push_accepted_connection(c1);
	server->_push_accepted_connection(c2);

	CHECK(server->take_connection() == c1);
	CHECK(server->take_connection() == c2);
	CHECK(server->take_connection().is_null());
}

TEST_CASE("[QUICServer] close() drops pending accepted connections") {
	Ref<QUICServer> server;
	server.instantiate();
	Ref<QUICClient> c;
	c.instantiate();
	server->_push_accepted_connection(c);
	server->close();
	CHECK(server->take_connection().is_null());
}

} // namespace TestQUICServer

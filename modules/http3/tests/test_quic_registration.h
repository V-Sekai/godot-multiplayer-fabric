/**************************************************************************/
/*  test_quic_registration.h                                              */
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

#include "core/object/class_db.h"
#include "tests/test_macros.h"

namespace TestQUICRegistration {

TEST_CASE("[QUIC] Classes are registered with ClassDB") {
	CHECK(ClassDB::class_exists("QUICClient"));
	CHECK(ClassDB::class_exists("QUICServer"));
}

TEST_CASE("[QUIC] QUICClient inherits RefCounted") {
	CHECK(ClassDB::is_parent_class("QUICClient", "RefCounted"));
	CHECK(ClassDB::is_parent_class("QUICServer", "RefCounted"));
}

TEST_CASE("[QUIC] QUICClient Status enum is exposed") {
	bool success = false;
	int64_t disconnected = ClassDB::get_integer_constant("QUICClient", "STATUS_DISCONNECTED", &success);
	CHECK(success);
	CHECK(disconnected == QUICClient::STATUS_DISCONNECTED);

	int64_t connected = ClassDB::get_integer_constant("QUICClient", "STATUS_CONNECTED", &success);
	CHECK(success);
	CHECK(connected == QUICClient::STATUS_CONNECTED);
}

TEST_CASE("[QUIC] QUICClient exposes core methods via ClassDB") {
	CHECK(ClassDB::has_method("QUICClient", "connect_to_host"));
	CHECK(ClassDB::has_method("QUICClient", "close"));
	CHECK(ClassDB::has_method("QUICClient", "poll"));
	CHECK(ClassDB::has_method("QUICClient", "get_status"));
	CHECK(ClassDB::has_method("QUICClient", "send_datagram"));
	CHECK(ClassDB::has_method("QUICClient", "set_alpn"));
	CHECK(ClassDB::has_method("QUICClient", "get_alpn"));
}

TEST_CASE("[QUIC] QUICServer exposes core methods via ClassDB") {
	CHECK(ClassDB::has_method("QUICServer", "listen"));
	CHECK(ClassDB::has_method("QUICServer", "close"));
	CHECK(ClassDB::has_method("QUICServer", "is_listening"));
	CHECK(ClassDB::has_method("QUICServer", "take_connection"));
}

} // namespace TestQUICRegistration

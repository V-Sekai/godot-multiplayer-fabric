/**************************************************************************/
/*  quic_server.h                                                         */
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

#include "quic_client.h"

#include "core/templates/list.h"

class QUICServer : public RefCounted {
	GDCLASS(QUICServer, RefCounted);

public:
	Error listen(int p_port, const String &p_cert_path, const String &p_key_path) {
		if (p_port <= 0 || p_port > 65535) {
			return ERR_INVALID_PARAMETER;
		}
		if (p_cert_path.is_empty() || p_key_path.is_empty()) {
			return ERR_INVALID_PARAMETER;
		}
		port = p_port;
		cert_path = p_cert_path;
		key_path = p_key_path;
		// TODO: picoquic_create server quic_ctx + bind UDP socket.
		listening = true;
		return OK;
	}

	void close() {
		listening = false;
		port = 0;
		cert_path = String();
		key_path = String();
		accept_queue.clear();
	}

	bool is_listening() const { return listening; }
	int get_port() const { return port; }

	void set_alpn(const String &p_alpn) {
		if (!p_alpn.is_empty()) {
			alpn = p_alpn;
		}
	}
	String get_alpn() const { return alpn; }

	Ref<QUICClient> take_connection() {
		if (accept_queue.is_empty()) {
			return Ref<QUICClient>();
		}
		Ref<QUICClient> front = accept_queue.front()->get();
		accept_queue.pop_front();
		return front;
	}

	// Entry point for the picoquic "new-connection" callback: wraps the
	// freshly-handshaked picoquic_cnx_t in a QUICClient and queues it.
	void _push_accepted_connection(const Ref<QUICClient> &p_client) {
		accept_queue.push_back(p_client);
	}

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("listen", "port", "cert_path", "key_path"), &QUICServer::listen);
		ClassDB::bind_method(D_METHOD("close"), &QUICServer::close);
		ClassDB::bind_method(D_METHOD("is_listening"), &QUICServer::is_listening);
		ClassDB::bind_method(D_METHOD("get_port"), &QUICServer::get_port);
		ClassDB::bind_method(D_METHOD("set_alpn", "alpn"), &QUICServer::set_alpn);
		ClassDB::bind_method(D_METHOD("get_alpn"), &QUICServer::get_alpn);
		ClassDB::bind_method(D_METHOD("take_connection"), &QUICServer::take_connection);
	}

private:
	bool listening = false;
	int port = 0;
	String cert_path;
	String key_path;
	String alpn = "h3";
	List<Ref<QUICClient>> accept_queue;
};

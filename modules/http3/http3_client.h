/**************************************************************************/
/*  http3_client.h                                                        */
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

#include "core/object/ref_counted.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"

// HTTP/3 client layered on QUICClient. Signatures match
// core/io/http_client.h (Status, Method, request, response accessors) so
// call sites migrate from HTTPClient by swapping the class name.
// Picohttp/QPACK decoding lives in http3_client.cpp to keep this header
// free of thirdparty includes.
class HTTP3Client : public RefCounted {
	GDCLASS(HTTP3Client, RefCounted);

public:
	// Values match HTTPClient::Status bit-for-bit so integer casts work.
	enum Status {
		STATUS_DISCONNECTED,
		STATUS_RESOLVING,
		STATUS_CANT_RESOLVE,
		STATUS_CONNECTING,
		STATUS_CANT_CONNECT,
		STATUS_CONNECTED,
		STATUS_REQUESTING,
		STATUS_BODY,
		STATUS_CONNECTION_ERROR,
		STATUS_TLS_HANDSHAKE_ERROR,
	};

	enum Method {
		METHOD_GET,
		METHOD_HEAD,
		METHOD_POST,
		METHOD_PUT,
		METHOD_DELETE,
		METHOD_OPTIONS,
		METHOD_TRACE,
		METHOD_CONNECT,
		METHOD_PATCH,
		METHOD_MAX,
	};

	HTTP3Client();
	~HTTP3Client();

	Error connect_to_host(const String &p_host, int p_port = -1, Ref<TLSOptions> p_tls_options = Ref<TLSOptions>());
	Error request(Method p_method, const String &p_url, const Vector<String> &p_headers, const uint8_t *p_body = nullptr, int p_body_size = 0);
	void close();
	Error poll();

	Status get_status() const { return status; }
	bool has_response() const { return response_code > 0; }
	bool is_response_chunked() const { return true; } // HTTP/3 body is always framed.
	int get_response_code() const { return response_code; }
	Error get_response_headers(List<String> *r_response);
	int64_t get_response_body_length() const { return response_body_length; }
	PackedByteArray read_response_body_chunk();

	void set_blocking_mode(bool p_enable) { blocking_mode = p_enable; }
	bool is_blocking_mode_enabled() const { return blocking_mode; }
	void set_read_chunk_size(int p_size);
	int get_read_chunk_size() const { return read_chunk_size; }

	// GDScript-friendly variants — flat PackedStringArray for headers,
	// matching HTTPClient's own binding style.
	Error request_from_strings(Method p_method, const String &p_url, const PackedStringArray &p_headers, const PackedByteArray &p_body = PackedByteArray());
	PackedStringArray get_response_headers_strings();

protected:
	static void _bind_methods();

private:
	Status status = STATUS_DISCONNECTED;
	int response_code = 0;
	int64_t response_body_length = -1; // -1 = unknown / unspecified.
	Vector<String> response_headers; // "name: value" entries.
	String host;
	bool blocking_mode = false;
	int read_chunk_size = 65536;
	Ref<QUICClient> quic;
	PackedByteArray response_accum; // unparsed bytes from stream 0
	size_t parse_cursor = 0;
	bool headers_parsed = false;
	PackedByteArray body_buffer; // decoded body bytes ready for read_response_body_chunk
};

VARIANT_ENUM_CAST(HTTP3Client::Status);
VARIANT_ENUM_CAST(HTTP3Client::Method);

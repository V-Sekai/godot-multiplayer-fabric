/**************************************************************************/
/*  http3_client.cpp                                                      */
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

#include "http3_client.h"

#include "core/object/class_db.h"

#ifdef GODOT_QUIC_NATIVE_BACKEND
#include <h3zero.h>
#endif

static const char *_METHOD_NAMES[HTTP3Client::METHOD_MAX] = {
	"GET", "HEAD", "POST", "PUT", "DELETE", "OPTIONS", "TRACE", "CONNECT", "PATCH"
};

HTTP3Client::HTTP3Client() {}
HTTP3Client::~HTTP3Client() {}

Error HTTP3Client::connect_to_host(const String &p_host, int p_port, Ref<TLSOptions> p_tls_options) {
	if (p_host.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}
	// -1 is the HTTPClient-spec sentinel meaning "use default". Any other
	// non-positive value or out-of-range port is an explicit caller error.
	if (p_port == 0 || p_port < -1 || p_port > 65535) {
		return ERR_INVALID_PARAMETER;
	}
	int port = (p_port == -1) ? 443 : p_port;
	close();
	quic.instantiate();
	quic->set_alpn("h3");
	if (p_tls_options.is_valid()) {
		quic->set_tls_options(p_tls_options);
	}
	host = p_host;
	Error err = quic->connect_to_host(p_host, port);
	if (err == ERR_CANT_RESOLVE) {
		status = STATUS_CANT_RESOLVE;
		return err;
	}
	if (err != OK) {
		status = STATUS_CANT_CONNECT;
		return err;
	}
	status = STATUS_CONNECTING;
	return OK;
}

Error HTTP3Client::request(Method p_method, const String &p_url, const Vector<String> &p_headers,
		const uint8_t *p_body, int p_body_size) {
	if (quic.is_null() || quic->get_status() != QUICClient::STATUS_CONNECTED) {
		return ERR_UNCONFIGURED;
	}
	if (p_method != METHOD_GET) {
		// TODO: POST/PUT/DELETE require an h3 DATA frame emission path.
		// The picoquic backend only exposes backend_send_h3_get_func today.
		return ERR_UNAVAILABLE;
	}
	(void)p_headers; // TODO: extra headers beyond the spec-required pseudo set.
	(void)p_body;
	(void)p_body_size;

	if (QUICClient::backend_send_h3_get_func == nullptr) {
		return ERR_UNCONFIGURED;
	}
	String path = p_url.is_empty() ? String("/") : p_url;
	Error err = QUICClient::backend_send_h3_get_func(
			quic->_get_backend_ctx(),
			path.utf8().get_data(),
			host.utf8().get_data());
	if (err != OK) {
		status = STATUS_CONNECTION_ERROR;
		return err;
	}
	status = STATUS_REQUESTING;
	response_code = 0;
	response_body_length = -1;
	response_headers.clear();
	response_accum.clear();
	body_buffer.clear();
	parse_cursor = 0;
	headers_parsed = false;
	return OK;
}

Error HTTP3Client::request_from_strings(Method p_method, const String &p_url,
		const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	Vector<String> hdrs;
	for (int i = 0; i < p_headers.size(); i++) {
		hdrs.push_back(p_headers[i]);
	}
	return request(p_method, p_url, hdrs, p_body.ptr(), p_body.size());
}

void HTTP3Client::close() {
	if (quic.is_valid()) {
		quic->close();
		quic.unref();
	}
	status = STATUS_DISCONNECTED;
	response_code = 0;
	response_body_length = -1;
	response_headers.clear();
	response_accum.clear();
	body_buffer.clear();
	parse_cursor = 0;
	headers_parsed = false;
	host = String();
}

Error HTTP3Client::get_response_headers(List<String> *r_response) {
	if (!r_response) {
		return ERR_INVALID_PARAMETER;
	}
	for (int i = 0; i < response_headers.size(); i++) {
		r_response->push_back(response_headers[i]);
	}
	return OK;
}

PackedStringArray HTTP3Client::get_response_headers_strings() {
	PackedStringArray out;
	for (int i = 0; i < response_headers.size(); i++) {
		out.push_back(response_headers[i]);
	}
	return out;
}

void HTTP3Client::set_read_chunk_size(int p_size) {
	if (p_size > 0) {
		read_chunk_size = p_size;
	}
}

#ifdef GODOT_QUIC_NATIVE_BACKEND

// Decode a QUIC varint from p_bytes starting at *io_pos. On success advances
// *io_pos past the varint and returns true. Returns false on truncation.
static bool _decode_varint(const PackedByteArray &p_bytes, size_t &io_pos, uint64_t &r_value) {
	if (io_pos >= (size_t)p_bytes.size()) {
		return false;
	}
	uint8_t b = p_bytes[io_pos];
	int vlen = 1 << (b >> 6);
	if (io_pos + vlen > (size_t)p_bytes.size()) {
		return false;
	}
	uint64_t v = b & 0x3F;
	for (int i = 1; i < vlen; i++) {
		v = (v << 8) | p_bytes[io_pos + i];
	}
	io_pos += vlen;
	r_value = v;
	return true;
}

static String _ascii_from_bytes(const uint8_t *p_bytes, size_t p_length) {
	if (!p_bytes || p_length == 0) {
		return String();
	}
	return String::utf8(reinterpret_cast<const char *>(p_bytes), static_cast<int>(p_length));
}

#endif // GODOT_QUIC_NATIVE_BACKEND

Error HTTP3Client::poll() {
	if (quic.is_null()) {
		return ERR_UNCONFIGURED;
	}
	if (status == STATUS_DISCONNECTED) {
		return OK;
	}

	// Lift QUICClient status up through our machine.
	QUICClient::Status qs = quic->get_status();
	if (qs == QUICClient::STATUS_CONNECTED && status == STATUS_CONNECTING) {
		status = STATUS_CONNECTED;
	}
	if (qs == QUICClient::STATUS_DISCONNECTED && status != STATUS_DISCONNECTED && status != STATUS_BODY) {
		status = STATUS_CONNECTION_ERROR;
		return OK;
	}

	if (status != STATUS_REQUESTING && status != STATUS_BODY) {
		return OK;
	}

	// Drain any freshly-arrived stream 0 bytes into our accumulator.
	PackedByteArray chunk = quic->stream_read(0);
	if (chunk.size() > 0) {
		response_accum.append_array(chunk);
	}

#ifdef GODOT_QUIC_NATIVE_BACKEND
	// Walk h3 frames from parse_cursor. HEADERS frames feed the header
	// parser; DATA frames append to body_buffer. Unknown (GREASE, etc.)
	// frames are skipped per RFC 9114 §7.2.8.
	while (parse_cursor < (size_t)response_accum.size()) {
		size_t start = parse_cursor;
		uint64_t frame_type = 0;
		uint64_t frame_len = 0;
		if (!_decode_varint(response_accum, parse_cursor, frame_type)) {
			parse_cursor = start;
			break;
		}
		if (!_decode_varint(response_accum, parse_cursor, frame_len)) {
			parse_cursor = start;
			break;
		}
		if (parse_cursor + frame_len > (size_t)response_accum.size()) {
			// Frame body not fully arrived yet; rewind and wait.
			parse_cursor = start;
			break;
		}
		const uint8_t *payload = response_accum.ptr() + parse_cursor;
		size_t payload_len = static_cast<size_t>(frame_len);
		if (frame_type == h3zero_frame_header) {
			h3zero_header_parts_t parts{};
			uint8_t *parse_res = h3zero_parse_qpack_header_frame(
					const_cast<uint8_t *>(payload),
					const_cast<uint8_t *>(payload) + payload_len,
					&parts);
			if (parse_res != nullptr) {
				response_code = parts.status;
				// Surface the parseable fields as "name: value" lines.
				response_headers.clear();
				if (parts.status > 0) {
					response_headers.push_back(vformat(":status: %d", parts.status));
				}
				if (parts.path && parts.path_length > 0) {
					response_headers.push_back(":path: " + _ascii_from_bytes(parts.path, parts.path_length));
				}
				if (parts.authority && parts.authority_length > 0) {
					response_headers.push_back(":authority: " + _ascii_from_bytes(parts.authority, parts.authority_length));
				}
				if (parts.range && parts.range_length > 0) {
					response_headers.push_back("range: " + _ascii_from_bytes(parts.range, parts.range_length));
				}
				// content-type lives on parts.content_type as an enum; picohttp
				// does not keep the raw string. A richer impl would use
				// h3zero's per-field callback variant.
				headers_parsed = true;
			}
			status = STATUS_BODY;
		} else if (frame_type == h3zero_frame_data) {
			int before = body_buffer.size();
			body_buffer.resize(before + payload_len);
			memcpy(body_buffer.ptrw() + before, payload, payload_len);
		}
		// else: GREASE / SETTINGS (shouldn't appear on request stream) / etc. — skip.
		parse_cursor += payload_len;
	}
#endif // GODOT_QUIC_NATIVE_BACKEND

	return OK;
}

PackedByteArray HTTP3Client::read_response_body_chunk() {
	// Drain whatever body bytes have been parsed so far. Subsequent calls
	// to poll() will refill body_buffer from newly-arrived DATA frames.
	PackedByteArray out = body_buffer;
	body_buffer.clear();
	return out;
}

void HTTP3Client::_bind_methods() {
	ClassDB::bind_method(D_METHOD("connect_to_host", "host", "port", "tls_options"), &HTTP3Client::connect_to_host, DEFVAL(-1), DEFVAL(Ref<TLSOptions>()));
	ClassDB::bind_method(D_METHOD("request", "method", "url", "headers", "body"), &HTTP3Client::request_from_strings, DEFVAL(PackedByteArray()));
	ClassDB::bind_method(D_METHOD("poll"), &HTTP3Client::poll);
	ClassDB::bind_method(D_METHOD("close"), &HTTP3Client::close);
	ClassDB::bind_method(D_METHOD("get_status"), &HTTP3Client::get_status);
	ClassDB::bind_method(D_METHOD("has_response"), &HTTP3Client::has_response);
	ClassDB::bind_method(D_METHOD("is_response_chunked"), &HTTP3Client::is_response_chunked);
	ClassDB::bind_method(D_METHOD("get_response_code"), &HTTP3Client::get_response_code);
	ClassDB::bind_method(D_METHOD("get_response_headers"), &HTTP3Client::get_response_headers_strings);
	ClassDB::bind_method(D_METHOD("get_response_body_length"), &HTTP3Client::get_response_body_length);
	ClassDB::bind_method(D_METHOD("read_response_body_chunk"), &HTTP3Client::read_response_body_chunk);
	ClassDB::bind_method(D_METHOD("set_blocking_mode", "enabled"), &HTTP3Client::set_blocking_mode);
	ClassDB::bind_method(D_METHOD("is_blocking_mode_enabled"), &HTTP3Client::is_blocking_mode_enabled);
	ClassDB::bind_method(D_METHOD("set_read_chunk_size", "size"), &HTTP3Client::set_read_chunk_size);
	ClassDB::bind_method(D_METHOD("get_read_chunk_size"), &HTTP3Client::get_read_chunk_size);

	BIND_ENUM_CONSTANT(STATUS_DISCONNECTED);
	BIND_ENUM_CONSTANT(STATUS_RESOLVING);
	BIND_ENUM_CONSTANT(STATUS_CANT_RESOLVE);
	BIND_ENUM_CONSTANT(STATUS_CONNECTING);
	BIND_ENUM_CONSTANT(STATUS_CANT_CONNECT);
	BIND_ENUM_CONSTANT(STATUS_CONNECTED);
	BIND_ENUM_CONSTANT(STATUS_REQUESTING);
	BIND_ENUM_CONSTANT(STATUS_BODY);
	BIND_ENUM_CONSTANT(STATUS_CONNECTION_ERROR);
	BIND_ENUM_CONSTANT(STATUS_TLS_HANDSHAKE_ERROR);

	BIND_ENUM_CONSTANT(METHOD_GET);
	BIND_ENUM_CONSTANT(METHOD_HEAD);
	BIND_ENUM_CONSTANT(METHOD_POST);
	BIND_ENUM_CONSTANT(METHOD_PUT);
	BIND_ENUM_CONSTANT(METHOD_DELETE);
	BIND_ENUM_CONSTANT(METHOD_OPTIONS);
	BIND_ENUM_CONSTANT(METHOD_TRACE);
	BIND_ENUM_CONSTANT(METHOD_CONNECT);
	BIND_ENUM_CONSTANT(METHOD_PATCH);

	(void)_METHOD_NAMES;
}

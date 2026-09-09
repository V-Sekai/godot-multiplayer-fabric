/**************************************************************************/
/*  otel_span.cpp                                                         */
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

#include "otel_span.h"

#include "../otel_document.h"

#include "core/crypto/crypto.h"
#include "core/object/class_db.h"
#include "core/os/time.h"

void OTelSpan::_bind_methods() {
	// Validation methods
	ClassDB::bind_static_method("OTelSpan", D_METHOD("is_valid_trace_id", "id"), &OTelSpan::is_valid_trace_id);
	ClassDB::bind_static_method("OTelSpan", D_METHOD("is_valid_span_id", "id"), &OTelSpan::is_valid_span_id);
	ClassDB::bind_static_method("OTelSpan", D_METHOD("generate_trace_id"), &OTelSpan::generate_trace_id);
	ClassDB::bind_static_method("OTelSpan", D_METHOD("generate_span_id"), &OTelSpan::generate_span_id);

	// Trace and Span IDs
	ClassDB::bind_method(D_METHOD("get_trace_id"), &OTelSpan::get_trace_id);
	ClassDB::bind_method(D_METHOD("set_trace_id", "trace_id"), &OTelSpan::set_trace_id);
	ClassDB::bind_method(D_METHOD("get_span_id"), &OTelSpan::get_span_id);
	ClassDB::bind_method(D_METHOD("set_span_id", "span_id"), &OTelSpan::set_span_id);
	ClassDB::bind_method(D_METHOD("get_parent_span_id"), &OTelSpan::get_parent_span_id);
	ClassDB::bind_method(D_METHOD("set_parent_span_id", "parent_span_id"), &OTelSpan::set_parent_span_id);

	// Span metadata
	// get_name/set_name are inherited from Resource and use virtual dispatch —
	// do not re-register them here or ClassDB will error "already has a method".
	ClassDB::bind_method(D_METHOD("get_kind"), &OTelSpan::get_kind);
	ClassDB::bind_method(D_METHOD("set_kind", "kind"), &OTelSpan::set_kind);

	// Timestamps
	ClassDB::bind_method(D_METHOD("get_start_time_unix_nano"), &OTelSpan::get_start_time_unix_nano);
	ClassDB::bind_method(D_METHOD("set_start_time_unix_nano", "time"), &OTelSpan::set_start_time_unix_nano);
	ClassDB::bind_method(D_METHOD("get_end_time_unix_nano"), &OTelSpan::get_end_time_unix_nano);
	ClassDB::bind_method(D_METHOD("set_end_time_unix_nano", "time"), &OTelSpan::set_end_time_unix_nano);

	// Attributes
	ClassDB::bind_method(D_METHOD("get_attributes"), &OTelSpan::get_attributes);
	ClassDB::bind_method(D_METHOD("set_attributes", "attributes"), &OTelSpan::set_attributes);
	ClassDB::bind_method(D_METHOD("add_attribute", "key", "value"), &OTelSpan::add_attribute);

	// Events
	ClassDB::bind_method(D_METHOD("get_events"), &OTelSpan::get_events);
	ClassDB::bind_method(D_METHOD("set_events", "events"), &OTelSpan::set_events);
	ClassDB::bind_method(D_METHOD("add_event", "name", "attributes", "timestamp"), &OTelSpan::add_event, DEFVAL(Dictionary()), DEFVAL(0));

	// Links
	ClassDB::bind_method(D_METHOD("get_links"), &OTelSpan::get_links);
	ClassDB::bind_method(D_METHOD("set_links", "links"), &OTelSpan::set_links);
	ClassDB::bind_method(D_METHOD("add_link", "trace_id", "span_id", "attributes"), &OTelSpan::add_link, DEFVAL(Dictionary()));

	// Status
	ClassDB::bind_method(D_METHOD("get_status_code"), &OTelSpan::get_status_code);
	ClassDB::bind_method(D_METHOD("set_status_code", "code"), &OTelSpan::set_status_code);
	ClassDB::bind_method(D_METHOD("get_status_message"), &OTelSpan::get_status_message);
	ClassDB::bind_method(D_METHOD("set_status_message", "message"), &OTelSpan::set_status_message);

	// State
	ClassDB::bind_method(D_METHOD("is_ended"), &OTelSpan::is_ended);
	ClassDB::bind_method(D_METHOD("mark_ended"), &OTelSpan::mark_ended);

	// Serialization
	ClassDB::bind_method(D_METHOD("to_otlp_dict"), &OTelSpan::to_otlp_dict);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "trace_id"), "set_trace_id", "get_trace_id");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "span_id"), "set_span_id", "get_span_id");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "parent_span_id"), "set_parent_span_id", "get_parent_span_id");
	// "name" property is already registered by Resource — no ADD_PROPERTY here.
	ADD_PROPERTY(PropertyInfo(Variant::INT, "kind", PROPERTY_HINT_ENUM, "Unspecified,Internal,Server,Client,Producer,Consumer"), "set_kind", "get_kind");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "start_time_unix_nano"), "set_start_time_unix_nano", "get_start_time_unix_nano");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "end_time_unix_nano"), "set_end_time_unix_nano", "get_end_time_unix_nano");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "attributes"), "set_attributes", "get_attributes");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "events"), "set_events", "get_events");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "links"), "set_links", "get_links");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "status_code", PROPERTY_HINT_ENUM, "Unset,Ok,Error"), "set_status_code", "get_status_code");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "status_message"), "set_status_message", "get_status_message");

	BIND_ENUM_CONSTANT(SPAN_KIND_UNSPECIFIED);
	BIND_ENUM_CONSTANT(SPAN_KIND_INTERNAL);
	BIND_ENUM_CONSTANT(SPAN_KIND_SERVER);
	BIND_ENUM_CONSTANT(SPAN_KIND_CLIENT);
	BIND_ENUM_CONSTANT(SPAN_KIND_PRODUCER);
	BIND_ENUM_CONSTANT(SPAN_KIND_CONSUMER);

	BIND_ENUM_CONSTANT(STATUS_CODE_UNSET);
	BIND_ENUM_CONSTANT(STATUS_CODE_OK);
	BIND_ENUM_CONSTANT(STATUS_CODE_ERROR);
}

OTelSpan::OTelSpan() {
}

// Validation helpers (private)
bool OTelSpan::is_valid_hex_string(const String &p_str, int p_expected_length) {
	if (p_str.length() != p_expected_length) {
		return false;
	}

	for (int i = 0; i < p_str.length(); i++) {
		char32_t c = p_str[i];
		bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
		if (!is_hex) {
			return false;
		}
	}

	return true;
}

String OTelSpan::generate_random_hex(int p_length) {
	// hex_encode_buffer produces exactly 2*byte_count lowercase hex chars.
	// Formally justified by HexEncodeLength.lean: hexEncodeLength theorem.
	Ref<Crypto> crypto = Crypto::create();
	int byte_count = (p_length + 1) / 2;
	PackedByteArray bytes = crypto->generate_random_bytes(byte_count);
	return String::hex_encode_buffer(bytes.ptr(), bytes.size()).substr(0, p_length);
}

// Validation methods (public)
bool OTelSpan::is_valid_trace_id(const String &p_id) {
	// Allow empty (unset)
	if (p_id.is_empty()) {
		return true;
	}
	// Must be exactly 32 hex characters
	return is_valid_hex_string(p_id, 32);
}

bool OTelSpan::is_valid_span_id(const String &p_id) {
	// Allow empty (unset)
	if (p_id.is_empty()) {
		return true;
	}
	// Must be exactly 16 hex characters
	return is_valid_hex_string(p_id, 16);
}

// ID generation helpers
String OTelSpan::generate_trace_id() {
	// UUIDv7: 128 bits = 16 bytes → hex_encode() → exactly 32 hex chars.
	// Formally: hexEncodeLength proves |hex_encode(b)| = 2*|b| for all b.
	// See modules/open_telemetry/lean/HexEncodeLength.lean.

	Ref<Crypto> crypto = Crypto::create();
	PackedByteArray bytes = crypto->generate_random_bytes(16);
	uint8_t *w = bytes.ptrw();

	// Bytes 0-5: 48-bit millisecond timestamp (UUIDv7 unix_ts_ms field).
	uint64_t ts = (uint64_t)(Time::get_singleton()->get_unix_time_from_system() * 1000.0);
	w[0] = (ts >> 40) & 0xFF;
	w[1] = (ts >> 32) & 0xFF;
	w[2] = (ts >> 24) & 0xFF;
	w[3] = (ts >> 16) & 0xFF;
	w[4] = (ts >> 8) & 0xFF;
	w[5] = ts & 0xFF;

	// Byte 6 upper nibble: version = 7.
	w[6] = (w[6] & 0x0F) | 0x70;

	// Byte 8 upper 2 bits: variant = 10.
	w[8] = (w[8] & 0x3F) | 0x80;

	// hex_encode_buffer(16 bytes) = 32 lowercase hex chars, always.
	return String::hex_encode_buffer(bytes.ptr(), bytes.size());
}

String OTelSpan::generate_span_id() {
	return generate_random_hex(16);
}

// Trace and Span IDs
String OTelSpan::get_trace_id() const {
	return trace_id;
}

void OTelSpan::set_trace_id(const String &p_trace_id) {
	ERR_FAIL_COND_MSG(!is_valid_trace_id(p_trace_id),
			vformat("Invalid trace_id: '%s'. Must be exactly 32 hexadecimal characters or empty.", p_trace_id));
	trace_id = p_trace_id;
}

String OTelSpan::get_span_id() const {
	return span_id;
}

void OTelSpan::set_span_id(const String &p_span_id) {
	ERR_FAIL_COND_MSG(!is_valid_span_id(p_span_id),
			vformat("Invalid span_id: '%s'. Must be exactly 16 hexadecimal characters or empty.", p_span_id));
	span_id = p_span_id;
}

String OTelSpan::get_parent_span_id() const {
	return parent_span_id;
}

void OTelSpan::set_parent_span_id(const String &p_parent_span_id) {
	ERR_FAIL_COND_MSG(!is_valid_span_id(p_parent_span_id),
			vformat("Invalid parent_span_id: '%s'. Must be exactly 16 hexadecimal characters or empty.", p_parent_span_id));
	parent_span_id = p_parent_span_id;
}

// Span metadata
String OTelSpan::get_name() const {
	return name;
}

void OTelSpan::set_name(const String &p_name) {
	name = p_name;
}

OTelSpan::SpanKind OTelSpan::get_kind() const {
	return kind;
}

void OTelSpan::set_kind(SpanKind p_kind) {
	kind = p_kind;
}

// Timestamps
uint64_t OTelSpan::get_start_time_unix_nano() const {
	return start_time_unix_nano;
}

void OTelSpan::set_start_time_unix_nano(uint64_t p_time) {
	start_time_unix_nano = p_time;
}

uint64_t OTelSpan::get_end_time_unix_nano() const {
	return end_time_unix_nano;
}

void OTelSpan::set_end_time_unix_nano(uint64_t p_time) {
	end_time_unix_nano = p_time;
}

// Attributes
Dictionary OTelSpan::get_attributes() const {
	return attributes;
}

void OTelSpan::set_attributes(const Dictionary &p_attributes) {
	attributes.merge(p_attributes, true);
}

void OTelSpan::add_attribute(const String &p_key, const Variant &p_value) {
	attributes[p_key] = p_value;
}

// Events
TypedArray<Dictionary> OTelSpan::get_events() const {
	return events;
}

void OTelSpan::set_events(const TypedArray<Dictionary> &p_events) {
	events = p_events;
}

void OTelSpan::add_event(const String &p_name, const Dictionary &p_attributes, uint64_t p_timestamp) {
	Dictionary event;
	event["name"] = p_name;
	uint64_t ts = p_timestamp == 0 ? (uint64_t)(Time::get_singleton()->get_unix_time_from_system() * 1000000000ULL) : p_timestamp;
	event["timeUnixNano"] = itos((int64_t)ts);
	if (p_attributes.size() > 0) {
		event["attributes"] = p_attributes;
	}
	events.push_back(event);
}

// Links
TypedArray<Dictionary> OTelSpan::get_links() const {
	return links;
}

void OTelSpan::set_links(const TypedArray<Dictionary> &p_links) {
	links = p_links;
}

void OTelSpan::add_link(const String &p_trace_id, const String &p_span_id, const Dictionary &p_attributes) {
	ERR_FAIL_COND_MSG(!is_valid_trace_id(p_trace_id),
			vformat("Invalid link trace_id: '%s'. Must be exactly 32 hexadecimal characters.", p_trace_id));
	ERR_FAIL_COND_MSG(!is_valid_span_id(p_span_id),
			vformat("Invalid link span_id: '%s'. Must be exactly 16 hexadecimal characters.", p_span_id));

	Dictionary link;
	link["traceId"] = p_trace_id;
	link["spanId"] = p_span_id;
	if (p_attributes.size() > 0) {
		link["attributes"] = p_attributes;
	}
	links.push_back(link);
}

// Status
OTelSpan::StatusCode OTelSpan::get_status_code() const {
	return status_code;
}

void OTelSpan::set_status_code(StatusCode p_code) {
	status_code = p_code;
}

String OTelSpan::get_status_message() const {
	return status_message;
}

void OTelSpan::set_status_message(const String &p_message) {
	status_message = p_message;
}

// State
bool OTelSpan::is_ended() const {
	return ended;
}

void OTelSpan::mark_ended() {
	if (!ended) {
		ended = true;
		if (end_time_unix_nano == 0) {
			end_time_unix_nano = (uint64_t)(Time::get_singleton()->get_unix_time_from_system() * 1000000000ULL);
		}
	}
}

// Serialization
Dictionary OTelSpan::to_otlp_dict() const {
	Dictionary span_dict;

	span_dict["traceId"] = trace_id;
	span_dict["spanId"] = span_id;

	if (!parent_span_id.is_empty()) {
		span_dict["parentSpanId"] = parent_span_id;
	}

	span_dict["name"] = name;
	span_dict["kind"] = (int)kind;
	// OTLP requires nanosecond timestamps as decimal strings (exceeds JS safe integer).
	span_dict["startTimeUnixNano"] = itos((int64_t)start_time_unix_nano);
	span_dict["endTimeUnixNano"] = itos((int64_t)end_time_unix_nano);

	if (attributes.size() > 0) {
		span_dict["attributes"] = OTelDocument::attributes_to_otlp(attributes);
	}

	if (events.size() > 0) {
		// Convert each event's raw attributes dict to OTLP array format.
		Array fixed_events;
		for (int i = 0; i < events.size(); i++) {
			Dictionary ev = events[i];
			if (ev.has("attributes") && ev["attributes"].get_type() == Variant::DICTIONARY) {
				ev["attributes"] = OTelDocument::attributes_to_otlp(ev["attributes"]);
			}
			fixed_events.push_back(ev);
		}
		span_dict["events"] = fixed_events;
	}

	if (links.size() > 0) {
		Array fixed_links;
		for (int i = 0; i < links.size(); i++) {
			Dictionary lk = links[i];
			if (lk.has("attributes") && lk["attributes"].get_type() == Variant::DICTIONARY) {
				lk["attributes"] = OTelDocument::attributes_to_otlp(lk["attributes"]);
			}
			fixed_links.push_back(lk);
		}
		span_dict["links"] = fixed_links;
	}

	if (status_code != STATUS_CODE_UNSET) {
		Dictionary status;
		status["code"] = (int)status_code;
		if (!status_message.is_empty()) {
			status["message"] = status_message;
		}
		span_dict["status"] = status;
	}

	return span_dict;
}

Ref<OTelSpan> OTelSpan::from_otlp_dict(const Dictionary &p_dict) {
	Ref<OTelSpan> span;
	span.instantiate();

	if (p_dict.has("traceId")) {
		span->set_trace_id(p_dict["traceId"]);
	}
	if (p_dict.has("spanId")) {
		span->set_span_id(p_dict["spanId"]);
	}
	if (p_dict.has("parentSpanId")) {
		span->set_parent_span_id(p_dict["parentSpanId"]);
	}
	if (p_dict.has("name")) {
		span->set_name(p_dict["name"]);
	}
	if (p_dict.has("kind")) {
		span->set_kind((SpanKind)(int)p_dict["kind"]);
	}
	if (p_dict.has("startTimeUnixNano")) {
		span->set_start_time_unix_nano((uint64_t)(int64_t)p_dict["startTimeUnixNano"]);
	}
	if (p_dict.has("endTimeUnixNano")) {
		span->set_end_time_unix_nano((uint64_t)(int64_t)p_dict["endTimeUnixNano"]);
	}
	if (p_dict.has("attributes")) {
		span->set_attributes(p_dict["attributes"]);
	}
	if (p_dict.has("events")) {
		span->set_events(p_dict["events"]);
	}
	if (p_dict.has("links")) {
		span->set_links(p_dict["links"]);
	}
	if (p_dict.has("status")) {
		Dictionary status = p_dict["status"];
		if (status.has("code")) {
			span->set_status_code((StatusCode)(int)status["code"]);
		}
		if (status.has("message")) {
			span->set_status_message(status["message"]);
		}
	}

	return span;
}

/**************************************************************************/
/*  test_open_telemetry.h                                                 */
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

#include "tests/test_macros.h"

#ifdef TOOLS_ENABLED

#include "core/variant/dictionary.h"

#include "modules/open_telemetry/open_telemetry.h"
#include "modules/open_telemetry/structures/otel_resource.h"
#include "modules/open_telemetry/structures/otel_scope.h"
#include "modules/open_telemetry/structures/otel_span.h"

namespace TestOpenTelemetry {

TEST_CASE("[OpenTelemetry] OTelSpan default construction") {
	Ref<OTelSpan> span = memnew(OTelSpan);

	CHECK(span->get_name() == "");
	CHECK(span->get_trace_id() == "");
	CHECK(span->get_span_id() == "");
	CHECK(span->get_parent_span_id() == "");
	CHECK(span->get_kind() == OTelSpan::SPAN_KIND_INTERNAL);
	CHECK(span->get_status_code() == OTelSpan::STATUS_CODE_UNSET);
	CHECK_FALSE(span->is_ended());
}

TEST_CASE("[OpenTelemetry] OTelSpan ID validation") {
	CHECK(OTelSpan::is_valid_trace_id("0123456789abcdef0123456789abcdef"));
	CHECK_FALSE(OTelSpan::is_valid_trace_id("short"));
	CHECK_FALSE(OTelSpan::is_valid_trace_id("0123456789ABCDEF0123456789abcdef"));

	CHECK(OTelSpan::is_valid_span_id("0123456789abcdef"));
	CHECK_FALSE(OTelSpan::is_valid_span_id("short"));
}

TEST_CASE("[OpenTelemetry] OTelSpan ID generation") {
	String trace_id = OTelSpan::generate_trace_id();
	String span_id = OTelSpan::generate_span_id();

	CHECK(trace_id.length() == 32);
	CHECK(span_id.length() == 16);
	CHECK(OTelSpan::is_valid_trace_id(trace_id));
	CHECK(OTelSpan::is_valid_span_id(span_id));

	CHECK(OTelSpan::generate_trace_id() != OTelSpan::generate_trace_id());
	CHECK(OTelSpan::generate_span_id() != OTelSpan::generate_span_id());
}

TEST_CASE("[OpenTelemetry] OTelSpan set and get fields") {
	Ref<OTelSpan> span = memnew(OTelSpan);

	String trace_id = "0123456789abcdef0123456789abcdef";
	String span_id = "0123456789abcdef";

	span->set_trace_id(trace_id);
	span->set_span_id(span_id);
	span->set_name("my-operation");
	span->set_kind(OTelSpan::SPAN_KIND_SERVER);
	span->set_status_code(OTelSpan::STATUS_CODE_OK);
	span->set_status_message("all good");

	CHECK(span->get_trace_id() == trace_id);
	CHECK(span->get_span_id() == span_id);
	CHECK(span->get_name() == "my-operation");
	CHECK(span->get_kind() == OTelSpan::SPAN_KIND_SERVER);
	CHECK(span->get_status_code() == OTelSpan::STATUS_CODE_OK);
	CHECK(span->get_status_message() == "all good");
}

TEST_CASE("[OpenTelemetry] OTelSpan attributes") {
	Ref<OTelSpan> span = memnew(OTelSpan);

	span->add_attribute("key1", "value1");
	span->add_attribute("key2", 42);
	span->add_attribute("key3", true);

	Dictionary attrs = span->get_attributes();
	CHECK(attrs["key1"] == "value1");
	CHECK(int(attrs["key2"]) == 42);
	CHECK(bool(attrs["key3"]) == true);
}

TEST_CASE("[OpenTelemetry] OTelSpan set_attributes merges") {
	Ref<OTelSpan> span = memnew(OTelSpan);

	Dictionary first;
	first["a"] = "first";
	first["b"] = 1;
	span->set_attributes(first);

	Dictionary second;
	second["c"] = true;
	second["b"] = 2;
	span->set_attributes(second);

	Dictionary attrs = span->get_attributes();
	CHECK(attrs["a"] == "first");
	CHECK(int(attrs["b"]) == 2);
	CHECK(bool(attrs["c"]) == true);
	CHECK(attrs.size() == 3);
}

TEST_CASE("[OpenTelemetry] OTelSpan mark_ended") {
	Ref<OTelSpan> span = memnew(OTelSpan);

	CHECK_FALSE(span->is_ended());
	span->mark_ended();
	CHECK(span->is_ended());
}

TEST_CASE("[OpenTelemetry] OTelSpan OTLP serialization roundtrip") {
	Ref<OTelSpan> span = memnew(OTelSpan);
	span->set_trace_id("0123456789abcdef0123456789abcdef");
	span->set_span_id("0123456789abcdef");
	span->set_name("roundtrip-test");
	span->set_kind(OTelSpan::SPAN_KIND_CLIENT);
	span->set_status_code(OTelSpan::STATUS_CODE_ERROR);
	span->set_status_message("something failed");
	span->add_attribute("attr", "val");

	Dictionary dict = span->to_otlp_dict();
	Ref<OTelSpan> restored = OTelSpan::from_otlp_dict(dict);

	CHECK(restored->get_trace_id() == span->get_trace_id());
	CHECK(restored->get_span_id() == span->get_span_id());
	CHECK(restored->get_name() == span->get_name());
	CHECK(restored->get_kind() == span->get_kind());
	CHECK(restored->get_status_code() == span->get_status_code());
}

TEST_CASE("[OpenTelemetry] OpenTelemetryTracer construction") {
	Ref<OpenTelemetryTracer> tracer = memnew(OpenTelemetryTracer("my-tracer", "1.0.0", "http://example.com/schema"));

	CHECK(tracer->get_name() == "my-tracer");
	CHECK(tracer->get_version() == "1.0.0");
	CHECK(tracer->get_schema_url() == "http://example.com/schema");
	CHECK(tracer->enabled());
}

TEST_CASE("[OpenTelemetry] OpenTelemetryTracerProvider tracer caching") {
	Dictionary resource_attrs;
	resource_attrs["service.name"] = "test-service";

	Ref<OpenTelemetryTracerProvider> provider = memnew(OpenTelemetryTracerProvider(resource_attrs));

	CHECK(provider->get_resource_attributes()["service.name"] == "test-service");

	Ref<OpenTelemetryTracer> t1 = provider->get_tracer("tracer1", "1.0", "http://example.com");
	Ref<OpenTelemetryTracer> t2 = provider->get_tracer("tracer1", "1.0", "http://example.com");
	CHECK(t1 == t2);

	Ref<OpenTelemetryTracer> t3 = provider->get_tracer("tracer2", "1.0", "http://example.com");
	CHECK(t1 != t3);
}

TEST_CASE("[OpenTelemetry] OTelResource service attributes") {
	Ref<OTelResource> resource = memnew(OTelResource);

	resource->set_service_name("my-service");
	resource->set_service_version("2.0.0");

	CHECK(resource->get_service_name() == "my-service");
	CHECK(resource->get_service_version() == "2.0.0");
	CHECK(resource->has_attribute("service.name"));
}

TEST_CASE("[OpenTelemetry] OTelScope name and version") {
	Ref<OTelScope> scope = memnew(OTelScope);

	scope->set_name("my-scope");
	scope->set_version("1.2.3");

	CHECK(scope->get_name() == "my-scope");
	CHECK(scope->get_version() == "1.2.3");
}

TEST_CASE("[OpenTelemetry] UUID v7 generation") {
	OpenTelemetry *otel = memnew(OpenTelemetry);

	String uuid1 = otel->generate_uuid_v7();
	String uuid2 = otel->generate_uuid_v7();

	CHECK(uuid1 != uuid2);
	CHECK(uuid1.length() == 36);
	CHECK(uuid2.length() == 36);
	CHECK(uuid1.substr(8, 1) == "-");
	CHECK(uuid1.substr(13, 1) == "-");
	CHECK(uuid1.substr(18, 1) == "-");
	CHECK(uuid1.substr(23, 1) == "-");
	CHECK(uuid1.substr(14, 1) == "7");

	memdelete(otel);
}

} //namespace TestOpenTelemetry

#endif // TOOLS_ENABLED

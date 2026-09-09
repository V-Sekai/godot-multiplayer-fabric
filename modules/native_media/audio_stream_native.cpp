/**************************************************************************/
/*  audio_stream_native.cpp                                               */
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

#include "audio_stream_native.h"

#include "core/io/file_access.h"
#include "core/object/class_db.h"

int AudioStreamPlaybackNative::_mix_internal(AudioFrame *p_buffer, int p_frames) {
	if (!active || !backend) {
		return 0;
	}
	int decoded = backend->decode_audio(p_buffer, p_frames);
	if (decoded < 0) {
		active = false;
		return 0;
	}
	frames_mixed += decoded;
	if (decoded < p_frames) {
		// An underrun means one of two things: real end-of-stream (the
		// playback either loops or stops), or just an async-decode-in-flight
		// gap (the playback keeps going and tries again next mix tick). The
		// backend's is_audio_eof() distinguishes the two.
		if (backend->is_audio_eof()) {
			bool should_loop = looping_override ? looping : (native_stream.is_valid() && native_stream->has_loop());
			if (should_loop) {
				backend->seek(native_stream.is_valid() ? native_stream->get_loop_offset() : 0.0);
				loops++;
				int remaining = backend->decode_audio(p_buffer + decoded, p_frames - decoded);
				if (remaining > 0) {
					decoded += remaining;
					frames_mixed += remaining;
				}
			} else {
				active = false;
			}
		}
		// else: the pipeline is transiently empty; active stays true and the
		// caller fills the remaining frames with silence (zeroed by the
		// AudioStreamPlaybackResampled buffer).
	}
	return decoded;
}

float AudioStreamPlaybackNative::get_stream_sampling_rate() {
	// info.sample_rate is the rate the backend delivers as of open() time
	// (AudioServer's mix rate). AudioStreamPlaybackResampled::mix()
	// re-queries AudioServer's current rate every frame and computes the
	// rebalance ratio against this value, so live rate changes track
	// transparently — the only suboptimal path is a brief period of double
	// resampling between an AudioServer rate change and the next open().
	return float(info.sample_rate ? info.sample_rate : 44100);
}

void AudioStreamPlaybackNative::start(double p_from_pos) {
	if (!backend || native_stream.is_null()) {
		return;
	}
	if (!opened) {
		if (backend->open(native_stream->get_data(), (NativeMediaBackend::ContainerFormat)native_stream->get_container_hint()) != OK) {
			return;
		}
		info = backend->get_audio_info();
		opened = true;
	}
	if (p_from_pos > 0.0) {
		backend->seek(p_from_pos);
	} else {
		// Rewinding to the start avoids paying the open() cost again.
		backend->reset();
	}
	frames_mixed = uint64_t(p_from_pos * info.sample_rate);
	loops = 0;
	active = true;
	begin_resample();
}

void AudioStreamPlaybackNative::stop() {
	active = false;
	// `opened` stays true and the backend's pipeline stays alive. Replaying
	// via start() then only pays a seek(), not a full pipeline rebuild.
}

bool AudioStreamPlaybackNative::is_playing() const {
	return active;
}

int AudioStreamPlaybackNative::get_loop_count() const {
	return loops;
}

double AudioStreamPlaybackNative::get_playback_position() const {
	if (info.sample_rate == 0) {
		return 0.0;
	}
	return double(frames_mixed) / double(info.sample_rate);
}

void AudioStreamPlaybackNative::seek(double p_time) {
	if (!backend) {
		return;
	}
	backend->seek(p_time);
	frames_mixed = uint64_t(p_time * info.sample_rate);
}

void AudioStreamPlaybackNative::tag_used_streams() {
	if (native_stream.is_valid()) {
		native_stream->tag_used(get_playback_position());
	}
}

AudioStreamPlaybackNative::AudioStreamPlaybackNative() {
	backend = NativeMediaBackend::create();
}

AudioStreamPlaybackNative::~AudioStreamPlaybackNative() {
	if (backend) {
		memdelete(backend);
		backend = nullptr;
	}
}

bool AudioStreamNative::_probe_info() {
	if (info_cached) {
		return cached_info.present;
	}
	NativeMediaBackend *probe = NativeMediaBackend::create();
	if (!probe) {
		info_cached = true;
		return false;
	}
	if (probe->open(data, container_hint) == OK) {
		cached_info = probe->get_audio_info();
	}
	memdelete(probe);
	info_cached = true;
	return cached_info.present;
}

Ref<AudioStreamNative> AudioStreamNative::load_from_buffer(const Vector<uint8_t> &p_buffer, int p_hint) {
	Ref<AudioStreamNative> stream;
	stream.instantiate();
	stream->set_container_hint(p_hint);
	stream->set_data(p_buffer);
	return stream;
}

Ref<AudioStreamNative> AudioStreamNative::load_from_file(const String &p_path) {
	Vector<uint8_t> buf = FileAccess::get_file_as_bytes(p_path);
	return load_from_buffer(buf);
}

void AudioStreamNative::set_data(const Vector<uint8_t> &p_data) {
	data = p_data;
	info_cached = false;
}

Vector<uint8_t> AudioStreamNative::get_data() const {
	return data;
}

void AudioStreamNative::set_container_hint(int p_hint) {
	container_hint = (NativeMediaBackend::ContainerFormat)p_hint;
	info_cached = false;
}

int AudioStreamNative::get_container_hint() const {
	return container_hint;
}

void AudioStreamNative::set_loop(bool p_enable) {
	loop = p_enable;
}

bool AudioStreamNative::has_loop() const {
	return loop;
}

void AudioStreamNative::set_loop_offset(double p_seconds) {
	loop_offset = p_seconds;
}

double AudioStreamNative::get_loop_offset() const {
	return loop_offset;
}

Ref<AudioStreamPlayback> AudioStreamNative::instantiate_playback() {
	Ref<AudioStreamPlaybackNative> playback;
	playback.instantiate();
	playback->native_stream = Ref<AudioStreamNative>(this);
	return playback;
}

String AudioStreamNative::get_stream_name() const {
	return "Native";
}

double AudioStreamNative::get_length() const {
	const_cast<AudioStreamNative *>(this)->_probe_info();
	return cached_info.duration_seconds;
}

bool AudioStreamNative::is_monophonic() const {
	return false;
}

void AudioStreamNative::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_data", "data"), &AudioStreamNative::set_data);
	ClassDB::bind_method(D_METHOD("get_data"), &AudioStreamNative::get_data);
	ClassDB::bind_method(D_METHOD("set_container_hint", "hint"), &AudioStreamNative::set_container_hint);
	ClassDB::bind_method(D_METHOD("get_container_hint"), &AudioStreamNative::get_container_hint);
	ClassDB::bind_method(D_METHOD("set_loop", "enable"), &AudioStreamNative::set_loop);
	ClassDB::bind_method(D_METHOD("has_loop"), &AudioStreamNative::has_loop);
	ClassDB::bind_method(D_METHOD("set_loop_offset", "seconds"), &AudioStreamNative::set_loop_offset);
	ClassDB::bind_method(D_METHOD("get_loop_offset"), &AudioStreamNative::get_loop_offset);

	ClassDB::bind_static_method("AudioStreamNative", D_METHOD("load_from_buffer", "buffer", "hint"), &AudioStreamNative::load_from_buffer, DEFVAL((int)NativeMediaBackend::CONTAINER_AUTO));
	ClassDB::bind_static_method("AudioStreamNative", D_METHOD("load_from_file", "path"), &AudioStreamNative::load_from_file);

	ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "data"), "set_data", "get_data");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "container_hint", PROPERTY_HINT_ENUM, "Auto,WebM,MP4,Ogg,WAV"), "set_container_hint", "get_container_hint");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "loop"), "set_loop", "has_loop");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "loop_offset"), "set_loop_offset", "get_loop_offset");
}

AudioStreamNative::AudioStreamNative() {}
AudioStreamNative::~AudioStreamNative() {}

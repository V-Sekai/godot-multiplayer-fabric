/**************************************************************************/
/*  audio_stream_native.h                                                 */
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

#include "servers/audio/audio_stream.h"

#include "modules/native_media/native_media_backend.h"

class AudioStreamNative;

class AudioStreamPlaybackNative : public AudioStreamPlaybackResampled {
	GDCLASS(AudioStreamPlaybackNative, AudioStreamPlaybackResampled);

	NativeMediaBackend *backend = nullptr;
	NativeMediaBackend::AudioInfo info;

	bool active = false;
	bool opened = false; // true once backend->open() runs for the current
						 // data; stop() preserves it so replay is cheap.
	uint64_t frames_mixed = 0;
	int loops = 0;
	bool looping_override = false;
	bool looping = false;

	friend class AudioStreamNative;
	Ref<AudioStreamNative> native_stream;

protected:
	virtual int _mix_internal(AudioFrame *p_buffer, int p_frames) override;
	virtual float get_stream_sampling_rate() override;

public:
	virtual void start(double p_from_pos = 0.0) override;
	virtual void stop() override;
	virtual bool is_playing() const override;
	virtual int get_loop_count() const override;
	virtual double get_playback_position() const override;
	virtual void seek(double p_time) override;
	virtual void tag_used_streams() override;

	AudioStreamPlaybackNative();
	~AudioStreamPlaybackNative();
};

class AudioStreamNative : public AudioStream {
	GDCLASS(AudioStreamNative, AudioStream);
	OBJ_SAVE_TYPE(AudioStream);
	RES_BASE_EXTENSION("natstr");

	friend class AudioStreamPlaybackNative;

	Vector<uint8_t> data;
	NativeMediaBackend::ContainerFormat container_hint = NativeMediaBackend::CONTAINER_AUTO;

	NativeMediaBackend::AudioInfo cached_info;
	bool info_cached = false;

	bool loop = false;
	double loop_offset = 0.0;

	bool _probe_info();

protected:
	static void _bind_methods();

public:
	static Ref<AudioStreamNative> load_from_buffer(const Vector<uint8_t> &p_buffer, int p_hint = NativeMediaBackend::CONTAINER_AUTO);
	static Ref<AudioStreamNative> load_from_file(const String &p_path);

	void set_data(const Vector<uint8_t> &p_data);
	Vector<uint8_t> get_data() const;

	void set_container_hint(int p_hint);
	int get_container_hint() const;

	void set_loop(bool p_enable);
	virtual bool has_loop() const override;

	void set_loop_offset(double p_seconds);
	double get_loop_offset() const;

	virtual Ref<AudioStreamPlayback> instantiate_playback() override;
	virtual String get_stream_name() const override;
	virtual double get_length() const override;
	virtual bool is_monophonic() const override;

	AudioStreamNative();
	virtual ~AudioStreamNative();
};

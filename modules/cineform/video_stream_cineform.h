/**************************************************************************/
/*  video_stream_cineform.h                                               */
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

#include "CFHDDecoder.h"
#include "mkvparser/mkvparser.h"

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "scene/resources/video_stream.h"

class ImageTexture;

class VideoStreamPlaybackCineForm : public VideoStreamPlayback {
	GDCLASS(VideoStreamPlaybackCineForm, VideoStreamPlayback);

	// mkvparser reads through this, so the file goes through FileAccess like every other read.
	class FileAccessMkvReader : public mkvparser::IMkvReader {
		Ref<FileAccess> file;
		long long length = 0;

	public:
		Error open(const String &p_path);
		int Read(long long p_pos, long p_len, unsigned char *p_buf) override;
		int Length(long long *r_total, long long *r_available) override;
	};

	FileAccessMkvReader reader;
	mkvparser::Segment *segment = nullptr;
	const mkvparser::Cluster *cluster = nullptr;
	const mkvparser::BlockEntry *block_entry = nullptr;
	long long video_track = -1;

	CFHD_DecoderRef decoder = nullptr;
	CFHD_PixelFormat decoded_format = CFHD_PIXEL_FORMAT_BGRA;
	int decoded_pitch = 0;

	Ref<ImageTexture> texture;
	Vector<uint8_t> decoded;
	Vector<uint8_t> sample;

	Size2i size;
	double time = 0.0;
	double next_frame_time = 0.0;
	double length = 0.0;
	bool playing = false;
	bool paused = false;
	bool eof = false;
	bool block_pending = false;

	bool _advance_to_next_block();
	bool _decode_current_block();
	void _clear();

public:
	Error set_file(const String &p_path);

	void play() override;
	void stop() override;
	bool is_playing() const override;
	void set_paused(bool p_paused) override;
	bool is_paused() const override;
	double get_length() const override;
	double get_playback_position() const override;
	void seek(double p_time) override;
	void set_audio_track(int p_idx) override;
	Ref<Texture2D> get_texture() const override;
	void update(double p_delta) override;
	int get_channels() const override;
	int get_mix_rate() const override;

	VideoStreamPlaybackCineForm();
	~VideoStreamPlaybackCineForm();
};

class VideoStreamCineForm : public VideoStream {
	GDCLASS(VideoStreamCineForm, VideoStream);

protected:
	static void _bind_methods() {}

public:
	Ref<VideoStreamPlayback> instantiate_playback() override {
		Ref<VideoStreamPlaybackCineForm> pb;
		pb.instantiate();
		if (pb->set_file(get_file()) != OK) {
			return Ref<VideoStreamPlayback>();
		}
		return pb;
	}
};

class ResourceFormatLoaderCineForm : public ResourceFormatLoader {
	GDSOFTCLASS(ResourceFormatLoaderCineForm, ResourceFormatLoader);

public:
	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual void get_recognized_extensions(List<String> *p_extensions) const override;
	virtual bool handles_type(const String &p_type) const override;
	virtual String get_resource_type(const String &p_path) const override;
};

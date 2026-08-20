/**************************************************************************/
/*  movie_writer_cineform.h                                               */
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

#include "core/io/file_access.h"
#include "core/templates/list.h"
#include "servers/movie_writer/movie_writer.h"

#include "CFHDEncoder.h"
#include "mkvmuxer/mkvmuxer.h"

class MovieWriterCineForm : public MovieWriter {
	GDCLASS(MovieWriterCineForm, MovieWriter)

	uint32_t mix_rate = 48000;
	AudioServer::SpeakerMode speaker_mode = AudioServer::SPEAKER_MODE_STEREO;
	uint32_t audio_bit_depth = 16;
	String base_path;
	uint32_t frame_count = 0;
	uint32_t submitted_count = 0;
	uint32_t fps = 0;
	Size2i size;
	int quality_index = 2;
	bool keep_alpha = false;

	uint32_t audio_block_size = 0;
	uint32_t track_channels = 2;
	uint32_t audio_frames = 0;

	CFHD_EncoderPoolRef pool = nullptr;
	uint32_t queued = 0;

	// RGBA to BGRA, into a buffer the encoder reads asynchronously.
	Vector<uint8_t> staging;

	// Audio waits here until its frame comes back from the pool, so each chunk pair stays
	// adjacent in the file and the index remains a simple alternation.
	List<Vector<int16_t>> pending_audio;

	Ref<FileAccess> f;

	// mkvmuxer writes through this interface, so the file goes out through FileAccess like
	// every other engine write.
	class FileAccessMkvWriter : public mkvmuxer::IMkvWriter {
		Ref<FileAccess> file;

	public:
		explicit FileAccessMkvWriter(const Ref<FileAccess> &p_file) :
				file(p_file) {}
		mkvmuxer::int32 Write(const void *p_buf, mkvmuxer::uint32 p_len) override;
		mkvmuxer::int64 Position() const override;
		mkvmuxer::int32 Position(mkvmuxer::int64 p_position) override;
		bool Seekable() const override { return true; }
		void ElementStartNotify(mkvmuxer::uint64, mkvmuxer::int64) override {}
	};

	FileAccessMkvWriter *mkv_writer = nullptr;
	mkvmuxer::Segment *segment = nullptr;
	uint64_t video_track = 0;
	uint64_t audio_track = 0;

	void _drain(bool p_block);
	void _store_video_chunk(const void *p_data, uint32_t p_size);

protected:
	virtual uint32_t get_audio_mix_rate() const override;
	virtual AudioServer::SpeakerMode get_audio_speaker_mode() const override;
	virtual void get_supported_extensions(List<String> *r_extensions) const override;

	virtual Error write_begin(const Size2i &p_movie_size, uint32_t p_fps, const String &p_base_path) override;
	virtual Error write_frame(const Ref<Image> &p_image, const int32_t *p_audio_data) override;
	virtual void write_end() override;

	virtual bool handles_file(const String &p_path) const override;

public:
	MovieWriterCineForm();
	~MovieWriterCineForm();
};

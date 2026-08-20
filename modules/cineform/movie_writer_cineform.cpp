/**************************************************************************/
/*  movie_writer_cineform.cpp                                             */
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

#include "movie_writer_cineform.h"

#include "core/config/project_settings.h"
#include "core/io/marshalls.h"
#include "core/os/os.h"

#include <tmmintrin.h>

static const CFHD_EncodingQuality QUALITY_LADDER[] = {
	CFHD_ENCODING_QUALITY_LOW,
	CFHD_ENCODING_QUALITY_MEDIUM,
	CFHD_ENCODING_QUALITY_HIGH,
	CFHD_ENCODING_QUALITY_FILMSCAN1,
	CFHD_ENCODING_QUALITY_FILMSCAN2,
	CFHD_ENCODING_QUALITY_FILMSCAN3,
};

mkvmuxer::int32 MovieWriterCineForm::FileAccessMkvWriter::Write(const void *p_buf, mkvmuxer::uint32 p_len) {
	if (file.is_null()) {
		return -1;
	}
	file->store_buffer((const uint8_t *)p_buf, p_len);
	return 0;
}

mkvmuxer::int64 MovieWriterCineForm::FileAccessMkvWriter::Position() const {
	return file.is_null() ? -1 : mkvmuxer::int64(file->get_position());
}

mkvmuxer::int32 MovieWriterCineForm::FileAccessMkvWriter::Position(mkvmuxer::int64 p_position) {
	if (file.is_null()) {
		return -1;
	}
	file->seek(uint64_t(p_position));
	return 0;
}

uint32_t MovieWriterCineForm::get_audio_mix_rate() const {
	return mix_rate;
}

AudioServer::SpeakerMode MovieWriterCineForm::get_audio_speaker_mode() const {
	return speaker_mode;
}

bool MovieWriterCineForm::handles_file(const String &p_path) const {
	const String ext = p_path.get_extension().to_lower();
	return ext == "cfhd" || ext == "mkv";
}

void MovieWriterCineForm::get_supported_extensions(List<String> *r_extensions) const {
	r_extensions->push_back("cfhd");
	r_extensions->push_back("mkv");
}

Error MovieWriterCineForm::write_begin(const Size2i &p_movie_size, uint32_t p_fps, const String &p_base_path) {
	base_path = p_base_path.get_basename();
	if (base_path.is_relative_path()) {
		base_path = "res://" + base_path;
	}
	base_path += ".mkv";

	size = p_movie_size;
	fps = p_fps;
	frame_count = 0;
	submitted_count = 0;
	queued = 0;

	// The codec works in 8x8 wavelet blocks and reports an odd size late and unhelpfully.
	ERR_FAIL_COND_V_MSG((size.width & 1) || (size.height & 1), ERR_INVALID_PARAMETER,
			vformat("CineForm requires even dimensions, got %dx%d.", size.width, size.height));

	int threads = int(GLOBAL_GET("editor/movie_writer/cineform/thread_count"));
	if (threads <= 0) {
		threads = CLAMP(OS::get_singleton()->get_processor_count(), 1, 16);
	}

	// A pool of zero encoders accepts every frame and fails it with CFHD_ERROR_UNEXPECTED.
	CFHD_Error err = CFHD_CreateEncoderPool(&pool, threads, 8, nullptr);
	ERR_FAIL_COND_V_MSG(err != CFHD_ERROR_OKAY, ERR_CANT_CREATE,
			vformat("CFHD_CreateEncoderPool failed with code %d.", int(err)));

	const CFHD_EncodedFormat encoded = keep_alpha ? CFHD_ENCODED_FORMAT_RGBA_4444 : CFHD_ENCODED_FORMAT_RGB_444;
	err = CFHD_PrepareEncoderPool(pool, uint_least16_t(size.width), uint_least16_t(size.height),
			CFHD_PIXEL_FORMAT_BGRA, encoded, CFHD_ENCODING_FLAGS_NONE, QUALITY_LADDER[quality_index]);
	ERR_FAIL_COND_V_MSG(err != CFHD_ERROR_OKAY, ERR_CANT_CREATE,
			vformat("CFHD_PrepareEncoderPool failed with code %d.", int(err)));

	err = CFHD_StartEncoderPool(pool);
	ERR_FAIL_COND_V_MSG(err != CFHD_ERROR_OKAY, ERR_CANT_CREATE,
			vformat("CFHD_StartEncoderPool failed with code %d.", int(err)));

	staging.resize(size.width * size.height * 4);

	f = FileAccess::open(base_path, FileAccess::WRITE_READ);
	ERR_FAIL_COND_V(f.is_null(), ERR_CANT_OPEN);

	mkv_writer = memnew(FileAccessMkvWriter(f));
	segment = memnew(mkvmuxer::Segment);
	ERR_FAIL_COND_V_MSG(!segment->Init(mkv_writer), ERR_CANT_CREATE,
			"Could not initialize the Matroska segment.");
	segment->set_mode(mkvmuxer::Segment::kFile);

	video_track = segment->AddVideoTrack(size.width, size.height, 1);
	ERR_FAIL_COND_V(video_track == 0, ERR_CANT_CREATE);
	mkvmuxer::VideoTrack *vt = (mkvmuxer::VideoTrack *)segment->GetTrackByNumber(video_track);
	ERR_FAIL_NULL_V(vt, ERR_CANT_CREATE);
	// AddVideoTrack defaults to VP8. A non-WebM codec id selects a DocType of matroska.
	vt->set_codec_id("V_MS/VFW/FOURCC");
	vt->set_frame_rate(double(fps));

	// CodecPrivate for V_MS/VFW/FOURCC is a BITMAPINFOHEADER.
	uint8_t bih[40];
	memset(bih, 0, sizeof(bih));
	encode_uint32(40, bih + 0); // biSize
	encode_uint32(uint32_t(size.width), bih + 4);
	encode_uint32(uint32_t(size.height), bih + 8);
	encode_uint16(1, bih + 12); // biPlanes
	encode_uint16(keep_alpha ? 32 : 24, bih + 14); // biBitCount
	memcpy(bih + 16, "CFHD", 4); // biCompression
	encode_uint32(uint32_t(size.width) * uint32_t(size.height) * (keep_alpha ? 4 : 3), bih + 20);
	ERR_FAIL_COND_V(!vt->SetCodecPrivate(bih, sizeof(bih)), ERR_CANT_CREATE);

	uint32_t channels = 2;
	switch (speaker_mode) {
		case AudioServer::SPEAKER_MODE_STEREO:
			channels = 2;
			break;
		case AudioServer::SPEAKER_SURROUND_31:
			channels = 4;
			break;
		case AudioServer::SPEAKER_SURROUND_51:
			channels = 6;
			break;
		case AudioServer::SPEAKER_SURROUND_71:
			channels = 8;
			break;
	}
	audio_block_size = (mix_rate / fps) * (audio_bit_depth / 8) * channels;

	audio_track = segment->AddAudioTrack(int32_t(mix_rate), int32_t(channels), 2);
	ERR_FAIL_COND_V(audio_track == 0, ERR_CANT_CREATE);
	mkvmuxer::AudioTrack *at = (mkvmuxer::AudioTrack *)segment->GetTrackByNumber(audio_track);
	ERR_FAIL_NULL_V(at, ERR_CANT_CREATE);
	at->set_codec_id("A_PCM/INT/LIT");
	at->set_bit_depth(audio_bit_depth);

	return OK;
}

void MovieWriterCineForm::_write_encoded_frame(const void *p_data, uint32_t p_size) {
	// Matroska timestamps are absolute nanoseconds. Every CineForm frame is a keyframe.
	const uint64_t timestamp_ns = uint64_t(frame_count) * 1000000000ULL / uint64_t(fps);
	if (!segment->AddFrame((const uint8_t *)p_data, p_size, video_track, timestamp_ns, true)) {
		ERR_PRINT(vformat("Could not add video frame %d to the segment.", frame_count));
	}
	frame_count++;

	// The pool returns samples in submission order.
	if (!pending_audio.is_empty()) {
		const Vector<int16_t> block = pending_audio.front()->get();
		pending_audio.pop_front();
		if (!segment->AddFrame((const uint8_t *)block.ptr(), audio_block_size, audio_track,
					timestamp_ns, true)) {
			ERR_PRINT(vformat("Could not add audio for frame %d to the segment.", audio_frames));
		}
		audio_frames++;
	}
}

void MovieWriterCineForm::_drain(bool p_block) {
	while (queued > 0) {
		uint32_t number = 0;
		CFHD_SampleBufferRef buffer = nullptr;
		CFHD_Error err = p_block ? CFHD_WaitForSample(pool, &number, &buffer)
								 : CFHD_TestForSample(pool, &number, &buffer);
		if (err != CFHD_ERROR_OKAY || buffer == nullptr) {
			break;
		}
		void *data = nullptr;
		size_t len = 0;
		if (CFHD_GetEncodedSample(buffer, &data, &len) == CFHD_ERROR_OKAY && data != nullptr) {
			_write_encoded_frame(data, uint32_t(len));
		}
		CFHD_ReleaseSampleBuffer(pool, buffer);
		queued--;
	}
}

Error MovieWriterCineForm::write_frame(const Ref<Image> &p_image, const int32_t *p_audio_data) {
	ERR_FAIL_COND_V(f.is_null() || pool == nullptr, ERR_UNCONFIGURED);

	Ref<Image> img = p_image;
	if (img->get_format() != Image::FORMAT_RGBA8) {
		img = Image::create_from_data(img->get_width(), img->get_height(), false, img->get_format(), img->get_data());
		img->convert(Image::FORMAT_RGBA8);
	}

	const Vector<uint8_t> src = img->get_data();
	const uint32_t pitch = size.width * 4;
	ERR_FAIL_COND_V(uint32_t(src.size()) < pitch * uint32_t(size.height), ERR_INVALID_DATA);

	// RGBA to BGRA with the rows reversed, as the encoder takes bottom-up input.
	const __m128i swizzle = _mm_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);
	const int wide = int(pitch) & ~15;
	const uint8_t *in = src.ptr();
	uint8_t *out = staging.ptrw();
	for (int y = 0; y < size.height; y++) {
		const uint8_t *s = in + pitch * uint32_t(size.height - 1 - y);
		uint8_t *d = out + pitch * uint32_t(y);
		int b = 0;
		for (; b < wide; b += 16) {
			_mm_storeu_si128((__m128i *)(d + b), _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(s + b)), swizzle));
		}
		for (; b < int(pitch); b += 4) {
			d[b + 0] = s[b + 2];
			d[b + 1] = s[b + 1];
			d[b + 2] = s[b + 0];
			d[b + 3] = s[b + 3];
		}
	}

	CFHD_Error err = CFHD_EncodeAsyncSample(pool, submitted_count, out, intptr_t(pitch), nullptr);
	ERR_FAIL_COND_V_MSG(err != CFHD_ERROR_OKAY, ERR_CANT_CREATE,
			vformat("CFHD_EncodeAsyncSample failed on frame %d with code %d.", submitted_count, int(err)));
	submitted_count++;
	queued++;

	Vector<int16_t> block;
	int num_samples = audio_block_size / 2;
	block.resize(num_samples);
	if (audio_bit_depth == 16) {
		for (int i = 0; i < num_samples; ++i) {
			block.write[i] = (int16_t)(p_audio_data[i] >> 16);
		}
	} else {
		memcpy(block.ptrw(), p_audio_data, audio_block_size);
	}
	pending_audio.push_back(block);

	// Not blocking: waiting for this frame's sample would idle the rest of the pool.
	_drain(false);

	return OK;
}

void MovieWriterCineForm::write_end() {
	if (pool != nullptr) {
		_drain(true); // everything still in flight, or the tail of the movie is lost
		CFHD_StopEncoderPool(pool);
		CFHD_ReleaseEncoderPool(pool);
		pool = nullptr;
	}

	if (segment != nullptr) {
		// Matroska takes its duration from the last timestamp, so the final frame needs one.
		const uint64_t duration_ns = uint64_t(frame_count) * 1000000000ULL / uint64_t(fps ? fps : 1);
		segment->set_duration(double(duration_ns) / 1000000.0);
		if (!segment->Finalize()) {
			ERR_PRINT("Could not finalize the Matroska segment. The file may be unreadable.");
		}
		memdelete(segment);
		segment = nullptr;
	}
	if (mkv_writer != nullptr) {
		memdelete(mkv_writer);
		mkv_writer = nullptr;
	}
	f.unref();

	staging.clear();
	pending_audio.clear();
}

MovieWriterCineForm::MovieWriterCineForm() {
	mix_rate = GLOBAL_GET("editor/movie_writer/mix_rate");
	speaker_mode = AudioServer::SpeakerMode(int(GLOBAL_GET("editor/movie_writer/speaker_mode")));
	audio_bit_depth = GLOBAL_GET("editor/movie_writer/audio_bit_depth");
	quality_index = CLAMP(int(GLOBAL_GET("editor/movie_writer/cineform/quality")), 0, 5);
	keep_alpha = GLOBAL_GET("editor/movie_writer/cineform/keep_alpha");
}

MovieWriterCineForm::~MovieWriterCineForm() {
	if (pool != nullptr) {
		CFHD_StopEncoderPool(pool);
		CFHD_ReleaseEncoderPool(pool);
	}
}

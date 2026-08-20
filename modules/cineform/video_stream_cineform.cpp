/**************************************************************************/
/*  video_stream_cineform.cpp                                             */
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

#include "video_stream_cineform.h"

#include "core/object/class_db.h"
#include "scene/resources/image_texture.h"

Error VideoStreamPlaybackCineForm::FileAccessMkvReader::open(const String &p_path) {
	file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		return ERR_CANT_OPEN;
	}
	length = (long long)file->get_length();
	return OK;
}

int VideoStreamPlaybackCineForm::FileAccessMkvReader::Read(long long p_pos, long p_len, unsigned char *p_buf) {
	if (file.is_null() || p_pos < 0 || p_len < 0 || p_pos + p_len > length) {
		return -1;
	}
	file->seek((uint64_t)p_pos);
	return file->get_buffer(p_buf, p_len) == (uint64_t)p_len ? 0 : -1;
}

int VideoStreamPlaybackCineForm::FileAccessMkvReader::Length(long long *r_total, long long *r_available) {
	if (file.is_null()) {
		return -1;
	}
	if (r_total) {
		*r_total = length;
	}
	if (r_available) {
		*r_available = length;
	}
	return 0;
}

Error VideoStreamPlaybackCineForm::set_file(const String &p_path) {
	_clear();
	ERR_FAIL_COND_V(reader.open(p_path) != OK, ERR_CANT_OPEN);

	long long pos = 0;
	mkvparser::EBMLHeader header;
	ERR_FAIL_COND_V_MSG(header.Parse(&reader, pos) < 0, ERR_FILE_UNRECOGNIZED,
			vformat("%s is not a Matroska file.", p_path));

	ERR_FAIL_COND_V(mkvparser::Segment::CreateInstance(&reader, pos, segment) < 0, ERR_FILE_CORRUPT);
	ERR_FAIL_NULL_V(segment, ERR_FILE_CORRUPT);
	ERR_FAIL_COND_V(segment->Load() < 0, ERR_FILE_CORRUPT);

	const mkvparser::Tracks *tracks = segment->GetTracks();
	ERR_FAIL_NULL_V(tracks, ERR_FILE_CORRUPT);
	for (unsigned long i = 0; i < tracks->GetTracksCount(); i++) {
		const mkvparser::Track *track = tracks->GetTrackByIndex(i);
		if (track == nullptr || track->GetType() != mkvparser::Track::kVideo) {
			continue;
		}
		// Matroska carries any codec, so leave a non-CineForm file to another loader.
		const char *codec_id = track->GetCodecId();
		if (codec_id == nullptr || String(codec_id) != "V_MS/VFW/FOURCC") {
			continue;
		}
		size_t private_size = 0;
		const unsigned char *private_data = track->GetCodecPrivate(private_size);
		// biCompression is the fourcc, sixteen bytes into a BITMAPINFOHEADER.
		if (private_data == nullptr || private_size < 20 || memcmp(private_data + 16, "CFHD", 4) != 0) {
			continue;
		}
		const mkvparser::VideoTrack *vt = static_cast<const mkvparser::VideoTrack *>(track);
		video_track = track->GetNumber();
		size = Size2i((int)vt->GetWidth(), (int)vt->GetHeight());
		break;
	}
	ERR_FAIL_COND_V_MSG(video_track < 0, ERR_FILE_UNRECOGNIZED,
			vformat("%s has no CineForm video track.", p_path));

	const mkvparser::SegmentInfo *info = segment->GetInfo();
	if (info && info->GetDuration() > 0) {
		length = double(info->GetDuration()) / 1000000000.0;
	}

	ERR_FAIL_COND_V(CFHD_OpenDecoder(&decoder, nullptr) != CFHD_ERROR_OKAY, ERR_CANT_CREATE);

	texture.instantiate();
	cluster = segment->GetFirst();
	return OK;
}

bool VideoStreamPlaybackCineForm::_advance_to_next_block() {
	while (cluster != nullptr && !cluster->EOS()) {
		if (block_entry == nullptr) {
			if (cluster->GetFirst(block_entry) < 0) {
				return false;
			}
		} else {
			if (cluster->GetNext(block_entry, block_entry) < 0) {
				return false;
			}
		}
		if (block_entry == nullptr || block_entry->EOS()) {
			cluster = segment->GetNext(cluster);
			block_entry = nullptr;
			continue;
		}
		const mkvparser::Block *block = block_entry->GetBlock();
		if (block && block->GetTrackNumber() == video_track) {
			return true;
		}
	}
	return false;
}

bool VideoStreamPlaybackCineForm::_decode_current_block() {
	const mkvparser::Block *block = block_entry ? block_entry->GetBlock() : nullptr;
	if (block == nullptr || block->GetFrameCount() < 1) {
		return false;
	}
	const mkvparser::Block::Frame &frame = block->GetFrame(0);
	sample.resize(frame.len);
	if (frame.Read(&reader, sample.ptrw()) < 0) {
		return false;
	}

	// PrepareToDecode reads the sample header, so it needs the first sample in hand.
	if (decoded.is_empty()) {
		int actual_w = 0, actual_h = 0;
		CFHD_PixelFormat actual_format = CFHD_PIXEL_FORMAT_UNKNOWN;
		const CFHD_Error err = CFHD_PrepareToDecode(decoder, size.width, size.height,
				CFHD_PIXEL_FORMAT_BGRA, CFHD_DECODED_RESOLUTION_FULL, CFHD_DECODING_FLAGS_NONE,
				sample.ptrw(), sample.size(), &actual_w, &actual_h, &actual_format);
		ERR_FAIL_COND_V_MSG(err != CFHD_ERROR_OKAY, false,
				vformat("CFHD_PrepareToDecode failed with code %d.", int(err)));
		size = Size2i(actual_w, actual_h);
		decoded_format = actual_format;
		int32_t pitch = 0;
		CFHD_GetImagePitch(size.width, decoded_format, &pitch);
		decoded_pitch = pitch;
		decoded.resize(pitch * size.height);
	}

	const CFHD_Error err = CFHD_DecodeSample(decoder, sample.ptrw(), sample.size(),
			decoded.ptrw(), decoded_pitch);
	if (err != CFHD_ERROR_OKAY) {
		ERR_PRINT(vformat("CFHD_DecodeSample failed with code %d.", int(err)));
		return false;
	}

	// BGRA out of the decoder, and the rows arrive bottom up, so both are undone here.
	Vector<uint8_t> rgba;
	rgba.resize(size.width * size.height * 4);
	uint8_t *dst = rgba.ptrw();
	const uint8_t *src = decoded.ptr();
	for (int y = 0; y < size.height; y++) {
		const uint8_t *s = src + decoded_pitch * (size.height - 1 - y);
		uint8_t *d = dst + size.width * 4 * y;
		for (int x = 0; x < size.width; x++) {
			d[0] = s[2];
			d[1] = s[1];
			d[2] = s[0];
			d[3] = s[3];
			s += 4;
			d += 4;
		}
	}

	Ref<Image> img = Image::create_from_data(size.width, size.height, false, Image::FORMAT_RGBA8, rgba);
	if (img.is_null()) {
		return false;
	}
	if (texture->get_size() != Size2(size)) {
		texture->set_image(img);
	} else {
		texture->update(img);
	}
	return true;
}

void VideoStreamPlaybackCineForm::update(double p_delta) {
	if (!playing || paused || eof) {
		return;
	}
	time += p_delta;

	// Read the block's timestamp before decoding it, or playback runs a frame ahead.
	while (true) {
		if (!block_pending) {
			if (!_advance_to_next_block()) {
				eof = true;
				playing = false;
				return;
			}
			block_pending = true;
		}
		const mkvparser::Block *block = block_entry ? block_entry->GetBlock() : nullptr;
		if (block == nullptr) {
			eof = true;
			playing = false;
			return;
		}
		const double block_time = double(block->GetTime(cluster)) / 1000000000.0;
		if (block_time > time) {
			return;
		}
		if (!_decode_current_block()) {
			eof = true;
			playing = false;
			return;
		}
		block_pending = false;
	}
}

void VideoStreamPlaybackCineForm::play() {
	stop();
	playing = true;
	eof = false;
	time = 0.0;
	next_frame_time = 0.0;
}

void VideoStreamPlaybackCineForm::stop() {
	playing = false;
	time = 0.0;
	next_frame_time = 0.0;
	if (segment != nullptr) {
		cluster = segment->GetFirst();
	}
	block_entry = nullptr;
	block_pending = false;
}

bool VideoStreamPlaybackCineForm::is_playing() const {
	return playing;
}

void VideoStreamPlaybackCineForm::set_paused(bool p_paused) {
	paused = p_paused;
}

bool VideoStreamPlaybackCineForm::is_paused() const {
	return paused;
}

double VideoStreamPlaybackCineForm::get_length() const {
	return length;
}

double VideoStreamPlaybackCineForm::get_playback_position() const {
	return time;
}

void VideoStreamPlaybackCineForm::seek(double p_time) {
	// Every frame is a keyframe, so seeking is a walk to the cluster holding the time.
	if (segment == nullptr) {
		return;
	}
	const mkvparser::Tracks *tracks = segment->GetTracks();
	const mkvparser::Track *track = tracks ? tracks->GetTrackByNumber(video_track) : nullptr;
	if (track == nullptr) {
		return;
	}
	cluster = segment->FindCluster((long long)(p_time * 1000000000.0));
	block_entry = nullptr;
	block_pending = false;
	time = p_time;
	next_frame_time = p_time;
	eof = false;
}

void VideoStreamPlaybackCineForm::set_audio_track(int p_idx) {
}

Ref<Texture2D> VideoStreamPlaybackCineForm::get_texture() const {
	return texture;
}

int VideoStreamPlaybackCineForm::get_channels() const {
	return 0;
}

int VideoStreamPlaybackCineForm::get_mix_rate() const {
	return 0;
}

void VideoStreamPlaybackCineForm::_clear() {
	if (decoder != nullptr) {
		CFHD_CloseDecoder(decoder);
		decoder = nullptr;
	}
	if (segment != nullptr) {
		delete segment;
		segment = nullptr;
	}
	cluster = nullptr;
	block_entry = nullptr;
	video_track = -1;
	decoded.clear();
	sample.clear();
	length = 0.0;
	eof = false;
}

VideoStreamPlaybackCineForm::VideoStreamPlaybackCineForm() {}

VideoStreamPlaybackCineForm::~VideoStreamPlaybackCineForm() {
	_clear();
}

Ref<Resource> ResourceFormatLoaderCineForm::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	if (f.is_null()) {
		if (r_error) {
			*r_error = ERR_CANT_OPEN;
		}
		return Ref<Resource>();
	}
	// Loading verifies the codec, so another loader can claim a non-CineForm Matroska.
	Ref<VideoStreamPlaybackCineForm> probe;
	probe.instantiate();
	if (probe->set_file(p_path) != OK) {
		if (r_error) {
			*r_error = ERR_FILE_UNRECOGNIZED;
		}
		return Ref<Resource>();
	}
	Ref<VideoStreamCineForm> stream;
	stream.instantiate();
	stream->set_file(p_path);
	if (r_error) {
		*r_error = OK;
	}
	return stream;
}

void ResourceFormatLoaderCineForm::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("cfhd");
	p_extensions->push_back("mkv");
}

bool ResourceFormatLoaderCineForm::handles_type(const String &p_type) const {
	return ClassDB::is_parent_class(p_type, "VideoStream");
}

String ResourceFormatLoaderCineForm::get_resource_type(const String &p_path) const {
	const String ext = p_path.get_extension().to_lower();
	return (ext == "cfhd" || ext == "mkv") ? "VideoStreamCineForm" : String();
}

/**************************************************************************/
/*  video_stream_native.cpp                                               */
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

#include "video_stream_native.h"

#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "servers/rendering/rendering_device_binds.h"
#include "servers/rendering/rendering_server.h"

#include "modules/native_media/ycbcr_sampler.glsl.gen.h"

VideoStreamPlaybackNative::VideoStreamPlaybackNative() {
	backend = NativeMediaBackend::create();
	rd = RenderingServer::get_singleton() ? RenderingServer::get_singleton()->get_rendering_device() : nullptr;
	// Pre-create the Texture2DRD so VideoStreamPlayer (which caches the
	// texture once at set_stream time) gets the same Ref that set_texture_rd_rid
	// keeps populating. Without this it grabs the 1x1 placeholder and never sees
	// the real compute output.
	texture.instantiate();
}

VideoStreamPlaybackNative::~VideoStreamPlaybackNative() {
	_release_gpu_pipeline();
	if (backend) {
		memdelete(backend);
		backend = nullptr;
	}
}

void VideoStreamPlaybackNative::_open_backend() {
	if (!backend || native_stream.is_null() || opened) {
		return;
	}
	if (backend->open(native_stream->get_data(), (NativeMediaBackend::ContainerFormat)native_stream->get_container_hint()) != OK) {
		return;
	}
	audio_info = backend->get_audio_info();
	video_info = backend->get_video_info();
	opened = true;

	// The backend holds the stream's dimensions once open() prerolls, so the
	// output texture takes its real size here, up front. This mirrors Theora,
	// which sizes its ImageTexture during setup: VideoStreamPlayer reads
	// get_texture()->get_size() synchronously right after instantiate_playback()
	// and feeds it to get_minimum_size(), so a placeholder-sized texture leaves
	// the `expand = false` auto-resize (and any layout keyed to the video's
	// native size) broken. The path serves every NativeMediaBackend that reports
	// size.
	if (video_info.present && video_info.width > 0 && video_info.height > 0) {
		_ensure_gpu_pipeline();
	}
}

bool VideoStreamPlaybackNative::_ensure_gpu_pipeline() {
	if (!rd) {
		print_verbose("native_media: no RenderingDevice (Compatibility renderer or headless).");
		return false;
	}
	if (!video_info.present || video_info.width == 0 || video_info.height == 0) {
		print_verbose(vformat("native_media: video_info not ready (present=%d size=%dx%d).",
				video_info.present, video_info.width, video_info.height));
		return false;
	}
	if (rd_pipeline.is_valid() && rd_tex_width == video_info.width && rd_tex_height == video_info.height) {
		return true;
	}

	_release_gpu_pipeline();

	Ref<RDShaderFile> shader_file;
	shader_file.instantiate();
	Error err = shader_file->parse_versions_from_text(ycbcr_sampler_shader_glsl);
	if (err != OK) {
		ERR_PRINT(vformat("native_media: ycbcr_sampler.glsl parse_versions_from_text failed (err=%d).", err));
		return false;
	}
	Ref<RDShaderSPIRV> spirv = shader_file->get_spirv();
	if (spirv.is_null()) {
		ERR_PRINT("native_media: get_spirv returned null for ycbcr_sampler.glsl.");
		return false;
	}
	rd_shader = rd->shader_create_from_spirv(spirv->get_stages());
	if (!rd_shader.is_valid()) {
		ERR_PRINT("native_media: shader_create_from_spirv failed for ycbcr_sampler.glsl.");
		return false;
	}
	rd_pipeline = rd->compute_pipeline_create(rd_shader);
	if (!rd_pipeline.is_valid()) {
		ERR_PRINT("native_media: compute_pipeline_create failed.");
		_release_gpu_pipeline();
		return false;
	}

	RD::TextureFormat y_fmt;
	y_fmt.format = RD::DATA_FORMAT_R8_UNORM;
	y_fmt.width = video_info.width;
	y_fmt.height = video_info.height;
	y_fmt.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_UPDATE_BIT;
	rd_y_tex = rd->texture_create(y_fmt, RD::TextureView());

	RD::TextureFormat cbcr_fmt;
	cbcr_fmt.format = RD::DATA_FORMAT_R8G8_UNORM;
	cbcr_fmt.width = video_info.width / 2;
	cbcr_fmt.height = video_info.height / 2;
	cbcr_fmt.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_UPDATE_BIT;
	rd_cbcr_tex = rd->texture_create(cbcr_fmt, RD::TextureView());

	RD::TextureFormat dst_fmt;
	dst_fmt.format = RD::DATA_FORMAT_R8G8B8A8_UNORM;
	dst_fmt.width = video_info.width;
	dst_fmt.height = video_info.height;
	// CAN_COPY_FROM lets get_video_texture().get_image() read the frame back to
	// the CPU (needed for ShaderMotion decoding); without it texture_get_data()
	// refuses the read.
	dst_fmt.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT;
	rd_dst_tex = rd->texture_create(dst_fmt, RD::TextureView());

	if (!rd_y_tex.is_valid() || !rd_cbcr_tex.is_valid() || !rd_dst_tex.is_valid()) {
		ERR_PRINT(vformat("native_media: texture_create failed (y=%d cbcr=%d dst=%d) at %dx%d.",
				rd_y_tex.is_valid(), rd_cbcr_tex.is_valid(), rd_dst_tex.is_valid(),
				video_info.width, video_info.height));
		_release_gpu_pipeline();
		return false;
	}

	RID linear_sampler = rd->sampler_create(RD::SamplerState());

	Vector<RD::Uniform> uniforms;
	{
		RD::Uniform u;
		u.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
		u.binding = 0;
		u.append_id(linear_sampler);
		u.append_id(rd_y_tex);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
		u.binding = 1;
		u.append_id(linear_sampler);
		u.append_id(rd_cbcr_tex);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.uniform_type = RD::UNIFORM_TYPE_IMAGE;
		u.binding = 2;
		u.append_id(rd_dst_tex);
		uniforms.push_back(u);
	}
	rd_uniform_set = rd->uniform_set_create(uniforms, rd_shader, 0);
	if (!rd_uniform_set.is_valid()) {
		ERR_PRINT("native_media: uniform_set_create failed for ycbcr_sampler.");
		_release_gpu_pipeline();
		return false;
	}

	rd_tex_width = video_info.width;
	rd_tex_height = video_info.height;

	if (texture.is_null()) {
		texture.instantiate();
	}
	texture->set_texture_rd_rid(rd_dst_tex);

	print_verbose(vformat("native_media: GPU pipeline ready (%dx%d, RGBA8 output Texture2DRD).",
			rd_tex_width, rd_tex_height));
	return true;
}

void VideoStreamPlaybackNative::_release_gpu_pipeline() {
	if (!rd) {
		return;
	}
	if (rd_uniform_set.is_valid()) {
		rd->free_rid(rd_uniform_set);
		rd_uniform_set = RID();
	}
	if (rd_dst_tex.is_valid()) {
		rd->free_rid(rd_dst_tex);
		rd_dst_tex = RID();
	}
	// Zero-copy path resources (disabled until per-plane texture_create_shared is fixed).
	// if (rd_imported_uniform_set.is_valid()) {
	// 	rd->free_rid(rd_imported_uniform_set);
	// 	rd_imported_uniform_set = RID();
	// }
	// if (rd_imported_y_view.is_valid()) {
	// 	rd->free_rid(rd_imported_y_view);
	// 	rd_imported_y_view = RID();
	// }
	// if (rd_imported_cbcr_view.is_valid()) {
	// 	rd->free_rid(rd_imported_cbcr_view);
	// 	rd_imported_cbcr_view = RID();
	// }
	// if (rd_imported_sampler.is_valid()) {
	// 	rd->free_rid(rd_imported_sampler);
	// 	rd_imported_sampler = RID();
	// }
	// rd_imported_source = RID();
	if (rd_cbcr_tex.is_valid()) {
		rd->free_rid(rd_cbcr_tex);
		rd_cbcr_tex = RID();
	}
	if (rd_y_tex.is_valid()) {
		rd->free_rid(rd_y_tex);
		rd_y_tex = RID();
	}
	if (rd_pipeline.is_valid()) {
		rd->free_rid(rd_pipeline);
		rd_pipeline = RID();
	}
	if (rd_shader.is_valid()) {
		rd->free_rid(rd_shader);
		rd_shader = RID();
	}
	rd_tex_width = 0;
	rd_tex_height = 0;
	if (texture.is_valid()) {
		texture->set_texture_rd_rid(RID());
	}
}

void VideoStreamPlaybackNative::_present_nv12(const uint8_t *p_y, const uint8_t *p_cbcr) {
	if (!_ensure_gpu_pipeline()) {
		return;
	}

	// Trace the first 5 frames to show whether the compute path actually
	// runs. Spammy beyond that.
	static int trace_frames = 0;
	if (trace_frames < 5) {
		print_verbose(vformat("native_media: _present_nv12 frame %d (%dx%d).",
				trace_frames, video_info.width, video_info.height));
		trace_frames++;
	}

	const uint32_t w = video_info.width;
	const uint32_t h = video_info.height;

	// CPU staging upload (used when the zero-copy bridge is unavailable).
	Vector<uint8_t> y_bytes;
	y_bytes.resize(w * h);
	memcpy(y_bytes.ptrw(), p_y, w * h);
	rd->texture_update(rd_y_tex, 0, y_bytes);

	Vector<uint8_t> cbcr_bytes;
	cbcr_bytes.resize(w * h / 2); // (w/2) * (h/2) * 2 channels.
	memcpy(cbcr_bytes.ptrw(), p_cbcr, w * h / 2);
	rd->texture_update(rd_cbcr_tex, 0, cbcr_bytes);

	struct PushConstants {
		uint32_t color_matrix;
		uint32_t full_range;
	} pc;
	pc.color_matrix = 1; // BT.709 default; the backend populates this once colorimetry is plumbed.
	pc.full_range = 0;

	RD::ComputeListID cl = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(cl, rd_pipeline);
	rd->compute_list_bind_uniform_set(cl, rd_uniform_set, 0);
	rd->compute_list_set_push_constant(cl, &pc, sizeof(PushConstants));
	rd->compute_list_dispatch(cl, (w + 7) / 8, (h + 7) / 8, 1);
	rd->compute_list_end();
}

void VideoStreamPlaybackNative::_present_imported(RID p_imported_nv12_rid) {
	// TODO: per-plane texture_create_shared support is missing. With it, this
	// path creates an RD::TextureView with aspect_mask set to
	// VK_IMAGE_ASPECT_PLANE_0/1 and calls rd->texture_create_shared() to
	// build GPU-resident views directly from the imported NV12 texture.
	ERR_PRINT_ONCE("native_media: _present_imported not yet implemented; using CPU staging path.");
}

void VideoStreamPlaybackNative::play() {
	if (playing) {
		return;
	}
	_open_backend();
	playing = true;
	paused = false;
	playback_position = 0.0;
	pending_pts = 0.0;
}

void VideoStreamPlaybackNative::stop() {
	playing = false;
	paused = false;
	playback_position = 0.0;
	if (backend) {
		// Rewind without tearing down the pipeline so replay() is cheap.
		backend->reset();
	}
	pending_pts = 0.0;
}

bool VideoStreamPlaybackNative::is_playing() const {
	return playing;
}

void VideoStreamPlaybackNative::set_paused(bool p_paused) {
	paused = p_paused;
}

bool VideoStreamPlaybackNative::is_paused() const {
	return paused;
}

double VideoStreamPlaybackNative::get_length() const {
	return video_info.duration_seconds > 0.0 ? video_info.duration_seconds : audio_info.duration_seconds;
}

double VideoStreamPlaybackNative::get_playback_position() const {
	return playback_position;
}

void VideoStreamPlaybackNative::seek(double p_time) {
	if (backend) {
		backend->seek(p_time);
	}
	playback_position = p_time;
	pending_pts = p_time;
}

void VideoStreamPlaybackNative::set_audio_track(int p_idx) {
	audio_track_idx = p_idx;
}

Ref<Texture2D> VideoStreamPlaybackNative::get_texture() const {
	// Always return our pre-instantiated Texture2DRD. Its underlying RID is
	// updated by _ensure_gpu_pipeline once we have a frame; until then the
	// Texture2DRD itself reports size 0 and VideoStreamPlayer skips drawing
	// (handled by its `if (texture->get_width() == 0) return;` guard in
	// NOTIFICATION_DRAW).
	return texture;
}

void VideoStreamPlaybackNative::update(double p_delta) {
	if (!playing || paused || !backend) {
		return;
	}
	playback_position += p_delta;

	// Stream and track info negotiates asynchronously. open() prerolls, so the
	// common path holds the info, but a preroll past its timeout falls
	// back to the async state machine, where the fetch in _open_backend() lands
	// before present/width/height hold. A re-poll each tick (each query pumps the
	// backend's state machine) carries the values once the caps resolve; absent
	// it, the video texture pipeline stays down and audio stays silent.
	if (!video_info.present) {
		video_info = backend->get_video_info();
	}
	if (!audio_info.present) {
		audio_info = backend->get_audio_info();
	}

	// Confirm the VideoStreamPlayer is actually driving us. Once.
	static bool announced = false;
	if (!announced) {
		print_verbose(vformat("native_media: VideoStreamPlaybackNative::update first tick, dt=%.3f.", p_delta));
		announced = true;
	}

	// Push audio to whatever consumer (VideoStreamPlayer) set the mix
	// callback. Mirror Theora's send_audio pattern: keep a leftover buffer
	// between ticks to avoid losing samples when the consumer's resampler
	// fills mid-pump, and loop until either the resampler refuses more
	// OR the backend has nothing more decoded right now.
	if (mix_callback && audio_info.present && audio_info.channels > 0) {
		const int channels = int(audio_info.channels);

		// Drain leftover first — these are samples the resampler refused
		// last tick (or earlier on this tick).
		if (audio_leftover_count > 0) {
			int accepted = mix_callback(mix_udata,
					audio_leftover.ptr() + audio_leftover_start * channels,
					audio_leftover_count);
			audio_leftover_start += accepted;
			audio_leftover_count -= accepted;
			if (audio_leftover_count > 0) {
				// Still backed up; nothing more to do this tick.
				return;
			}
			audio_leftover_start = 0;
		}

		// Pull more audio from the backend until the resampler refuses or
		// the backend has nothing to give.
		const int CHUNK_FRAMES = 1024;
		AudioFrame audio_buf[CHUNK_FRAMES];
		while (true) {
			int got = backend->decode_audio(audio_buf, CHUNK_FRAMES);
			if (got <= 0) {
				break;
			}
			if (audio_leftover.size() < got * channels) {
				audio_leftover.resize(got * channels);
			}
			float *out = audio_leftover.ptrw();
			for (int i = 0; i < got; i++) {
				out[i * channels + 0] = audio_buf[i].left;
				if (channels > 1) {
					out[i * channels + 1] = audio_buf[i].right;
				}
			}
			int accepted = mix_callback(mix_udata, out, got);
			if (accepted < got) {
				audio_leftover_start = accepted;
				audio_leftover_count = got - accepted;
				break;
			}
		}
	}

	// Stay one frame ahead: catch up if PTS lags more than one frame interval.
	const double frame_interval = video_info.frame_rate > 0.0f
			? 1.0 / double(video_info.frame_rate)
			: 1.0 / 30.0;

	while (playback_position >= pending_pts) {
		double pts = 0.0;

		// TODO: zero-copy imported NV12 path stays disabled until per-plane
		// texture_create_shared works. Every frame falls through to CPU staging.
		// RID imported = backend->decode_video_frame_imported(&pts);
		// if (imported.is_valid()) {
		// 	_present_imported(imported);
		// 	pending_pts = pts > 0.0 ? pts + frame_interval : pending_pts + frame_interval;
		// 	continue;
		// }

		// CPU staging path (the only path while zero-copy stays disabled).
		Error err = backend->decode_video_frame(&nv12_scratch, &pts);
		if (err == ERR_BUSY) {
			break;
		}
		if (err == ERR_FILE_EOF) {
			playing = false;
			break;
		}
		if (err != OK) {
			break;
		}
		pending_pts = pts > 0.0 ? pts + frame_interval : pending_pts + frame_interval;

		const uint32_t w = video_info.width;
		const uint32_t h = video_info.height;
		if (uint64_t(nv12_scratch.size()) < uint64_t(w) * h * 3 / 2) {
			break;
		}
		_present_nv12(nv12_scratch.ptr(), nv12_scratch.ptr() + w * h);
	}
}

int VideoStreamPlaybackNative::get_channels() const {
	return audio_info.channels;
}

int VideoStreamPlaybackNative::get_mix_rate() const {
	return audio_info.sample_rate;
}

// ----- VideoStreamNative -----

Ref<VideoStreamNative> VideoStreamNative::load_from_buffer(const Vector<uint8_t> &p_buffer, int p_hint) {
	Ref<VideoStreamNative> stream;
	stream.instantiate();
	stream->set_container_hint(p_hint);
	stream->set_data(p_buffer);
	return stream;
}

Ref<VideoStreamNative> VideoStreamNative::load_from_file(const String &p_path) {
	Vector<uint8_t> buf = FileAccess::get_file_as_bytes(p_path);
	return load_from_buffer(buf);
}

void VideoStreamNative::set_data(const Vector<uint8_t> &p_data) {
	data = p_data;
}

Vector<uint8_t> VideoStreamNative::get_data() const {
	return data;
}

void VideoStreamNative::set_container_hint(int p_hint) {
	container_hint = (NativeMediaBackend::ContainerFormat)p_hint;
}

int VideoStreamNative::get_container_hint() const {
	return container_hint;
}

double VideoStreamNative::get_frame_rate() {
	if (frame_rate_cache > 0.0) {
		return frame_rate_cache;
	}
	if (data.is_empty()) {
		return 0.0;
	}
	// Open a throwaway backend just to read the container's video info. Cached
	// so repeated GDScript queries don't re-open the decoder.
	NativeMediaBackend *probe = NativeMediaBackend::create();
	if (!probe) {
		return 0.0;
	}
	if (probe->open(data, container_hint) == OK) {
		frame_rate_cache = probe->get_video_info().frame_rate;
	}
	memdelete(probe);
	return frame_rate_cache;
}

Ref<VideoStreamPlayback> VideoStreamNative::instantiate_playback() {
	Ref<VideoStreamPlaybackNative> playback;
	playback.instantiate();
	playback->native_stream = Ref<VideoStreamNative>(this);
	// Open the backend eagerly so VideoStreamPlayer's immediate
	// get_channels() / get_mix_rate() query sees real values — otherwise
	// it sees 0 channels and never installs the audio mix callback,
	// leaving playback silent.
	playback->_open_backend();
	return playback;
}

void VideoStreamNative::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_data", "data"), &VideoStreamNative::set_data);
	ClassDB::bind_method(D_METHOD("get_data"), &VideoStreamNative::get_data);
	ClassDB::bind_method(D_METHOD("set_container_hint", "hint"), &VideoStreamNative::set_container_hint);
	ClassDB::bind_method(D_METHOD("get_container_hint"), &VideoStreamNative::get_container_hint);
	ClassDB::bind_method(D_METHOD("get_frame_rate"), &VideoStreamNative::get_frame_rate);

	ClassDB::bind_static_method("VideoStreamNative", D_METHOD("load_from_buffer", "buffer", "hint"), &VideoStreamNative::load_from_buffer, DEFVAL((int)NativeMediaBackend::CONTAINER_AUTO));
	ClassDB::bind_static_method("VideoStreamNative", D_METHOD("load_from_file", "path"), &VideoStreamNative::load_from_file);

	ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "data"), "set_data", "get_data");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "container_hint", PROPERTY_HINT_ENUM, "Auto,WebM,MP4,Ogg,WAV"), "set_container_hint", "get_container_hint");
}

VideoStreamNative::VideoStreamNative() {}
VideoStreamNative::~VideoStreamNative() {}

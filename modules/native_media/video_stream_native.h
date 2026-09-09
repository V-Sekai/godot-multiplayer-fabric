/**************************************************************************/
/*  video_stream_native.h                                                 */
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

#include "scene/resources/image_texture.h"
#include "scene/resources/texture.h"
#include "scene/resources/texture_rd.h"
#include "scene/resources/video_stream.h"
#include "servers/rendering/rendering_device.h"

#include "modules/native_media/native_media_backend.h"

class VideoStreamNative;

// Playback engine: pulls NV12 frames from the OS decoder, runs them through
// the ycbcr_sampler.glsl compute shader on the active RenderingDevice, and
// publishes the result as a Texture2DRD that VideoStreamPlayer can sample
// like any other texture. Designed for 8K-on-many-screens scale —
// per-pixel CPU work is forbidden.
class VideoStreamPlaybackNative : public VideoStreamPlayback {
	GDCLASS(VideoStreamPlaybackNative, VideoStreamPlayback);

	NativeMediaBackend *backend = nullptr;

	// Interleaved float audio that didn't fit through mix_callback last tick.
	// Drained first on the next update before pulling more from the backend.
	Vector<float> audio_leftover;
	int audio_leftover_start = 0;
	int audio_leftover_count = 0; // in frames, not floats.

	bool playing = false;
	bool paused = false;
	bool opened = false; // backend->open() has been called for the current
						 // resource data; stop() preserves so replay is cheap.
	double playback_position = 0.0;
	double pending_pts = 0.0;
	int audio_track_idx = 0;

	NativeMediaBackend::AudioInfo audio_info;
	NativeMediaBackend::VideoInfo video_info;

	// Reusable scratch for one decoded frame (planar NV12).
	Vector<uint8_t> nv12_scratch;

	// RD-side GPU pipeline (RenderingDevice backends: Vulkan, D3D12, Metal).
	RenderingDevice *rd = nullptr;
	RID rd_shader;
	RID rd_pipeline;
	RID rd_y_tex;
	RID rd_cbcr_tex;
	RID rd_dst_tex;
	RID rd_uniform_set;
	uint32_t rd_tex_width = 0;
	uint32_t rd_tex_height = 0;

	// Zero-copy imported path; inactive while per-plane texture_create_shared lacks support.
	// RID rd_imported_y_view;
	// RID rd_imported_cbcr_view;
	// RID rd_imported_uniform_set;
	// RID rd_imported_sampler;
	// RID rd_imported_source;

	Ref<Texture2DRD> texture;
	Ref<ImageTexture> placeholder_texture; // For headless / Compatibility builds.

	friend class VideoStreamNative;
	Ref<VideoStreamNative> native_stream;

	bool _ensure_gpu_pipeline();
	void _release_gpu_pipeline();
	void _present_nv12(const uint8_t *p_y, const uint8_t *p_cbcr);
	void _present_imported(RID p_imported_nv12_rid);
	void _open_backend();

public:
	virtual void play() override;
	virtual void stop() override;
	virtual bool is_playing() const override;

	virtual void set_paused(bool p_paused) override;
	virtual bool is_paused() const override;

	virtual double get_length() const override;
	virtual double get_playback_position() const override;
	virtual void seek(double p_time) override;

	virtual void set_audio_track(int p_idx) override;

	virtual Ref<Texture2D> get_texture() const override;
	virtual void update(double p_delta) override;

	virtual int get_channels() const override;
	virtual int get_mix_rate() const override;

	VideoStreamPlaybackNative();
	~VideoStreamPlaybackNative();
};

class VideoStreamNative : public VideoStream {
	GDCLASS(VideoStreamNative, VideoStream);
	OBJ_SAVE_TYPE(VideoStream);
	RES_BASE_EXTENSION("nvidstr");

	friend class VideoStreamPlaybackNative;

	Vector<uint8_t> data;
	NativeMediaBackend::ContainerFormat container_hint = NativeMediaBackend::CONTAINER_AUTO;
	double frame_rate_cache = 0.0; // Lazily filled by get_frame_rate().

protected:
	static void _bind_methods();

public:
	static Ref<VideoStreamNative> load_from_buffer(const Vector<uint8_t> &p_buffer, int p_hint = NativeMediaBackend::CONTAINER_AUTO);
	static Ref<VideoStreamNative> load_from_file(const String &p_path);

	void set_data(const Vector<uint8_t> &p_data);
	Vector<uint8_t> get_data() const;

	void set_container_hint(int p_hint);
	int get_container_hint() const;

	// Source video frame rate in Hz, or 0 if unknown. Opens the backend lazily
	// on the in-memory data and caches the result.
	double get_frame_rate();

	virtual Ref<VideoStreamPlayback> instantiate_playback() override;

	VideoStreamNative();
	~VideoStreamNative();
};

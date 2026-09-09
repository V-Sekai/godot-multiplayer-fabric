/**************************************************************************/
/*  native_media_backend.h                                                */
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

#include "core/error/error_list.h"
#include "core/math/audio_frame.h"
#include "core/math/vector2i.h"
#include "core/string/ustring.h"
#include "core/templates/rid.h"
#include "core/templates/vector.h"

// OS-native media decoder backend.
// One concrete subclass per platform — Media Foundation (Windows), GStreamer
// (Linux/Steam Deck), AVFoundation (Apple), MediaCodec (Android). The factory
// NativeMediaBackend::create() picks the right one at compile time.
//
// Each backend decodes both audio and video from a container buffer. It serves
// as the cross-platform fallback when Vulkan video decode is unavailable; the
// Vulkan path is the fast path on hardware that supports
// VK_KHR_video_decode_*.
//
// Architecture reference (do not link against): Meta Ocean's media subsystem.
class NativeMediaBackend {
public:
	enum ContainerFormat {
		CONTAINER_AUTO,
		CONTAINER_WEBM,
		CONTAINER_MP4,
		CONTAINER_OGG,
		CONTAINER_WAV,
	};

	enum VideoFrameFormat {
		VIDEO_FRAME_NONE,
		VIDEO_FRAME_RGBA8,
		VIDEO_FRAME_NV12,
		VIDEO_FRAME_YUV420P,
	};

	struct AudioInfo {
		uint32_t sample_rate = 0;
		uint32_t channels = 0;
		double duration_seconds = 0.0;
		bool present = false;
	};

	struct VideoInfo {
		uint32_t width = 0;
		uint32_t height = 0;
		float frame_rate = 0.0f;
		double duration_seconds = 0.0;
		VideoFrameFormat preferred_format = VIDEO_FRAME_NONE;
		bool present = false;
	};

	virtual ~NativeMediaBackend() = default;

	virtual Error open(const Vector<uint8_t> &p_data, ContainerFormat p_hint = CONTAINER_AUTO) = 0;

	virtual AudioInfo get_audio_info() const = 0;
	virtual VideoInfo get_video_info() const = 0;

	// Decode up to p_frames interleaved float audio frames. Returns the count
	// written; <0 on error. 0 means "no frames available right now" — could
	// be EOF or just async-decode-in-flight; check is_audio_eof().
	virtual int decode_audio(AudioFrame *p_buffer, int p_frames) = 0;

	// True only when the audio stream has actually run out. Distinguishes
	// real end-of-stream from transient empty pulls (async ReadSample
	// pending, decoder pre-roll, format renegotiation gap). Callers keep
	// their playback active until this is true.
	virtual bool is_audio_eof() const = 0;

	// Decode one video frame in get_video_info().preferred_format. r_pts_seconds
	// is the presentation timestamp.
	// OK on success, ERR_FILE_EOF at end, ERR_BUSY when no frame ready.
	virtual Error decode_video_frame(Vector<uint8_t> *r_buffer, double *r_pts_seconds) = 0;

	// Fast-path variant: if the backend can deliver an NV12 frame as a
	// GPU-resident texture aliased via RenderingDevice::texture_create_from_extension
	// (D3D11 shared NT handle on Windows, DMA-BUF on Linux, IOSurface on
	// Apple, AHardwareBuffer on Android), return the RID for the current
	// frame. Caller samples it directly through the ycbcr compute shader,
	// skipping the CPU NV12 upload. Returns an invalid RID when the
	// fast-path isn't supported on this build/host — caller falls back to
	// decode_video_frame above.
	virtual RID decode_video_frame_imported(double *r_pts_seconds) { return RID(); }

	virtual Error seek(double p_time_seconds) = 0;
	virtual void reset() = 0;

	virtual String get_backend_name() const = 0;

	static NativeMediaBackend *create();
};

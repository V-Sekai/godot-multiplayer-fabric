/**************************************************************************/
/*  gstreamer_backend.h                                                   */
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

#include "modules/native_media/native_media_backend.h"

#if defined(LINUXBSD_ENABLED) && defined(GSTREAMER_ENABLED)

#include "modules/native_media/backends/gstreamer/gst_decls.h"

// Linux / Steam Deck backend. Decoded via GStreamer's decodebin pipeline
// (auto-picks VA-API on AMD RDNA2 for hardware decode).
//
// Self-contained: no -lgstreamer-1.0 at link time, no gstreamer-1.0-dev
// headers needed at build time. GLib + GStreamer function calls go through
// a generate_stubs.py table — dlopen("libgstreamer-1.0.so.0", ...) +
// dlsym at first use. LGPL-clean dynamic loading.
class GStreamerBackend : public NativeMediaBackend {
	GstElement *pipeline = nullptr;
	GstAppSrc *src = nullptr;
	GstAppSink *audio_sink = nullptr;
	GstAppSink *video_sink = nullptr;

	enum PipelineState {
		PS_IDLE, // No data pushed yet.
		PS_PROBING, // PLAYING set, waiting for caps to negotiate.
		PS_READY, // Caps negotiated, decoding.
		PS_ERROR, // Unrecoverable; teardown only.
	};

	PipelineState ps = PS_IDLE;
	AudioInfo audio_info_cache;
	VideoInfo video_info_cache;
	// Fallback rate for the rare case where the appsink caps carry none; the real
	// rate is the source's native rate, read in _probe_negotiated_caps().
	uint32_t output_sample_rate = 44100;
	uint32_t output_channels = 2; // The caps pin stereo; decode_audio() emits stereo AudioFrames.
	bool audio_eof = false;
	bool video_eof = false;

	// Holds the source buffer for the lifetime of the pipeline (zero-copy
	// GstBuffer wrap).
	Vector<uint8_t> input_data;

	// Interleaved PCM frames from the audio appsink that overflow one
	// decode_audio() call. GStreamer buffer sizes do not align to the caller's
	// frame count; the remainder carries forward so every decoded sample reaches
	// the mixer, since a dropped buffer tail speeds playback up.
	Vector<float> audio_residual;
	int audio_residual_read = 0; // Read cursor into audio_residual, in floats.

	static void _on_pad_added(GstElement *p_src, GstPad *p_pad, gpointer p_user);
	bool _build_pipeline();
	void _teardown_pipeline();
	void _probe_negotiated_caps();
	// Non-blocking state-machine pump: poll pipeline state without waiting.
	// Returns true once caps are negotiated (PS_READY); returns false while
	// the pipeline is still PROBING or has errored. Safe to call every
	// public API entry-point.
	bool _pump_state();

public:
	GStreamerBackend();
	~GStreamerBackend();

	virtual Error open(const Vector<uint8_t> &p_data, ContainerFormat p_hint) override;
	virtual AudioInfo get_audio_info() const override;
	virtual VideoInfo get_video_info() const override;
	virtual int decode_audio(AudioFrame *p_buffer, int p_frames) override;
	virtual bool is_audio_eof() const override;
	virtual Error decode_video_frame(Vector<uint8_t> *r_buffer, double *r_pts_seconds) override;
	virtual Error seek(double p_time_seconds) override;
	virtual void reset() override;
	virtual String get_backend_name() const override { return "GStreamer"; }
};

#else

// Built without GSTREAMER_ENABLED define (non-Linux compile paths). Factory
// still returns the class but every entry point reports ERR_UNAVAILABLE.
class GStreamerBackend : public NativeMediaBackend {
public:
	virtual Error open(const Vector<uint8_t> &, ContainerFormat) override { return ERR_UNAVAILABLE; }
	virtual AudioInfo get_audio_info() const override { return AudioInfo(); }
	virtual VideoInfo get_video_info() const override { return VideoInfo(); }
	virtual int decode_audio(AudioFrame *, int) override { return 0; }
	virtual bool is_audio_eof() const override { return true; }
	virtual Error decode_video_frame(Vector<uint8_t> *, double *) override { return ERR_UNAVAILABLE; }
	virtual Error seek(double) override { return ERR_UNAVAILABLE; }
	virtual void reset() override {}
	virtual String get_backend_name() const override { return "GStreamer (unavailable)"; }
};

#endif // LINUXBSD_ENABLED && GSTREAMER_ENABLED

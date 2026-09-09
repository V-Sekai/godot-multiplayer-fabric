/**************************************************************************/
/*  mediafoundation_backend.h                                             */
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

#ifdef WINDOWS_ENABLED

#include "core/os/mutex.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objbase.h>

class MFAsyncCallback; // Forward decl — defined in the .cpp.

// Windows Media Foundation backend.
//
// IMFSourceReader runs in async mode (MF_SOURCE_READER_ASYNC_CALLBACK) so
// decode_audio / decode_video_frame never block the caller. Per
// feedback_no-blocking-waits: the state machine drives reads, not real-time
// timeouts. WMF dispatches OnReadSample on its worker thread, samples land
// in mutex-protected queues, and the public decode entry points drain
// those queues without waiting on anything.
//
// The audio decode path derives from vsk-wmf-audio-4.5
// (Mark Kuo <starryalley@gmail.com>, d359cc0533).

class MediaFoundationBackend : public NativeMediaBackend {
	friend class MFAsyncCallback;

	IStream *istream = nullptr;
	IMFByteStream *byte_stream = nullptr;
	IMFSourceReader *reader = nullptr;
	MFAsyncCallback *callback = nullptr;

	AudioInfo audio_info_cache;
	VideoInfo video_info_cache;
	uint32_t output_sample_rate = 44100;
	uint32_t output_channels = 2;
	uint32_t video_default_stride = 0;

	// IMFSourceReaderCallback::OnReadSample reports the REAL stream index
	// the sample came from (0, 1, 2, ...) — not the MF_SOURCE_READER_FIRST_*
	// sentinels passed into ReadSample. open() caches the real indices so
	// dispatch can match them.
	uint32_t audio_stream_index = UINT32_MAX;
	uint32_t video_stream_index = UINT32_MAX;

	// Protects the sample queues and eof flags. WMF's worker thread writes
	// from OnReadSample; the public API drains on the caller's thread.
	mutable Mutex queue_mutex;
	Vector<float> pending_audio_samples; // Interleaved L,R floats.
	int pending_audio_read_index = 0;

	// One pre-decoded video frame buffer, written on the worker thread and
	// consumed by decode_video_frame. Most consumers need only one frame
	// per tick; a deeper queue would require a ring buffer here.
	Vector<uint8_t> pending_video_frame;
	double pending_video_pts = 0.0;
	bool video_frame_ready = false;

	bool audio_eof = false;
	bool video_eof = false;

	bool audio_request_pending = false; // True between ReadSample and OnReadSample.
	bool video_request_pending = false;

	Error _configure_audio_output();
	Error _configure_video_output();
	Error _request_next_audio_sample(); // Issues a non-blocking ReadSample.
	Error _request_next_video_sample();
	void _on_audio_sample(DWORD dwStreamFlags, IMFSample *sample); // Worker-thread.
	void _on_video_sample(DWORD dwStreamFlags, IMFSample *sample); // Worker-thread.
	void _release_reader();

public:
	MediaFoundationBackend();
	~MediaFoundationBackend();

	virtual Error open(const Vector<uint8_t> &p_data, ContainerFormat p_hint) override;
	virtual AudioInfo get_audio_info() const override;
	virtual VideoInfo get_video_info() const override;
	virtual int decode_audio(AudioFrame *p_buffer, int p_frames) override;
	virtual bool is_audio_eof() const override;
	virtual Error decode_video_frame(Vector<uint8_t> *r_buffer, double *r_pts_seconds) override;
	virtual RID decode_video_frame_imported(double *r_pts_seconds) override { return RID(); }
	virtual Error seek(double p_time_seconds) override;
	virtual void reset() override;
	virtual String get_backend_name() const override { return "MediaFoundation"; }
};

#else

// Non-Windows compile path: factory uses WINDOWS_ENABLED so this is unreachable
// at runtime, but the header still has to parse on cross-compiles.
class MediaFoundationBackend : public NativeMediaBackend {
public:
	virtual Error open(const Vector<uint8_t> &, ContainerFormat) override { return ERR_UNAVAILABLE; }
	virtual AudioInfo get_audio_info() const override { return AudioInfo(); }
	virtual VideoInfo get_video_info() const override { return VideoInfo(); }
	virtual int decode_audio(AudioFrame *, int) override { return 0; }
	virtual bool is_audio_eof() const override { return true; }
	virtual Error decode_video_frame(Vector<uint8_t> *, double *) override { return ERR_UNAVAILABLE; }
	virtual Error seek(double) override { return ERR_UNAVAILABLE; }
	virtual void reset() override {}
	virtual String get_backend_name() const override { return "MediaFoundation (unavailable)"; }
};

#endif // WINDOWS_ENABLED

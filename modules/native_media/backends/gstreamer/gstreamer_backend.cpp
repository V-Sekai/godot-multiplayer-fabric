/**************************************************************************/
/*  gstreamer_backend.cpp                                                 */
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

#include "gstreamer_backend.h"

#if defined(LINUXBSD_ENABLED) && defined(GSTREAMER_ENABLED)

#include "core/error/error_macros.h"
#include "core/os/mutex.h"
// For vformat/print_verbose argument packing.
#include "core/variant/variant.h" // IWYU pragma: keep

#include "modules/native_media/backends/gstreamer/gstreamer_stubs.h"

#include <dlfcn.h>

#include <cstring>

namespace stubs = modules_native_media_backends_gstreamer;

namespace {

Mutex &_gst_lifecycle_mutex() {
	static Mutex m;
	return m;
}
bool _gst_init_attempted = false;
bool _gst_init_succeeded = false;

bool _ensure_gst_loaded() {
	MutexLock lock(_gst_lifecycle_mutex());
	if (_gst_init_attempted) {
		return _gst_init_succeeded;
	}
	_gst_init_attempted = true;

	// The stub path map gives each module one library. The generator emits one
	// module per .sigs file, each opening a single library: it tries the listed
	// paths in order, stops at the first that dlopen()s, then dlsym()s that
	// module's symbols from that one handle, so each module names the library
	// that exports its symbols — the list pools nothing across modules.
	//
	// gst_app_* symbols live in libgstapp-1.0.so.0, which libgstreamer does not
	// depend on, so they take their own module. The GLib/GObject symbols
	// (g_free, g_signal_connect_data, ...) resolve through the libgstreamer
	// handle because libgstreamer-1.0.so.0 links libgobject + libglib, and
	// dlsym() searches a handle's dependency tree.
	stubs::StubPathMap path_map;
	path_map[stubs::kModuleGstreamer] = {
		"libgstreamer-1.0.so.0",
	};
	path_map[stubs::kModuleGstreamerapp] = {
		"libgstapp-1.0.so.0",
	};

	if (!stubs::InitializeStubs(path_map)) {
		print_verbose("native_media: GStreamer runtime not available; backend disabled.");
		return false;
	}

	int argc = 0;
	gst_init(&argc, nullptr);
	_gst_init_succeeded = true;
	return true;
}

} // namespace

GStreamerBackend::GStreamerBackend() {
	_ensure_gst_loaded();
}

GStreamerBackend::~GStreamerBackend() {
	_teardown_pipeline();
}

void GStreamerBackend::_teardown_pipeline() {
	if (!_gst_init_succeeded) {
		return;
	}
	if (pipeline) {
		// set_state(NULL) returns immediately; the actual teardown runs on
		// GStreamer's worker threads. gst_object_unref releases the reference,
		// and the pipeline finishes cleaning itself up.
		gst_element_set_state(pipeline, GST_STATE_NULL);
		gst_object_unref(pipeline);
		pipeline = nullptr;
	}
	src = nullptr;
	audio_sink = nullptr;
	video_sink = nullptr;
	audio_eof = false;
	video_eof = false;
	ps = PS_IDLE;
	audio_info_cache = AudioInfo();
	video_info_cache = VideoInfo();
	audio_residual.clear();
	audio_residual_read = 0;
}

bool GStreamerBackend::_pump_state() {
	if (ps == PS_READY) {
		return true;
	}
	if (ps != PS_PROBING || !pipeline) {
		return false;
	}

	// A timeout of 0 makes the state query non-blocking: it returns immediately
	// whether or not the pipeline has finished its async state change.
	GstState state = GST_STATE_NULL;
	GstState pending = GST_STATE_NULL;
	GstStateChangeReturn sc = gst_element_get_state(pipeline, &state, &pending, 0);
	if (sc == GST_STATE_CHANGE_FAILURE) {
		ps = PS_ERROR;
		return false;
	}
	if (sc == GST_STATE_CHANGE_ASYNC) {
		// Still transitioning; the caller retries next tick.
		return false;
	}
	// SUCCESS or NO_PREROLL — the pipeline is at its target state.
	_probe_negotiated_caps();
	ps = PS_READY;
	return true;
}

void GStreamerBackend::_on_pad_added(GstElement *p_src, GstPad *p_pad, gpointer p_user) {
	GStreamerBackend *self = static_cast<GStreamerBackend *>(p_user);
	GstCaps *caps = gst_pad_get_current_caps(p_pad);
	if (!caps) {
		return;
	}
	const GstStructure *s = gst_caps_get_structure(caps, 0);
	const gchar *name = gst_structure_get_name(s);

	GstElement *target_head = nullptr;
	if (g_str_has_prefix(name, "audio/")) {
		target_head = gst_bin_get_by_name(GST_BIN(self->pipeline), "audio_conv");
	} else if (g_str_has_prefix(name, "video/")) {
		target_head = gst_bin_get_by_name(GST_BIN(self->pipeline), "video_conv");
	}
	gst_caps_unref(caps);

	if (!target_head) {
		return;
	}
	GstPad *sink_pad = gst_element_get_static_pad(target_head, "sink");
	if (sink_pad && !gst_pad_is_linked(sink_pad)) {
		gst_pad_link(p_pad, sink_pad);
	}
	if (sink_pad) {
		gst_object_unref(sink_pad);
	}
	gst_object_unref(target_head);
}

bool GStreamerBackend::_build_pipeline() {
	// The audio appsink carries the source's native sample rate, and
	// get_audio_info() reports it; VideoStreamPlayer's resampler converts that
	// to AudioServer::get_mix_rate(). This matches VideoStreamTheora, which
	// reports vi.rate. The pipeline pins no output rate: Movie Maker mode runs
	// the audio driver at editor/movie_writer/mix_rate, a different value from
	// audio/driver/mix_rate, so a rate captured once at open() mismatches the
	// driver the movie writer pulls from. The caps pin stereo F32LE because
	// decode_audio() emits stereo AudioFrames.
	gchar *desc = g_strdup_printf(
			"appsrc name=src is-live=false do-timestamp=false stream-type=0 format=time block=false ! "
			"decodebin name=dec "
			"audioconvert name=audio_conv ! "
			"  capsfilter caps=\"audio/x-raw,format=F32LE,channels=%u\" ! "
			"  appsink name=audio_sink emit-signals=false sync=false max-buffers=4 drop=false "
			"videoconvert name=video_conv ! "
			"  capsfilter caps=\"video/x-raw,format=NV12\" ! "
			"  appsink name=video_sink emit-signals=false sync=false max-buffers=4 drop=false",
			output_channels);

	GError *parse_err = nullptr;
	pipeline = gst_parse_launch(desc, &parse_err);
	g_free(desc);
	if (!pipeline) {
		if (parse_err) {
			ERR_PRINT(String("GStreamer pipeline parse failed: ") + String::utf8(parse_err->message));
			g_error_free(parse_err);
		}
		return false;
	}

	src = (GstAppSrc *)gst_bin_get_by_name(GST_BIN(pipeline), "src");
	audio_sink = (GstAppSink *)gst_bin_get_by_name(GST_BIN(pipeline), "audio_sink");
	video_sink = (GstAppSink *)gst_bin_get_by_name(GST_BIN(pipeline), "video_sink");

	GstElement *dec = gst_bin_get_by_name(GST_BIN(pipeline), "dec");
	// The dynamic pad signal connects via g_signal_connect_data (the
	// g_signal_connect macro from glib expands to this).
	g_signal_connect_data(dec, "pad-added", (GCallback)_on_pad_added, this, nullptr, 0);
	gst_object_unref(dec);
	return true;
}

// Returns the *negotiated* caps flowing into an appsink, ref'd (caller unrefs).
// gst_app_sink_get_caps() returns only the optional filter caps from
// gst_app_sink_set_caps() (NULL here), not the streaming caps, so the read goes
// to the sink pad's current caps.
static GstCaps *_sink_negotiated_caps(GstAppSink *p_sink) {
	if (!p_sink) {
		return nullptr;
	}
	GstPad *pad = gst_element_get_static_pad(GST_ELEMENT(p_sink), "sink");
	if (!pad) {
		return nullptr;
	}
	GstCaps *caps = gst_pad_get_current_caps(pad);
	gst_object_unref(pad);
	return caps;
}

void GStreamerBackend::_probe_negotiated_caps() {
	if (audio_sink) {
		GstCaps *caps = _sink_negotiated_caps(audio_sink);
		if (caps && gst_caps_get_size(caps) > 0) {
			const GstStructure *s = gst_caps_get_structure(caps, 0);
			int rate = 0, ch = 0;
			gst_structure_get_int(s, "rate", &rate);
			gst_structure_get_int(s, "channels", &ch);
			audio_info_cache.sample_rate = rate > 0 ? uint32_t(rate) : output_sample_rate;
			audio_info_cache.channels = ch > 0 ? uint32_t(ch) : output_channels;
			audio_info_cache.present = true;
		}
		if (caps) {
			gst_caps_unref(caps);
		}
	}
	if (video_sink) {
		GstCaps *caps = _sink_negotiated_caps(video_sink);
		if (caps && gst_caps_get_size(caps) > 0) {
			const GstStructure *s = gst_caps_get_structure(caps, 0);
			int w = 0, h = 0, fps_n = 0, fps_d = 1;
			gst_structure_get_int(s, "width", &w);
			gst_structure_get_int(s, "height", &h);
			gst_structure_get_fraction(s, "framerate", &fps_n, &fps_d);
			video_info_cache.width = uint32_t(w);
			video_info_cache.height = uint32_t(h);
			video_info_cache.frame_rate = fps_d > 0 ? float(fps_n) / float(fps_d) : 0.0f;
			video_info_cache.preferred_format = VIDEO_FRAME_NV12;
			video_info_cache.present = w > 0 && h > 0;
		}
		if (caps) {
			gst_caps_unref(caps);
		}
	}

	int64_t dur = 0;
	if (pipeline && gst_element_query_duration(pipeline, GST_FORMAT_TIME, &dur) && dur > 0) {
		const double seconds = double(dur) / double(GST_SECOND);
		audio_info_cache.duration_seconds = seconds;
		video_info_cache.duration_seconds = seconds;
	}
}

Error GStreamerBackend::open(const Vector<uint8_t> &p_data, ContainerFormat p_hint) {
	if (!_ensure_gst_loaded()) {
		return ERR_UNAVAILABLE;
	}
	_teardown_pipeline();
	if (p_data.is_empty()) {
		return ERR_INVALID_DATA;
	}
	input_data = p_data;

	if (!_build_pipeline()) {
		return ERR_CANT_OPEN;
	}

	GstBuffer *buf = gst_buffer_new_wrapped_full(
			GST_MEMORY_FLAG_READONLY,
			(gpointer)input_data.ptr(), input_data.size(),
			0, input_data.size(),
			nullptr, nullptr);
	GstFlowReturn ret = gst_app_src_push_buffer(src, buf);
	if (ret != GST_FLOW_OK) {
		_teardown_pipeline();
		return ERR_CANT_OPEN;
	}
	gst_app_src_end_of_stream(src);

	GstStateChangeReturn sc = gst_element_set_state(pipeline, GST_STATE_PLAYING);
	if (sc == GST_STATE_CHANGE_FAILURE) {
		_teardown_pipeline();
		return ERR_CANT_OPEN;
	}

	// The wait for preroll lives here, the one blocking point: VideoStreamPlayer
	// queries get_channels()/get_mix_rate() synchronously right after
	// instantiate_playback(), and a count of 0 channels leaves the audio mix
	// callback uninstalled and playback silent. Preroll leaves the appsink caps
	// negotiated, so get_audio_info()/get_video_info() report real values.
	// Per-frame decode runs non-blocking (try_pull_sample with a 0 timeout);
	// this one-time cost falls on stream open alone, as Theora/WebM playback do.
	GstState state = GST_STATE_NULL;
	GstState pending = GST_STATE_NULL;
	sc = gst_element_get_state(pipeline, &state, &pending, 5 * GST_SECOND);
	if (sc == GST_STATE_CHANGE_FAILURE) {
		_teardown_pipeline();
		return ERR_CANT_OPEN;
	}
	if (sc == GST_STATE_CHANGE_ASYNC) {
		// Preroll runs past the timeout (slow decode / unusual container). The
		// non-blocking state machine takes over; video negotiates on a later
		// tick, though audio setup may miss its window.
		ps = PS_PROBING;
		return OK;
	}
	// SUCCESS or NO_PREROLL — the caps negotiate here, so the first
	// get_audio_info()/get_video_info() returns complete data.
	_probe_negotiated_caps();
	ps = PS_READY;
	return OK;
}

NativeMediaBackend::AudioInfo GStreamerBackend::get_audio_info() const {
	const_cast<GStreamerBackend *>(this)->_pump_state();
	return audio_info_cache;
}

NativeMediaBackend::VideoInfo GStreamerBackend::get_video_info() const {
	const_cast<GStreamerBackend *>(this)->_pump_state();
	return video_info_cache;
}

int GStreamerBackend::decode_audio(AudioFrame *p_buffer, int p_frames) {
	if (!_gst_init_succeeded || !audio_sink || !p_buffer || p_frames <= 0 || audio_eof) {
		return 0;
	}
	if (!_pump_state()) {
		// Pipeline still probing caps; caller retries next mix tick.
		return 0;
	}

	const int floats_per_frame = audio_info_cache.channels > 0 ? int(audio_info_cache.channels) : 2;
	int written = 0;

	// Frames left over from the previous call drain first, ahead of any new
	// buffers, so no decoded sample slips past.
	if (audio_residual_read < audio_residual.size()) {
		const float *r = audio_residual.ptr() + audio_residual_read;
		const int residual_frames = (audio_residual.size() - audio_residual_read) / floats_per_frame;
		const int n = MIN(residual_frames, p_frames - written);
		for (int i = 0; i < n; i++) {
			p_buffer[written + i].left = r[i * floats_per_frame + 0];
			p_buffer[written + i].right = floats_per_frame > 1 ? r[i * floats_per_frame + 1] : r[i * floats_per_frame + 0];
		}
		written += n;
		audio_residual_read += n * floats_per_frame;
		if (audio_residual_read >= audio_residual.size()) {
			audio_residual.clear();
			audio_residual_read = 0;
		}
	}

	while (written < p_frames) {
		GstSample *sample = gst_app_sink_try_pull_sample(audio_sink, 0);
		if (!sample) {
			if (gst_app_sink_is_eos(audio_sink)) {
				audio_eof = true;
			}
			break;
		}
		GstBuffer *gb = gst_sample_get_buffer(sample);
		GstMapInfo info;
		if (!gst_buffer_map(gb, &info, GST_MAP_READ)) {
			gst_sample_unref(sample);
			continue;
		}
		const int available_frames = int(info.size / (sizeof(float) * floats_per_frame));
		const int frames_to_copy = MIN(available_frames, p_frames - written);
		const float *src_floats = reinterpret_cast<const float *>(info.data);
		for (int i = 0; i < frames_to_copy; i++) {
			p_buffer[written + i].left = src_floats[i * floats_per_frame + 0];
			p_buffer[written + i].right = floats_per_frame > 1 ? src_floats[i * floats_per_frame + 1] : src_floats[i * floats_per_frame + 0];
		}
		written += frames_to_copy;

		// Frames that overflow this call carry to the next rather than drop; a
		// dropped buffer tail speeds audio up.
		if (available_frames > frames_to_copy) {
			const int leftover_floats = (available_frames - frames_to_copy) * floats_per_frame;
			audio_residual.resize(leftover_floats);
			memcpy(audio_residual.ptrw(), src_floats + frames_to_copy * floats_per_frame, leftover_floats * sizeof(float));
			audio_residual_read = 0;
		}
		gst_buffer_unmap(gb, &info);
		gst_sample_unref(sample);
	}
	return written;
}

bool GStreamerBackend::is_audio_eof() const {
	return audio_eof;
}

Error GStreamerBackend::decode_video_frame(Vector<uint8_t> *r_buffer, double *r_pts_seconds) {
	if (!_gst_init_succeeded || !video_sink || !r_buffer) {
		return ERR_UNAVAILABLE;
	}
	if (video_eof) {
		return ERR_FILE_EOF;
	}
	if (!_pump_state() || !video_info_cache.present) {
		// Caps are not negotiated yet (or there is no video track); the caller retries.
		return ERR_BUSY;
	}

	GstSample *sample = gst_app_sink_try_pull_sample(video_sink, 0);
	if (!sample) {
		if (gst_app_sink_is_eos(video_sink)) {
			video_eof = true;
			return ERR_FILE_EOF;
		}
		return ERR_BUSY;
	}

	GstBuffer *gb = gst_sample_get_buffer(sample);
	GstMapInfo info;
	if (!gst_buffer_map(gb, &info, GST_MAP_READ)) {
		gst_sample_unref(sample);
		return ERR_CANT_OPEN;
	}

	const uint32_t w = video_info_cache.width;
	const uint32_t h = video_info_cache.height;
	const uint32_t y_plane_bytes = w * h;
	const uint32_t chroma_plane_bytes = w * (h / 2);

	// The copy assumes stride == width, which holds for software decoders.
	// Hardware decoders pad rows to e.g. a 64-byte stride; reading the real
	// stride needs GstVideoFrame / GstVideoMeta, whose struct ABI surface the
	// backend vendors no typedefs for, so padded strides are unsupported.
	if (info.size >= y_plane_bytes + chroma_plane_bytes) {
		r_buffer->resize(y_plane_bytes + chroma_plane_bytes);
		memcpy(r_buffer->ptrw(), info.data, y_plane_bytes + chroma_plane_bytes);
	}

	// The PTS reports 0.0 because reading the buffer's pts field needs more of
	// the GstBuffer struct than the backend vendors typedefs for; the caller
	// supplies its own playback clock.
	if (r_pts_seconds) {
		*r_pts_seconds = 0.0;
	}

	gst_buffer_unmap(gb, &info);
	gst_sample_unref(sample);
	return OK;
}

Error GStreamerBackend::seek(double p_time_seconds) {
	if (!_gst_init_succeeded || !pipeline) {
		return ERR_UNAVAILABLE;
	}
	const int64_t ns = int64_t(p_time_seconds * double(GST_SECOND));
	int seek_flags = GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT;
	gboolean ok = gst_element_seek_simple(pipeline, GST_FORMAT_TIME, (GstSeekFlags)seek_flags, ns);
	audio_eof = false;
	video_eof = false;
	return ok ? OK : ERR_CANT_OPEN;
}

void GStreamerBackend::reset() {
	if (pipeline) {
		seek(0.0);
	}
}

#endif // LINUXBSD_ENABLED && GSTREAMER_ENABLED

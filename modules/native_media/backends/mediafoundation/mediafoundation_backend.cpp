/**************************************************************************/
/*  mediafoundation_backend.cpp                                           */
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

#include "mediafoundation_backend.h"

#ifdef WINDOWS_ENABLED

#include "core/error/error_macros.h"
#include "core/os/mutex.h"
#include "servers/audio/audio_server.h"

#include <mferror.h>
#include <mfobjects.h>
#include <propvarutil.h>
#include <shlwapi.h>

// MinGW's mfreadwrite.h does not declare MFCreateMFByteStreamOnStream even
// though mfplat.dll exports it since Windows 8. A lazy GetProcAddress lookup
// resolves it so the module builds on MinGW.
typedef HRESULT(WINAPI *PFN_MFCreateMFByteStreamOnStream)(IStream *, IMFByteStream **);
static PFN_MFCreateMFByteStreamOnStream pfn_MFCreateMFByteStreamOnStream = nullptr;

static PFN_MFCreateMFByteStreamOnStream _resolve_mf_byte_stream_on_stream() {
	if (pfn_MFCreateMFByteStreamOnStream) {
		return pfn_MFCreateMFByteStreamOnStream;
	}
	HMODULE m = GetModuleHandleA("mfplat.dll");
	if (!m) {
		m = LoadLibraryA("mfplat.dll");
	}
	if (m) {
		pfn_MFCreateMFByteStreamOnStream = (PFN_MFCreateMFByteStreamOnStream)(void *)GetProcAddress(m, "MFCreateMFByteStreamOnStream");
	}
	return pfn_MFCreateMFByteStreamOnStream;
}

#define SAFE_RELEASE(p) \
	do { \
		if (p) { \
			(p)->Release(); \
			(p) = nullptr; \
		} \
	} while (0)

namespace {

Mutex &_mf_lifecycle_mutex() {
	static Mutex m;
	return m;
}
int _mf_ref_count = 0;

void _mf_startup() {
	MutexLock lock(_mf_lifecycle_mutex());
	if (++_mf_ref_count == 1) {
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		MFStartup(MF_VERSION, MFSTARTUP_LITE);
	}
}

void _mf_shutdown() {
	MutexLock lock(_mf_lifecycle_mutex());
	if (--_mf_ref_count == 0) {
		MFShutdown();
		CoUninitialize();
	}
}

} // namespace

// COM callback that WMF invokes on a worker thread when ReadSample finishes.
// It holds a raw backend pointer; the backend's destructor releases its own
// reference, and the backend pointer stays guarded by the backend's queue_mutex
// so OnReadSample never touches a half-destroyed backend.
GODOT_GCC_WARNING_PUSH_AND_IGNORE("-Wnon-virtual-dtor") // COM interfaces have non-virtual dtors by design; lifetime is managed via AddRef/Release.
class MFAsyncCallback : public IMFSourceReaderCallback {
	LONG ref_count = 1;

public:
	// The backend clears this to nullptr during teardown so any in-flight
	// OnReadSample dispatched after the backend is gone becomes a no-op.
	MediaFoundationBackend *backend = nullptr;
	Mutex backend_mutex;

	virtual ~MFAsyncCallback() = default;

	// IUnknown ---------------------------------------------------------------
	STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
		if (!ppv) {
			return E_POINTER;
		}
		if (riid == IID_IUnknown || riid == IID_IMFSourceReaderCallback) {
			*ppv = static_cast<IMFSourceReaderCallback *>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG)
	AddRef() override { return InterlockedIncrement(&ref_count); }
	STDMETHODIMP_(ULONG)
	Release() override {
		LONG c = InterlockedDecrement(&ref_count);
		if (c == 0) {
			delete this;
		}
		return c;
	}

	// IMFSourceReaderCallback ------------------------------------------------
	STDMETHODIMP OnReadSample(HRESULT hr, DWORD dwStreamIndex, DWORD dwStreamFlags,
			LONGLONG llTimestamp, IMFSample *pSample) override {
		static int trace = 0;
		if (trace < 10) {
			String msg = "native_media: WMF OnReadSample #";
			msg += itos(trace);
			msg += " stream=" + itos(dwStreamIndex);
			msg += " flags=" + itos(dwStreamFlags);
			msg += " sample=";
			msg += pSample ? "yes" : "null";
			print_verbose(msg);
			trace++;
		}
		MutexLock lock(backend_mutex);
		if (!backend) {
			return S_OK;
		}
		// dwStreamIndex is the real container stream index (0, 1, 2, ...),
		// not the MF_SOURCE_READER_FIRST_* sentinel passed to ReadSample.
		// It matches against the indices resolved at open() time.
		if (dwStreamIndex == backend->audio_stream_index) {
			backend->_on_audio_sample(dwStreamFlags, pSample);
		} else if (dwStreamIndex == backend->video_stream_index) {
			(void)llTimestamp; // The video path reads PTS from the sample itself.
			backend->_on_video_sample(dwStreamFlags, pSample);
		}
		return S_OK;
	}
	STDMETHODIMP OnEvent(DWORD, IMFMediaEvent *) override { return S_OK; }
	STDMETHODIMP OnFlush(DWORD) override { return S_OK; }
};
GODOT_GCC_WARNING_POP

MediaFoundationBackend::MediaFoundationBackend() {
	_mf_startup();
}

MediaFoundationBackend::~MediaFoundationBackend() {
	_release_reader();
	_mf_shutdown();
}

void MediaFoundationBackend::_release_reader() {
	// Detaching the callback from the backend first turns any in-flight
	// OnReadSample firing on a WMF worker thread into a no-op.
	if (callback) {
		MutexLock lock(callback->backend_mutex);
		callback->backend = nullptr;
	}
	SAFE_RELEASE(reader);
	if (callback) {
		callback->Release();
		callback = nullptr;
	}
	SAFE_RELEASE(byte_stream);
	SAFE_RELEASE(istream);

	MutexLock lock(queue_mutex);
	pending_audio_samples.clear();
	pending_audio_read_index = 0;
	pending_video_frame.clear();
	video_frame_ready = false;
	pending_video_pts = 0.0;
	audio_eof = false;
	video_eof = false;
	audio_request_pending = false;
	video_request_pending = false;
	audio_stream_index = UINT32_MAX;
	video_stream_index = UINT32_MAX;
}

Error MediaFoundationBackend::_configure_video_output() {
	IMFMediaType *out_type = nullptr;
	HRESULT hr = MFCreateMediaType(&out_type);
	if (FAILED(hr)) {
		print_verbose(vformat("native_media: MFCreateMediaType (video out) failed 0x%08x.", uint32_t(hr)));
		return ERR_CANT_OPEN;
	}
	out_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	out_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);

	hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, out_type);
	SAFE_RELEASE(out_type);
	if (FAILED(hr)) {
		print_verbose(vformat("native_media: SetCurrentMediaType(NV12) on video stream failed 0x%08x.", uint32_t(hr)));
		return ERR_UNAVAILABLE;
	}

	IMFMediaType *actual = nullptr;
	hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual);
	if (FAILED(hr)) {
		return ERR_UNAVAILABLE;
	}

	UINT32 w = 0, h = 0;
	MFGetAttributeSize(actual, MF_MT_FRAME_SIZE, &w, &h);
	video_info_cache.width = w;
	video_info_cache.height = h;

	UINT32 num = 0, den = 1;
	if (SUCCEEDED(MFGetAttributeRatio(actual, MF_MT_FRAME_RATE, &num, &den)) && den > 0) {
		video_info_cache.frame_rate = float(num) / float(den);
	}

	UINT32 stride_attr = 0;
	if (SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride_attr))) {
		video_default_stride = stride_attr;
	} else {
		video_default_stride = w;
	}

	video_info_cache.preferred_format = VIDEO_FRAME_NV12;
	video_info_cache.duration_seconds = audio_info_cache.duration_seconds;
	video_info_cache.present = true;

	SAFE_RELEASE(actual);
	reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
	print_verbose(vformat("native_media: WMF video configured %dx%d @ %.2f Hz (stride %d).",
			int(video_info_cache.width), int(video_info_cache.height),
			double(video_info_cache.frame_rate), int(video_default_stride)));
	return OK;
}

Error MediaFoundationBackend::_configure_audio_output() {
	IMFMediaType *out_type = nullptr;
	HRESULT hr = MFCreateMediaType(&out_type);
	if (FAILED(hr)) {
		return ERR_CANT_OPEN;
	}

	out_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	out_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
	out_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
	out_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, output_channels);
	out_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, output_sample_rate);
	out_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, output_channels * sizeof(float));
	out_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, output_sample_rate * output_channels * sizeof(float));
	out_type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, 1);

	hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, out_type);
	SAFE_RELEASE(out_type);
	if (FAILED(hr)) {
		return ERR_CANT_OPEN;
	}

	reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
	return OK;
}

Error MediaFoundationBackend::_request_next_audio_sample() {
	if (!reader || audio_eof) {
		return ERR_UNAVAILABLE;
	}
	// ReadSample is async (MF_SOURCE_READER_ASYNC_CALLBACK is set at create
	// time); this call returns immediately and OnReadSample fires on a WMF
	// worker thread when a sample is ready. It never waits.
	HRESULT hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);
	if (FAILED(hr)) {
		return ERR_CANT_OPEN;
	}
	audio_request_pending = true;
	return OK;
}

Error MediaFoundationBackend::_request_next_video_sample() {
	if (!reader || video_eof) {
		return ERR_UNAVAILABLE;
	}
	HRESULT hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);
	if (FAILED(hr)) {
		print_verbose(vformat("native_media: video ReadSample failed 0x%08x.", uint32_t(hr)));
		return ERR_CANT_OPEN;
	}
	video_request_pending = true;
	return OK;
}

void MediaFoundationBackend::_on_audio_sample(DWORD dwStreamFlags, IMFSample *pSample) {
	// This runs on a WMF worker thread under callback->backend_mutex; the
	// queue stays guarded with queue_mutex so the public decode_audio path
	// drains without contention against ReadSample dispatch.
	{
		MutexLock lock(queue_mutex);
		audio_request_pending = false;
		if (dwStreamFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
			audio_eof = true;
			return;
		}
	}
	if (!pSample) {
		// Stream gap or format change — no data; the public path
		// re-arms via _request_next_audio_sample on the next decode_audio call.
		return;
	}

	IMFMediaBuffer *buffer = nullptr;
	if (FAILED(pSample->ConvertToContiguousBuffer(&buffer)) || !buffer) {
		return;
	}
	BYTE *bytes = nullptr;
	DWORD size_bytes = 0;
	if (FAILED(buffer->Lock(&bytes, nullptr, &size_bytes))) {
		buffer->Release();
		return;
	}
	const int float_count = int(size_bytes / sizeof(float));
	{
		MutexLock lock(queue_mutex);
		const int prev_size = pending_audio_samples.size();
		pending_audio_samples.resize(prev_size + float_count);
		memcpy(pending_audio_samples.ptrw() + prev_size, bytes, size_bytes);
	}
	buffer->Unlock();
	buffer->Release();
}

void MediaFoundationBackend::_on_video_sample(DWORD dwStreamFlags, IMFSample *pSample) {
	static int trace = 0;
	if (trace < 5) {
		String msg = "native_media: WMF _on_video_sample #";
		msg += itos(trace);
		msg += " flags=" + itos(dwStreamFlags);
		msg += " sample=";
		msg += pSample ? "yes" : "null";
		print_verbose(msg);
		trace++;
	}
	{
		MutexLock lock(queue_mutex);
		video_request_pending = false;
		if (dwStreamFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
			video_eof = true;
			print_verbose("native_media: WMF video EOF.");
			return;
		}
	}
	if (!pSample) {
		print_verbose("native_media: WMF video sample is null (stream gap or format change).");
		return;
	}

	// TODO(zero-copy): the IMFDXGIBuffer fast path stays disabled until
	// Godot's RenderingDevice::texture_create_shared supports per-plane
	// views from multi-planar VkImages (G8_B8R8_2PLANE_420_UNORM →
	// R8_UNORM + R8G8_UNORM views via VK_IMAGE_ASPECT_PLANE_0/1).
	// texture_create_shared returns superficially valid RIDs that fail to
	// bind in uniform sets, producing green screen + "Attempted to free
	// invalid ID" errors on pool teardown. The bridge init + pool
	// infrastructure stays alive so the WMF source reader still gets an
	// IMFDXGIDeviceManager — hardware MFTs accelerate decode without the
	// zero-copy import.
#if 0
	if (bridge.is_valid() && bridge->is_available()) {
		IMFMediaBuffer *raw_buffer = nullptr;
		if (SUCCEEDED(pSample->GetBufferByIndex(0, &raw_buffer)) && raw_buffer) {
			IMFDXGIBuffer *dxgi_buf = nullptr;
			if (SUCCEEDED(raw_buffer->QueryInterface(IID_PPV_ARGS(&dxgi_buf)))) {
				ID3D11Texture2D *src_tex = nullptr;
				UINT subresource = 0;
				if (SUCCEEDED(dxgi_buf->GetResource(IID_PPV_ARGS(&src_tex))) &&
						SUCCEEDED(dxgi_buf->GetSubresourceIndex(&subresource))) {
					RID imported = bridge->copy_and_import(src_tex, subresource);
					if (imported.is_valid()) {
						LONGLONG pts_100ns = 0;
						pSample->GetSampleTime(&pts_100ns);
						MutexLock lock(queue_mutex);
						pending_video_rid = imported;
						pending_video_pts = double(pts_100ns) / 1e7;
						video_rid_ready = true;
						src_tex->Release();
						dxgi_buf->Release();
						raw_buffer->Release();
						return;
					}
				}
				if (src_tex) {
					src_tex->Release();
				}
				dxgi_buf->Release();
			}
			raw_buffer->Release();
		}
	}
#endif
	// CPU staging path — the path that runs while the zero-copy bridge is
	// disabled (see #if 0 above).
	static int cpu_trace = 0;
	if (cpu_trace < 5) {
		print_verbose("native_media: WMF CPU staging path frame " + itos(cpu_trace));
		cpu_trace++;
	}

	IMFMediaBuffer *buffer = nullptr;
	HRESULT cv_hr = pSample->ConvertToContiguousBuffer(&buffer);
	if (FAILED(cv_hr) || !buffer) {
		print_verbose("native_media: ConvertToContiguousBuffer failed 0x" + String::num_int64(uint32_t(cv_hr), 16));
		return;
	}

	const uint32_t w = video_info_cache.width;
	const uint32_t h = video_info_cache.height;
	const uint32_t y_plane_bytes = w * h;
	const uint32_t chroma_plane_bytes = w * (h / 2);
	const uint32_t packed = y_plane_bytes + chroma_plane_bytes;

	Vector<uint8_t> staging;
	staging.resize(packed);
	uint8_t *dst = staging.ptrw();

	IMF2DBuffer2 *buf2d = nullptr;
	if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buf2d)))) {
		BYTE *scan0 = nullptr;
		LONG pitch = 0;
		BYTE *start = nullptr;
		DWORD length = 0;
		if (SUCCEEDED(buf2d->Lock2DSize(MF2DBuffer_LockFlags_Read, &scan0, &pitch, &start, &length))) {
			// NV12 layout: Y plane (padded height × pitch) + UV plane (h/2 rows × pitch).
			// Hardware decoders pad Y height to macroblock boundaries (e.g. 1088 for
			// 1080p), so chroma does NOT start at scan0 + h*pitch. The actual Y
			// allocation height derives from the total buffer length Lock2DSize gives.
			const DWORD content_len = length - DWORD(scan0 - start);
			if (pitch <= 0) {
				pitch = LONG(w); // Guards zero/negative stride; rows are packed.
			}
			const LONG total_rows = LONG(content_len / uint32_t(pitch));

			// The Y-plane height depends on the decoder: hardware MFTs pad Y to
			// a 16-row macroblock boundary (1088 for 1080p), while the software
			// path returns tightly-packed NV12 with chroma at row h. total_rows
			// (the real buffer height) disambiguates: when it equals h + h/2 the
			// buffer is tightly packed, otherwise Y is padded. The padded
			// offset on a packed buffer reads past the end and crashes.
			const LONG y_padded_height = ((h + 15) / 16) * 16; // Rounds up to 16.
			const LONG y_alloc_rows = (total_rows == LONG(h) + LONG(h / 2))
					? LONG(h)
					: y_padded_height;

			print_verbose(vformat("native_media: NV12: w=%d h=%d pitch=%d y_padded=%d total_rows=%d",
					w, h, pitch, y_padded_height, total_rows));

			// The Y plane copies only the actual content rows, not padding. A
			// per-row bound keeps a truncated buffer (total_rows < h) from over-reading.
			for (uint32_t row = 0; row < h; row++) {
				if (LONG(row) * LONG(pitch) + LONG(w) > LONG(content_len)) {
					break;
				}
				memcpy(dst + row * w, scan0 + LONG(row) * pitch, w);
			}

			// Chroma starts after the Y allocation. A per-row bound against the
			// locked buffer keeps a wrong offset from reading past the end.
			BYTE *chroma_src = scan0 + y_alloc_rows * pitch;
			uint8_t *chroma_dst = dst + y_plane_bytes;
			for (uint32_t row = 0; row < h / 2; row++) {
				if ((y_alloc_rows + LONG(row)) * LONG(pitch) + LONG(w) > LONG(content_len)) {
					break;
				}
				memcpy(chroma_dst + row * w, chroma_src + LONG(row) * pitch, w);
			}
			buf2d->Unlock2D();
		}
		buf2d->Release();
	} else {
		BYTE *bytes = nullptr;
		DWORD size = 0;
		if (SUCCEEDED(buffer->Lock(&bytes, nullptr, &size))) {
			// ConvertToContiguousBuffer produces a tightly packed NV12 buffer:
			// Y plane (w*h bytes) directly followed by interleaved UV plane
			// (w*h/2 bytes). Row stride is always w regardless of what
			// MF_MT_DEFAULT_STRIDE reported for the decoder's output type.
			for (uint32_t row = 0; row < h; row++) {
				if ((row + 1) * w > size) {
					break; // never read past the locked buffer
				}
				memcpy(dst + row * w, bytes + row * w, w);
			}
			BYTE *chroma_src = bytes + h * w;
			uint8_t *chroma_dst = dst + y_plane_bytes;
			for (uint32_t row = 0; row < h / 2; row++) {
				if (y_plane_bytes + (row + 1) * w > size) {
					break; // never read past the locked buffer
				}
				memcpy(chroma_dst + row * w, chroma_src + row * w, w);
			}
			buffer->Unlock();
		}
	}

	LONGLONG pts_100ns = 0;
	pSample->GetSampleTime(&pts_100ns);

	{
		MutexLock lock(queue_mutex);
		pending_video_frame = staging;
		pending_video_pts = double(pts_100ns) / 1e7;
		video_frame_ready = true;
	}
	static int queued = 0;
	if (queued < 5) {
		print_verbose("native_media: WMF video_frame_ready=true frame " + itos(queued) + " (" + itos(staging.size()) + " bytes)");
		queued++;
	}
	buffer->Release();
}

Error MediaFoundationBackend::open(const Vector<uint8_t> &p_data, ContainerFormat p_hint) {
	_release_reader();

	if (p_data.is_empty()) {
		return ERR_INVALID_DATA;
	}

	if (AudioServer::get_singleton()) {
		output_sample_rate = uint32_t(AudioServer::get_singleton()->get_mix_rate());
	}

	istream = SHCreateMemStream(p_data.ptr(), (UINT)p_data.size());
	if (!istream) {
		return ERR_CANT_OPEN;
	}

	PFN_MFCreateMFByteStreamOnStream pfn = _resolve_mf_byte_stream_on_stream();
	if (!pfn) {
		_release_reader();
		return ERR_CANT_OPEN;
	}
	HRESULT hr = pfn(istream, &byte_stream);
	if (FAILED(hr)) {
		_release_reader();
		return ERR_CANT_OPEN;
	}

	// The async callback gets built and wired into the reader attributes.
	callback = new MFAsyncCallback();
	callback->backend = this;

	IMFAttributes *reader_attrs = nullptr;
	MFCreateAttributes(&reader_attrs, 3);
	if (reader_attrs) {
		reader_attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
		reader_attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
		reader_attrs->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback);
	}

	hr = MFCreateSourceReaderFromByteStream(byte_stream, reader_attrs, &reader);
	SAFE_RELEASE(reader_attrs);
	if (FAILED(hr)) {
		_release_reader();
		return ERR_CANT_OPEN;
	}

	PROPVARIANT pv;
	PropVariantInit(&pv);
	if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &pv))) {
		LONGLONG duration_100ns = 0;
		if (SUCCEEDED(PropVariantToInt64(pv, &duration_100ns))) {
			audio_info_cache.duration_seconds = double(duration_100ns) / 1e7;
		}
	}
	PropVariantClear(&pv);

	if (_configure_audio_output() == OK) {
		audio_info_cache.sample_rate = output_sample_rate;
		audio_info_cache.channels = output_channels;
		audio_info_cache.present = true;
		// This primes the audio decode pipeline; samples land asynchronously.
		_request_next_audio_sample();
	}

	if (_configure_video_output() == OK) {
		_request_next_video_sample();
	}

	// The loop resolves the actual stream indices. WMF assigns stream 0 / 1 /
	// ... to the streams in container order; OnReadSample reports those real
	// indices, not the MF_SOURCE_READER_FIRST_* sentinels passed into
	// ReadSample. It iterates until GetCurrentMediaType errors out.
	for (DWORD i = 0; i < 32; i++) {
		IMFMediaType *type = nullptr;
		if (FAILED(reader->GetCurrentMediaType(i, &type)) || !type) {
			break;
		}
		GUID major_type;
		if (SUCCEEDED(type->GetGUID(MF_MT_MAJOR_TYPE, &major_type))) {
			if (major_type == MFMediaType_Audio && audio_stream_index == UINT32_MAX) {
				audio_stream_index = i;
			} else if (major_type == MFMediaType_Video && video_stream_index == UINT32_MAX) {
				video_stream_index = i;
			}
		}
		type->Release();
	}
	print_verbose("native_media: WMF stream indices audio=" + itos(audio_stream_index) + " video=" + itos(video_stream_index));

	return OK;
}

NativeMediaBackend::AudioInfo MediaFoundationBackend::get_audio_info() const {
	return audio_info_cache;
}

NativeMediaBackend::VideoInfo MediaFoundationBackend::get_video_info() const {
	return video_info_cache;
}

int MediaFoundationBackend::decode_audio(AudioFrame *p_buffer, int p_frames) {
	if (!reader || !p_buffer || p_frames <= 0) {
		return 0;
	}

	int frames_written = 0;
	while (frames_written < p_frames) {
		// A snapshot of what's available avoids holding the mutex during memcpy.
		int available_floats = 0;
		bool need_request = false;
		bool reached_eof = false;
		{
			MutexLock lock(queue_mutex);
			available_floats = pending_audio_samples.size() - pending_audio_read_index;
			reached_eof = audio_eof && available_floats <= 0;
			need_request = !audio_eof && !audio_request_pending && available_floats <= 0;
		}
		if (reached_eof) {
			break;
		}
		if (available_floats <= 0) {
			if (need_request) {
				_request_next_audio_sample();
			}
			// Nothing to feed this tick — the loop returns what it has; the
			// caller retries next mix.
			break;
		}

		const int available_frames = available_floats / 2; // Stereo.
		const int frames_to_copy = MIN(p_frames - frames_written, available_frames);
		{
			MutexLock lock(queue_mutex);
			const float *src = pending_audio_samples.ptr() + pending_audio_read_index;
			for (int i = 0; i < frames_to_copy; i++) {
				p_buffer[frames_written + i].left = src[i * 2 + 0];
				p_buffer[frames_written + i].right = src[i * 2 + 1];
			}
			pending_audio_read_index += frames_to_copy * 2;
			// Occasional compaction keeps the buffer from growing without bound.
			if (pending_audio_read_index > 0 && pending_audio_read_index == pending_audio_samples.size()) {
				pending_audio_samples.clear();
				pending_audio_read_index = 0;
			}
		}
		frames_written += frames_to_copy;
	}

	// The pipeline stays primed for the next tick when not at eof and no
	// request is outstanding. Re-arming happens outside the lock to avoid
	// holding queue_mutex across the ReadSample dispatch.
	bool re_arm = false;
	{
		MutexLock lock(queue_mutex);
		re_arm = !audio_eof && !audio_request_pending;
	}
	if (re_arm) {
		_request_next_audio_sample();
	}

	return frames_written;
}

bool MediaFoundationBackend::is_audio_eof() const {
	return audio_eof && (pending_audio_samples.size() - pending_audio_read_index) <= 0;
}

Error MediaFoundationBackend::decode_video_frame(Vector<uint8_t> *r_buffer, double *r_pts_seconds) {
	if (!reader || !video_info_cache.present || !r_buffer) {
		return ERR_UNAVAILABLE;
	}

	bool eof = false;
	bool has_frame = false;
	{
		MutexLock lock(queue_mutex);
		eof = video_eof && !video_frame_ready;
		has_frame = video_frame_ready;
	}
	if (eof) {
		return ERR_FILE_EOF;
	}
	if (!has_frame) {
		// Re-arming when nothing is pending lets OnReadSample deliver eventually.
		bool re_arm = false;
		{
			MutexLock lock(queue_mutex);
			re_arm = !video_eof && !video_request_pending;
		}
		if (re_arm) {
			_request_next_video_sample();
		}
		return ERR_BUSY;
	}

	{
		MutexLock lock(queue_mutex);
		*r_buffer = pending_video_frame;
		if (r_pts_seconds) {
			*r_pts_seconds = pending_video_pts;
		}
		pending_video_frame.clear();
		video_frame_ready = false;
	}
	// Pulling the next frame ahead of time keeps the GPU upload path from stalling.
	_request_next_video_sample();
	return OK;
}

Error MediaFoundationBackend::seek(double p_time_seconds) {
	if (!reader) {
		return ERR_UNAVAILABLE;
	}
	PROPVARIANT pv;
	PropVariantInit(&pv);
	pv.vt = VT_I8;
	pv.hVal.QuadPart = LONGLONG(p_time_seconds * 1e7);
	HRESULT hr = reader->SetCurrentPosition(GUID_NULL, pv);
	PropVariantClear(&pv);

	{
		MutexLock lock(queue_mutex);
		pending_audio_samples.clear();
		pending_audio_read_index = 0;
		pending_video_frame.clear();
		video_frame_ready = false;
		audio_eof = false;
		video_eof = false;
	}
	// Both streams re-prime.
	_request_next_audio_sample();
	if (video_info_cache.present) {
		_request_next_video_sample();
	}

	return SUCCEEDED(hr) ? OK : ERR_CANT_OPEN;
}

void MediaFoundationBackend::reset() {
	if (reader) {
		seek(0.0);
	}
}

#endif // WINDOWS_ENABLED

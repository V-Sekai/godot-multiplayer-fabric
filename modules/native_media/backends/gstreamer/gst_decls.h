/**************************************************************************/
/*  gst_decls.h                                                           */
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

// Self-contained vendored declarations of the GLib / GStreamer 1.x API
// surface used by the native_media GStreamer backend. The backend includes
// no upstream <gst/gst.h> and links against no -lgstreamer-1.0; instead a
// stubgen-generated table dispatches calls through dlsym at runtime
// (LGPL-clean dynamic loading, no build-time host deps).
//
// ABI stability: GStreamer 1.x guarantees its public ABI across minor
// versions. The types declared here are either opaque (the backend holds
// only pointers) or are the documented public stack-allocatable structs
// (GstMapInfo, GstMappedMemory). Internal struct fields stay untouched —
// access goes through library functions.

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// --- GLib basic typedefs ------------------------------------------------

typedef void *gpointer;
typedef const void *gconstpointer;
typedef char gchar;
typedef int gboolean;
typedef int gint;
typedef unsigned int guint;
typedef long glong;
typedef unsigned long gulong;
typedef size_t gsize;
typedef ptrdiff_t gssize;
typedef int8_t gint8;
typedef uint8_t guint8;
typedef int16_t gint16;
typedef uint16_t guint16;
typedef int32_t gint32;
typedef uint32_t guint32;
typedef int64_t gint64;
typedef uint64_t guint64;
typedef float gfloat;
typedef double gdouble;
typedef gsize GType;

typedef void (*GCallback)();
typedef void (*GClosureNotify)(gpointer, void *);

#define G_GNUC_CONST
#define G_TRUE 1
#define G_FALSE 0
#define G_MAXUINT64 ((guint64)0xFFFFFFFFFFFFFFFFULL)

typedef struct _GError {
	guint32 domain;
	gint code;
	gchar *message;
} GError;

// --- GStreamer opaque types ---------------------------------------------

typedef struct _GstElement GstElement;
typedef struct _GstPad GstPad;
typedef struct _GstCaps GstCaps;
typedef struct _GstStructure GstStructure;
typedef struct _GstBuffer GstBuffer;
typedef struct _GstSample GstSample;
typedef struct _GstBin GstBin;
typedef struct _GstObject GstObject;
typedef struct _GstMemory GstMemory;
typedef struct _GstAppSrc GstAppSrc;
typedef struct _GstAppSink GstAppSink;

typedef guint64 GstClockTime;
typedef gint64 GstClockTimeDiff;

// --- GStreamer enums (numeric values are part of the public ABI) --------

typedef enum {
	GST_FLOW_CUSTOM_SUCCESS_2 = 102,
	GST_FLOW_CUSTOM_SUCCESS_1 = 101,
	GST_FLOW_CUSTOM_SUCCESS = 100,
	GST_FLOW_OK = 0,
	GST_FLOW_NOT_LINKED = -1,
	GST_FLOW_FLUSHING = -2,
	GST_FLOW_EOS = -3,
	GST_FLOW_NOT_NEGOTIATED = -4,
	GST_FLOW_ERROR = -5,
} GstFlowReturn;

typedef enum {
	GST_STATE_VOID_PENDING = 0,
	GST_STATE_NULL = 1,
	GST_STATE_READY = 2,
	GST_STATE_PAUSED = 3,
	GST_STATE_PLAYING = 4,
} GstState;

typedef enum {
	GST_STATE_CHANGE_FAILURE = 0,
	GST_STATE_CHANGE_SUCCESS = 1,
	GST_STATE_CHANGE_ASYNC = 2,
	GST_STATE_CHANGE_NO_PREROLL = 3,
} GstStateChangeReturn;

typedef enum {
	GST_FORMAT_UNDEFINED = 0,
	GST_FORMAT_DEFAULT = 1,
	GST_FORMAT_BYTES = 2,
	GST_FORMAT_TIME = 3,
	GST_FORMAT_BUFFERS = 4,
	GST_FORMAT_PERCENT = 5,
} GstFormat;

typedef enum {
	GST_SEEK_FLAG_NONE = 0,
	GST_SEEK_FLAG_FLUSH = (1 << 0),
	GST_SEEK_FLAG_ACCURATE = (1 << 1),
	GST_SEEK_FLAG_KEY_UNIT = (1 << 2),
} GstSeekFlags;

typedef enum {
	GST_MAP_READ = (1 << 0),
	GST_MAP_WRITE = (1 << 1),
} GstMapFlags;

typedef enum {
	GST_MEMORY_FLAG_READONLY = (1 << 1),
} GstMemoryFlags;

// --- Stack-allocatable public structs -----------------------------------

// Public, stable since GStreamer 1.0. Used by gst_buffer_map() / unmap().
typedef struct _GstMapInfo {
	GstMemory *memory;
	GstMapFlags flags;
	guint8 *data;
	gsize size;
	gsize maxsize;
	gpointer user_data[4];
	gpointer _gst_reserved[4];
} GstMapInfo;

// --- Constants ---------------------------------------------------------

#define GST_SECOND ((GstClockTime)1000000000ULL)
#define GST_MSECOND ((GstClockTime)1000000ULL)
#define GST_USECOND ((GstClockTime)1000ULL)
#define GST_NSECOND ((GstClockTime)1ULL)
#define GST_CLOCK_TIME_NONE G_MAXUINT64

// --- Cast macros (just pointer casts; no field access) -----------------

#define GST_OBJECT(obj) ((GstObject *)(obj))
#define GST_BIN(obj) ((GstBin *)(obj))
#define GST_ELEMENT(obj) ((GstElement *)(obj))
#define GST_APP_SRC(obj) ((GstAppSrc *)(obj))
#define GST_APP_SINK(obj) ((GstAppSink *)(obj))

// --- Function prototypes ------------------------------------------------
//
// These stay in lockstep with backends/gstreamer/gstreamer.sigs: that
// file feeds generate_stubs.py, which emits the dlsym-dispatched definitions
// (gstreamer_stubs.cc) for exactly these symbols. The prototypes below give
// callers (gstreamer_backend.cpp) the declarations to compile against; the
// stub .cc provides the matching definitions at link time. Both lists stay
// identical across any borrowed-symbol addition or change.

// GLib core
void g_free(gpointer mem);
void g_error_free(GError *error);
gchar *g_strdup_printf(const gchar *format, ...);
gboolean g_str_has_prefix(const gchar *str, const gchar *prefix);
gulong g_signal_connect_data(gpointer instance, const gchar *detailed_signal, GCallback c_handler, gpointer data, GClosureNotify destroy_data, gint connect_flags);

// GStreamer core (libgstreamer-1.0.so.0)
void gst_init(int *argc, char ***argv);
GstElement *gst_parse_launch(const gchar *pipeline_description, GError **error);
void gst_object_unref(gpointer object);
GstStateChangeReturn gst_element_set_state(GstElement *element, GstState state);
GstStateChangeReturn gst_element_get_state(GstElement *element, GstState *state, GstState *pending, GstClockTime timeout);
GstElement *gst_bin_get_by_name(GstBin *bin, const gchar *name);
GstPad *gst_element_get_static_pad(GstElement *element, const gchar *name);
gboolean gst_element_query_duration(GstElement *element, GstFormat format, gint64 *duration);
gboolean gst_element_seek_simple(GstElement *element, GstFormat format, GstSeekFlags seek_flags, gint64 seek_pos);
GstCaps *gst_pad_get_current_caps(GstPad *pad);
gboolean gst_pad_is_linked(GstPad *pad);
gint gst_pad_link(GstPad *srcpad, GstPad *sinkpad);

// Buffers + caps + structures
GstBuffer *gst_buffer_new_wrapped_full(gint flags, gpointer data, gsize maxsize, gsize offset, gsize size, gpointer user_data, GCallback notify);
gboolean gst_buffer_map(GstBuffer *buffer, GstMapInfo *info, GstMapFlags flags);
void gst_buffer_unmap(GstBuffer *buffer, GstMapInfo *info);
guint gst_caps_get_size(const GstCaps *caps);
GstStructure *gst_caps_get_structure(const GstCaps *caps, guint index);
void gst_caps_unref(GstCaps *caps);
GstBuffer *gst_sample_get_buffer(GstSample *sample);
GstCaps *gst_sample_get_caps(GstSample *sample);
void gst_sample_unref(GstSample *sample);
gboolean gst_structure_get_int(const GstStructure *structure, const gchar *fieldname, gint *value);
gboolean gst_structure_get_fraction(const GstStructure *structure, const gchar *fieldname, gint *value_numerator, gint *value_denominator);
const gchar *gst_structure_get_name(const GstStructure *structure);

// AppSrc / AppSink (libgstapp-1.0.so.0)
GstFlowReturn gst_app_src_push_buffer(GstAppSrc *appsrc, GstBuffer *buffer);
GstFlowReturn gst_app_src_end_of_stream(GstAppSrc *appsrc);
GstCaps *gst_app_sink_get_caps(GstAppSink *appsink);
GstSample *gst_app_sink_try_pull_sample(GstAppSink *appsink, GstClockTime timeout);
gboolean gst_app_sink_is_eos(GstAppSink *appsink);

#ifdef __cplusplus
}
#endif

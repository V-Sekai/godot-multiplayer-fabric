/**************************************************************************/
/*  resource_importer_video_native.cpp                                    */
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

#ifdef TOOLS_ENABLED

#include "resource_importer_video_native.h"

#include "core/io/resource_saver.h"

#include "modules/native_media/video_stream_native.h"

String ResourceImporterVideoNative::get_importer_name() const {
	return "video_native";
}

String ResourceImporterVideoNative::get_visible_name() const {
	return "Native Media Video";
}

void ResourceImporterVideoNative::get_recognized_extensions(List<String> *p_extensions) const {
	// Containers the OS-native decoders handle on every supported platform.
	// The actual codec mix that plays depends on what's installed at runtime
	// (Media Foundation / GStreamer / AVFoundation / MediaCodec / WebCodecs),
	// but the demux side is uniform.
	p_extensions->push_back("mp4");
	p_extensions->push_back("m4v");
	p_extensions->push_back("mov");
	p_extensions->push_back("webm");
	p_extensions->push_back("mkv");
	p_extensions->push_back("avi");
	p_extensions->push_back("wmv");
	p_extensions->push_back("flv");
}

String ResourceImporterVideoNative::get_save_extension() const {
	return "nvidstr";
}

String ResourceImporterVideoNative::get_resource_type() const {
	return "VideoStreamNative";
}

int ResourceImporterVideoNative::get_preset_count() const {
	return 0;
}

String ResourceImporterVideoNative::get_preset_name(int p_idx) const {
	return String();
}

void ResourceImporterVideoNative::get_import_options(const String &p_path, List<ImportOption> *r_options, int p_preset) const {
	r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "container_hint", PROPERTY_HINT_ENUM, "Auto,WebM,MP4,Ogg,WAV"), 0));
}

bool ResourceImporterVideoNative::get_option_visibility(const String &p_path, const String &p_option, const HashMap<StringName, Variant> &p_options) const {
	return true;
}

Error ResourceImporterVideoNative::import(ResourceUID::ID p_source_id, const String &p_source_file, const String &p_save_path, const HashMap<StringName, Variant> &p_options, List<String> *r_platform_variants, List<String> *r_gen_files, Variant *r_metadata) {
	int container_hint = 0;
	if (p_options.has("container_hint")) {
		container_hint = int(p_options["container_hint"]);
	}

	Ref<VideoStreamNative> stream = VideoStreamNative::load_from_file(p_source_file);
	if (stream.is_null() || stream->get_data().is_empty()) {
		return ERR_CANT_OPEN;
	}
	stream->set_container_hint(container_hint);

	return ResourceSaver::save(stream, p_save_path + ".nvidstr");
}

#endif // TOOLS_ENABLED

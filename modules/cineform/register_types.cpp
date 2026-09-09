/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "movie_writer_cineform.h"
#include "video_stream_cineform.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"

static MovieWriterCineForm *writer_cineform = nullptr;
static Ref<ResourceFormatLoaderCineForm> resource_loader_cineform;

void initialize_cineform_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		GLOBAL_DEF(PropertyInfo(Variant::INT, "editor/movie_writer/cineform/quality", PROPERTY_HINT_ENUM,
						   "Low,Medium,High,Filmscan 1,Filmscan 2,Filmscan 3"),
				2);
		GLOBAL_DEF(PropertyInfo(Variant::INT, "editor/movie_writer/cineform/thread_count", PROPERTY_HINT_RANGE,
						   "0,64,1"),
				0);
		GLOBAL_DEF("editor/movie_writer/cineform/keep_alpha", false);

		if constexpr (GD_IS_CLASS_ENABLED(MovieWriterCineForm)) {
			writer_cineform = memnew(MovieWriterCineForm);
			MovieWriter::add_writer(writer_cineform);
		}
		if constexpr (GD_IS_CLASS_ENABLED(VideoStreamCineForm)) {
			GDREGISTER_CLASS(VideoStreamCineForm);
			GDREGISTER_CLASS(VideoStreamPlaybackCineForm);
			resource_loader_cineform.instantiate();
			ResourceLoader::add_resource_format_loader(resource_loader_cineform, true);
		}
	}
}

void uninitialize_cineform_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		if constexpr (GD_IS_CLASS_ENABLED(MovieWriterCineForm)) {
			if (writer_cineform) {
				memdelete(writer_cineform);
				writer_cineform = nullptr;
			}
		}
		if constexpr (GD_IS_CLASS_ENABLED(VideoStreamCineForm)) {
			if (resource_loader_cineform.is_valid()) {
				ResourceLoader::remove_resource_format_loader(resource_loader_cineform);
				resource_loader_cineform.unref();
			}
		}
	}
}

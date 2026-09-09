/**************************************************************************/
/*  spatial_audio_server.h                                                */
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

#include "core/math/audio_frame.h"
#include "core/math/transform_3d.h"

class AudioSourceId {
	int id = -1;

public:
	AudioSourceId(int p_id = -1) :
			id(p_id) {}
	int get_id() const { return id; }
};

class SpatialAudioServer {
	static SpatialAudioServer *singleton;

public:
	static SpatialAudioServer *get_singleton() { return singleton; }

	virtual AudioSourceId create_source() = 0;
	virtual void destroy_source(AudioSourceId p_source) = 0;

	virtual void set_source_transform(AudioSourceId p_source, const Transform3D &p_transform) = 0;
	virtual void set_head_transform(const Transform3D &p_transform) = 0;

	virtual void set_source_attenuation(AudioSourceId p_source, float p_attenuation) = 0;

	virtual void push_source_buffer(AudioSourceId p_source, int p_channel, int p_num_frames, AudioFrame *p_frames) = 0;
	virtual bool pull_listener_buffer(int p_channel, int p_num_frames, AudioFrame *p_frames) = 0;

	SpatialAudioServer() { singleton = this; }
	virtual ~SpatialAudioServer() { singleton = nullptr; }
};

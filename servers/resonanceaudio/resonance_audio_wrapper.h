/**************************************************************************/
/*  resonance_audio_wrapper.h                                             */
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

#include "core/error/error_macros.h"
#include "core/math/audio_frame.h"
#include "core/object/object.h"
#include "core/templates/rid.h"
#include "core/templates/rid_owner.h"
#include "core/variant/variant.h"
#include "servers/audio/spatial_audio_server.h"

#include <thirdparty/resonanceaudio/resonance_audio/api/resonance_audio_api.h>

class AudioServer;

struct ResonanceAudioBus {
	RID self;

private:
	vraudio::ResonanceAudioApi *resonance_api = nullptr;

public:
	AudioSourceId register_audio_source() {
		vraudio::ResonanceAudioApi::SourceId new_id =
				resonance_api->CreateSoundObjectSource(vraudio::RenderingMode::kBinauralHighQuality);
		resonance_api->SetSourceDistanceModel(
				new_id, vraudio::DistanceRolloffModel::kNone, 0.0f, 0.0f);
		return AudioSourceId(new_id);
	}

	void unregister_audio_source(AudioSourceId audio_source) {
		if (audio_source.get_id() == -1) {
			return;
		}
		resonance_api->DestroySource(audio_source.get_id());
	}

	AudioSourceId register_stereo_audio_source() {
		return AudioSourceId(resonance_api->CreateStereoSource(2));
	}

	void set_source_transform(AudioSourceId source, Transform3D source_transform) {
		if (source.get_id() == -1) {
			return;
		}
		Quaternion q = source_transform.basis.get_rotation_quaternion();
		float x = source_transform.origin.x;
		float y = source_transform.origin.y;
		float z = source_transform.origin.z;
		resonance_api->SetSourcePosition(source.get_id(), x, y, z);
		resonance_api->SetSourceRotation(source.get_id(), q.x, q.y, q.z, q.w);
	}

	void set_head_transform(Transform3D head_transform) {
		Quaternion q = head_transform.basis.get_rotation_quaternion();
		float x = head_transform.origin.x;
		float y = head_transform.origin.y;
		float z = head_transform.origin.z;
		resonance_api->SetHeadPosition(x, y, z);
		resonance_api->SetHeadRotation(q.x, q.y, q.z, q.w);
	}

	void push_source_buffer(AudioSourceId source, int num_frames, AudioFrame *frames, bool p_stereo = false) {
		if (source.get_id() == -1) {
			return;
		}
		mono_buffer.resize(num_frames);
		float *mb = mono_buffer.ptrw();
		for (int i = 0; i < num_frames; i++) {
			mb[i] = (frames[i].left + frames[i].right) * 0.5f;
		}
		resonance_api->SetInterleavedBuffer(source.get_id(), mono_buffer.ptr(), 1, num_frames);
	}

	Vector<float> mono_buffer;

	bool pull_listener_buffer(int num_frames, AudioFrame *frames) {
		bool success = resonance_api->FillInterleavedOutputBuffer(2, num_frames, (float *)frames);
		if (!success) {
			for (int32_t i = 0; i < num_frames; i++) {
				frames[i] = {};
			}
		}
		return success;
	}

	void set_source_attenuation(AudioSourceId source, float attenuation_linear) {
		if (source.get_id() == -1) {
			return;
		}
		resonance_api->SetSourceDistanceAttenuation(source.get_id(), attenuation_linear);
	}

	void set_source_volume(AudioSourceId source, float volume) {
		if (source.get_id() == -1) {
			return;
		}
		resonance_api->SetSourceVolume(source.get_id(), volume);
	}

	// --- New features ---

	void set_source_directivity(AudioSourceId source, float alpha, float order) {
		if (source.get_id() == -1) {
			return;
		}
		resonance_api->SetSoundObjectDirectivity(source.get_id(), alpha, order);
	}

	void set_source_spread(AudioSourceId source, float spread_deg) {
		if (source.get_id() == -1) {
			return;
		}
		resonance_api->SetSoundObjectSpread(source.get_id(), spread_deg);
	}

	void set_source_distance_model(AudioSourceId source, float min_distance, float max_distance) {
		if (source.get_id() == -1) {
			return;
		}
		resonance_api->SetSourceDistanceModel(source.get_id(),
				vraudio::DistanceRolloffModel::kLogarithmic, min_distance, max_distance);
	}

	void set_source_occlusion(AudioSourceId source, float intensity) {
		if (source.get_id() == -1) {
			return;
		}
		resonance_api->SetSoundObjectOcclusionIntensity(source.get_id(), intensity);
	}

	void set_source_near_field_gain(AudioSourceId source, float gain) {
		if (source.get_id() == -1) {
			return;
		}
		resonance_api->SetSoundObjectNearFieldEffectGain(source.get_id(), gain);
	}

	void set_source_room_effects_gain(AudioSourceId source, float gain) {
		if (source.get_id() == -1) {
			return;
		}
		resonance_api->SetSourceRoomEffectsGain(source.get_id(), gain);
	}

	void enable_room_effects(bool enable) {
		resonance_api->EnableRoomEffects(enable);
	}

	void set_reflection_properties(float gain) {
		vraudio::ReflectionProperties props{};
		props.gain = gain;
		resonance_api->SetReflectionProperties(props);
	}

	void set_reverb_properties(float rt60_values[9], float gain) {
		vraudio::ReverbProperties props{};
		for (int i = 0; i < 9; i++) {
			props.rt60_values[i] = rt60_values[i];
		}
		props.gain = gain;
		resonance_api->SetReverbProperties(props);
	}

	_FORCE_INLINE_ void set_self(const RID &p_self) { self = p_self; }
	_FORCE_INLINE_ RID get_self() const { return self; }

	ResonanceAudioBus();
	~ResonanceAudioBus() {
		if (resonance_api) {
			delete resonance_api;
			resonance_api = nullptr;
		}
	}
};

class ResonanceAudioServer : public Object, public SpatialAudioServer {
	GDCLASS(ResonanceAudioServer, Object);

	static ResonanceAudioServer *singleton;

public:
	static ResonanceAudioServer *get_singleton() { return singleton; }

protected:
	static void _bind_methods() {}

private:
	RID_Owner<ResonanceAudioBus, true> bus_owner;
	RID master_bus;
	Transform3D _head_transform;

	ResonanceAudioBus *_get_bus() {
		return bus_owner.get_or_null(master_bus);
	}

public:
	RID create_bus() {
		RID ret = bus_owner.make_rid();
		ResonanceAudioBus *ptr = bus_owner.get_or_null(ret);
		ERR_FAIL_NULL_V(ptr, RID());
		ptr->set_self(ret);
		master_bus = ret;
		return ret;
	}

	AudioSourceId create_source() override {
		ResonanceAudioBus *bus = _get_bus();
		return bus ? bus->register_audio_source() : AudioSourceId(-1);
	}
	void destroy_source(AudioSourceId source) override {
		ResonanceAudioBus *bus = _get_bus();
		if (bus) {
			bus->unregister_audio_source(source);
		}
	}
	AudioSourceId register_stereo_audio_source() {
		ResonanceAudioBus *bus = _get_bus();
		return bus ? bus->register_stereo_audio_source() : AudioSourceId(-1);
	}

	void set_source_transform(AudioSourceId source, const Transform3D &transform) override {
		ResonanceAudioBus *bus = _get_bus();
		if (bus) {
			bus->set_source_transform(source, transform);
		}
	}
	void set_head_transform(const Transform3D &transform) override {
		_head_transform = transform;
		ResonanceAudioBus *bus = _get_bus();
		if (bus) {
			bus->set_head_transform(transform);
		}
	}
	Transform3D get_head_transform() const {
		return _head_transform;
	}

	void push_source_buffer(AudioSourceId source, int p_channel, int num_frames, AudioFrame *frames) override {
		ResonanceAudioBus *bus = _get_bus();
		if (bus) {
			bus->push_source_buffer(source, num_frames, frames);
		}
	}
	bool pull_listener_buffer(int p_channel, int num_frames, AudioFrame *frames) override {
		ResonanceAudioBus *bus = _get_bus();
		return bus ? bus->pull_listener_buffer(num_frames, frames) : false;
	}

	void set_source_attenuation(AudioSourceId source, float attenuation) override {
		ResonanceAudioBus *bus = _get_bus();
		if (bus) {
			bus->set_source_attenuation(source, attenuation);
		}
	}
	void set_source_volume(AudioSourceId source, float volume) {
		ResonanceAudioBus *bus = _get_bus();
		if (bus) {
			bus->set_source_volume(source, volume);
		}
	}
	void set_source_directivity(AudioSourceId source, float alpha, float order) {
		ResonanceAudioBus *bus = _get_bus();
		if (bus) {
			bus->set_source_directivity(source, alpha, order);
		}
	}
	void set_source_spread(AudioSourceId source, float spread_deg) {
		ResonanceAudioBus *bus = _get_bus();
		if (bus) {
			bus->set_source_spread(source, spread_deg);
		}
	}
	void set_source_distance_model(AudioSourceId source, float min_distance, float max_distance) {
		ResonanceAudioBus *bus = _get_bus();
		if (bus) {
			bus->set_source_distance_model(source, min_distance, max_distance);
		}
	}
	void set_source_occlusion(AudioSourceId source, float intensity) {
		ResonanceAudioBus *bus = _get_bus();
		if (bus) {
			bus->set_source_occlusion(source, intensity);
		}
	}
	void set_source_near_field_gain(AudioSourceId source, float gain) {
		ResonanceAudioBus *bus = _get_bus();
		if (bus) {
			bus->set_source_near_field_gain(source, gain);
		}
	}
	void set_source_room_effects_gain(AudioSourceId source, float gain) {
		ResonanceAudioBus *bus = _get_bus();
		if (bus) {
			bus->set_source_room_effects_gain(source, gain);
		}
	}

	// Room effects
	void enable_room_effects(bool enable) {
		ResonanceAudioBus *bus = _get_bus();
		if (bus) {
			bus->enable_room_effects(enable);
		}
	}
	void set_reflection_properties(float gain) {
		ResonanceAudioBus *bus = _get_bus();
		if (bus) {
			bus->set_reflection_properties(gain);
		}
	}
	void set_reverb_properties(float rt60_values[9], float gain) {
		ResonanceAudioBus *bus = _get_bus();
		if (bus) {
			bus->set_reverb_properties(rt60_values, gain);
		}
	}

	ResonanceAudioServer() {
		singleton = this;
		master_bus = create_bus();
	}
	~ResonanceAudioServer() {
		bus_owner.free(master_bus);
		singleton = nullptr;
	}
};

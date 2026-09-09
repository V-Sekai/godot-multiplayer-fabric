/**************************************************************************/
/*  resonance_audio_room.h                                                */
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

#include "scene/3d/node_3d.h"

class ResonanceAudioRoom : public Node3D {
	GDCLASS(ResonanceAudioRoom, Node3D);

	bool room_effects_enabled = true;
	float reflection_gain = 1.0f;
	float reverb_gain = 1.0f;
	float reverb_brightness = 0.0f;
	float reverb_time_scale = 1.0f;

	int wall_material = 1; // Default: kPlasterSmooth

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_room_effects_enabled(bool p_enabled);
	bool get_room_effects_enabled() const;

	void set_reflection_gain(float p_gain);
	float get_reflection_gain() const;

	void set_reverb_gain(float p_gain);
	float get_reverb_gain() const;

	void set_reverb_brightness(float p_brightness);
	float get_reverb_brightness() const;

	void set_reverb_time_scale(float p_scale);
	float get_reverb_time_scale() const;

	void set_wall_material(int p_material);
	int get_wall_material() const;

	void update_room_from_colliders();

	ResonanceAudioRoom() = default;
};

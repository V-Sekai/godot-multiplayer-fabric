/**************************************************************************/
/*  xr_grid_gpu_trail_3d.h                                                */
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

#include "scene/3d/gpu_particles_3d.h"
#include "scene/resources/curve_texture.h"
#include "scene/resources/gradient_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

class XRGridGPUTrail3D : public GPUParticles3D {
	GDCLASS(XRGridGPUTrail3D, GPUParticles3D);

public:
	// Bitfield layout exposed to GDScript via BIND_BITFIELD_FLAG. Bit
	// positions match upstream GPUTrail3D.gd's `_set_flag(_, idx, v)`
	// for shader-side compatibility — the shader reads each bit by
	// index, so the order here is load-bearing.
	enum TrailFlags {
		FLAG_NONE = 0,
		FLAG_VERTICAL_TEXTURE = 1 << 0,
		FLAG_USE_RED_AS_ALPHA = 1 << 1,
		FLAG_BILLBOARD = 1 << 2,
		FLAG_DEWIGGLE = 1 << 3,
		FLAG_SNAP_TO_TRANSFORM = 1 << 4,
		FLAG_CLIP_OVERLAPS = 1 << 5,
	};

private:
	int length = 100;
	Ref<Texture2D> texture;
	Ref<GradientTexture1D> color_ramp;
	Ref<CurveTexture> curve;
	bool vertical_texture = false;
	bool use_red_as_alpha = false;
	bool billboard = false;
	bool dewiggle = true;
	bool clip_overlaps = true;
	bool snap_to_transform = false;
	int flags = 0;

	Vector3 old_pos;
	Transform3D billboard_transform;

	void _refresh_flags();
	void _refresh_draw_pass_param(const String &p_name, const Variant &p_value);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	XRGridGPUTrail3D() = default;

	void set_trail_length(int p_v);
	int get_trail_length() const { return length; }
	void set_texture(const Ref<Texture2D> &p_t);
	Ref<Texture2D> get_texture() const { return texture; }
	void set_color_ramp(const Ref<GradientTexture1D> &p_t);
	Ref<GradientTexture1D> get_color_ramp() const { return color_ramp; }
	void set_curve(const Ref<CurveTexture> &p_t);
	Ref<CurveTexture> get_curve() const { return curve; }
	void set_vertical_texture(bool p_v);
	bool get_vertical_texture() const { return vertical_texture; }
	void set_use_red_as_alpha(bool p_v);
	bool get_use_red_as_alpha() const { return use_red_as_alpha; }
	void set_billboard(bool p_v);
	bool get_billboard() const { return billboard; }
	void set_dewiggle(bool p_v);
	bool get_dewiggle() const { return dewiggle; }
	void set_clip_overlaps(bool p_v);
	bool get_clip_overlaps() const { return clip_overlaps; }
	void set_snap_to_transform(bool p_v);
	bool get_snap_to_transform() const { return snap_to_transform; }

	int get_flags() const { return flags; }
};

VARIANT_BITFIELD_CAST(XRGridGPUTrail3D::TrailFlags);

/**************************************************************************/
/*  xr_grid_gpu_trail_3d.cpp                                              */
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

#include "xr_grid_gpu_trail_3d.h"

#include "core/object/class_db.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/mesh.h"
#include "scene/resources/shader.h"

// Vendored shaders (celyk / V-Sekai.xr-grid GPUTrail-main).
// Originally loaded via `preload("res://addons/GPUTrail-main/shaders/*.gdshader")`;
// embedded here as a raw string so the engine module is asset-free and the
// trail works regardless of whether the addon tree ships in the project.
namespace {

constexpr const char TRAIL_PROCESS_SHADER[] = R"GLSL(
shader_type particles;

render_mode keep_data,disable_force,disable_velocity;

void process() {
	// CUSTOM.w tracks the particles place in the trail, in range (0..LIFETIME]
	// requires that LIFETIME = number of particles
	const float amount = LIFETIME;

	vec4 a = EMISSION_TRANSFORM * vec4(0,1,0,1);
	vec4 b = EMISSION_TRANSFORM * vec4(0,-1,0,1);

	// start
	if(CUSTOM.w == 0.0){
		CUSTOM.w = float(INDEX)+1.0;
		// needed to pass to draw pass
		CUSTOM.z = amount;
		// needed to initialize in case of CUSTOM.w == 2.0
		TRANSFORM = mat4(a,a,b,b);
	}

	// restart
	if(CUSTOM.w == amount+1.0){
		CUSTOM.w = 1.0;
	}

	if(CUSTOM.w == 1.0){
		// sets the quad to the line to cache this frame, it is not yet visible
		TRANSFORM = mat4(a,a,b,b);
	}

	if(CUSTOM.w == 2.0){
		// sets the right edge of the quad
		TRANSFORM[1] = a;
		TRANSFORM[2] = b;
	}

	CUSTOM.w++;
}
)GLSL";

constexpr const char TRAIL_DRAW_PASS_SHADER[] = R"GLSL(
shader_type spatial;

render_mode unshaded,world_vertex_coords,cull_disabled;

uniform sampler2D tex : repeat_disable, source_color, hint_default_white;
uniform sampler2D color_ramp : repeat_disable, source_color, hint_default_white;
uniform sampler2D curve : repeat_disable, hint_default_white;
uniform mat4 emmission_transform = mat4(1);
uniform int flags = 0;

#define vertical_texture  bool(flags & 1)
#define use_red_as_alpha  bool(flags & 2)
#define billboard         bool(flags & 4)
#define dewiggle          bool(flags & 8)
#define snap_to_transform bool(flags & 16)
#define clip_overlaps     bool(flags & 32)

varying float scale_interp;
varying vec2 clip;
varying vec2 mesh_uv;
void vertex(){
	mesh_uv = UV;

	mat4 my_model_matrix = MODEL_MATRIX;
	if(snap_to_transform && INSTANCE_CUSTOM.w==2.0){
		my_model_matrix[1] = emmission_transform * vec4(0,1,0,1);
		my_model_matrix[2] = emmission_transform * vec4(0,-1,0,1);
	}

	if(billboard){
		vec3 t0 = my_model_matrix[0].xyz-my_model_matrix[3].xyz;
		vec3 t1 = my_model_matrix[1].xyz-my_model_matrix[2].xyz;

		vec3 up0 = length(t0)*normalize(
			cross(
				my_model_matrix[3].xyz-INV_VIEW_MATRIX[3].xyz,
				t0));
		vec3 up1 = length(t1)*normalize(
			cross(
				my_model_matrix[2].xyz-INV_VIEW_MATRIX[3].xyz,
				t1));

		my_model_matrix[0] = my_model_matrix[3];
		my_model_matrix[1] = my_model_matrix[2];

		my_model_matrix[0].xyz += up0;
		my_model_matrix[3].xyz -= up0;

		my_model_matrix[1].xyz += up1;
		my_model_matrix[2].xyz -= up1;
	}

	vec3 a = mix(my_model_matrix[1].xyz,my_model_matrix[0].xyz,UV.x);
	vec3 b = mix(my_model_matrix[2].xyz,my_model_matrix[3].xyz,UV.x);

	UV.x = (UV.x + INSTANCE_CUSTOM.w-1.0 - 2.0)/(INSTANCE_CUSTOM.z-1.0);

	float h = textureLod(curve, vec2(UV.x), 0.0).x;

	VERTEX = mix(a,b,(UV.y-0.5)*h + 0.5);

	if(dewiggle){
		scale_interp = h;
		UV *= scale_interp;
	}

	clip.x = dot(VERTEX - INV_VIEW_MATRIX[3].xyz,cross(my_model_matrix[1].xyz - INV_VIEW_MATRIX[3].xyz,my_model_matrix[2].xyz - INV_VIEW_MATRIX[3].xyz));
	clip.y = dot(VERTEX - INV_VIEW_MATRIX[3].xyz,cross(my_model_matrix[3].xyz - INV_VIEW_MATRIX[3].xyz,my_model_matrix[0].xyz - INV_VIEW_MATRIX[3].xyz));
}

void fragment(){
	vec2 clip0 = clip;
	float ababab = clip0.x*clip0.y;
	if(clip_overlaps && ababab < 0.0) {
		if(abs(mesh_uv.x-0.5)<0.5)
			discard;
	}

	vec2 base_uv = UV;

	if(dewiggle){
		base_uv /= scale_interp;
	}

	vec2 raw_uv = base_uv;

	if(vertical_texture){
		base_uv = base_uv.yx;
	}

	vec4 T = textureLod(tex, base_uv, 0.0);
	ALBEDO = T.rgb;
	ALPHA = T.a;

	if(use_red_as_alpha){
		ALBEDO = vec3(1);
		ALPHA = T.r;
	}

	T = textureLod(color_ramp, raw_uv, 0.0);
	ALBEDO *= T.rgb;
	ALPHA *= T.a;
}
)GLSL";

} // namespace

void XRGridGPUTrail3D::_refresh_flags() {
	flags = FLAG_NONE;
	if (vertical_texture) {
		flags |= FLAG_VERTICAL_TEXTURE;
	}
	if (use_red_as_alpha) {
		flags |= FLAG_USE_RED_AS_ALPHA;
	}
	if (billboard) {
		flags |= FLAG_BILLBOARD;
	}
	if (dewiggle) {
		flags |= FLAG_DEWIGGLE;
	}
	if (snap_to_transform) {
		flags |= FLAG_SNAP_TO_TRANSFORM;
	}
	if (clip_overlaps) {
		flags |= FLAG_CLIP_OVERLAPS;
	}
	_refresh_draw_pass_param("flags", flags);
}

void XRGridGPUTrail3D::_refresh_draw_pass_param(const String &p_name, const Variant &p_value) {
	Ref<Mesh> dp = get_draw_pass_mesh(0);
	if (dp.is_null()) {
		return;
	}
	Ref<Material> mat = dp->surface_get_material(0);
	if (mat.is_null()) {
		return;
	}
	Ref<ShaderMaterial> sm = mat;
	if (sm.is_valid()) {
		sm->set_shader_parameter(p_name, p_value);
	}
}

void XRGridGPUTrail3D::set_trail_length(int p_v) {
	length = MAX(1, p_v);
	set_amount(length);
	set_lifetime(double(length));
	restart();
}

void XRGridGPUTrail3D::set_texture(const Ref<Texture2D> &p_t) {
	texture = p_t;
	_refresh_draw_pass_param("tex", texture);
}

void XRGridGPUTrail3D::set_color_ramp(const Ref<GradientTexture1D> &p_t) {
	color_ramp = p_t;
	_refresh_draw_pass_param("color_ramp", color_ramp);
}

void XRGridGPUTrail3D::set_curve(const Ref<CurveTexture> &p_t) {
	curve = p_t;
	_refresh_draw_pass_param("curve", curve);
}

void XRGridGPUTrail3D::set_vertical_texture(bool p_v) {
	vertical_texture = p_v;
	_refresh_flags();
}
void XRGridGPUTrail3D::set_use_red_as_alpha(bool p_v) {
	use_red_as_alpha = p_v;
	_refresh_flags();
}
void XRGridGPUTrail3D::set_billboard(bool p_v) {
	billboard = p_v;
	_refresh_flags();
}
void XRGridGPUTrail3D::set_dewiggle(bool p_v) {
	dewiggle = p_v;
	_refresh_flags();
}
void XRGridGPUTrail3D::set_clip_overlaps(bool p_v) {
	clip_overlaps = p_v;
	_refresh_flags();
}
void XRGridGPUTrail3D::set_snap_to_transform(bool p_v) {
	snap_to_transform = p_v;
	_refresh_flags();
}

void XRGridGPUTrail3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_amount(length);
			set_lifetime(double(length));
			set_explosiveness_ratio(1.0);
			set_fixed_fps(0);

			// Build the process material from the vendored particle shader.
			if (get_process_material().is_null()) {
				Ref<Shader> proc_shader;
				proc_shader.instantiate();
				proc_shader->set_code(String::utf8(TRAIL_PROCESS_SHADER));
				Ref<ShaderMaterial> proc_mat;
				proc_mat.instantiate();
				proc_mat->set_shader(proc_shader);
				set_process_material(proc_mat);
			}

			// Build the draw pass mesh + draw pass material from the
			// vendored spatial shader. resource_local_to_scene matches the
			// upstream so per-instance shader_parameter writes don't
			// alias across multiple GPUTrails in the same scene.
			if (get_draw_pass_mesh(0).is_null()) {
				Ref<QuadMesh> quad;
				quad.instantiate();
				Ref<Shader> draw_shader;
				draw_shader.instantiate();
				draw_shader->set_code(String::utf8(TRAIL_DRAW_PASS_SHADER));
				Ref<ShaderMaterial> draw_mat;
				draw_mat.instantiate();
				draw_mat->set_shader(draw_shader);
				draw_mat->set_local_to_scene(true);
				quad->set_material(draw_mat);
				set_draw_pass_mesh(0, quad);
			}

			_refresh_flags();
			if (texture.is_valid()) {
				_refresh_draw_pass_param("tex", texture);
			}
			if (color_ramp.is_valid()) {
				_refresh_draw_pass_param("color_ramp", color_ramp);
			}
			if (curve.is_valid()) {
				_refresh_draw_pass_param("curve", curve);
			}

			old_pos = get_global_position();
			billboard_transform = get_global_transform();
			set_process(true);
		} break;
		case NOTIFICATION_PROCESS:
			if (snap_to_transform) {
				_refresh_draw_pass_param("emmission_transform", get_global_transform());
			}
			old_pos = get_global_position();
			break;
		default:
			break;
	}
}

void XRGridGPUTrail3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_trail_length", "v"), &XRGridGPUTrail3D::set_trail_length);
	ClassDB::bind_method(D_METHOD("get_trail_length"), &XRGridGPUTrail3D::get_trail_length);
	ClassDB::bind_method(D_METHOD("set_texture", "t"), &XRGridGPUTrail3D::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &XRGridGPUTrail3D::get_texture);
	ClassDB::bind_method(D_METHOD("set_color_ramp", "t"), &XRGridGPUTrail3D::set_color_ramp);
	ClassDB::bind_method(D_METHOD("get_color_ramp"), &XRGridGPUTrail3D::get_color_ramp);
	ClassDB::bind_method(D_METHOD("set_curve", "t"), &XRGridGPUTrail3D::set_curve);
	ClassDB::bind_method(D_METHOD("get_curve"), &XRGridGPUTrail3D::get_curve);
	ClassDB::bind_method(D_METHOD("set_vertical_texture", "v"), &XRGridGPUTrail3D::set_vertical_texture);
	ClassDB::bind_method(D_METHOD("get_vertical_texture"), &XRGridGPUTrail3D::get_vertical_texture);
	ClassDB::bind_method(D_METHOD("set_use_red_as_alpha", "v"), &XRGridGPUTrail3D::set_use_red_as_alpha);
	ClassDB::bind_method(D_METHOD("get_use_red_as_alpha"), &XRGridGPUTrail3D::get_use_red_as_alpha);
	ClassDB::bind_method(D_METHOD("set_billboard", "v"), &XRGridGPUTrail3D::set_billboard);
	ClassDB::bind_method(D_METHOD("get_billboard"), &XRGridGPUTrail3D::get_billboard);
	ClassDB::bind_method(D_METHOD("set_dewiggle", "v"), &XRGridGPUTrail3D::set_dewiggle);
	ClassDB::bind_method(D_METHOD("get_dewiggle"), &XRGridGPUTrail3D::get_dewiggle);
	ClassDB::bind_method(D_METHOD("set_clip_overlaps", "v"), &XRGridGPUTrail3D::set_clip_overlaps);
	ClassDB::bind_method(D_METHOD("get_clip_overlaps"), &XRGridGPUTrail3D::get_clip_overlaps);
	ClassDB::bind_method(D_METHOD("set_snap_to_transform", "v"), &XRGridGPUTrail3D::set_snap_to_transform);
	ClassDB::bind_method(D_METHOD("get_snap_to_transform"), &XRGridGPUTrail3D::get_snap_to_transform);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "trail_length"),
			"set_trail_length", "get_trail_length");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture",
						 PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"),
			"set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "color_ramp",
						 PROPERTY_HINT_RESOURCE_TYPE, "GradientTexture1D"),
			"set_color_ramp", "get_color_ramp");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "curve",
						 PROPERTY_HINT_RESOURCE_TYPE, "CurveTexture"),
			"set_curve", "get_curve");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "vertical_texture"),
			"set_vertical_texture", "get_vertical_texture");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_red_as_alpha"),
			"set_use_red_as_alpha", "get_use_red_as_alpha");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "billboard"),
			"set_billboard", "get_billboard");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "dewiggle"),
			"set_dewiggle", "get_dewiggle");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "clip_overlaps"),
			"set_clip_overlaps", "get_clip_overlaps");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "snap_to_transform"),
			"set_snap_to_transform", "get_snap_to_transform");

	ClassDB::bind_method(D_METHOD("get_flags"), &XRGridGPUTrail3D::get_flags);

	BIND_BITFIELD_FLAG(FLAG_VERTICAL_TEXTURE);
	BIND_BITFIELD_FLAG(FLAG_USE_RED_AS_ALPHA);
	BIND_BITFIELD_FLAG(FLAG_BILLBOARD);
	BIND_BITFIELD_FLAG(FLAG_DEWIGGLE);
	BIND_BITFIELD_FLAG(FLAG_SNAP_TO_TRANSFORM);
	BIND_BITFIELD_FLAG(FLAG_CLIP_OVERLAPS);
}

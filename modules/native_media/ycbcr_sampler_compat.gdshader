// NV12 -> RGB fragment shader for the Compatibility (OpenGL / GLES3) renderer.
//
// native_media uses this when running on the Compatibility backend (which does
// not expose RenderingDevice). Bind src_y (R8, full resolution) and src_cbcr
// (RG8, half resolution); fragment output is sRGB-correct RGB.
//
// This stays in sync with ycbcr_sampler.glsl (the compute version that runs on
// Vulkan / D3D12 / Metal).

shader_type canvas_item;
render_mode unshaded;

uniform sampler2D src_y : filter_linear;
uniform sampler2D src_cbcr : filter_linear;
uniform int color_matrix : hint_range(0, 2) = 1; // 0=BT.601, 1=BT.709, 2=BT.2020
uniform bool full_range = false;

vec3 ycbcr_to_rgb(float y_in, float cb_in, float cr_in) {
	float Y, Cb, Cr;
	if (full_range) {
		Y = y_in;
		Cb = cb_in - 0.5;
		Cr = cr_in - 0.5;
	} else {
		Y = (y_in - 16.0 / 255.0) * (255.0 / 219.0);
		Cb = (cb_in - 128.0 / 255.0) * (255.0 / 224.0);
		Cr = (cr_in - 128.0 / 255.0) * (255.0 / 224.0);
	}

	vec3 rgb;
	if (color_matrix == 0) {
		rgb.r = Y + 1.402 * Cr;
		rgb.g = Y - 0.344136 * Cb - 0.714136 * Cr;
		rgb.b = Y + 1.772 * Cb;
	} else if (color_matrix == 2) {
		rgb.r = Y + 1.4746 * Cr;
		rgb.g = Y - 0.16455 * Cb - 0.57135 * Cr;
		rgb.b = Y + 1.8814 * Cb;
	} else {
		rgb.r = Y + 1.5748 * Cr;
		rgb.g = Y - 0.187324 * Cb - 0.468124 * Cr;
		rgb.b = Y + 1.8556 * Cb;
	}
	return clamp(rgb, vec3(0.0), vec3(1.0));
}

void fragment() {
	float y = texture(src_y, UV).r;
	vec2 cbcr = texture(src_cbcr, UV).rg;
	COLOR = vec4(ycbcr_to_rgb(y, cbcr.r, cbcr.g), 1.0);
}

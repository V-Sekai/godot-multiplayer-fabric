#[compute]
#version 450

// Convert NV12 (Y plane + interleaved CbCr plane) to RGBA8.
//
// Runs on every Godot RenderingDevice backend (Vulkan, D3D12, Metal). For the
// OpenGL Compatibility renderer (which does not expose RenderingDevice) the
// native_media path falls back to a CanvasItem fragment shader equivalent —
// see modules/native_media/ycbcr_sampler_compat.gdshader.
//
// Inputs:
//   binding 0: src_y      — R8 texture, full resolution
//   binding 1: src_cbcr   — RG8 texture, half resolution (4:2:0 subsampled)
// Output:
//   binding 2: dst_rgba   — RGBA8 image, full resolution
//
// Push constant selects the color matrix: 0 = BT.601 (SD), 1 = BT.709 (HD),
// 2 = BT.2020 (UHD). Range (limited 16..235 / 16..240 vs full 0..255) is
// reported by the OS decoder and passed through.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D src_y;
layout(set = 0, binding = 1) uniform sampler2D src_cbcr;
layout(set = 0, binding = 2, rgba8) uniform writeonly image2D dst_rgba;

layout(push_constant, std430) uniform Params {
	uint color_matrix; // 0 = BT.601, 1 = BT.709, 2 = BT.2020
	uint full_range; // 0 = limited, 1 = full
}
params;

vec3 ycbcr_to_rgb(float y, float cb, float cr, uint mat, uint full) {
	float Y, Cb, Cr;
	if (full == 1u) {
		Y = y;
		Cb = cb - 0.5;
		Cr = cr - 0.5;
	} else {
		Y = (y - 16.0 / 255.0) * (255.0 / 219.0);
		Cb = (cb - 128.0 / 255.0) * (255.0 / 224.0);
		Cr = (cr - 128.0 / 255.0) * (255.0 / 224.0);
	}

	vec3 rgb;
	if (mat == 0u) {
		rgb.r = Y + 1.402 * Cr;
		rgb.g = Y - 0.344136 * Cb - 0.714136 * Cr;
		rgb.b = Y + 1.772 * Cb;
	} else if (mat == 2u) {
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

void main() {
	ivec2 size = imageSize(dst_rgba);
	ivec2 px = ivec2(gl_GlobalInvocationID.xy);
	if (px.x >= size.x || px.y >= size.y) {
		return;
	}

	vec2 uv = (vec2(px) + 0.5) / vec2(size);
	float y = texture(src_y, uv).r;
	vec2 cbcr = texture(src_cbcr, uv).rg;

	vec3 rgb = ycbcr_to_rgb(y, cbcr.r, cbcr.g, params.color_matrix, params.full_range);
	imageStore(dst_rgba, px, vec4(rgb, 1.0));
}

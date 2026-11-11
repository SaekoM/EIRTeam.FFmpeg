#[compute]
#version 450

// 10-bit twin of yuv_to_rgb.glsl.
//
// It exists as a separate file rather than a branch inside the 8-bit shader because a storage image's
// format qualifier (r8 / r16) has to match the format of the texture view bound to it. One shader
// cannot serve both, so there are two pipelines and the converter picks by bit depth.
//
// Godot's engine build could express this as #[versions] in a single file, but the GDExtension path
// deliberately skips version parsing (see _ensure_pipeline), so two plain files work on both.

// Invocations in the (x, y, z) dimension
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Our textures. r16 rather than r8 - the planes arrive as 16-bit samples.
layout(r16, set = 0, binding = 0) uniform restrict readonly image2D tex_y;
layout(r16, set = 1, binding = 0) uniform restrict readonly image2D tex_u;
layout(r16, set = 2, binding = 0) uniform restrict readonly image2D tex_v;
layout(r16, set = 3, binding = 0) uniform restrict readonly image2D tex_a;
layout(rgba8, set = 4, binding = 0) uniform restrict writeonly image2D output_image;

// Set per video by YUVGPUConverter (fill_colour_constants): the video's range and colour standard. Matches the C++
// PushConstant member for member.
layout(push_constant, std430) uniform Params {
	uint use_alpha;
	float luma_offset;
	float luma_scale;
	float chroma_offset;
	float chroma_scale;
	float red_from_v;
	float green_from_u;
	float green_from_v;
	float blue_from_u;
	float padding_0;
	float padding_1;
	float padding_2;
}
params;

// YUV420P10LE stores each sample in the LOW 10 bits of a 16-bit word, so values run 0-1023 rather than
// 0-65535. An r16_unorm read returns value/65535, which lands in 0.0-0.0156 and would render a nearly
// black picture. Scaling by 65535/1023 puts it back in 0.0-1.0.
const float TEN_BIT_SCALE = 65535.0 / 1023.0;

// The code we want to execute in each invocation
void main() {
	ivec2 uv = ivec2(gl_GlobalInvocationID.xy);
	ivec2 uv_chroma = ivec2(gl_GlobalInvocationID.xy) / 2;

	float y = imageLoad(tex_y, uv).r * TEN_BIT_SCALE;
	vec2 chroma;
	chroma.r = imageLoad(tex_u, uv_chroma).r * TEN_BIT_SCALE;
	chroma.g = imageLoad(tex_v, uv_chroma).r * TEN_BIT_SCALE;
	y = (y - params.luma_offset) * params.luma_scale;
	float u = (chroma.r - params.chroma_offset) * params.chroma_scale;
	float v = (chroma.g - params.chroma_offset) * params.chroma_scale;
	vec4 rgba;
	rgba.r = y + params.red_from_v * v;
	rgba.g = y + params.green_from_u * u + params.green_from_v * v;
	rgba.b = y + params.blue_from_u * u;
	rgba.rgb = clamp(rgba.rgb, 0.0, 1.0); // limited-range video may hold values just past black and white
	if (params.use_alpha != 0u) {
		rgba.a = imageLoad(tex_a, uv).r * TEN_BIT_SCALE;
	} else {
		rgba.a = 1.0;
	}
	imageStore(output_image, uv, rgba);
}

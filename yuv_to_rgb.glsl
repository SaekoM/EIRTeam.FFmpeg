#[compute]
#version 450

// Invocations in the (x, y, z) dimension
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Our textures
layout(r8, set = 0, binding = 0) uniform restrict readonly image2D tex_y;
layout(r8, set = 1, binding = 0) uniform restrict readonly image2D tex_u;
layout(r8, set = 2, binding = 0) uniform restrict readonly image2D tex_v;
layout(r8, set = 3, binding = 0) uniform restrict readonly image2D tex_a;
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

// The code we want to execute in each invocation
void main() {
	ivec2 uv = ivec2(gl_GlobalInvocationID.xy);
	ivec2 uv_chroma = ivec2(gl_GlobalInvocationID.xy) / 2;

	float y = imageLoad(tex_y, uv).r;
	vec2 chroma;
	chroma.r = imageLoad(tex_u, uv_chroma).r;
	chroma.g = imageLoad(tex_v, uv_chroma).r;
	y = (y - params.luma_offset) * params.luma_scale;
	float u = (chroma.r - params.chroma_offset) * params.chroma_scale;
	float v = (chroma.g - params.chroma_offset) * params.chroma_scale;
	vec4 rgba;
	rgba.r = y + params.red_from_v * v;
	rgba.g = y + params.green_from_u * u + params.green_from_v * v;
	rgba.b = y + params.blue_from_u * u;
	rgba.rgb = clamp(rgba.rgb, 0.0, 1.0); // limited-range video may hold values just past black and white
	if (params.use_alpha != 0u) {
		rgba.a = imageLoad(tex_a, uv).r;
	} else {
		rgba.a = 1.0;
	}
	imageStore(output_image, uv, rgba);
}
/**************************************************************************/
/*  ffmpeg_video_stream.h                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             EIRTeam.FFmpeg                             */
/*                         https://ph.eirteam.moe                         */
/**************************************************************************/
/* Copyright (c) 2023-present Álex Román (EIRTeam) & contributors.        */
/*                                                                        */
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

#ifndef FFMPEG_VIDEO_STREAM_H
#define FFMPEG_VIDEO_STREAM_H

#include "video_decoder.h"

#ifdef GDEXTENSION

// Headers for building as GDExtension plug-in.
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/classes/video_stream.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/templates/vector.hpp>

using namespace godot;

#else

#include "core/object/ref_counted.h"
#include "scene/resources/atlas_texture.h"
#include "scene/resources/texture_rd.h"
#include "scene/resources/video_stream.h"

#endif

class YUVGPUConverter : public RefCounted {
	RID shader;
	Ref<Image> yuv_plane_images[4];
	RID yuv_plane_textures[4];
	RID yuv_planes_uniform_sets[4];
	RID pipeline;
	Ref<Texture2DRD> out_texture;
	RID out_uniform_set;
	Vector2i frame_size;
	// 8 or 10. Decides three things that must move together: which compute shader is compiled, whether
	// the plane textures are R8_UNORM or R16_UNORM, and how many bytes per sample arrive from the
	// decoder. Changing it tears down the pipeline, the textures AND the uniform sets, because a
	// uniform set is created against a specific shader and cannot be rebound to another.
	//
	// TWO fields, because two threads. `pending_bit_depth` is written by the main thread as frames
	// arrive; `bit_depth` is what the GPU resources are actually built for and is only ever touched on
	// the RENDER thread, inside _convert_internal. Every other RD call in this class is marshalled
	// there by call_on_render_thread, and freeing RIDs from the main thread instead raced a conversion
	// that was already queued - which surfaced as "Attempted to free invalid ID".
	int bit_depth = 8;
	int pending_bit_depth = 8;

	// Written by the main thread as frames arrive, read on the render thread like the plane images.
	YUVColour colour;

	// Matches Params in both yuv_to_rgb shaders, member for member: 12 four-byte values, 48 bytes.
	struct PushConstant {
		uint32_t use_alpha;
		// Limited range is stretched to full: (sample - offset) * scale, luma and chroma separately.
		float luma_offset;
		float luma_scale;
		float chroma_offset;
		float chroma_scale;
		// The colour standard's matrix, as the four terms that aren't 0 or 1.
		float red_from_v;
		float green_from_u;
		float green_from_v;
		float blue_from_u;
		float padding[3];
	} push_constant;

private:
	void _ensure_pipeline();
	Error _ensure_plane_textures();
	Error _ensure_output_texture();
	RID _create_uniform_set(const RID &p_texture_rd_rid, int p_shader_set);
	void _upload_plane_images();
	void _clear_texture_internal();
	void _release_depth_dependent_resources();
	void _convert_internal();

public:
	// Records what the decoder is now producing. Cheap and thread-safe: the actual teardown happens on
	// the render thread the next time convert() runs.
	void set_bit_depth(int p_bit_depth);
	// The colour standard and range of the frames now arriving. Cheap; read when the next frame is converted.
	void set_colour(const YUVColour &p_colour);
	void set_plane_image(int p_plane_idx, Ref<Image> p_image);
	Vector2i get_frame_size() const;
	void set_frame_size(const Vector2i &p_frame_size);
	void convert();
	Ref<Texture2D> get_output_texture() const;
	void clear_output_texture();

	YUVGPUConverter();
	~YUVGPUConverter();
};

// We have to use this function redirection system for GDExtension because the naming conventions
// for the functions we are supposed to override are different there
#include "gdextension_build/func_redirect.h"
class FFmpegVideoStreamPlayback : public VideoStreamPlayback {
	GDCLASS(FFmpegVideoStreamPlayback, VideoStreamPlayback);

	const int LENIENCE_BEFORE_SEEK = 2500;
	// How far ahead of the playhead an audio frame may be handed to Godot's ring buffer.
	// Handing audio over only once it is due leaves the mixer with almost no headroom:
	// delivery happens once per rendered frame, so any main-thread hitch longer than a
	// frame starves the ring and Godot fades the rest of its mix block to silence — an
	// audible crackle (issue #9). The ring drains in real time, so pushing early only
	// builds headroom; it does not shift A/V sync.
	const double AUDIO_PREBUFFER_MS = 100.0;
	double playback_position = 0.0f;

	Ref<VideoDecoder> decoder;
	List<Ref<DecodedFrame>> available_frames;
	List<Ref<DecodedAudioFrame>> available_audio_frames;
	Ref<DecodedFrame> last_frame;
#ifndef FFMPEG_MT_GPU_UPLOAD
	Ref<ImageTexture> last_frame_texture;
#endif
	Ref<Image> last_frame_image;
	Ref<ImageTexture> texture;
	Ref<Texture2DRD> yuv_texture;
	bool looping = false;
	bool buffering = false;
	int frames_processed = 0;
	void seek_into_sync();
	double get_current_frame_time();
	bool check_next_frame_valid(Ref<DecodedFrame> p_decoded_frame);
	bool check_next_audio_frame_valid(Ref<DecodedAudioFrame> p_decoded_frame);
	bool paused = false;
	bool playing = false;
	bool just_seeked = false;

	Ref<YUVGPUConverter> yuv_converter;

private:
	bool is_paused_internal() const;
	void update_internal(double p_delta);
	bool is_playing_internal() const;
	void set_paused_internal(bool p_paused);
	void play_internal();
	void stop_internal();
	void seek_internal(double p_time);
	double get_length_internal() const;
	Ref<Texture2D> get_texture_internal() const;
	double get_playback_position_internal() const;
	int get_mix_rate_internal() const;
	int get_channels_internal() const;

protected:
	void clear();
	static void _bind_methods(){}; // Required by GDExtension, do not remove

public:
	Error load(Ref<FileAccess> p_file_access);

	STREAM_FUNC_REDIRECT_0_CONST(bool, is_paused);
	STREAM_FUNC_REDIRECT_1(void, update, double, p_delta);
	STREAM_FUNC_REDIRECT_0_CONST(bool, is_playing);
	STREAM_FUNC_REDIRECT_1(void, set_paused, bool, p_paused);
	STREAM_FUNC_REDIRECT_0(void, play);
	STREAM_FUNC_REDIRECT_0(void, stop);
	STREAM_FUNC_REDIRECT_1(void, seek, double, p_time);
	STREAM_FUNC_REDIRECT_0_CONST(double, get_length);
	STREAM_FUNC_REDIRECT_0_CONST(Ref<Texture2D>, get_texture);
	STREAM_FUNC_REDIRECT_0_CONST(double, get_playback_position);
	STREAM_FUNC_REDIRECT_0_CONST(int, get_mix_rate);
	STREAM_FUNC_REDIRECT_0_CONST(int, get_channels);
	FFmpegVideoStreamPlayback();
};

class FFmpegVideoStream : public VideoStream {
	GDCLASS(FFmpegVideoStream, VideoStream);

protected:
	static void _bind_methods(){}; // Required by GDExtension, do not remove
	Ref<VideoStreamPlayback> instantiate_playback_internal() {
		Ref<FileAccess> fa = FileAccess::open(get_file(), FileAccess::READ);
		if (!fa.is_valid()) {
			return Ref<VideoStreamPlayback>();
		}
		Ref<FFmpegVideoStreamPlayback> pb;
		pb.instantiate();
		if (pb->load(fa) != OK) {
			return nullptr;
		}
		return pb;
	}

public:
	STREAM_FUNC_REDIRECT_0(Ref<VideoStreamPlayback>, instantiate_playback);
};

#endif // FFMPEG_VIDEO_STREAM_H

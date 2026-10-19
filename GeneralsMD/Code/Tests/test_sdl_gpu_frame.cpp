/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// A3a's frame (PosixDevice/Render/SdlGpuFrame), on this machine's GPU, offscreen, read back:
//   - a colour clear lands as D3D9's ARGB in the back buffer's B8G8R8A8 bytes, every pixel;
//   - a depth-only clear leaves the colour as it was (the colour is loaded, not cleared);
//   - the gamma pass maps each channel through its own ramp, on a picture where every pixel differs, so
//     a flipped, shifted or channel-swapped pass cannot match; the identity ramp's blit gives the picture
//     back unchanged;
//   - an armed control: the gamma output compared against the wrong ramp must not match.
//
// WHAT THIS DOES NOT PROVE:
//   - Presenting to a window: the swap chain needs a display, and ctest has none.  The window's path is
//     the same pass into the swap chain's texture instead of this test's.
//   - Any GPU but this one.  Exits 77, and ctest reports it skipped, where SDL has no GPU device.

#include "SdlGpuFrame.h"
#include "SdlPipelineCache.h"
#include "../Libraries/Source/WWVegas/WW3D2/sdl3shadercompile.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <cstring>

static int failures = 0;

#define CHECK(condition) \
	do { if (!(condition)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static const unsigned int WIDTH = 64;
static const unsigned int HEIGHT = 48;

// B, G, R and A of pixel (x, y) in the gamma test's picture: no two pixels alike, no channel alike.
static void pattern(unsigned int x, unsigned int y, uint8_t out[4])
{
	out[0] = (uint8_t)(x * 4);
	out[1] = (uint8_t)(y * 5);
	out[2] = (uint8_t)((x * 3 + y * 7) & 0xFF);
	out[3] = 0xFF;
}

// How many pixels are more than one level from expected in any channel.
static unsigned int mismatches(const std::vector<uint8_t> &got, const uint16_t (*ramp)[256])
{
	unsigned int bad = 0;
	for (unsigned int y = 0; y < HEIGHT; ++y) {
		for (unsigned int x = 0; x < WIDTH; ++x) {
			uint8_t in[4];
			pattern(x, y, in);
			const uint8_t *px = &got[(y * WIDTH + x) * 4];
			// The ramp is indexed R, G, B; the bytes are B, G, R.
			const int expect[4] = {
				(int)((ramp[2][in[0]] * 255u + 32767u) / 65535u),
				(int)((ramp[1][in[1]] * 255u + 32767u) / 65535u),
				(int)((ramp[0][in[2]] * 255u + 32767u) / 65535u),
				in[3] };
			for (int c = 0; c < 4; ++c) {
				if (abs((int)px[c] - expect[c]) > 1) {
					++bad;
					break;
				}
			}
		}
	}
	return bad;
}

#if defined(__linux__)
class DepthObject
{
public:
	SDL_GPUShader *Vertex = NULL, *Pixel = NULL;
	SDL_GPUGraphicsPipeline *Pipeline = NULL;
	SdlPipelineCache *Cache = NULL;
	SdlGpuFrame *Frame;
	explicit DepthObject(SdlGpuFrame *frame, bool sun = false) : Frame(frame)
	{
		const char *vertex =
			"cbuffer Object : register(b0, space1) { float4 Depth; };\n"
			"float4 main(uint id : SV_VertexID) : SV_Position {\n"
			"float2 p = id == 0 ? float2(-0.75,-0.75) : (id == 1 ? float2(0.75,-0.75) : float2(0,0.75));\n"
			"return float4(p,Depth.x,1); }\n";
		const char *pixel = sun ? "void main() {}\n" : "float4 main() : SV_Target0 { return float4(0.8,0.2,0.1,1); }\n";
		std::vector<unsigned char> spirv;
		std::string error;
		CHECK(SDL3_Compile_HLSL_To_SPIRV(vertex, true, spirv, error));
		Vertex = SDL3_Create_Shader(frame->Device(), spirv, true, error);
		CHECK(Vertex != NULL);
		CHECK(SDL3_Compile_HLSL_To_SPIRV(pixel, false, spirv, error));
		Pixel = SDL3_Create_Shader(frame->Device(), spirv, false, error);
		CHECK(Pixel != NULL);
		SDL_GPUColorTargetDescription colour = {};
		colour.format = (SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();
		SDL_GPUGraphicsPipelineCreateInfo info = {};
		info.vertex_shader = Vertex;
		info.fragment_shader = Pixel;
		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
		info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
		info.depth_stencil_state.enable_depth_test = true;
		info.depth_stencil_state.enable_depth_write = true;
		info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
		info.target_info.color_target_descriptions = &colour;
		info.target_info.num_color_targets = 1;
		info.target_info.has_depth_stencil_target = true;
		info.target_info.depth_stencil_format = (SDL_GPUTextureFormat)frame->Depth_Format();
		if (sun) {
			// Exercise the production cache with no colour attachment and a distinct
			// depth format, not just a hand-built SDL pipeline.
			RenderUInt32 states[256] = {};
			states[D3DRS_FILLMODE] = D3DFILL_SOLID;
			states[D3DRS_CULLMODE] = D3DCULL_NONE;
			states[D3DRS_ZENABLE] = states[D3DRS_ZWRITEENABLE] = 1;
			states[D3DRS_ZFUNC] = D3DCMP_LESS;
			SdlPipelineKey key;
			CHECK(Sdl_Pipeline_Key(states, D3DPT_TRIANGLELIST, Vertex, Pixel, D3DFVF_XYZ,
				0, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, key, error));
			Cache = new SdlPipelineCache(frame->Device());
			Pipeline = Cache->Pipeline(key);
		} else Pipeline = SDL_CreateGPUGraphicsPipeline(frame->Device(), &info);
		CHECK(Pipeline != NULL);
	}
	~DepthObject()
	{
		Frame->Flush();
		if (Cache) delete Cache;
		else if (Pipeline) SDL_ReleaseGPUGraphicsPipeline(Frame->Device(), Pipeline);
		if (Vertex) SDL_ReleaseGPUShader(Frame->Device(), Vertex);
		if (Pixel) SDL_ReleaseGPUShader(Frame->Device(), Pixel);
	}
	void Draw(const SdlTarget &target, float depth)
	{
		if (!Pipeline) return;
		Frame->Set_Target(target);
		SdlRecordedDraw draw = {};
		draw.Pipeline = Pipeline;
		draw.Count = 3;
		memset(Frame->Stage(36, draw.VertexOffset), 0, 36);
		const float values[4] = { depth, 0, 0, 0 };
		draw.VertexConstants = Frame->Constants(0, values, sizeof(values));
		draw.VertexConstantsSize = sizeof(values);
		draw.Viewport[2] = (float)target.Width;
		draw.Viewport[3] = (float)target.Height;
		draw.Viewport[5] = 1;
		draw.Scissor[2] = target.Width;
		draw.Scissor[3] = target.Height;
		Frame->Record_Draw(draw);
	}
};

static void check_sun_depth_target(SdlGpuFrame *frame)
{
	SdlTarget sun;
	std::string error;
	CHECK(frame->Sun_Shadow_Target(32, sun, error));
	CHECK(sun.Colour == NULL && sun.Depth != NULL && sun.Width == 32 && sun.Height == 32);
	CHECK(frame->Target_Depth_Format(sun) == SDL_GPU_TEXTUREFORMAT_D32_FLOAT);
	if (!sun.Depth) return;
	DepthObject caster(frame, true);
	frame->Set_Target(frame->Back_Buffer_Target());
	frame->Clear_Back_Buffer(true, true, true, 0xff123456, 0.8f, 0);
	frame->Set_Target(sun);
	frame->Clear_Back_Buffer(false, true, false, 0, 1.0f, 0);
	caster.Draw(sun, 0.25f);
	frame->Set_Target(frame->Back_Buffer_Target());
	std::vector<uint8_t> pixels;
	CHECK(frame->Read_Back(frame->Back_Buffer_Target().Colour, WIDTH, HEIGHT, pixels));
	CHECK(pixels.size() == WIDTH * HEIGHT * 4);
	for (size_t i = 0; i + 3 < pixels.size(); i += 4)
		CHECK(pixels[i] == 0x56 && pixels[i+1] == 0x34 && pixels[i+2] == 0x12 && pixels[i+3] == 255);

	const char *vs = "float4 main(uint id:SV_VertexID):SV_Position { return float4(id==1?3:-1,id==2?3:-1,0,1); }";
	const char *ps =
		"Texture2D<float> depthMap:register(t0,space2); SamplerState pointSampler:register(s0,space2);"
		"cbuffer Size:register(b0,space3) { float4 size; };"
		"float4 main(float4 p:SV_Position):SV_Target0 { float d=depthMap.SampleLevel(pointSampler,p.xy/size.xy,0); return float4(d,d,d,1); }";
	std::vector<unsigned char> spirv;
	CHECK(SDL3_Compile_HLSL_To_SPIRV(vs, true, spirv, error));
	SDL_GPUShader *vertex = SDL3_Create_Shader(frame->Device(), spirv, true, error);
	CHECK(SDL3_Compile_HLSL_To_SPIRV(ps, false, spirv, error));
	SDL_GPUShader *pixel = SDL3_Create_Shader(frame->Device(), spirv, false, error);
	SDL_GPUColorTargetDescription colour = {};
	colour.format = (SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();
	SDL_GPUGraphicsPipelineCreateInfo info = {};
	info.vertex_shader = vertex; info.fragment_shader = pixel;
	info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	info.target_info.color_target_descriptions = &colour; info.target_info.num_color_targets = 1;
	info.target_info.has_depth_stencil_target = true;
	info.target_info.depth_stencil_format = (SDL_GPUTextureFormat)frame->Depth_Format();
	SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(frame->Device(), &info);
	SDL_GPUSamplerCreateInfo samplerInfo = {};
	samplerInfo.address_mode_u = samplerInfo.address_mode_v = samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	SDL_GPUSampler *sampler = SDL_CreateGPUSampler(frame->Device(), &samplerInfo);
	CHECK(vertex && pixel && pipeline && sampler);
	if (pipeline && sampler) {
		auto sample = [&](SDL_GPUTexture *texture) {
			const SdlTarget scene = frame->Back_Buffer_Target();
			frame->Set_Target(scene);
			SdlRecordedDraw draw = {};
			draw.Pipeline = pipeline; draw.Count = 3;
			memset(frame->Stage(16, draw.VertexOffset), 0, 16);
			const float size[4] = { (float)scene.Width, (float)scene.Height, 0, 0 };
			draw.PixelConstants = frame->Constants(1, size, sizeof(size)); draw.PixelConstantsSize = sizeof(size);
			draw.SamplerCount = 1; draw.Textures[0] = texture; draw.Samplers[0] = sampler;
			draw.Viewport[2] = draw.Scissor[2] = scene.Width;
			draw.Viewport[3] = draw.Scissor[3] = scene.Height; draw.Viewport[5] = 1;
			frame->Record_Draw(draw);
		};
		auto verify = [&](float expected) {
			const SdlTarget scene = frame->Back_Buffer_Target();
			CHECK(frame->Read_Back(scene.Colour, scene.Width, scene.Height, pixels));
			if (pixels.size() != scene.Width * scene.Height * 4) { CHECK(false); return; }
			CHECK(abs((int)pixels[((scene.Height/2)*scene.Width+scene.Width/2)*4] - (int)(expected*255+0.5f)) <= 1);
			CHECK(pixels[0] == 255); // clear depth outside the triangle
		};
		sample(sun.Depth);
		SdlTarget replacement;
		CHECK(frame->Sun_Shadow_Target(64, replacement, error));
		CHECK(replacement.Depth != sun.Depth);
		verify(0.25f); // queued sampling still owns the retired 32x32 resource
		frame->Set_Target(replacement);
		frame->Clear_Back_Buffer(false, true, false, 0, 1, 0);
		caster.Draw(replacement, 0.6f);
		sample(replacement.Depth);
		SdlTarget refused;
		CHECK(!frame->Sun_Shadow_Target(0, refused, error) && refused.Depth == NULL);
		CHECK(!frame->Sun_Shadow_Target(8193, refused, error) && refused.Depth == NULL);
		verify(0.6f);
		CHECK(frame->Resize(WIDTH+16, HEIGHT+8));
		SdlTarget kept;
		CHECK(frame->Sun_Shadow_Target(64, kept, error) && kept.Depth == replacement.Depth);
		sample(kept.Depth); verify(0.6f); // window resizing does not invalidate the sun allocation
		frame->Set_Target(kept);
		frame->Clear_Back_Buffer(false, true, false, 0, 1, 0);
		sample(kept.Depth); verify(1.0f); // empty capture clears stale caster depth
		CHECK(frame->Resize(WIDTH, HEIGHT));
	}
	frame->Set_Target(frame->Back_Buffer_Target());
	CHECK(frame->Flush());
	if (pipeline) SDL_ReleaseGPUGraphicsPipeline(frame->Device(), pipeline);
	if (sampler) SDL_ReleaseGPUSampler(frame->Device(), sampler);
	if (vertex) SDL_ReleaseGPUShader(frame->Device(), vertex);
	if (pixel) SDL_ReleaseGPUShader(frame->Device(), pixel);
	printf("sun depth target: depth-only draw/sample, scene preservation, empty clear, retirement and resize checked\n");
}

#endif // __linux__

int main()
{
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		printf("sdl_gpu_frame_selfcheck: SKIP - SDL_Init: %s\n", SDL_GetError());
		return 77;
	}
	std::string error;
	SdlGpuFrame *frame = SdlGpuFrame::Create(NULL, WIDTH, HEIGHT, error);
	if (frame == NULL) {
		printf("sdl_gpu_frame_selfcheck: SKIP - no GPU device here: %s\n", error.c_str());
		SDL_Quit();
		return 77;
	}
	printf("sdl_gpu_frame_selfcheck: %s\n", SDL_GetGPUDeviceDriver(frame->Device()));
#if defined(__linux__)
	check_sun_depth_target(frame);
#endif

	// A colour clear: D3DCOLOR 0x80402010 is A 0x80, R 0x40, G 0x20, B 0x10.
	frame->Clear_Back_Buffer(true, true, true, 0x80402010u, 1.0f, 0);
	std::vector<uint8_t> pixels;
	CHECK(frame->Read_Back(frame->Back_Buffer(), WIDTH, HEIGHT, pixels));
	unsigned int wrong = 0;
	for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
		if (pixels[i] != 0x10 || pixels[i + 1] != 0x20 || pixels[i + 2] != 0x40 || pixels[i + 3] != 0x80) {
			++wrong;
		}
	}
	CHECK(pixels.size() == WIDTH * HEIGHT * 4 && wrong == 0);

	// Depth and stencil only: the colour is loaded, so it stays.
	frame->Clear_Back_Buffer(false, true, true, 0xFFFFFFFFu, 0.5f, 7);
	CHECK(frame->Read_Back(frame->Back_Buffer(), WIDTH, HEIGHT, pixels));
	CHECK(pixels.size() >= 4 && pixels[0] == 0x10 && pixels[2] == 0x40);

	// The gamma pass, into a target of the test's.
	std::vector<uint8_t> picture(WIDTH * HEIGHT * 4);
	for (unsigned int y = 0; y < HEIGHT; ++y) {
		for (unsigned int x = 0; x < WIDTH; ++x) {
			pattern(x, y, &picture[(y * WIDTH + x) * 4]);
		}
	}
	CHECK(frame->Upload_Back_Buffer(picture));

	SDL_GPUTextureCreateInfo info;
	SDL_zero(info);
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = (SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	info.width = WIDTH;
	info.height = HEIGHT;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	SDL_GPUTexture *target = SDL_CreateGPUTexture(frame->Device(), &info);
	CHECK(target != NULL);

	uint16_t identity[3][256];
	uint16_t ramp[3][256];
	for (int i = 0; i < 256; ++i) {
		identity[0][i] = identity[1][i] = identity[2][i] = (uint16_t)(i * 257);
		ramp[0][i] = (uint16_t)((255 - i) * 257);	// red inverted
		ramp[1][i] = (uint16_t)(i * 257);			// green as it is
		ramp[2][i] = (uint16_t)((i / 2) * 257);		// blue halved
	}
	CHECK(Sdl_Gamma_Is_Identity(identity) && !Sdl_Gamma_Is_Identity(ramp));

	CHECK(frame->Present_To(target, WIDTH, HEIGHT, ramp));
	CHECK(frame->Read_Back(target, WIDTH, HEIGHT, pixels));
	const unsigned int gamma_wrong = mismatches(pixels, ramp);
	CHECK(gamma_wrong == 0);
	// The armed control: the same output against the identity must differ nearly everywhere.
	const unsigned int control = mismatches(pixels, identity);
	CHECK(control > WIDTH * HEIGHT / 2);

	// The identity ramp is a blit: the picture back as it went in.
	CHECK(frame->Present_To(target, WIDTH, HEIGHT, identity));
	CHECK(frame->Read_Back(target, WIDTH, HEIGHT, pixels));
	CHECK(pixels == picture);

	SDL_ReleaseGPUTexture(frame->Device(), target);
	delete frame;
	SDL_Quit();
	if (failures != 0) {
		printf("sdl_gpu_frame_selfcheck: %d FAILED (gamma: %u pixels wrong; control differed in %u)\n",
			failures, gamma_wrong, control);
		return 1;
	}
	printf("sdl_gpu_frame_selfcheck: clear, depth-only clear, gamma (control differs in %u of %u pixels) "
		"and the identity blit all hold\n", control, WIDTH * HEIGHT);
	return 0;
}

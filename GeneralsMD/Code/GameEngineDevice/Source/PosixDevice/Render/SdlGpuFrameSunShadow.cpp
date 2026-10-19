// Independent sun-depth allocation and diagnostic readback. No scene attachments are sampled.
#include "SdlGpuFrame.h"
#include "SdlSunShadowDebug.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>

// A depth-to-R32 pass avoids backend-specific raw depth download restrictions.
static const char SUN_DEPTH_VERTEX_HLSL[] =
	"struct Output\n"
	"{\n"
	"	float4 Position : SV_Position;\n"
	"	[[vk::location(0)]] float2 Uv : TEXCOORD0;\n"
	"};\n"
	"Output main(uint id : SV_VertexID)\n"
	"{\n"
	"	Output output;\n"
	"	float2 corner = float2(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0);\n"
	"	output.Position = float4(corner, 0.0, 1.0);\n"
	"	output.Uv = float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);\n"
	"	return output;\n"
	"}\n";

static const char SUN_DEPTH_PIXEL_HLSL[] =
	"Texture2D<float> SceneDepth : register(t0, space2);\n"
	"SamplerState DepthSampler : register(s0, space2);\n"
	"float main([[vk::location(0)]] float2 uv : TEXCOORD0) : SV_Target0\n"
	"{ return SceneDepth.SampleLevel(DepthSampler, uv, 0.0); }\n";

bool SdlGpuFrame::Sun_Shadow_Target(unsigned int size, SdlTarget &target, std::string &error)
{
	target = {};
	const SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
	const SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	if (size == 0 || size > 8192 || !SDL_GPUTextureSupportsFormat(GpuDevice, format, SDL_GPU_TEXTURETYPE_2D, usage)) {
		error = "unsupported sampleable sun depth target or size";
		return false;
	}
	if (!SunShadowTexture || SunShadowSize != size) {
		SDL_GPUTextureCreateInfo info = {};
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = format;
		info.usage = usage;
		info.width = info.height = size;
		info.layer_count_or_depth = info.num_levels = 1;
		info.sample_count = SDL_GPU_SAMPLECOUNT_1;
		SDL_GPUTexture *replacement = nullptr;
		if (Sdl_Sun_Shadow_Fault("target")) SDL_SetError("injected sun depth allocation failure");
		else replacement = SDL_CreateGPUTexture(GpuDevice, &info);
		if (!replacement) { error = SDL_GetError(); return false; }
		if (SunShadowTexture) Release_After_Batch(SunShadowTexture, NULL);
		SunShadowTexture = replacement;
		SunShadowSize = size;
	}
	target = { NULL, SunShadowTexture, size, size, (uint32_t)format };
	error.clear();
	return true;
}

bool SdlGpuFrame::Create_Sun_Depth_Reader()
{
	if (SunDepthPipeline && SunDepthSampler) return true;
	std::string log;
	if (!SunDepthVertex) {
		std::vector<unsigned char> spirv;
		if (!SDL3_Compile_HLSL_To_SPIRV(SUN_DEPTH_VERTEX_HLSL, true, spirv, log)
			|| !(SunDepthVertex = SDL3_Create_Shader(GpuDevice, spirv, true, log))) {
			fprintf(stderr, "SdlGpuFrame: sun depth reader vertex: %s\n", log.c_str());
			return false;
		}
	}
	if (!SunDepthPixel) {
		std::vector<unsigned char> spirv;
		if (!SDL3_Compile_HLSL_To_SPIRV(SUN_DEPTH_PIXEL_HLSL, false, spirv, log)
			|| !(SunDepthPixel = SDL3_Create_Shader(GpuDevice, spirv, false, log))) {
			fprintf(stderr, "SdlGpuFrame: sun depth reader pixel: %s\n", log.c_str());
			return false;
		}
	}
	if (!SunDepthPipeline) {
		SDL_GPUColorTargetDescription target = {};
		target.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
		SDL_GPUGraphicsPipelineCreateInfo info = {};
		info.vertex_shader = SunDepthVertex;
		info.fragment_shader = SunDepthPixel;
		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
		info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
		info.target_info.color_target_descriptions = &target;
		info.target_info.num_color_targets = 1;
		SunDepthPipeline = SDL_CreateGPUGraphicsPipeline(GpuDevice, &info);
	}
	if (!SunDepthSampler) {
		SDL_GPUSamplerCreateInfo sampler = {};
		sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
		sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
		sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		SunDepthSampler = SDL_CreateGPUSampler(GpuDevice, &sampler);
	}
	if (!SunDepthPipeline || !SunDepthSampler)
		fprintf(stderr, "SdlGpuFrame: sun depth reader resolve: %s\n", SDL_GetError());
	return SunDepthPipeline && SunDepthSampler;
}

bool SdlGpuFrame::Resolve_Sun_Depth(SDL_GPUCommandBuffer *commands, SDL_GPUTexture *source,
	SDL_GPUTexture *destination, unsigned int width, unsigned int height)
{
	SDL_GPUColorTargetInfo colour = {};
	colour.texture = destination;
	colour.load_op = SDL_GPU_LOADOP_DONT_CARE;
	colour.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &colour, 1, NULL);
	if (!pass) return false;
	SDL_BindGPUGraphicsPipeline(pass, SunDepthPipeline);
	const SDL_GPUTextureSamplerBinding binding = { source, SunDepthSampler };
	SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
	const SDL_GPUViewport viewport = { 0, 0, (float)width, (float)height, 0, 1 };
	SDL_SetGPUViewport(pass, &viewport);
	SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
	SDL_EndGPURenderPass(pass);
	return true;
}


void SdlGpuFrame::Release_Sun_Shadow()
{
    if (SunShadowTexture) SDL_ReleaseGPUTexture(GpuDevice, SunShadowTexture);
    if (SunDepthPipeline) SDL_ReleaseGPUGraphicsPipeline(GpuDevice, SunDepthPipeline);
    if (SunDepthVertex) SDL_ReleaseGPUShader(GpuDevice, SunDepthVertex);
    if (SunDepthPixel) SDL_ReleaseGPUShader(GpuDevice, SunDepthPixel);
    if (SunDepthSampler) SDL_ReleaseGPUSampler(GpuDevice, SunDepthSampler);
    SunShadowTexture = NULL;
    SunDepthPipeline = NULL;
    SunDepthVertex = SunDepthPixel = NULL;
    SunDepthSampler = NULL;
    SunShadowSize = 0;
}

bool SdlGpuFrame::Read_Depth(SDL_GPUTexture *texture, unsigned width, unsigned height,
    std::vector<float> &depth)
{
    depth.clear();
    if (!texture || !width || !height || !Flush() || !Create_Sun_Depth_Reader()) return false;
    SDL_GPUTextureCreateInfo info = {};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = info.num_levels = 1;
    SDL_GPUTexture *resolved = SDL_CreateGPUTexture(GpuDevice, &info);
    if (!resolved) return false;

    SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
    bool submitted = false;
    if (commands) {
        if (Resolve_Sun_Depth(commands, texture, resolved, width, height)) {
            // Previously queued readers were flushed above; this ordered submission owns
            // the resolve, and the existing download waits for it before mapping bytes.
            submitted = SerializeSubmits ? Submit(commands) : Submit_Flush(commands);
        } else SDL_CancelGPUCommandBuffer(commands);
    }
    std::vector<uint8_t> bytes;
    const bool read = submitted && Read_Back(resolved, width, height, bytes);
    const size_t expected = size_t(width) * height * sizeof(float);
    const bool ok = read && bytes.size() == expected;
    if (ok) {
        depth.resize(size_t(width) * height);
        std::memcpy(depth.data(), bytes.data(), expected);
    }
    Release_After_Batch(resolved, NULL);
    return ok;
}

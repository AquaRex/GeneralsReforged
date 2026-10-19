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
// Portions adapted from GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx11backend.h by Olcay Seygan (upstream CnCGeneralsZH-Reforged), GPL-3.0-or-later.

/*
** The constants a generated program reads (decision 7, phase A3c), pushed with SDL_PushGPUVertexUniformData
** and SDL_PushGPUFragmentUniformData at slot 0.  Laid out as the generators declare their blocks
** (ffvertex.cpp's `cbuffer VertexPipeline`, ffshader.cpp's `cbuffer CombinerConstants`), which is
** dx11backend's VertexConstantBlock and the first three fields of its PixelConstantBlock: a program
** declares a prefix of its block, and a longer push is legal.  Every field is a float4 or a row-major
** float4x4, so HLSL's packing and std140, which SDL3 asks for, agree.
**
** Ordinary combiners upload the small prefix. Sun/smoke receivers and cloak casters upload
** the shared extended engine block without changing the emulated register banks.
*/

#pragma once

#ifndef SDLCONSTANTS_H
#define SDLCONSTANTS_H

#include <cstddef>
#include "ffvertex.h"		// MAXIMUM_VERTEX_STAGES, MAXIMUM_VERTEX_LIGHTS

struct SdlVertexConstants
{
	float WorldViewProjection[16];
	float WorldView[16];
	float NormalTransform[16];		///< the inverse transpose of WorldView
	float TextureMatrix[MAXIMUM_VERTEX_STAGES][16];
	float MaterialAmbient[4];
	float MaterialDiffuse[4];
	float MaterialSpecular[4];
	float MaterialEmissive[4];
	float MaterialPower[4];
	float GlobalAmbient[4];
	float FogParameters[4];			///< start, end, density
	float ViewportInverse[4];		///< one over the viewport's width and height
	float LightFields[MAXIMUM_VERTEX_LIGHTS][VERTEX_REGISTERS_PER_LIGHT][4];	///< position, direction, diffuse, specular, attenuation, spot, ambient
};

struct SdlPixelConstants
{
	float TextureFactor[4];
	float FogColour[4];
	float AlphaReference[4];		///< .x is D3DRS_ALPHAREF as a whole level, 0 to 255
};

/// Shared engine/generated receiver b0 layout. Normal-map and sky fields remain zero;
/// eligible receivers fill sun/smoke fields; Windows per-pixel light counts remain zero.
struct SdlEnginePixelConstants
{
	SdlPixelConstants Combiner;
	float NormalLightDirection[4][4];
	float NormalLightDiffuse[4][4];
	float NormalMapParameters[4];
	float TerrainSunDirection[4];
	float ShadowFromClip[16];
	float ShadowParameters[4];
	float ShadowViewport[4];
	float ShadowSoftness[4];
	float Sky[4];
	float SkyUp[4];
	float VolumeParameters[4];
	float WorldFromClip[16];
	float HeadlightParameters[4];
	float HeadlightPosition[16][4], HeadlightDirection[16][4];
	float BlastLightParameters[4];
	float BlastLightPosition[32][4], BlastLightDiffuse[32][4], BlastLightAmbient[32][4];
	float SoftParticleDepth[4];
	float DrawEffectParameters[4];
};
static_assert(sizeof(SdlEnginePixelConstants) == 159 * 16, "shared PixelConstantBlock packing");
static_assert(offsetof(SdlEnginePixelConstants, VolumeParameters) == 22 * 16, "smoke follows SkyUp");
static_assert(offsetof(SdlEnginePixelConstants, DrawEffectParameters) == 158 * 16, "caster opacity follows soft depth");

static_assert(sizeof(SdlVertexConstants) % 16 == 0 && sizeof(SdlPixelConstants) % 16 == 0,
	"whole float4s, as std140 lays them out");
static_assert(sizeof(SdlVertexConstants) == (3 + MAXIMUM_VERTEX_STAGES) * 64 + 8 * 16 + MAXIMUM_VERTEX_LIGHTS * VERTEX_REGISTERS_PER_LIGHT * 16,
	"the generated VertexPipeline block, field for field");

#endif // SDLCONSTANTS_H

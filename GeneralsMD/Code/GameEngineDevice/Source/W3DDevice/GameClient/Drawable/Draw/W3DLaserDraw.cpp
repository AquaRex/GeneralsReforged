/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DLaserDraw.cpp /////////////////////////////////////////////////////////////////////////
// Author: Colin Day, May 2001
// Desc:   W3DLaserDraw 
// Updated: Kris Morness July 2002 -- made it data driven and added new features to make it flexible.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include <stdlib.h>

#include "Common/GlobalData.h"
#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/Color.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/RayEffect.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/LaserUpdate.h"
#include "GameLogic/Module/LifetimeUpdate.h"
#include "W3DDevice/GameClient/Module/W3DLaserDraw.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/camera.h"
#include "WW3D2/segline.h"
#include "WWMath/vector3.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/ww3d.h"


#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// BEAM GLOW //////////////////////////////////////////////////////////////////////////////////////
// Weapon lasers get three more additive lines per segment over EA's beams, whose texture leaves a
// line three percent of each layer's width: a wide halo in the beam's own hue, a body in the same
// hue with bright knots running down it from the gun to the target, and a narrow core run toward
// white. A flare sits at each end, a glow and a white-hot heart, small at the muzzle and larger
// where the beam lands. Everything but the knots draws through one texture made here, a falloff
// across the width that reaches zero at the edge, so nothing has a visible border. The beam comes
// up over two logic frames and dies away over the last third of its life, and every layer
// flickers a little, the flares most. The core and the hearts stack past white, which the
// Direct3D 11 bloom then picks up; under -d3d9 the halo and the flare glows are the glow. All of
// it follows the Glow option (glowStrength), and Off draws EA's beam alone. Draw side only:
// nothing here is read by GameLogic or parsed from INI.
static const Real GLOW_HALO_WIDTH_SCALE	= 2.4f;		// halo width over the beam's widest layer
static const Real GLOW_HALO_MIN_WIDTH		= 16.0f;
static const Real GLOW_HALO_INTENSITY		= 0.3f;
static const Real GLOW_HALO_SPREAD			= 0.5f;		// halo width gained per unit of strength past Medium
static const Real GLOW_BODY_WIDTH_SCALE	= 0.9f;
static const Real GLOW_BODY_MIN_WIDTH		= 8.0f;
static const Real GLOW_BODY_INTENSITY		= 0.7f;		// with BEAM_FLICKER on top it stays under one
static const Real GLOW_BODY_WHITEN			= 0.5f;		// a red beam's red is spent on the halo; the knots show in white
static const Real GLOW_BODY_KNOT_SPACING	= 22.0f;	// world units from one bright knot to the next
static const Real GLOW_BODY_KNOT_SPEED	= -30.0f;	// knots a second, toward the target
static const Real GLOW_CORE_WIDTH_SCALE	= 0.2f;
static const Real GLOW_CORE_MIN_WIDTH		= 2.5f;
static const Real GLOW_CORE_WHITEN			= 0.85f;	// how far the core's hue is run toward white
static const Int GLOW_LAYERS						= 3;			// [0] halo, [1] body, [2] core
static const Int GLOW_TEXTURE_SIZE			= 64;
static const Int GLOW_LINE_POINTS				= 4;			// cap tip, start, end, cap tip

// [0] muzzle glow, [1] muzzle heart, [2] impact glow, [3] impact heart: width over the beam's widest
// layer, the least width, and how far toward white
static const Int FLARE_COUNT						= 4;
static const Real FLARE_WIDTH_SCALE[ FLARE_COUNT ]	= { 1.6f, 0.6f, 2.6f, 1.0f };
static const Real FLARE_MIN_WIDTH[ FLARE_COUNT ]		= { 16.0f, 6.0f, 26.0f, 10.0f };
static const Real FLARE_WHITEN[ FLARE_COUNT ]				= { 0.2f, 0.9f, 0.2f, 0.9f };
static const Real FLARE_FLICKER									= 0.3f;		// share of a flare's light and width that flickers
static const Real BEAM_FLICKER									= 0.15f;	// the same for the three beam lines
static const UnsignedInt FLICKER_STEP_MS				= 40;			// one new flicker value this often, blended between

static const UnsignedInt FADE_IN_FRAMES					= 2;
static const Real FADE_OUT_SHARE								= 0.34f;	// of the beam's lifetime

static TextureClass *s_glowTexture = NULL;
static TextureClass *s_knotTexture = NULL;

// ponytail: chosen by template name so no INI field and no checksum change; a "Glow" field in
// W3DLaserDraw is the upgrade if a mod ever wants it per beam. Every weapon laser in ZH ends in
// LaserBeam; the data streams, the waypoint line and the Particle Cannon's beams do not. The
// Avenger's target designator is a marker, not a weapon, and keeps EA's thin blue line.
static Bool beamGlows( const Thing *thing )
{
	if( !thing || !thing->getTemplate() )
		return FALSE;
	const AsciiString &name = thing->getTemplate()->getName();
	return name.endsWith( "LaserBeam" ) && name != "AvengerTargetingLaserBeam";
}

// The falloff from one at the middle (0) to zero at the edge (1), across the width and over a cap.
static Real glowFalloff( Real distance )
{
	const Real edge = expf( -4.0f );
	return MAX( ( expf( -4.0f * distance * distance ) - edge ) / ( 1.0f - edge ), 0.0f );
}

// u runs across the beam. v runs along a glow line of four points (glowLinePoints): the first third
// is the cap behind the start, fading in, the middle third the beam at full, the last third the cap
// past the end, fading out. A cap as long as half the width with the width's own falloff rounds the
// end, where a line stopping at its last point left a straight cut through the halo. A flare is the
// same line with no middle, a round glow.
static Real glowTexel( Real across, Real along )
{
	const Real third = 1.0f / 3.0f;
	Real cap = MAX( third - along, along - 2.0f * third ) * 3.0f;	// 0 at a beam end, 1 at a cap's tip
	return glowFalloff( across ) * glowFalloff( MAX( cap, 0.0f ) );
}

// The body's texture tiles along the beam, one knot a tile: a bright swelling over a dim floor.
static Real knotTexel( Real across, Real along )
{
	Real wave = 0.5f + 0.5f * cosf( 2.0f * PI * along );
	return glowFalloff( across ) * ( 0.25f + 0.75f * wave * wave * wave );
}

// u is -1..1 across the beam, v 0..1 along it
static TextureClass *acquireBeamTexture( TextureClass *&slot, Real (*texel)( Real across, Real along ), Bool wrapAlong )
{
	if( !slot )
	{
		slot = MSGNEW("TextureClass") TextureClass( GLOW_TEXTURE_SIZE, GLOW_TEXTURE_SIZE, WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_1 );
		SurfaceClass *surf = slot->Get_Surface_Level();
		Int pitch;
		UnsignedByte *pData = (UnsignedByte*)surf->Lock( &pitch );
		for( Int y = 0; y < GLOW_TEXTURE_SIZE; y++ )
		{
			Real along = ( y + 0.5f ) / GLOW_TEXTURE_SIZE;
			UnsignedInt *row = (UnsignedInt*)( pData + y * pitch );
			for( Int x = 0; x < GLOW_TEXTURE_SIZE; x++ )
			{
				Real across = ( x + 0.5f ) / GLOW_TEXTURE_SIZE * 2.0f - 1.0f;
				UnsignedInt c = (UnsignedInt)( texel( across, along ) * 255.0f + 0.5f );
				row[ x ] = ( c << 24 ) | ( c << 16 ) | ( c << 8 ) | c;
			}
		}
		surf->Unlock();
		REF_PTR_RELEASE( surf );
		slot->Get_Filter().Set_U_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );
		slot->Get_Filter().Set_V_Addr_Mode( wrapAlong ? TextureFilterClass::TEXTURE_ADDRESS_REPEAT : TextureFilterClass::TEXTURE_ADDRESS_CLAMP );
	}
	slot->Add_Ref();
	return slot;
}

static void releaseBeamTexture( TextureClass *&slot )
{
	// the static pointer holds one reference of its own; the last beam takes it with it
	slot->Release_Ref();
	if( slot->Num_Refs() == 1 )
		REF_PTR_RELEASE( slot );
}

// A flicker of one on average, varying by up to depth: a value per FLICKER_STEP_MS for each seed,
// blended into the next, so it shimmers rather than strobes. On the picture's clock, so a -video
// recording gets the same flicker every run.
static Real flicker( UnsignedInt seed, Real depth )
{
	UnsignedInt now = WW3D::Get_Sync_Time();
	UnsignedInt step = now / FLICKER_STEP_MS;
	Real blend = (Real)( now % FLICKER_STEP_MS ) / FLICKER_STEP_MS;
	Real value[ 2 ];
	for( Int i = 0; i < 2; i++ )
	{
		UnsignedInt h = ( seed * 2654435761u ) ^ ( ( step + i ) * 2246822519u );
		h ^= h >> 15;
		h *= 2654435761u;
		h ^= h >> 13;
		value[ i ] = ( h & 0xFFFF ) / 65535.0f;
	}
	Real noise = value[ 0 ] + ( value[ 1 ] - value[ 0 ] ) * blend;
	return 1.0f + depth * ( noise - 0.5f ) * 2.0f;
}

// The beam's hue at full brightness: whichever of the two INI colours is more saturated, so a
// white-cored red laser glows red and a grey-edged orange one glows orange.
static Vector3 glowHue( Color inner, Color outer )
{
	Real c[ 2 ][ 4 ];
	GameGetColorComponentsReal( inner, &c[0][0], &c[0][1], &c[0][2], &c[0][3] );
	GameGetColorComponentsReal( outer, &c[1][0], &c[1][1], &c[1][2], &c[1][3] );
	Int best = 0;
	Real bestSat = -1.0f;
	for( Int i = 0; i < 2; i++ )
	{
		Real hi = MAX( c[i][0], MAX( c[i][1], c[i][2] ) );
		Real lo = MIN( c[i][0], MIN( c[i][1], c[i][2] ) );
		Real sat = ( hi > 0.0f ) ? ( hi - lo ) / hi : -1.0f;
		if( sat > bestSat )
		{
			bestSat = sat;
			best = i;
		}
	}
	Real hi = MAX( c[best][0], MAX( c[best][1], c[best][2] ) );
	if( hi <= 0.0f )
		return Vector3( 1.0f, 1.0f, 1.0f );
	return Vector3( c[best][0] / hi, c[best][1] / hi, c[best][2] / hi );
}

// What the Glow option makes of the two glow lines: nothing at Off, which leaves EA's beams alone,
// and the look they were tuned at for Medium, twice it at Ultra.  A line's colour goes to the device
// as eight bits a channel and wraps past one, so the brightness stops at Medium's; past it the halo
// widens instead, and the Direct3D 11 gain takes the lines over white (GLOW_ENABLE below).
static Real glowStrength()
{
	return TheGlobalData->m_bloomIntensity / 50.0f;
}

// The plain additive preset, opted into the Glow option's gain: EA's beam layers and the glow
// lines alike are light.
static ShaderClass glowingAdditiveShader()
{
	ShaderClass shader = ShaderClass::_PresetAdditiveShader;
	shader.Set_Glow( ShaderClass::GLOW_ENABLE );
	return shader;
}

// A Tile = Yes beam with a TilingScalar of zero or less (every EA laser: -3) asks for a negative tile
// factor, which the line clamps to zero, so the whole beam samples one row of its texture and the
// ScrollRate only picks which row. At -2500 a 33 ms frame moves it 82.5 rows' worth, half a texture,
// and EXLaser4's rows alternate between the lit band and the dark border: at a steady 30 pictures a
// second, retail's clock included, the beam was there on every other frame. Such a beam holds the
// texture's middle row instead; a beam that does tile still scrolls.
static const Real BEAM_TEXTURE_ROW = 0.5f;

static Bool holdsOneTextureRow( const W3DLaserDrawModuleData *data )
{
	return data->m_tile && data->m_tilingScalar <= 0.0f && !data->m_textureName.isEmpty();
}

// EA's colour for beam layer i of numBeams, inner to outer
static Vector3 beamLayerColor( const W3DLaserDrawModuleData *data, Int i )
{
	Real innerRed, innerGreen, innerBlue, innerAlpha, outerRed, outerGreen, outerBlue, outerAlpha;
	GameGetColorComponentsReal( data->m_innerColor, &innerRed, &innerGreen, &innerBlue, &innerAlpha );
	GameGetColorComponentsReal( data->m_outerColor, &outerRed, &outerGreen, &outerBlue, &outerAlpha );
	if( data->m_numBeams == 1 )
		return Vector3( innerRed * innerAlpha, innerGreen * innerAlpha, innerBlue * innerAlpha );

	//Calculate the scale between min and max values
	//0 means use min value, 1 means use max value
	//0.2 means min value + 20% of the diff between min and max
	Real scale = i / ( data->m_numBeams - 1.0f );
	return Vector3( innerRed		+ scale * ( outerRed - innerRed ) * innerAlpha,
									innerGreen	+ scale * ( outerGreen - innerGreen ) * innerAlpha,
									innerBlue		+ scale * ( outerBlue - innerBlue ) * innerAlpha );
}

// How long the beam lives, from its template's LifetimeUpdate: template data, not the object's.
static UnsignedInt beamLifetimeFrames( const Thing *thing )
{
	const ModuleInfo &info = thing->getTemplate()->getBehaviorModuleInfo();
	for( Int i = 0; i < info.getCount(); i++ )
	{
		if( info.getNthName( i ) == "LifetimeUpdate" )
			return ( (const LifetimeUpdateModuleData *)info.getNthData( i ) )->m_maxFrames;
	}
	return 0;
}

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DLaserDrawModuleData::W3DLaserDrawModuleData()
{
	m_innerBeamWidth = 0.0f;         //The total width of beam
	m_outerBeamWidth = 1.0f;         //The total width of beam
  m_numBeams = 1;                 //Number of overlapping cylinders that make the beam. 1 beam will just use inner data.
  m_maxIntensityFrames = 0;				//Laser stays at max intensity for specified time in ms.
  m_fadeFrames = 0;               //Laser will fade and delete.
	m_scrollRate = 0.0f;
	m_tile = false;
	m_segments = 1;
	m_arcHeight = 0.0f;
	m_segmentOverlapRatio = 0.0f;
	m_tilingScalar = 1.0f;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DLaserDrawModuleData::~W3DLaserDrawModuleData()
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DLaserDrawModuleData::buildFieldParse(MultiIniFieldParse& p) 
{
  ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] = 
	{
		{ "NumBeams",							INI::parseUnsignedInt,					NULL, offsetof( W3DLaserDrawModuleData, m_numBeams ) },
		{ "InnerBeamWidth",				INI::parseReal,									NULL, offsetof( W3DLaserDrawModuleData, m_innerBeamWidth ) },
		{ "OuterBeamWidth",				INI::parseReal,									NULL, offsetof( W3DLaserDrawModuleData, m_outerBeamWidth ) },
		{ "InnerColor",						INI::parseColorInt,							NULL, offsetof( W3DLaserDrawModuleData, m_innerColor ) },
		{ "OuterColor",						INI::parseColorInt,							NULL, offsetof( W3DLaserDrawModuleData, m_outerColor ) },
		{ "MaxIntensityLifetime",	INI::parseDurationUnsignedInt,	NULL, offsetof( W3DLaserDrawModuleData, m_maxIntensityFrames ) },
		{ "FadeLifetime",					INI::parseDurationUnsignedInt,	NULL, offsetof( W3DLaserDrawModuleData, m_fadeFrames ) },
		{ "Texture",							INI::parseAsciiString,					NULL, offsetof( W3DLaserDrawModuleData, m_textureName ) },
		{ "ScrollRate",						INI::parseReal,									NULL, offsetof( W3DLaserDrawModuleData, m_scrollRate ) },
		{ "Tile",									INI::parseBool,									NULL, offsetof( W3DLaserDrawModuleData, m_tile ) },
		{ "Segments",							INI::parseUnsignedInt,					NULL, offsetof( W3DLaserDrawModuleData, m_segments ) },
    { "ArcHeight",						INI::parseReal,									NULL, offsetof( W3DLaserDrawModuleData, m_arcHeight ) },
		{ "SegmentOverlapRatio",	INI::parseReal,									NULL, offsetof( W3DLaserDrawModuleData, m_segmentOverlapRatio ) },
		{ "TilingScalar",					INI::parseReal,									NULL, offsetof( W3DLaserDrawModuleData, m_tilingScalar ) },
		{ 0, 0, 0, 0 }
	};
  p.add(dataFieldParse);
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DLaserDraw::W3DLaserDraw( Thing *thing, const ModuleData* moduleData ) : 
	DrawModule( thing, moduleData ),
	m_line3D(NULL),
	m_glow3D(NULL),
	m_beamStart(0.0f, 0.0f, 0.0f),
	m_beamEnd(0.0f, 0.0f, 0.0f),
	m_birthFrame(0),
	m_lifetimeFrames(0),
	m_texture(NULL),
	m_textureAspectRatio(1.0f),
	m_selfDirty(TRUE)
{
}

//-------------------------------------------------------------------------------------------------
void W3DLaserDraw::buildRenderObjects( Bool madeInTick )
{
	Vector3 dummyPos1( 0.0f, 0.0f, 0.0f );
	Vector3 dummyPos2( 1.0f, 1.0f, 1.0f );
	Int i;

	const W3DLaserDrawModuleData *data = getW3DLaserDrawModuleData();

	m_texture = WW3DAssetManager::Get_Instance()->Get_Texture( data->m_textureName.str() );
	if (m_texture)
	{
		if (!m_texture->Is_Initialized())
			m_texture->Init();	//make sure texture is actually loaded before accessing surface.

		SurfaceClass::SurfaceDescription surfaceDesc; 
		m_texture->Get_Level_Description(surfaceDesc);
		m_textureAspectRatio = (Real)surfaceDesc.Width/(Real)surfaceDesc.Height;
	}

	//Make sure our beams range between 1 and the maximum cap.
#ifdef I_WANT_TO_BE_FIRED
// srj sez: this data is const for a reason. casting away the constness because we don't like the values
// isn't an acceptable solution. if you need to constrain the values, do so at parsing time, when
// it's still legal to modify these values. (In point of fact, there's not even really any reason to limit
// the numBeams or segments anymore.)
	data->m_numBeams =		 __min( __max( 1, data->m_numBeams ), MAX_LASER_LINES );
	data->m_segments =		 __min( __max( 1, data->m_segments ), MAX_SEGMENTS );
	data->m_tilingScalar = __max( 0.01f, data->m_tilingScalar );
#endif

	//Allocate an array of lines equal to the number of beams * segments
	m_line3D = NEW SegmentedLineClass *[ data->m_numBeams * data->m_segments ];

	for( int segment = 0; segment < data->m_segments; segment++ )
	{
		//We don't care about segment positioning yet until we actually set the position
		
		// create all the lines we need at the right transparency level
		for( i = data->m_numBeams - 1; i >= 0; i-- )
		{
			int index = segment * data->m_numBeams + i;

			Real width = data->m_innerBeamWidth;
			if( data->m_numBeams > 1 )
				width += i / ( data->m_numBeams - 1.0f ) * ( data->m_outerBeamWidth - data->m_innerBeamWidth );

			m_line3D[ index ] = NEW SegmentedLineClass;

			SegmentedLineClass *line = m_line3D[ index ];
			if( line )
			{
				line->Set_Texture( m_texture );
				line->Set_Shader( glowingAdditiveShader() );	//pick the alpha blending mode you want - see shader.h for others.
				line->Set_Width( width );
				line->Set_Color( beamLayerColor( data, i ) );
				line->Set_UV_Offset_Rate( Vector2(0.0f, data->m_scrollRate) );	//amount to scroll texture on each draw
				if( m_texture )
				{
					line->Set_Texture_Mapping_Mode(SegLineRendererClass::TILED_TEXTURE_MAP);	//this tiles the texture across the line
				}
				if( holdsOneTextureRow( data ) )
				{
					line->Set_UV_Offset_Rate( Vector2( 0.0f, 0.0f ) );
					line->Set_Current_UV_Offset( Vector2( 0.0f, BEAM_TEXTURE_ROW ) );
				}

				// add to scene
				W3DDisplay::m_3DScene->Add_Render_Object( line );	//add it to our scene so it gets rendered with other objects.

				// hide the render object until the first time we come to draw it and
				// set the correct position
				line->Set_Visible( 0 );
			}


		}  // end for i

	} //end segment loop

	if( beamGlows( getDrawable() ) )
	{
		// this beam's references on both textures are given back by releaseBeamTexture in the destructor
		TextureClass *glowTexture = acquireBeamTexture( s_glowTexture, glowTexel, FALSE );
		TextureClass *knotTexture = acquireBeamTexture( s_knotTexture, knotTexel, TRUE );

		const Int glowCount = (Int)data->m_segments * GLOW_LAYERS;
		m_glow3D = NEW SegmentedLineClass *[ glowCount ];
		for( Int g = 0; g < glowCount + FLARE_COUNT; g++ )
		{
			SegmentedLineClass *line = NEW SegmentedLineClass;
			if( g < glowCount )
				m_glow3D[ g ] = line;
			else
				m_flare3D[ g - glowCount ] = line;
			line->Set_Shader( glowingAdditiveShader() );
			line->Set_Texture_Mapping_Mode( SegLineRendererClass::TILED_TEXTURE_MAP );
			if( g < glowCount && g % GLOW_LAYERS == 1 )
			{
				// the body: two points, the knots tiled along it (tile factor set with the points) and
				// running toward the target
				line->Set_Texture( knotTexture );
				line->Set_UV_Offset_Rate( Vector2( 0.0f, GLOW_BODY_KNOT_SPEED ) );
			}
			else
			{
				// u across the width, v a third further at each of the four points (glowTexel)
				line->Set_Texture( glowTexture );
				line->Set_Texture_Tile_Factor( 1.0f / ( GLOW_LINE_POINTS - 1 ) );
			}
			W3DDisplay::m_3DScene->Add_Render_Object( line );
			line->Set_Visible( 0 );
		}
		m_lifetimeFrames = beamLifetimeFrames( getDrawable() );
		// the beam's birth, so one the camera only reaches mid-life still fades out on time
		m_birthFrame = TheGameLogic->getFrame();
	}

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DLaserDraw::~W3DLaserDraw( void )
{
	const W3DLaserDrawModuleData *data = getW3DLaserDrawModuleData();

	// a beam logic made and destroyed in one tick never built its lines
	for( int i = 0; m_line3D && i < data->m_numBeams * data->m_segments; i++ )
	{

		// remove line from scene
		W3DDisplay::m_3DScene->Remove_Render_Object( m_line3D[ i ] );

		// delete line
		REF_PTR_RELEASE( m_line3D[ i ] );

	}  // end for i

	delete [] m_line3D;

	if( m_glow3D )
	{
		for( Int g = 0; g < (Int)data->m_segments * GLOW_LAYERS; g++ )
		{
			W3DDisplay::m_3DScene->Remove_Render_Object( m_glow3D[ g ] );
			REF_PTR_RELEASE( m_glow3D[ g ] );
		}
		delete [] m_glow3D;
		for( Int f = 0; f < FLARE_COUNT; f++ )
		{
			W3DDisplay::m_3DScene->Remove_Render_Object( m_flare3D[ f ] );
			REF_PTR_RELEASE( m_flare3D[ f ] );
		}
		releaseBeamTexture( s_glowTexture );
		releaseBeamTexture( s_knotTexture );
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Real W3DLaserDraw::getLaserTemplateWidth() const
{
	Real width = 0.0f;
	getW3DLaserDrawModuleData()->getLaserTemplateWidth( width );
	return width;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DLaserDraw::doDrawModule(const Matrix3D* transformMtx)
{
	//UnsignedInt currentFrame = TheGameClient->getFrame();
	const W3DLaserDrawModuleData *data = getW3DLaserDrawModuleData();

	//Get the updatemodule that drives it...
	Drawable *draw = getDrawable();
	static NameKeyType key_LaserUpdate = NAMEKEY( "LaserUpdate" );
	LaserUpdate *update = (LaserUpdate*)draw->findClientUpdateModule( key_LaserUpdate );
	if( !update )
	{
		DEBUG_ASSERTCRASH( 0, ("W3DLaserDraw::doDrawModule() expects its owner drawable %s to have a ClientUpdate = LaserUpdate module.", draw->getTemplate()->getName().str() ));
		return;
	}

	//If the update has moved the laser, it requires a reset of the laser.
	if (update->isDirty() || m_selfDirty)
	{
		update->setDirty(false);
		m_selfDirty = false;

		Vector3 laserPoints[ 2 ];

		for( int segment = 0; segment < data->m_segments; segment++ )
		{
			// a laser drawn before LaserUpdate has placed it has both ends at the origin, and the arc
			// below divides by half its length: NaN points into the segmented line
			const Coord3D *start = update->getStartPos();
			const Coord3D *end = update->getEndPos();
			const Bool hasLength = start->x != end->x || start->y != end->y || start->z != end->z;
			if( data->m_arcHeight > 0.0f && data->m_segments > 1 && hasLength )
			{
				//CALCULATE A CURVED LINE BASED ON TOTAL LENGTH AND DESIRED HEIGHT INCREASE
				//To do this we will use a portion of the cos wave ranging between -0.25PI
				//and +0.25PI. 0PI is 1.0 and 0.25PI is 0.70 -- resulting in a somewhat
				//gentle curve depending on the line height and length. We also have to make
				//the line *level* for this phase of the calculations.

				//Get the desired direct line
				Coord3D lineStart, lineEnd, lineVector;
				lineStart.set( update->getStartPos() );
				lineEnd.set( update->getEndPos() );
				//This is critical -- in the case we have sloped lines (at the end, we'll fix it)
//				lineEnd.z = lineStart.z;

				//Get the length of the line
				lineVector.set( &lineEnd );
				lineVector.sub( &lineStart );
				Real lineLength = lineVector.length();

				//Get the middle point (we'll use this to determine how far we are from
				//that to calculate our height -- middle point is the highest).
				Coord3D lineMiddle;
				lineMiddle.set( &lineStart );
				lineMiddle.add( &lineEnd );
				lineMiddle.scale( 0.5 );

				//The half length is used to scale with the distance from middle to 
				//get our cos( 0 to 0.25 PI) cos value
				Real halfLength = lineLength * 0.5f;

				//Now calculate which segment we will use.
				Real startSegmentRatio = segment / ((Real)data->m_segments);
				Real endSegmentRatio = (segment + 1.0f) / ((Real)data->m_segments);

				//Offset the segment ever-so-slightly to minimize overlap -- only apply
				//to segments that are not the start/end point
				if( segment > 0 ) 
				{
					startSegmentRatio -= data->m_segmentOverlapRatio;
				}
				if( segment < data->m_segments - 1 )
				{
					endSegmentRatio += data->m_segmentOverlapRatio;
				}

				//Calculate our start segment position on the *ground*.
				Coord3D segmentStart, segmentEnd, vector;
				vector.set( &lineVector );
				vector.scale( startSegmentRatio );
				segmentStart.set( &lineStart );
				segmentStart.add( &vector );

				//Calculate our end segment position on the *ground*.
				vector.set( &lineVector );
				vector.scale( endSegmentRatio );
				segmentEnd.set( &lineStart );
				segmentEnd.add( &vector );

				//--------------------------------------------------------------------------------
				//Now at this point, we have our segment line in the level positions that we want.
				//Calculate the raised height for the start/end segment positions using cosine.
				//--------------------------------------------------------------------------------

				//Calculate the distance from midpoint for the start positions.
				vector.set( &lineMiddle );
				vector.sub( &segmentStart );
				Real dist = vector.length();
				Real scaledRadians = dist / halfLength * PI * 0.5f; 
				Real height = cos( scaledRadians );
				height *= data->m_arcHeight;
				segmentStart.z += height;

				//Now do the same thing for the end position.
				vector.set( &lineMiddle );
				vector.sub( &segmentEnd );
				dist = vector.length();
				scaledRadians = dist / halfLength * PI * 0.5f; 
				height = cos( scaledRadians );
				height *= data->m_arcHeight;
				segmentEnd.z += height;
				
				//This makes the laser skim the ground rather than penetrate it!
				laserPoints[ 0 ].Set( segmentStart.x, segmentStart.y, 
					MAX( segmentStart.z, 2.0f + TheTerrainLogic->getGroundHeight(segmentStart.x, segmentStart.y) ) );
				laserPoints[ 1 ].Set( segmentEnd.x, segmentEnd.y, 
					MAX( segmentEnd.z, 2.0f + TheTerrainLogic->getGroundHeight(segmentEnd.x, segmentEnd.y) ) );
				
			}
			else
			{
				//No arc -- way simpler!
				laserPoints[ 0 ].Set( update->getStartPos()->x, update->getStartPos()->y, update->getStartPos()->z );
				laserPoints[ 1 ].Set( update->getEndPos()->x, update->getEndPos()->y, update->getEndPos()->z );
			}

			//Get the color components for calculation purposes.
			Real innerRed, innerGreen, innerBlue, innerAlpha, outerRed, outerGreen, outerBlue, outerAlpha;
			GameGetColorComponentsReal( data->m_innerColor, &innerRed, &innerGreen, &innerBlue, &innerAlpha );
			GameGetColorComponentsReal( data->m_outerColor, &outerRed, &outerGreen, &outerBlue, &outerAlpha );

			for( Int i = data->m_numBeams - 1; i >= 0; i-- )
			{

				Real alpha, width;
				int index = segment * data->m_numBeams + i;

				if( data->m_numBeams == 1 )
				{	
					width = data->m_innerBeamWidth * update->getWidthScale();
					alpha = innerAlpha;
				}
				else
				{
					//Calculate the scale between min and max values
					//0 means use min value, 1 means use max value
					//0.2 means min value + 20% of the diff between min and max
					Real scale = i / ( data->m_numBeams - 1.0f);
					Real ultimateScale = update->getWidthScale();
					width		= (data->m_innerBeamWidth	+ scale * (data->m_outerBeamWidth - data->m_innerBeamWidth));
					width *= ultimateScale;
					alpha		= innerAlpha							+ scale * (outerAlpha - innerAlpha);
				}


				//Calculate the number of times to tile the line based on the height of the texture used.
				if( m_texture && data->m_tile )
				{
					//Calculate the length of the line.
					Vector3 lineVector;
					Vector3::Subtract( laserPoints[1], laserPoints[0], &lineVector );
					Real length = lineVector.Length();

					//Adjust tile factor so texture is NOT stretched but tiled equally in both width and length.
					Real tileFactor = length/width*m_textureAspectRatio*data->m_tilingScalar;

					//Set the tile factor
					m_line3D[ index ]->Set_Texture_Tile_Factor( tileFactor );	//number of times to tile texture across each segment
				}

				m_line3D[ index ]->Set_Width( width );
				m_line3D[ index ]->Set_Points( 2, &laserPoints[0] );
			}

			if( m_glow3D )
			{
				// set with the points rather than once, so a change of the Glow option reaches the
				// next beam drawn; the colours and the body's width are set on every draw, below
				const Real spread = 1.0f + GLOW_HALO_SPREAD * MAX( glowStrength() - 1.0f, 0.0f );
				Real widest = MAX( data->m_innerBeamWidth, data->m_outerBeamWidth );
				Real widthScale = update->getWidthScale();
				Real widths[ GLOW_LAYERS ] =
				{
					MAX( widest * GLOW_HALO_WIDTH_SCALE, GLOW_HALO_MIN_WIDTH ) * widthScale * spread,
					0.0f,
					MAX( widest * GLOW_CORE_WIDTH_SCALE, GLOW_CORE_MIN_WIDTH ) * widthScale
				};
				// ponytail: every segment gets its own caps, so an arc of several segments would double
				// up at its joints; no glowing laser in the data has more than one
				Vector3 direction = laserPoints[ 1 ] - laserPoints[ 0 ];
				const Real length = direction.Length();
				for( Int g = 0; g < GLOW_LAYERS; g++ )
				{
					SegmentedLineClass *line = m_glow3D[ segment * GLOW_LAYERS + g ];
					if( g == 1 )
					{
						line->Set_Texture_Tile_Factor( length / GLOW_BODY_KNOT_SPACING );
						line->Set_Points( 2, &laserPoints[0] );
						continue;
					}
					line->Set_Width( widths[ g ] );
					if( length > 0.0f )
					{
						Vector3 cap = direction * ( widths[ g ] * 0.5f / length );
						Vector3 glowPoints[ GLOW_LINE_POINTS ] =
						{
							laserPoints[ 0 ] - cap, laserPoints[ 0 ], laserPoints[ 1 ], laserPoints[ 1 ] + cap
						};
						line->Set_Points( GLOW_LINE_POINTS, glowPoints );
					}
					else
					{
						line->Set_Points( 2, &laserPoints[0] );
					}
				}
				if( segment == 0 )
					m_beamStart = laserPoints[ 0 ];
				m_beamEnd = laserPoints[ 1 ];
			}
		}
	}

	if( m_glow3D )
		drawGlow( update->getWidthScale() );
}

//-------------------------------------------------------------------------------------------------
/** The glow's light on this draw: the fade in and out, the flicker, the body's width and the flares,
	* which sit at the beam's ends and are re-placed every draw because their size flickers. */
//-------------------------------------------------------------------------------------------------
void W3DLaserDraw::drawGlow( Real widthScale )
{
	const W3DLaserDrawModuleData *data = getW3DLaserDrawModuleData();
	const Real brightness = MIN( glowStrength(), 1.0f );
	if( brightness <= 0.0f )
	{
		// Off: EA's beam alone, as it was
		for( Int g = 0; g < (Int)data->m_segments * GLOW_LAYERS; g++ )
			m_glow3D[ g ]->Set_Color( Vector3( 0.0f, 0.0f, 0.0f ) );
		for( Int f = 0; f < FLARE_COUNT; f++ )
			m_flare3D[ f ]->Set_Color( Vector3( 0.0f, 0.0f, 0.0f ) );
		return;
	}

	// up over FADE_IN_FRAMES, down over the last FADE_OUT_SHARE of the template's lifetime
	const Real age = (Real)( TheGameLogic->getFrame() - m_birthFrame );
	Real fade = MIN( ( age + 1.0f ) / FADE_IN_FRAMES, 1.0f );
	if( m_lifetimeFrames > 0 )
	{
		const Real fadeOutFrames = MAX( m_lifetimeFrames * FADE_OUT_SHARE, 1.0f );
		fade = MIN( fade, MAX( ( m_lifetimeFrames - age ) / fadeOutFrames, 0.0f ) );
	}

	const UnsignedInt seed = (UnsignedInt)getDrawable()->getID() * 8;
	const Vector3 hue = glowHue( data->m_innerColor, data->m_outerColor );
	const Vector3 white( 1.0f, 1.0f, 1.0f );
	const Real light = brightness * fade;

	// glowHue's brightest channel is one, so no colour passes one before the flicker
	const Vector3 colors[ GLOW_LAYERS ] =
	{
		hue * ( GLOW_HALO_INTENSITY * light * flicker( seed, BEAM_FLICKER ) ),
		( hue * ( 1.0f - GLOW_BODY_WHITEN ) + white * GLOW_BODY_WHITEN ) * ( GLOW_BODY_INTENSITY * light * flicker( seed + 1, BEAM_FLICKER ) ),
		( hue * ( 1.0f - GLOW_CORE_WHITEN ) + white * GLOW_CORE_WHITEN ) * light
	};
	const Real widest = MAX( data->m_innerBeamWidth, data->m_outerBeamWidth );
	const Real bodyWidth = MAX( widest * GLOW_BODY_WIDTH_SCALE, GLOW_BODY_MIN_WIDTH ) * widthScale * flicker( seed + 2, BEAM_FLICKER );
	for( Int segment = 0; segment < (Int)data->m_segments; segment++ )
	{
		for( Int g = 0; g < GLOW_LAYERS; g++ )
			m_glow3D[ segment * GLOW_LAYERS + g ]->Set_Color( colors[ g ] );
		m_glow3D[ segment * GLOW_LAYERS + 1 ]->Set_Width( bodyWidth );
		// EA's layers come up and go down with the rest
		for( Int i = 0; i < (Int)data->m_numBeams; i++ )
			m_line3D[ segment * data->m_numBeams + i ]->Set_Color( beamLayerColor( data, i ) * fade );
	}

	// a beam LaserUpdate has not placed yet has both ends at the origin: no flares in the map's corner
	Vector3 direction = m_beamEnd - m_beamStart;
	const Real length = direction.Length();
	for( Int f = 0; f < FLARE_COUNT; f++ )
	{
		if( length <= 0.0f )
		{
			m_flare3D[ f ]->Set_Color( Vector3( 0.0f, 0.0f, 0.0f ) );
			continue;
		}
		if( f == 0 )
			direction /= length;
		const Real shimmer = flicker( seed + 3 + f, FLARE_FLICKER );
		const Real width = MAX( widest * FLARE_WIDTH_SCALE[ f ], FLARE_MIN_WIDTH[ f ] ) * widthScale * shimmer * ( 0.6f + 0.4f * fade );
		const Vector3 &at = ( f < 2 ) ? m_beamStart : m_beamEnd;
		// a glow line with no middle is a round glow (glowTexel); its middle is kept a hair long so
		// the line has a direction to turn its width across
		Vector3 cap = direction * ( width * 0.5f );
		Vector3 hair = direction * ( width * 0.01f );
		Vector3 flarePoints[ GLOW_LINE_POINTS ] = { at - cap, at - hair, at + hair, at + cap };
		SegmentedLineClass *line = m_flare3D[ f ];
		line->Set_Width( width );
		line->Set_Points( GLOW_LINE_POINTS, flarePoints );
		line->Set_Color( ( hue * ( 1.0f - FLARE_WHITEN[ f ] ) + white * FLARE_WHITEN[ f ] ) * MIN( light * shimmer, 1.0f ) );
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DLaserDraw::crc( Xfer *xfer )
{

	// extend base class
	DrawModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void W3DLaserDraw::xfer( Xfer *xfer )
{

	// version
	const XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	DrawModule::xfer( xfer );

	// Kris says there is no data to save for these, go ask him.
	// m_selfDirty is not saved, is runtime only

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DLaserDraw::loadPostProcess( void )
{

	// extend base class
	DrawModule::loadPostProcess();

	m_selfDirty = true;	// so we update the first time after reload

}  // end loadPostProcess

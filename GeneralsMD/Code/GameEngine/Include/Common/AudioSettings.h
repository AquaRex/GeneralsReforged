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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// EA Pacific
// John McDonald, Jr
// Do not distribute

#pragma once

#ifndef _AUDIOSETTINGS_H_
#define _AUDIOSETTINGS_H_

#include "Common/AsciiString.h"

#include <vector>

enum { MAX_HW_PROVIDERS = 4 };

struct AudioSettings
{
	// Every field below is filled in by AudioSettings.ini and nothing gives them a value before
	// that, so a key the shipped file does not carry arrives as whatever was on the heap. New
	// fields get their default here.
	AudioSettings() :
		m_rangeVolumeFade( FALSE ),
		m_speedOfSound( 1700.0f ),
		m_maxSoundDelay( 1000.0f ),
		m_dopplerScale( 1.0f ),
		m_airAbsorptionDistance( 400.0f ),
		m_reverbLevel( 1.0f ),
		m_duckVolume( 0.35f ),
		m_duckTime( 2500.0f )
	{
		m_duckSounds.push_back( "ExplosionNeutron" );
		m_duckSounds.push_back( "ExplosionDaisyCutter" );
		m_duckSounds.push_back( "ExplosionMOAB" );
		m_duckSounds.push_back( "ExplosionMiniNuke" );
		m_duckSounds.push_back( "ParticleUplinkCannon_GroundAnnihilationSoundLoop" );
	}

	AsciiString m_audioRoot;
	AsciiString m_soundsFolder;
	AsciiString m_musicFolder;
	AsciiString m_streamingFolder;
	AsciiString m_soundsExtension;
	Bool m_useDigital;
	Bool m_useMidi;
	Int m_outputRate;
	Int m_outputBits;
	Int m_outputChannels;
	Int m_sampleCount2D;
	Int m_sampleCount3D;
	Int m_streamCount;
	Int m_globalMinRange;
	Int m_globalMaxRange;
	/** How a 3D sound loses volume with distance.
		*
		* No, the default, is what the game has always done: a 1/distance curve past the minimum range
		* and then a hard cut to silence at the maximum, so a sound you are walking away from stops
		* dead rather than fading out. Yes uses the linear fade between the two ranges that EA left
		* commented out next to it, which reaches zero exactly where the cut is.
		* `RangeVolumeFade` in AudioSettings.ini. */
	Bool m_rangeVolumeFade;

	/** Sound in air, none of which the shipped file carries, so the defaults below are what plays.
		*
		* `SpeedOfSound`, game units a second: a world sound starts late by its distance to the
		* microphone over it, never more than `MaxSoundDelay` milliseconds, and a moving one is pitched
		* by doppler. The ground is drawn at about 0.2 m a unit (a Ranger stands 13 units, a Crusader
		* is 30 long), so 343 m/s is 1700. Zero is neither. `DopplerScale` multiplies the speed doppler
		* hears, zero switching doppler alone off.
		* `AirAbsorptionDistance`, game units: where air has taken the top end down from 20 kHz to 10,
		* falling with the square of the distance past it. Zero is no air.
		* `ReverbLevel`: the room's send, as a share of what the map asks for. Zero is no room.
		* `DuckVolume` and `DuckTime` (milliseconds): when one of `DuckSounds` starts within earshot,
		* everything else dips to that volume and comes back over that time. 100% is no duck. */
	Real m_speedOfSound;
	Real m_maxSoundDelay;
	Real m_dopplerScale;
	Real m_airAbsorptionDistance;
	Real m_reverbLevel;
	Real m_duckVolume;
	Real m_duckTime;
	std::vector<AsciiString> m_duckSounds;

	Int m_drawableAmbientFrames;
	Int m_fadeAudioFrames;
	UnsignedInt m_maxCacheSize;

	Real m_minVolume;		// At volumes less than this, the sample will be culled.
	
	AsciiString m_preferred3DProvider[MAX_HW_PROVIDERS + 1];

	//Defaults actually don't ever get changed!
	Real m_relative2DVolume;		//2D volume compared to 3D
	Real m_defaultSoundVolume;
	Real m_default3DSoundVolume;
	Real m_defaultSpeechVolume;
	Real m_defaultMusicVolume;
	UnsignedInt m_defaultSpeakerType2D;
	UnsignedInt m_defaultSpeakerType3D;

	//If you want to change a value, store it somewhere else (like here)
	Real m_preferredSoundVolume;
	Real m_preferred3DSoundVolume;
	Real m_preferredSpeechVolume;
	Real m_preferredMusicVolume;

	//The desired altitude of the microphone to improve panning relative to terrain.
	Real m_microphoneDesiredHeightAboveTerrain;

	//When tracing a line between the ground look-at-point and the camera, we want
	//to ensure a maximum percentage, so the microphone never goes behind the camera.
	Real m_microphoneMaxPercentageBetweenGroundAndCamera;

	//Handles changing sound volume whenever the camera is close to the microphone.
  Real m_zoomMinDistance;			//If we're closer than the minimum distance, then apply the full bonus no matter how close.
  Real m_zoomMaxDistance;			//The maximum distance from microphone we need to be before benefiting from any bonus.
	
  //NOTE: The higher this value is, the lower normal sounds will be! If you specify a sound volume value of 25%, then sounds will play
	//between 75% and 100%, not 100% to 125%!
  Real m_zoomSoundVolumePercentageAmount;	//The amount of sound volume dedicated to zooming.
};

#endif // _AUDIOSETTINGS_H_

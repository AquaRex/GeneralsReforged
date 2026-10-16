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
// Modified 2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

// FILE: LogicThread.h ////////////////////////////////////////////////////////////////////////////
// Desc:   LOGIC-THREAD.md phase 1: GameLogic::update on a thread of its own, the main thread
//         waiting for it, and a census of the client calls the tick makes from there.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __LOGICTHREAD_H__
#define __LOGICTHREAD_H__

#include "Lib/BaseType.h"

/**
	* `-logicthread 0` (the default) runs the tick on the main thread, as it always ran.
	* `-logicthread 1` hands every tick to a dedicated thread and waits for it: nothing overlaps
	* yet, so nothing can race, and any code that cares which thread it is on shows itself.
	*
	* The census counts calls into the client made from the logic thread, one count per channel
	* of LOGIC-THREAD.md section 1's table.  Each count is an entry point phase 2 has to turn into
	* a record replayed at the sync point; the phase 2 exit is every count at zero.  At
	* `-logicthread 0` nothing runs on the logic thread and every count stays zero.
	*/
namespace LogicThread
{

	enum Channel
	{
		CENSUS_DRAWABLE,				///< Object::getDrawable from the tick
		CENSUS_MODEL_CONDITION,	///< Drawable model condition setters
		CENSUS_AUDIO,						///< AudioManager add/remove event
		CENSUS_INGAMEUI,				///< TheInGameUI messages, floating text, selection, superweapons, idle workers
		CENSUS_CONTROLBAR,			///< TheControlBar dirty mark and command lookups
		CENSUS_TACTICALVIEW,		///< TheTacticalView camera moves, shakes, filters, the time freeze reads
		CENSUS_RADAR,						///< TheRadar objects, events, shroud
		CENSUS_FXLIST,					///< FXList::doFXPos / doFXObj
		CENSUS_TERRAINVISUAL,		///< TheTerrainVisual heights, bibs, the water grid
		CENSUS_PARTICLES,				///< ParticleSystemManager create, find, destroy
		CENSUS_GAMECLIENT,			///< TheGameClient destroyDrawable, findDrawableByID, addScorch, setFrame
		CENSUS_EVA,							///< TheEva setShouldPlay
		CENSUS_NEW_DRAWABLE,		///< ThingFactory::newDrawable
		CENSUS_CLIENT_RANDOM,		///< GameClientRandomValue draws
		CENSUS_GHOST,						///< GhostObjectManager add/remove
		CENSUS_MESSAGESTREAM,		///< MessageStream::appendMessage
		CENSUS_DISPLAY,					///< TheDisplay shroud levels
		CENSUS_JOBS,						///< JobSystem::parallel_for
		CENSUS_COUNT
	};

	/** TRUE on the logic thread, FALSE everywhere else.  Constant-initialised, so a read is one
		* thread-local load. */
	extern thread_local Bool t_isLogicThread;

	void noteCensus( Channel channel );

	inline void census( Channel channel )
	{
		if( t_isLogicThread )
			noteCensus( channel );
	}

	/** Read -logicthread and start the thread if it asks for one.  Called once, from GameEngine::init. */
	void init( void );

	/** Join the thread.  Safe without an init(). */
	void shutdown( void );

	/** Run one TheGameLogic->UPDATE(): on the logic thread with this one waiting, or inline at
		* -logicthread 0 and on the ticks that load or tear down a map, which build windows, the load
		* screen and every render object on the device and stay on the main thread.  An exception
		* the tick throws is thrown again here, on the main thread, where GameEngine::execute catches
		* it as it always did. */
	void runTick( void );

	/** The census and the handoff cost, as HEADLESS LOGICTHREAD lines. */
	void report( void );

}  // namespace LogicThread

#define LOGIC_CENSUS( channel ) LogicThread::census( LogicThread::channel )

#endif // __LOGICTHREAD_H__

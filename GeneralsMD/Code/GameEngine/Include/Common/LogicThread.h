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

#include <functional>

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
		CENSUS_DRAWABLE,				///< draw modules told of a transform change from the tick
		CENSUS_MODEL_CONDITION,	///< a model condition flush from the tick
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
		CENSUS_NEW_DRAWABLE,		///< Drawable::buildRenderObjects, a drawable's render objects built (M4: after the tick for one logic made)
		CENSUS_CLIENT_RANDOM,		///< GameClientRandomValue draws
		CENSUS_GHOST,						///< GhostObjectManager add/remove
		CENSUS_MESSAGESTREAM,		///< MessageStream::appendMessage
		CENSUS_DISPLAY,					///< TheDisplay shroud levels
		CENSUS_JOBS,						///< JobSystem::parallel_for
		CENSUS_ON_DEMAND_FLUSH,	///< Drawable::getDrawModulesFlushed applying deferred work inside the tick
		CENSUS_OBSERVER_LOOKAT,	///< ControlBar::getObserverLookAtPlayer, a logic read of client state (M7)
		CENSUS_DRAWABLE_PUSH,		///< Drawable::doClientWork run inside the tick: hidden, colour, decal, shadows, sub-objects, ambient sound, caption
		CENSUS_DESTROY_DRAWABLE,	///< a drawable deleted: draw modules, render objects, shadows (M4: after the tick for one logic destroyed)
		CENSUS_AUDIO_READ,			///< a logic read of the audio device: sound lengths, a track done, a handle still playing (hazard 4, M7)
		CENSUS_CLIENT_READ,			///< a logic read of other client state: selection, input enabled, camera, the water grid (hazard 4, M7)
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

	/** LOGIC-THREAD.md phase 2, M5.  The effects a tick asks of the client (sounds, FX lists, particle
		* systems) wait in one queue, in the order the tick asked for them, and runTick runs them on the
		* main thread after the drawables' deferred work, so every drawable they attach to has its render
		* objects.  Keyed on isInGameLogicUpdate past the map load, like the drawables, so -logicthread 0
		* and 1 run one path. */
	Bool defersEffects( void );

	/** Inside a tick, queue `call` for after it; anywhere else, run it now. */
	void callAfterTick( const std::function<void()>& call );

	/** LOGIC-THREAD.md phase 2, M6.  The camera calls a tick's scripts make (moves, filters, letterbox,
		* fades, the time freeze) wait in a queue of their own, run first after the tick.  Their own queue,
		* because a script asks the camera back in the same tick whether its move is finished and whether
		* time is frozen: readViewFromTick runs what is queued before such a read, so the answer is the
		* one an immediate call gave.  That run counts as ondemandflush, not tacticalview; the read itself
		* counts as clientread, which M7's captured block replaces. */
	void callViewAfterTick( const std::function<void()>& call );
	void readViewFromTick( void );
	/** defersEffects, except while readViewFromTick runs the queue: then a camera call runs its body. */
	Bool defersView( void );

	/** The tick cleared the game: what it queued has no game left to play in.  GameEngine::reset. */
	void discardEffects( void );

}  // namespace LogicThread

#define LOGIC_CENSUS( channel ) LogicThread::census( LogicThread::channel )

/** LOGIC-THREAD.md phase 2, M6.  First line of a client entry point logic calls: inside a tick the
	* call queues itself (a lambda calling the function again with copies of its arguments) and returns;
	* after the tick the copy runs the body on the main thread.  A void function only. */
#define LOGIC_AFTER_TICK( ... ) \
	if( LogicThread::defersEffects() ) { LogicThread::callAfterTick( __VA_ARGS__ ); return; }

#endif // __LOGICTHREAD_H__

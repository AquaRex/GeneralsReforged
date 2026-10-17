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

// FILE: LogicThread.cpp //////////////////////////////////////////////////////////////////////////
// Desc:   LOGIC-THREAD.md phase 1: the tick on its own thread, serialised with the main thread.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"
#include "Common/LogicThread.h"
#include "Common/AudioEventRTS.h"
#include "Common/EarlyCommandLine.h"
#include "Common/GameAudio.h"
#include "Common/MessageStream.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameClient/View.h"
#include "GameLogic/Object.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/GameLogic.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <eh.h>
#endif

thread_local Bool LogicThread::t_isLogicThread = FALSE;

namespace
{
	typedef std::chrono::steady_clock LogicClock;

	Bool						s_enabled = FALSE;
	/* A pointer, so a process that ends without ~GameEngine (a RELEASE_CRASH, exit() from a menu)
		 does not run a joinable std::thread's destructor at static teardown, which is std::terminate. */
	std::thread			*s_thread = NULL;

	/* One request and one answer, both under s_lock.  The main thread sets s_tickPending and waits
		 for s_tickDone; the logic thread waits for s_tickPending, runs the tick, sets s_tickDone.  The
		 lock's release and acquire on each side is what publishes everything the other thread wrote
		 before it, which is the whole of the memory model phase 1 needs: one thread runs at a time. */
	std::mutex								s_lock;
	std::condition_variable		s_wakeCv;
	std::condition_variable		s_doneCv;
	Bool											s_tickPending = FALSE;
	Bool											s_tickDone = FALSE;
	Bool											s_quitting = FALSE;
	std::exception_ptr				s_error;
	Real											s_tickMS = 0.0f;		///< the tick as the logic thread timed it

	// Written by the logic thread only, read by the main thread after a handoff has published them.
	Int64				s_census[ LogicThread::CENSUS_COUNT ] = { 0 };

	// Main thread only.
	Int					s_threadedTicks = 0;
	Int					s_inlineTicks = 0;
	Real				s_handoffTotalMS = 0.0f;
	Real				s_handoffMaxMS = 0.0f;
	// The handoff in microseconds, one bucket each up to a millisecond; the last bucket is everything past it.
	const Int		HANDOFF_BUCKETS = 1001;
	Int					s_handoffHistogram[ HANDOFF_BUCKETS ] = { 0 };

	const char *const CHANNEL_NAMES[ LogicThread::CENSUS_COUNT ] =
	{
		"drawable", "modelcondition", "audio", "ingameui", "controlbar", "tacticalview", "radar",
		"fxlist", "terrainvisual", "particles", "gameclient", "eva", "newdrawable", "clientrandom",
		"ghost", "messagestream", "display", "jobs", "ondemandflush", "observerlookat",
		"drawablepush", "destroydrawable", "audioread", "clientread", "assetload"
	};

	// M5: what the tick asked of the audio, the FX lists and the particle systems, in call order.
	// Filled inside the tick, run and emptied by flushAfterTick; one thread touches it at a time.
	std::vector< std::function<void()> > s_effects;

	// M6: the camera calls of the tick, see callViewAfterTick
	std::vector< std::function<void()> > s_viewCalls;

	// M7: what the tick reads of the client, see LogicThread::ClientBlock
	LogicThread::ClientBlock s_block;

	/* The sync point: the main thread, before the tick, with every call the last tick queued already
		 run.  So the block holds what the tick's first read of the client would have returned. */
	void captureClientBlock( void )
	{
		s_block.selection.clear();
		const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
		for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
		{
			LogicThread::SelectedEntry entry = { (*it)->getID(), (*it)->getObject()->getID() };
			s_block.selection.push_back( entry );
		}
		s_block.frameSelectionChanged = TheInGameUI->getFrameSelectionChanged();
		s_block.observerLookAt = TheControlBar->getObserverLookAtPlayer();
		s_block.cameraFrozen = TheTacticalView->isTimeFrozen();
		s_block.cameraMoving = TheTacticalView->isCameraMoving();
		s_block.cameraBlurring = TheTacticalView->isCameraMotionBlurring();
		TheAudio->captureForLogic();
	}

	void runViewCalls( void )
	{
		// swapped out first: a call that queues another (none does today) waits for the next run
		std::vector< std::function<void()> > calls;
		calls.swap( s_viewCalls );
		for( size_t i = 0; i < calls.size(); ++i )
			calls[ i ]();
	}

	/* Everything a tick left for the main thread, in the order it has to run: the drawables' work first
		 (M1, M3, M4 builds the render objects of the drawables the tick made), the camera (M6), then the
		 effects (M5, and M6's interface, radar, EVA, display and terrain pushes), which attach to those
		 drawables and read their bones, then the deletion of the drawables the tick destroyed, whose
		 bones a death effect may still have read, then the control bar (M2). */
	void flushAfterTick( void )
	{
		// M7: the frame the tick ran, which it used to hand the client itself; a tick that started a map
		// ran frame 0 after the load, one that cleared the game left 0
		TheGameClient->setFrame( TheGameLogic->getFrameForClient() );
		// phase 3: the client's list and lookup take what the tick made and destroyed, before anything
		// below looks a drawable up
		TheGameClient->linkDrawablesOfTick();
		Drawable::flushPendingClientWork();
		runViewCalls();
		// an effect queues nothing more: none of them runs inside a tick
		for( size_t i = 0; i < s_effects.size(); ++i )
			s_effects[ i ]();
		s_effects.clear();
		AudioEventRTS::forgetHeldCallers();
		TheGameClient->flushDestroyedDrawables();
		ControlBar::flushLogicCalls();
	}

	Real elapsedMS( LogicClock::time_point a, LogicClock::time_point b )
	{
		return std::chrono::duration<Real, std::milli>( b - a ).count();
	}

#if defined(_WIN32)
	/* WinMain's _set_se_translator covers only the thread that set it.  Without it here a fault in the
		 tick reached the catch below as a bare structured exception and the dump showed the main thread's
		 stack after the rethrow, not the fault's. */
	_se_translator_function s_seTranslator = NULL;
#endif

	void threadMain( void )
	{
#if defined(_WIN32)
		_set_se_translator( s_seTranslator );
#endif
		LogicThread::t_isLogicThread = TRUE;
		// GameLogic::update sets it again on entry; a new thread starts on the CRT default.
		setFPMode();

		for( ;; )
		{
			{
				std::unique_lock<std::mutex> lock( s_lock );
				s_wakeCv.wait( lock, []{ return s_tickPending || s_quitting; } );
				if( s_quitting )
					break;
				s_tickPending = FALSE;
			}

			const LogicClock::time_point t0 = LogicClock::now();
			std::exception_ptr error;
			try
			{
				TheGameLogic->UPDATE();
			}
			catch( ... )
			{
				error = std::current_exception();
			}
			const LogicClock::time_point t1 = LogicClock::now();

			{
				std::lock_guard<std::mutex> lock( s_lock );
				s_error = error;
				s_tickMS = elapsedMS( t0, t1 );
				s_tickDone = TRUE;
			}
			s_doneCv.notify_one();
		}
	}

	/* A tick that starts or ends a map builds the load screen, pushes shell windows, pumps the window
		 messages and creates every render object of the map on the device.  The plan's invariant puts
		 loading and teardown at the sync point with the logic thread parked, and in phase 1 that means
		 these ticks run where they always ran. */
	Bool tickNeedsMainThread( void )
	{
		return TheGameLogic->isNewGamePending()
			|| TheCommandList->containsMessageOfType( GameMessage::MSG_NEW_GAME )
			|| TheCommandList->containsMessageOfType( GameMessage::MSG_CLEAR_GAME_DATA );
	}

	Int handoffPercentileMicros( Real fraction )
	{
		const Int want = (Int)( s_threadedTicks * fraction );
		Int seen = 0;
		for( Int i = 0; i < HANDOFF_BUCKETS; ++i )
		{
			seen += s_handoffHistogram[ i ];
			if( seen > want )
				return i;
		}
		return HANDOFF_BUCKETS - 1;
	}

}  // anonymous namespace

//-------------------------------------------------------------------------------------------------
void LogicThread::noteCensus( Channel channel )
{
	++s_census[ channel ];
}

//-------------------------------------------------------------------------------------------------
void LogicThread::init( void )
{
	char value[ 32 ];
	if( !findEarlyCommandLineValue( L"-logicthread", value, sizeof(value) ) || atoi( value ) == 0 )
		return;

	s_enabled = TRUE;
#if defined(_WIN32)
	// there is no getter: setting returns the one before, which goes straight back
	s_seTranslator = _set_se_translator( NULL );
	_set_se_translator( s_seTranslator );
#endif
	s_thread = new std::thread( threadMain );
	DEBUG_LOG(( "LOGICTHREAD: the tick runs on its own thread, the main thread waiting for it\n" ));
}

//-------------------------------------------------------------------------------------------------
void LogicThread::shutdown( void )
{
	if( s_thread == NULL )
		return;
	{
		std::lock_guard<std::mutex> lock( s_lock );
		s_quitting = TRUE;
	}
	s_wakeCv.notify_one();
	s_thread->join();
	delete s_thread;
	s_thread = NULL;
	s_enabled = FALSE;
}

//-------------------------------------------------------------------------------------------------
void LogicThread::runTick( void )
{
	captureClientBlock();
	// the client's clock reads the frame the tick runs, as GameLogic::update set it at its top
	TheGameClient->setFrame( TheGameLogic->getFrame() );

	if( !s_enabled || tickNeedsMainThread() )
	{
		++s_inlineTicks;
		TheGameLogic->UPDATE();
		flushAfterTick();
		return;
	}

	const LogicClock::time_point t0 = LogicClock::now();
	{
		std::lock_guard<std::mutex> lock( s_lock );
		s_tickPending = TRUE;
	}
	s_wakeCv.notify_one();

	std::exception_ptr error;
	Real tickMS;
	{
		std::unique_lock<std::mutex> lock( s_lock );
		s_doneCv.wait( lock, []{ return s_tickDone; } );
		s_tickDone = FALSE;
		error = s_error;
		s_error = NULL;
		tickMS = s_tickMS;
	}
	const LogicClock::time_point t1 = LogicClock::now();

	// The inline tick left this thread's FPU in setFPMode's state; keep the main thread as it was at 0.
	setFPMode();

	// What the thread costs: the wait less the tick the logic thread timed for itself.
	Real handoffMS = elapsedMS( t0, t1 ) - tickMS;
	if( handoffMS < 0.0f )
		handoffMS = 0.0f;
	++s_threadedTicks;
	s_handoffTotalMS += handoffMS;
	if( handoffMS > s_handoffMaxMS )
		s_handoffMaxMS = handoffMS;
	Int bucket = (Int)( handoffMS * 1000.0f );
	if( bucket >= HANDOFF_BUCKETS )
		bucket = HANDOFF_BUCKETS - 1;
	++s_handoffHistogram[ bucket ];

	if( error )
		std::rethrow_exception( error );

	// What the tick deferred on the drawables (phase 2, M1), the effects (M5) and the control bar (M2),
	// here on the main thread at both settings.
	flushAfterTick();
}

//-------------------------------------------------------------------------------------------------
Bool LogicThread::defersEffects( void )
{
	return TheGameLogic != NULL && TheGameLogic->isInGameLogicUpdate() && !TheGameLogic->isLoadingMap();
}

//-------------------------------------------------------------------------------------------------
void LogicThread::callAfterTick( const std::function<void()>& call )
{
	if( defersEffects() )
		s_effects.push_back( call );
	else
		call();
}

//-------------------------------------------------------------------------------------------------
Bool LogicThread::defersView( void )
{
	return defersEffects();
}

//-------------------------------------------------------------------------------------------------
void LogicThread::callViewAfterTick( const std::function<void()>& call )
{
	if( defersView() )
		s_viewCalls.push_back( call );
	else
		call();
}

//-------------------------------------------------------------------------------------------------
const LogicThread::ClientBlock &LogicThread::clientBlock( void )
{
	return s_block;
}

//-------------------------------------------------------------------------------------------------
void LogicThread::noteSelected( DrawableID drawable, ObjectID object, Bool selected )
{
	std::vector<SelectedEntry>::iterator it = s_block.selection.begin();
	while( it != s_block.selection.end() && it->drawable != drawable )
		++it;
	if( selected == ( it != s_block.selection.end() ) )
		return;
	if( selected )
	{
		SelectedEntry entry = { drawable, object };
		s_block.selection.push_back( entry );
	}
	else
		s_block.selection.erase( it );
	s_block.frameSelectionChanged = TheGameLogic->getFrame();
}

//-------------------------------------------------------------------------------------------------
void LogicThread::noteSelectionCleared( void )
{
	if( s_block.selection.empty() )
		return;
	s_block.selection.clear();
	s_block.frameSelectionChanged = TheGameLogic->getFrame();
}

//-------------------------------------------------------------------------------------------------
Bool LogicThread::isSelectedForTick( const Drawable *draw )
{
	if( !defersEffects() )
		return draw->isSelected();
	const DrawableID id = draw->getID();
	for( size_t i = 0; i < s_block.selection.size(); ++i )
		if( s_block.selection[ i ].drawable == id )
			return TRUE;
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void LogicThread::noteViewEffect( ViewEffect effect )
{
	switch( effect )
	{
		case VIEW_MOVES:					s_block.cameraMoving = TRUE;		break;
		case VIEW_FREEZES_TIME:		s_block.cameraFrozen = TRUE;		break;
		case VIEW_BLUR_STARTS:		s_block.cameraBlurring = TRUE;	break;
		case VIEW_BLUR_ENDS:			s_block.cameraBlurring = FALSE;	break;
	}
}

//-------------------------------------------------------------------------------------------------
/* Phase 3: the block at both settings.  Asking the camera meant running the tick's queued camera calls
	 inside the tick, on the camera the render reads; phase 2's three campaign missions found 0 reads,
	 of 18680, where the camera and the block disagreed. */
Bool LogicThread::isCameraMovementFinishedForTick( void )
{
	return s_block.cameraBlurring || !s_block.cameraMoving;
}

//-------------------------------------------------------------------------------------------------
Bool LogicThread::isTimeFrozenForTick( void )
{
	return s_block.cameraFrozen;
}

//-------------------------------------------------------------------------------------------------
void LogicThread::discardEffects( void )
{
	s_effects.clear();
	s_viewCalls.clear();
	AudioEventRTS::forgetHeldCallers();
}

//-------------------------------------------------------------------------------------------------
void LogicThread::report( void )
{
	DEBUG_LOG(( "HEADLESS LOGICTHREAD: %s | %d ticks on the logic thread, %d on the main thread | handoff %.3f/%.3f/%.3f max %.3f ms mean/p50/p99\n",
		s_enabled ? "on" : "off", s_threadedTicks, s_inlineTicks,
		s_threadedTicks ? s_handoffTotalMS / s_threadedTicks : 0.0f,
		handoffPercentileMicros( 0.5f ) / 1000.0f, handoffPercentileMicros( 0.99f ) / 1000.0f, s_handoffMaxMS ));

	char line[ 1024 ];
	Int used = 0;
	for( Int i = 0; i < CENSUS_COUNT; ++i )
		used += snprintf( line + used, sizeof(line) - used, "%s%s %lld", i ? " | " : "", CHANNEL_NAMES[ i ], (long long)s_census[ i ] );
	DEBUG_LOG(( "HEADLESS LOGICTHREAD CENSUS: %s\n", line ));
}

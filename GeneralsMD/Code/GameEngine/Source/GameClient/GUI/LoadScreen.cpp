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

// FILE: LoadScreen.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Mar 2002
//
//	Filename: 	LoadScreen.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	Contains each of the different derived LoadClasses for each of the
//						Different kind of games we can have.
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Platform/SleepMilliseconds.h"
#include "Lib/Clock.h"

#include "Lib/WideCharFns.h"

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "Common/NameKeyGenerator.h"
#include "Common/AudioAffect.h"
#include "Common/AudioEventRTS.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GameLOD.h"
#include "Common/GameState.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GadgetProgressBar.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/LoadScreen.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/WindowVideoManager.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameLogic/AI.h"	// aiPersonalityName, for a computer seat's card
#include "GameLogic/FPUControl.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/RankPointValue.h"

//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-----------------------------------------------------------------------------
// PRIVATE TYPES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE DATA ///////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PUBLIC DATA ////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
enum{
FRAME_TITLES_START = 20,
FRAME_TELETYPE_START = 24,
FRAME_FUDGE_ADD = 30,
FRAME_PORTRAITS_START = 35,
FRAME_OUTER_CIRCLE_LINE_SHOW = 50,
FRAME_INNER_CIRCLE_LINE_SHOW = 52,
FRAME_OUTER_CIRCLE_ALPHA_SHOW = 63,
FRAME_INNER_CIRCLE_ALPHA_SHOW = 74,
FRAME_OUTER_CIRCLE_LINE_HIDE = 75,
FRAME_INNER_BACKDROP_ALPHA_SHOW = 80,
FRAME_INNER_CIRCLE_LINE_HIDE = 81,
FRAME_VS_ANIM_START = 98,
FRAME_RIGHT_VOICE = 140,
};

static const Int TELETYPE_UPDATE_FREQ = 2; // how many frames between teletype updates



//-----------------------------------------------------------------------------
// LoadScreen Class 
//-----------------------------------------------------------------------------

LoadScreen::LoadScreen( void )
{
	m_loadScreen = NULL;		
}

LoadScreen::~LoadScreen( void )
{
	//if(m_loadScreen)
	//	delete (m_loadScreen);
	if(m_loadScreen)
		TheWindowManager->winDestroy( m_loadScreen );
	m_loadScreen = NULL;
}

void LoadScreen::update( Int percent )
{
	TheGameEngine->serviceWindowsOS();
	if (TheGameEngine->getQuitting())
		return;	//don't bother with any of this if the player is exiting game.

	TheWindowManager->update();
	TheDisplay->update();
	// redraw all views, update the GUI
	TheDisplay->draw();

	setFPMode();
}

// LoadScreenBoard ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
/* A match's load, skirmish, LAN, online or replay, as one board drawn in code: a card per player with
	 the general's portrait or the side's crest, the name in the player's colour, the army and, for a
	 computer seat, its level and personality; the teams in two columns facing each other across the
	 map, or the players shared between the columns in a free-for-all; the map's name, its preview
	 with the start spots, a tip and the load's progress along the bottom.  In a network game every
	 human's card carries his own bar.

	 Everything is laid out once, when the board is made, against a 1920x1080 picture scaled to the
	 screen; a pass draws flat fills, the images the lobby and the challenge menu already load (the
	 portraits share one 512 page, the crests another) and display strings made up front.  The
	 colours are zerohour.gg's, as the -directorrecord broadcast's are, and so are its fonts' Windows
	 stand-ins. */

static const Color BOARD_GROUND = GameMakeColor( 0x0c, 0x12, 0x20, 255 );
static const Color BOARD_PANEL = GameMakeColor( 0x12, 0x1a, 0x2b, 255 );
static const Color BOARD_LINE = GameMakeColor( 0x1f, 0x29, 0x40, 255 );
static const Color BOARD_EDGE = GameMakeColor( 0x2f, 0x3b, 0x57, 255 );
static const Color BOARD_INK = GameMakeColor( 0xe8, 0xea, 0xf0, 255 );
static const Color BOARD_MUTED = GameMakeColor( 0x8e, 0x97, 0xad, 255 );
static const Color BOARD_BLUE = GameMakeColor( 0x80, 0x95, 0xea, 255 );
static const Color BOARD_SHADOW = GameMakeColor( 0, 0, 0, 255 );
static const char *const BOARD_WORDS = "Segoe UI";
static const char *const BOARD_NUMBERS = "Consolas";
/// the picture the sizes below are given for; a smaller screen scales them down, 12 pixels the floor
static const Real BOARD_WIDTH = 1920.0f;
static const Real BOARD_HEIGHT = 1080.0f;
static const Int BOARD_SMALLEST_TEXT = 12;
static const Int BOARD_TIP_COUNT = 10;
static const UnsignedInt BOARD_TIP_MILLISECONDS = 8000;
static const Int BOARD_WASH_STRIPS = 48;
/// a middle dot between spaces, spelled out: the source is read in the machine's code page
static const WideChar BOARD_SEPARATOR[] = { ' ', ' ', ' ', 0x00B7, ' ', ' ', ' ', 0 };

/// one player's card
struct BoardCard
{
	Int slot;
	Color color;
	Color nameColor;					///< the colour, lifted toward white where it is too dark to read on the panel
	Int team;
	Int startSpot;						///< the start position the others may see, -1 when hidden
	Int percent;							///< this player's own load, -1 on a card without a bar
	Bool mirrored;						///< the right column's cards face left
	const Image *emblem;			///< the general's portrait or the side's crest, NULL for a hidden or random side
	const Image *watermark;		///< the side's flag, faint behind the words
	const Image *badge;				///< an online player's rank
	DisplayString *name;
	DisplayString *army;
	DisplayString *detail;		///< a computer seat's level and personality
	DisplayString *teamText;	///< the team on a card in a free-for-all layout
	DisplayString *percentText;
	IRegion2D box;
};

struct BoardSpot
{
	ICoord2D pos;
	Color color;
};

class LoadScreenBoard
{
public:
	LoadScreenBoard( void );
	~LoadScreenBoard( void );

	/// lays the board out for this game and makes the window that draws it
	GameWindow *create( GameInfo *game, const char *modeLabel, Bool cardBars );
	/// a campaign mission's: the header, the briefing and the foot over the mission's own picture
	GameWindow *createMission( GameWindow *parent, const UnicodeString &title, const UnicodeString &subtitle,
		const UnicodeString *briefing, Int briefingLines );
	void setBadge( Int slot, const Image *badge );
	void setProgress( Int slot, Int percent );
	void setLocalProgress( Int percent );

private:
	static void drawWindow( GameWindow *window, WinInstanceData *instData );
	void prepare( const UnicodeString &title, const UnicodeString &subtitle );
	GameWindow *makeWindow( GameWindow *parent );
	void draw( void );
	void drawMatch( void );
	void drawBriefing( void );
	void drawCard( const BoardCard &card );
	Int scaled( Real pixels ) const { return REAL_TO_INT( pixels * m_scale + 0.5f ); }
	GameFont *font( const char *name, Real pixels, Bool bold ) const;
	DisplayString *text( const UnicodeString &words, GameFont *font );
	BoardCard *cardFor( Int slot );
	void layoutColumn( std::vector< Int > &column, Int left, Int top, Int bottom, Bool mirrored );

	Real m_scale;
	Int m_left;								///< the 16:9 picture's corner, inset on a squarer screen
	Int m_top;
	Int m_width;							///< the screen
	Int m_height;
	std::vector< BoardCard > m_cards;
	std::vector< BoardSpot > m_spots;
	std::vector< DisplayString * > m_strings;
	Bool m_mission;
	DisplayString *m_briefingHead;
	std::vector< DisplayString * > m_briefing;
	IRegion2D m_briefingBox;
	DisplayString *m_title;
	DisplayString *m_subtitle;
	DisplayString *m_wordmark[ 2 ];
	DisplayString *m_columnHead[ 2 ];
	const Image *m_backdrop[ 2 ];
	IRegion2D m_backdropBox[ 2 ];
	Color m_columnTeam[ 2 ];
	ICoord2D m_columnHeadPos[ 2 ];
	DisplayString *m_centerLabel;
	ICoord2D m_centerLabelPos;
	DisplayString *m_observers;
	ICoord2D m_observersPos;
	DisplayString *m_unknown;
	const Image *m_mapImage;
	IRegion2D m_mapBox;
	IRegion2D m_mapDrawn;
	DisplayString *m_tipLabel;
	DisplayString *m_tip;
	Int m_tipIndex;
	UnsignedInt m_tipSince;
	DisplayString *m_percent;
	DisplayString *m_waiting;
	Int m_localPercent;
	Bool m_network;
};

LoadScreenBoard::LoadScreenBoard( void )
{
	m_width = TheDisplay->getWidth();
	m_height = TheDisplay->getHeight();
	m_scale = min( m_width / BOARD_WIDTH, m_height / BOARD_HEIGHT );
	m_left = ( m_width - scaled( BOARD_WIDTH ) ) / 2;
	m_top = ( m_height - scaled( BOARD_HEIGHT ) ) / 2;
	m_mission = FALSE;
	m_briefingHead = NULL;
	m_title = m_subtitle = m_centerLabel = m_observers = m_unknown = NULL;
	m_wordmark[ 0 ] = m_wordmark[ 1 ] = NULL;
	m_columnHead[ 0 ] = m_columnHead[ 1 ] = NULL;
	m_backdrop[ 0 ] = m_backdrop[ 1 ] = NULL;
	m_mapImage = NULL;
	m_tipLabel = m_tip = m_percent = m_waiting = NULL;
	m_tipIndex = 0;
	m_tipSince = 0;
	m_localPercent = 0;
	m_network = FALSE;
}

LoadScreenBoard::~LoadScreenBoard( void )
{
	for( size_t each = 0; each < m_strings.size(); each++ )
		TheDisplayStringManager->freeDisplayString( m_strings[ each ] );
}

GameFont *LoadScreenBoard::font( const char *name, Real pixels, Bool bold ) const
{
	// a point is 4/3 of a pixel at the 96 dots an inch the glyphs are drawn at
	const Int height = max( BOARD_SMALLEST_TEXT, scaled( pixels ) );
	return TheFontLibrary->getFont( AsciiString( name ), max( 1, height * 3 / 4 ), bold );
}

DisplayString *LoadScreenBoard::text( const UnicodeString &words, GameFont *font )
{
	DisplayString *string = TheDisplayStringManager->newDisplayString();
	string->setFont( font );
	string->setText( words );
	m_strings.push_back( string );
	return string;
}

BoardCard *LoadScreenBoard::cardFor( Int slot )
{
	for( size_t each = 0; each < m_cards.size(); each++ )
	{
		if( m_cards[ each ].slot == slot )
			return &m_cards[ each ];
	}
	return NULL;
}

static Int stringWidth( DisplayString *string )
{
	Int width, height;
	string->getSize( &width, &height );
	return width;
}

static Int stringHeight( DisplayString *string )
{
	Int width, height;
	string->getSize( &width, &height );
	return height;
}

static Color withAlpha( Color color, Int alpha )
{
	UnsignedByte r, g, b, a;
	GameGetColorComponents( color, &r, &g, &b, &a );
	return GameMakeColor( r, g, b, alpha );
}

/** Blue and purple house colours are too dark for words on the panel; they are lifted part of the
	* way to white, the bar and the map spot keep the colour itself. */
static Color readableColor( Color color )
{
	UnsignedByte r, g, b, a;
	GameGetColorComponents( color, &r, &g, &b, &a );
	const Int luma = ( 54 * r + 183 * g + 19 * b ) / 256;
	if( luma >= 120 )
		return color;
	const Int lift = 110;	// of 256
	return GameMakeColor( r + ( 255 - r ) * lift / 256, g + ( 255 - g ) * lift / 256, b + ( 255 - b ) * lift / 256, 255 );
}

/** The side's crest from the lobby: the three 144 pixel logos on one page.  A mod's side brings its
	* own lobby glyph or nothing. */
static const Image *sideCrest( const PlayerTemplate *side )
{
	const AsciiString base = side->getBaseSide();
	if( base.compareNoCase( "USA" ) == 0 )
		return TheMappedImageCollection->findImageByName( "SAFactionLogo144_US" );
	if( base.compareNoCase( "China" ) == 0 )
		return TheMappedImageCollection->findImageByName( "SNFactionLogo144_China" );
	if( base.compareNoCase( "GLA" ) == 0 )
		return TheMappedImageCollection->findImageByName( "SUFactionLogo144_GLA" );
	return side->getGeneralImage();
}

/** A general's face from the challenge menu, all ten on one page; a plain side shows its crest. */
static const Image *sideEmblem( const PlayerTemplate *side )
{
	const GeneralPersona *general = TheChallengeGenerals->getGeneralByTemplateName( side->getName() );
	if( general && general->getBioPortraitSmall() )
		return general->getBioPortraitSmall();
	return sideCrest( side );
}

/** One tip at random to start, the next every eight seconds; the clock is the wall's, never the
	* logic's dice. */
static UnicodeString tipText( Int index )
{
	AsciiString label;
	label.format( "GUI:LoadTip%d", index + 1 );
	return TheGameText->fetch( label );
}

GameWindow *LoadScreenBoard::create( GameInfo *game, const char *modeLabel, Bool cardBars )
{
	m_network = cardBars;
	GameFont *labelFont = font( BOARD_NUMBERS, 16, TRUE );
	GameFont *centerFont = font( BOARD_WORDS, 64, TRUE );
	GameFont *centerSmallFont = font( BOARD_WORDS, 24, TRUE );
	GameFont *observerFont = font( BOARD_WORDS, 16, FALSE );
	GameFont *nameFont = font( BOARD_WORDS, 26, TRUE );
	GameFont *armyFont = font( BOARD_WORDS, 18, FALSE );
	GameFont *detailFont = font( BOARD_WORDS, 16, FALSE );

	// a watcher, a replay's viewer among them, sees every side as it is; a player sees what the
	// lobby let him see of his enemies
	// (a director's recording has no local seat at all)
	const Bool seeAll = TheGameLogic->isInReplayGame() || game->getLocalSlotNum() < 0
		|| game->getConstSlot( game->getLocalSlotNum() )->getOriginalPlayerTemplate() == PLAYERTEMPLATE_OBSERVER;

	// the cards, with each team's players together in the order the lobby lists them
	UnicodeString observerNames;
	std::vector< Int > teamOf;		// a team number, or -100 - slot for a player alone
	std::vector< std::vector< Int > > groups;
	for( Int i = 0; i < MAX_SLOTS; ++i )
	{
		const GameSlot *slot = game->getConstSlot( i );
		if( slot == NULL || !slot->isOccupied() )
			continue;
		if( slot->getOriginalPlayerTemplate() == PLAYERTEMPLATE_OBSERVER )
		{
			if( !observerNames.isEmpty() )
				observerNames.concat( u", " );
			observerNames.concat( slot->getName() );
			continue;
		}

		BoardCard card;
		card.slot = i;
		const Int colorIndex = seeAll ? slot->getColor() : slot->getApparentColor();
		card.color = TheMultiplayerSettings->getColor( colorIndex )->getColor();
		card.nameColor = readableColor( card.color );
		card.team = slot->getTeamNumber();
		card.startSpot = seeAll ? slot->getStartPos() : slot->getApparentStartPos();
		card.percent = cardBars && slot->isHuman() ? 0 : -1;
		card.mirrored = FALSE;
		card.badge = NULL;
		card.emblem = NULL;
		card.watermark = NULL;
		const Int sideIndex = seeAll ? slot->getPlayerTemplate() : slot->getApparentPlayerTemplate();
		UnicodeString army = TheGameText->fetch( "GUI:Random" );
		if( sideIndex >= 0 )
		{
			const PlayerTemplate *side = ThePlayerTemplateStore->getNthPlayerTemplate( sideIndex );
			card.emblem = sideEmblem( side );
			// a plain side's emblem is its crest already
			card.watermark = card.emblem == sideCrest( side ) ? NULL : sideCrest( side );
			army = side->getDisplayName();
		}
		card.name = text( slot->getName(), nameFont );
		card.army = text( army, armyFont );

		UnicodeString detail;
		if( slot->isAI() )
		{
			const UnicodeString level = SlotStateName( slot->getState() );
			if( slot->getName().compare( level ) != 0 )
				detail = level;
			if( slot->getAIPersonality() != AIPERSONA_NORMAL )
			{
				AsciiString label;
				label.format( "GUI:AIPersona%s", aiPersonalityName( (AIPersonality)slot->getAIPersonality() ) );
				if( !detail.isEmpty() )
					detail.concat( BOARD_SEPARATOR );
				detail.concat( TheGameText->fetch( label ) );
			}
		}
		card.detail = text( detail, detailFont );
		UnicodeString teamWords;
		if( card.team >= 0 )
			teamWords.format( TheGameText->fetch( "GUI:LoadTeam" ), card.team + 1 );
		card.teamText = text( teamWords, labelFont );
		card.percentText = text( UnicodeString( u"0%" ), labelFont );
		m_cards.push_back( card );

		const Int key = card.team >= 0 ? card.team : -100 - i;
		size_t group = 0;
		while( group < teamOf.size() && teamOf[ group ] != key )
			group++;
		if( group == teamOf.size() )
		{
			teamOf.push_back( key );
			groups.push_back( std::vector< Int >() );
		}
		groups[ group ].push_back( (Int)m_cards.size() - 1 );
	}

	// two sides face each other across the map; more share the two columns, a whole team to a side
	const Bool versus = groups.size() == 2;
	Bool everyoneAlone = TRUE;
	std::vector< Int > columns[ 2 ];
	UnicodeString makeup;
	for( size_t group = 0; group < groups.size(); group++ )
	{
		if( groups[ group ].size() > 1 )
			everyoneAlone = FALSE;
		const Int side = versus ? (Int)group : ( columns[ 1 ].size() < columns[ 0 ].size() ? 1 : 0 );
		for( size_t each = 0; each < groups[ group ].size(); each++ )
			columns[ side ].push_back( groups[ group ][ each ] );
		UnicodeString count;
		count.format( u"%d", (Int)groups[ group ].size() );
		if( !makeup.isEmpty() )
			makeup.concat( u" vs " );
		makeup.concat( count );
	}
	const Bool freeForAll = everyoneAlone && groups.size() > 2;
	if( freeForAll )
		makeup = TheGameText->fetch( "GUI:LoadFreeForAll" );

	// the header: the map, then what kind of game, how many a side and the starting cash
	const MapMetaData *map = TheMapCache->findMap( game->getMap() );
	UnicodeString mapName;
	if( map )
		mapName = map->m_displayName;
	else
		mapName.translate( TheGameState->getMapLeafName( game->getMap() ) );
	const Int margin = scaled( 64 );
	UnicodeString subtitle = TheGameText->fetch( modeLabel );
	if( !makeup.isEmpty() )
	{
		subtitle.concat( BOARD_SEPARATOR );
		subtitle.concat( makeup );
	}
	UnicodeString cash;
	cash.format( u"$%u", game->getStartingCash().countMoney() );
	subtitle.concat( BOARD_SEPARATOR );
	subtitle.concat( cash );
	prepare( mapName, subtitle );

	// the cards: two columns with the map between, as tall as four cards a side allow
	m_unknown = text( UnicodeString( u"?" ), centerFont );

	const Int columnWidth = scaled( 640 );
	const Int bandTop = m_top + scaled( 200 );
	const Int bandBottom = m_top + scaled( 920 );
	Int cardsTop = bandTop;
	for( Int side = 0; side < 2; side++ )
	{
		m_columnHead[ side ] = NULL;
		if( !versus || teamOf[ side ] < 0 )
			continue;
		UnicodeString label;
		label.format( TheGameText->fetch( "GUI:LoadTeam" ), teamOf[ side ] + 1 );
		m_columnHead[ side ] = text( label, labelFont );
		m_columnTeam[ side ] = LobbyTeamColor( teamOf[ side ] );
		cardsTop = bandTop + stringHeight( m_columnHead[ side ] ) + scaled( 12 );
	}
	const Int leftX = m_left + margin;
	const Int rightX = m_left + scaled( BOARD_WIDTH ) - margin - columnWidth;
	layoutColumn( columns[ 0 ], leftX, cardsTop, bandBottom, FALSE );
	layoutColumn( columns[ 1 ], rightX, cardsTop, bandBottom, TRUE );
	// behind each column, large and faint, the crest of the side its first card plays
	for( Int side = 0; side < 2; side++ )
	{
		m_backdrop[ side ] = NULL;
		if( columns[ side ].empty() )
			continue;
		const BoardCard &first = m_cards[ columns[ side ][ 0 ] ];
		m_backdrop[ side ] = first.watermark ? first.watermark : first.emblem;
		const Int size = scaled( 760 );
		const Int centerX = ( side == 0 ? leftX : rightX ) + columnWidth / 2;
		m_backdropBox[ side ].lo.x = centerX - size / 2;
		m_backdropBox[ side ].lo.y = ( bandTop + bandBottom - size ) / 2;
		m_backdropBox[ side ].hi.x = centerX + size / 2;
		m_backdropBox[ side ].hi.y = m_backdropBox[ side ].lo.y + size;
	}
	// a team's name sits over its first card, the swatch on the outer edge
	for( Int side = 0; side < 2; side++ )
	{
		if( m_columnHead[ side ] == NULL )
			continue;
		const Int swatch = stringHeight( m_columnHead[ side ] ) - scaled( 4 );
		m_columnHeadPos[ side ].y = m_cards[ columns[ side ][ 0 ] ].box.lo.y - stringHeight( m_columnHead[ side ] ) - scaled( 12 );
		m_columnHeadPos[ side ].x = side == 0 ? leftX + swatch + scaled( 10 ) : rightX + columnWidth - stringWidth( m_columnHead[ side ] ) - swatch - scaled( 10 );
	}

	// the middle: VS or free for all over the map, the watchers under it
	m_centerLabel = NULL;
	if( versus )
		m_centerLabel = text( UnicodeString( u"VS" ), centerFont );
	else if( freeForAll )
		m_centerLabel = text( TheGameText->fetch( "GUI:LoadFreeForAll" ), centerSmallFont );
	m_observers = NULL;
	const Int mapSize = min( scaled( 384 ), rightX - leftX - columnWidth - 2 * scaled( 48 ) );
	if( !observerNames.isEmpty() )
	{
		UnicodeString line = TheGameText->fetch( "GUI:LoadObservers" );
		line.concat( u"  " );
		line.concat( observerNames );
		m_observers = text( line, observerFont );
		m_observers->setWordWrap( mapSize );
		m_observers->setWordWrapCentered( TRUE );
	}
	const Int gap = scaled( 28 );
	Int stack = mapSize;
	if( m_centerLabel )
		stack += stringHeight( m_centerLabel ) + gap;
	if( m_observers )
		stack += gap + stringHeight( m_observers );
	const Int middle = m_left + scaled( BOARD_WIDTH ) / 2;
	Int y = ( bandTop + bandBottom - stack ) / 2;
	if( m_centerLabel )
	{
		m_centerLabelPos.x = middle - stringWidth( m_centerLabel ) / 2;
		m_centerLabelPos.y = y;
		y += stringHeight( m_centerLabel ) + gap;
	}
	m_mapBox.lo.x = middle - mapSize / 2;
	m_mapBox.lo.y = y;
	m_mapBox.hi.x = m_mapBox.lo.x + mapSize;
	m_mapBox.hi.y = y + mapSize;
	m_mapDrawn = m_mapBox;
	y += mapSize + gap;
	if( m_observers )
	{
		m_observersPos.x = middle - mapSize / 2;
		m_observersPos.y = y;
	}

	// the preview the lobby showed, at the map's own shape, and every start spot that is not hidden
	m_mapImage = getMapPreviewImage( game->getMap() );
	if( map )
	{
		findDrawPositions( m_mapBox.lo.x, m_mapBox.lo.y, mapSize, mapSize, map->m_extent, &m_mapDrawn.lo, &m_mapDrawn.hi );
		for( size_t each = 0; each < m_cards.size(); each++ )
		{
			AsciiString waypoint;
			waypoint.format( "Player_%d_Start", m_cards[ each ].startSpot + 1 );
			WaypointMap::const_iterator it = map->m_waypoints.find( waypoint );
			if( m_cards[ each ].startSpot < 0 || it == map->m_waypoints.end() )
				continue;
			const Real across = ( it->second.x - map->m_extent.lo.x ) / map->m_extent.width();
			const Real down = 1.0f - ( it->second.y - map->m_extent.lo.y ) / map->m_extent.height();
			BoardSpot spot;
			spot.pos.x = m_mapDrawn.lo.x + REAL_TO_INT( across * ( m_mapDrawn.hi.x - m_mapDrawn.lo.x ) );
			spot.pos.y = m_mapDrawn.lo.y + REAL_TO_INT( down * ( m_mapDrawn.hi.y - m_mapDrawn.lo.y ) );
			spot.color = m_cards[ each ].color;
			m_spots.push_back( spot );
		}
	}

	return makeWindow( NULL );
}

/** The header's and the foot's words, the same on every board. */
void LoadScreenBoard::prepare( const UnicodeString &title, const UnicodeString &subtitle )
{
	GameFont *wordmarkFont = font( BOARD_WORDS, 24, TRUE );
	GameFont *tipFont = font( BOARD_WORDS, 18, FALSE );
	m_title = text( title, font( BOARD_WORDS, 40, TRUE ) );
	m_subtitle = text( subtitle, font( BOARD_WORDS, 18, FALSE ) );
	m_wordmark[ 0 ] = text( UnicodeString( u"Zero Hour" ), wordmarkFont );
	m_wordmark[ 1 ] = text( UnicodeString( u" Reforged" ), wordmarkFont );

	// the foot: a tip, the load's percentage and its bar
	m_tipLabel = text( TheGameText->fetch( "GUI:LoadTip" ), font( BOARD_NUMBERS, 16, TRUE ) );
	m_tipIndex = (Int)( Clock_Milliseconds() % BOARD_TIP_COUNT );
	m_tipSince = Clock_Milliseconds();
	m_tip = text( tipText( m_tipIndex ), tipFont );
	m_percent = text( UnicodeString( u"0%" ), font( BOARD_NUMBERS, 20, TRUE ) );
	m_waiting = text( TheGameText->fetch( "GUI:LoadWaiting" ), tipFont );
	m_localPercent = 0;
}

GameWindow *LoadScreenBoard::makeWindow( GameWindow *parent )
{
	GameWindow *window = TheWindowManager->winCreate( parent, WIN_STATUS_ENABLED, 0, 0, m_width, m_height, GameWinDefaultSystem );
	window->winSetUserData( this );
	window->winSetDrawFunc( drawWindow );
	window->winBringToTop();
	return window;
}

/** A mission keeps its own picture behind; the header names it, a panel on the left carries the
	* briefing the mission's load always had and never showed, and the foot is a match's. */
GameWindow *LoadScreenBoard::createMission( GameWindow *parent, const UnicodeString &title, const UnicodeString &subtitle,
	const UnicodeString *briefing, Int briefingLines )
{
	m_mission = TRUE;
	prepare( title, subtitle );

	const Int pad = scaled( 28 );
	const Int width = scaled( 640 );
	m_briefingBox.lo.x = m_left + scaled( 64 );
	m_briefingBox.lo.y = m_top + scaled( 216 );
	m_briefingBox.hi.x = m_briefingBox.lo.x + width;
	m_briefingHead = text( TheGameText->fetch( "GUI:LoadBriefing" ), font( BOARD_NUMBERS, 16, TRUE ) );
	Int bottom = m_briefingBox.lo.y + pad + stringHeight( m_briefingHead ) + scaled( 8 );
	GameFont *lineFont = font( BOARD_WORDS, 22, FALSE );
	for( Int line = 0; line < briefingLines; line++ )
	{
		if( briefing[ line ].isEmpty() )
			continue;
		DisplayString *words = text( briefing[ line ], lineFont );
		words->setWordWrap( width - 2 * pad );
		m_briefing.push_back( words );
		bottom += scaled( 10 ) + stringHeight( words );
	}
	m_briefingBox.hi.y = bottom + pad;
	return makeWindow( parent );
}

/** One column's cards, as tall as the column allows up to a 1v1's, centred in it. */
void LoadScreenBoard::layoutColumn( std::vector< Int > &column, Int left, Int top, Int bottom, Bool mirrored )
{
	if( column.empty() )
		return;
	const Int count = (Int)column.size();
	const Int gap = scaled( 16 );
	const Int height = min( scaled( 220 ), ( bottom - top - gap * ( count - 1 ) ) / count );
	Int y = top + ( bottom - top - ( height * count + gap * ( count - 1 ) ) ) / 2;
	for( Int each = 0; each < count; each++ )
	{
		BoardCard &card = m_cards[ column[ each ] ];
		card.mirrored = mirrored;
		card.box.lo.x = left;
		card.box.lo.y = y;
		card.box.hi.x = left + scaled( 640 );
		card.box.hi.y = y + height;
		y += height + gap;
	}
}

void LoadScreenBoard::setBadge( Int slot, const Image *badge )
{
	cardFor( slot )->badge = badge;
}

void LoadScreenBoard::setProgress( Int slot, Int percent )
{
	BoardCard *card = cardFor( slot );
	if( card == NULL || card->percent < 0 || card->percent == percent )
		return;
	card->percent = percent;
	UnicodeString words;
	words.format( u"%d%%", percent );
	card->percentText->setText( words );
}

void LoadScreenBoard::setLocalProgress( Int percent )
{
	if( percent == m_localPercent )
		return;
	m_localPercent = percent;
	UnicodeString words;
	words.format( u"%d%%", min( percent, 100 ) );
	m_percent->setText( words );
}

void LoadScreenBoard::drawWindow( GameWindow *window, WinInstanceData *instData )
{
	( (LoadScreenBoard *)window->winGetUserData() )->draw();
}

void LoadScreenBoard::drawCard( const BoardCard &card )
{
	const Int x0 = card.box.lo.x;
	const Int y0 = card.box.lo.y;
	const Int width = card.box.hi.x - x0;
	const Int height = card.box.hi.y - y0;
	TheDisplay->drawFillRect( x0, y0, width, height, BOARD_PANEL );

	// the player's colour washes in from the outer edge, under a solid stripe of it
	const Int stripe = max( 3, scaled( 6 ) );
	const Int wash = width * 9 / 20;
	for( Int strip = 0; strip < BOARD_WASH_STRIPS; strip++ )
	{
		const Int from = wash * strip / BOARD_WASH_STRIPS;
		const Int to = wash * ( strip + 1 ) / BOARD_WASH_STRIPS;
		const Int alpha = 64 * ( BOARD_WASH_STRIPS - strip ) / BOARD_WASH_STRIPS;
		const Int x = card.mirrored ? x0 + width - to : x0 + from;
		TheDisplay->drawFillRect( x, y0, to - from, height, withAlpha( card.color, alpha ) );
	}

	// the side's crest, faint, at the inner end
	if( card.watermark )
	{
		const Int size = height - scaled( 16 );
		const Int x = card.mirrored ? x0 + scaled( 24 ) : x0 + width - scaled( 24 ) - size;
		TheDisplay->drawImage( card.watermark, x, y0 + ( height - size ) / 2, x + size, y0 + ( height + size ) / 2, GameMakeColor( 255, 255, 255, 30 ) );
	}
	TheDisplay->drawFillRect( card.mirrored ? x0 + width - stripe : x0, y0, stripe, height, card.color );
	TheDisplay->drawOpenRect( x0, y0, width, height, 1.0f, BOARD_EDGE );

	// the portrait, in a frame of the player's colour
	const Int pad = scaled( 16 );
	const Int emblem = min( height - 2 * pad, scaled( 168 ) );
	const Int emblemX = card.mirrored ? x0 + width - stripe - pad - emblem : x0 + stripe + pad;
	const Int emblemY = y0 + ( height - emblem ) / 2;
	TheDisplay->drawFillRect( emblemX, emblemY, emblem, emblem, BOARD_GROUND );
	if( card.emblem )
		TheDisplay->drawImage( card.emblem, emblemX + 2, emblemY + 2, emblemX + emblem - 2, emblemY + emblem - 2 );
	else
		m_unknown->draw( emblemX + ( emblem - stringWidth( m_unknown ) ) / 2, emblemY + ( emblem - stringHeight( m_unknown ) ) / 2, BOARD_MUTED, BOARD_SHADOW );
	TheDisplay->drawOpenRect( emblemX, emblemY, emblem, emblem, 1.0f, withAlpha( card.color, 200 ) );

	// the words, on the portrait's far side, aligned away from it
	const Int textGap = scaled( 24 );
	const Int textLeft = card.mirrored ? x0 + pad : emblemX + emblem + textGap;
	const Int textRight = card.mirrored ? emblemX - textGap : x0 + width - pad;
	const Int lineGap = scaled( 4 );
	// seven cards to a column leave no room for the third line beside a bar
	Int block = stringHeight( card.name ) + lineGap + stringHeight( card.army );
	const Int barRoom = card.percent >= 0 ? scaled( 24 ) : 0;
	const Bool hasDetail = card.detail->getTextLength() > 0
		&& block + lineGap + stringHeight( card.detail ) + barRoom <= height - 2 * scaled( 8 );
	if( hasDetail )
		block += lineGap + stringHeight( card.detail );
	Int y = y0 + ( height - block ) / 2;
	if( card.percent >= 0 )
		y -= scaled( 8 );
	DisplayString *lines[ 3 ] = { card.name, card.army, card.detail };
	const Color colors[ 3 ] = { card.nameColor, BOARD_INK, BOARD_MUTED };
	for( Int line = 0; line < ( hasDetail ? 3 : 2 ); line++ )
	{
		const Int w = stringWidth( lines[ line ] );
		lines[ line ]->draw( card.mirrored ? textRight - w : textLeft, y, colors[ line ], BOARD_SHADOW );
		if( line == 0 && card.badge )
		{
			const Int size = stringHeight( card.name ) * 3 / 4;
			const Int bx = card.mirrored ? textRight - w - scaled( 10 ) - size : textLeft + w + scaled( 10 );
			TheDisplay->drawImage( card.badge, bx, y + ( stringHeight( card.name ) - size ) / 2, bx + size, y + ( stringHeight( card.name ) + size ) / 2 );
		}
		y += stringHeight( lines[ line ] ) + lineGap;
	}

	// a team chip in the inner top corner where the columns are not the teams
	if( card.teamText->getTextLength() > 0 && m_columnHead[ 0 ] == NULL && m_columnHead[ 1 ] == NULL )
	{
		const Int swatch = stringHeight( card.teamText ) - scaled( 4 );
		const Int w = stringWidth( card.teamText );
		const Int ty = y0 + scaled( 10 );
		const Int tx = card.mirrored ? x0 + scaled( 14 ) : x0 + width - scaled( 14 ) - w;
		const Int sx = card.mirrored ? tx + w + scaled( 8 ) : tx - scaled( 8 ) - swatch;
		TheDisplay->drawFillRect( sx, ty + scaled( 2 ), swatch, swatch, LobbyTeamColor( card.team ) );
		TheDisplay->drawOpenRect( sx, ty + scaled( 2 ), swatch, swatch, 1.0f, BOARD_EDGE );
		card.teamText->draw( tx, ty, BOARD_MUTED, BOARD_SHADOW );
	}

	// a network player's own load
	if( card.percent >= 0 )
	{
		const Int barHeight = max( 3, scaled( 5 ) );
		const Int barY = y0 + height - pad - barHeight;
		const Int label = stringWidth( card.percentText ) + scaled( 12 );
		const Int barLeft = card.mirrored ? textLeft + label : textLeft;
		const Int barRight = card.mirrored ? textRight : textRight - label;
		TheDisplay->drawFillRect( barLeft, barY, barRight - barLeft, barHeight, BOARD_LINE );
		const Int filled = ( barRight - barLeft ) * card.percent / 100;
		TheDisplay->drawFillRect( card.mirrored ? barRight - filled : barLeft, barY, filled, barHeight, card.color );
		const Int labelY = barY + barHeight / 2 - stringHeight( card.percentText ) / 2;
		card.percentText->draw( card.mirrored ? textLeft : textRight - stringWidth( card.percentText ), labelY,
			card.percent >= 100 ? BOARD_INK : BOARD_MUTED, BOARD_SHADOW );
	}
}

void LoadScreenBoard::draw( void )
{
	if( Clock_Milliseconds() - m_tipSince > BOARD_TIP_MILLISECONDS )
	{
		m_tipIndex = ( m_tipIndex + 1 ) % BOARD_TIP_COUNT;
		m_tipSince = Clock_Milliseconds();
		m_tip->setText( tipText( m_tipIndex ) );
	}

	const Int margin = scaled( 64 );
	const Int left = m_left + margin;
	const Int right = m_left + scaled( BOARD_WIDTH ) - margin;
	const Int headerBottom = m_top + scaled( 168 );
	const Int footTop = m_top + scaled( 952 );
	if( m_mission )
	{
		const Color band = withAlpha( BOARD_GROUND, 224 );
		TheDisplay->drawFillRect( 0, 0, m_width, headerBottom, band );
		TheDisplay->drawFillRect( 0, footTop, m_width, m_height - footTop, band );
		drawBriefing();
	}
	else
	{
		TheDisplay->drawFillRect( 0, 0, m_width, m_height, BOARD_GROUND );
		drawMatch();
	}

	// the header
	const Int titleY = m_top + scaled( 52 );
	m_title->draw( left, titleY, BOARD_INK, BOARD_SHADOW );
	m_subtitle->draw( left, titleY + stringHeight( m_title ) + scaled( 4 ), BOARD_MUTED, BOARD_SHADOW );
	const Int markWidth = stringWidth( m_wordmark[ 0 ] ) + stringWidth( m_wordmark[ 1 ] );
	const Int markY = titleY + ( stringHeight( m_title ) - stringHeight( m_wordmark[ 0 ] ) ) / 2;
	m_wordmark[ 0 ]->draw( right - markWidth, markY, BOARD_INK, BOARD_SHADOW );
	m_wordmark[ 1 ]->draw( right - stringWidth( m_wordmark[ 1 ] ), markY, BOARD_BLUE, BOARD_SHADOW );
	TheDisplay->drawFillRect( left, headerBottom, right - left, 1, BOARD_LINE );

	// the foot
	const Int barHeight = max( 4, scaled( 8 ) );
	const Int barY = m_top + scaled( 1016 );
	const Int rowY = barY - scaled( 18 ) - stringHeight( m_tip );
	TheDisplay->drawFillRect( left, footTop, right - left, 1, BOARD_LINE );
	const Int chipPad = scaled( 8 );
	const Int chipHeight = stringHeight( m_tipLabel ) + scaled( 4 );
	const Int chipY = rowY + ( stringHeight( m_tip ) - chipHeight ) / 2;
	TheDisplay->drawOpenRect( left, chipY, stringWidth( m_tipLabel ) + 2 * chipPad, chipHeight, 1.0f, BOARD_BLUE );
	m_tipLabel->draw( left + chipPad, chipY + scaled( 2 ), BOARD_BLUE, BOARD_SHADOW );
	m_tip->draw( left + stringWidth( m_tipLabel ) + 2 * chipPad + scaled( 16 ), rowY, BOARD_INK, BOARD_SHADOW );
	if( m_network && m_localPercent > 100 )
		m_waiting->draw( right - stringWidth( m_waiting ), rowY, BOARD_MUTED, BOARD_SHADOW );
	else
		m_percent->draw( right - stringWidth( m_percent ), rowY + stringHeight( m_tip ) - stringHeight( m_percent ), BOARD_INK, BOARD_SHADOW );
	TheDisplay->drawFillRect( left, barY, right - left, barHeight, BOARD_LINE );
	TheDisplay->drawFillRect( left, barY, ( right - left ) * min( m_localPercent, 100 ) / 100, barHeight, BOARD_BLUE );
}

void LoadScreenBoard::drawBriefing( void )
{
	if( m_briefing.empty() )
		return;
	const IRegion2D &box = m_briefingBox;
	const Int width = box.hi.x - box.lo.x;
	const Int height = box.hi.y - box.lo.y;
	const Int pad = scaled( 28 );
	TheDisplay->drawFillRect( box.lo.x, box.lo.y, width, height, withAlpha( BOARD_GROUND, 216 ) );
	TheDisplay->drawFillRect( box.lo.x, box.lo.y, max( 3, scaled( 6 ) ), height, BOARD_BLUE );
	TheDisplay->drawOpenRect( box.lo.x, box.lo.y, width, height, 1.0f, BOARD_EDGE );
	Int y = box.lo.y + pad;
	m_briefingHead->draw( box.lo.x + pad, y, BOARD_BLUE, BOARD_SHADOW );
	y += stringHeight( m_briefingHead ) + scaled( 8 );
	for( size_t each = 0; each < m_briefing.size(); each++ )
	{
		y += scaled( 10 );
		m_briefing[ each ]->draw( box.lo.x + pad, y, BOARD_INK, BOARD_SHADOW );
		y += stringHeight( m_briefing[ each ] );
	}
}

void LoadScreenBoard::drawMatch( void )
{
	for( Int side = 0; side < 2; side++ )
	{
		const IRegion2D &box = m_backdropBox[ side ];
		if( m_backdrop[ side ] )
			TheDisplay->drawImage( m_backdrop[ side ], box.lo.x, box.lo.y, box.hi.x, box.hi.y, GameMakeColor( 255, 255, 255, 14 ) );
	}

	for( Int side = 0; side < 2; side++ )
	{
		if( m_columnHead[ side ] == NULL )
			continue;
		const Int swatch = stringHeight( m_columnHead[ side ] ) - scaled( 4 );
		const Int sx = side == 0 ? m_columnHeadPos[ side ].x - scaled( 10 ) - swatch : m_columnHeadPos[ side ].x + stringWidth( m_columnHead[ side ] ) + scaled( 10 );
		TheDisplay->drawFillRect( sx, m_columnHeadPos[ side ].y + scaled( 2 ), swatch, swatch, m_columnTeam[ side ] );
		TheDisplay->drawOpenRect( sx, m_columnHeadPos[ side ].y + scaled( 2 ), swatch, swatch, 1.0f, BOARD_EDGE );
		m_columnHead[ side ]->draw( m_columnHeadPos[ side ].x, m_columnHeadPos[ side ].y, BOARD_MUTED, BOARD_SHADOW );
	}

	for( size_t each = 0; each < m_cards.size(); each++ )
		drawCard( m_cards[ each ] );

	// the middle
	if( m_centerLabel )
		m_centerLabel->draw( m_centerLabelPos.x, m_centerLabelPos.y, BOARD_INK, BOARD_SHADOW );
	const Int mapSize = m_mapBox.hi.x - m_mapBox.lo.x;
	TheDisplay->drawFillRect( m_mapBox.lo.x, m_mapBox.lo.y, mapSize, mapSize, BOARD_PANEL );
	if( m_mapImage )
		TheDisplay->drawImage( m_mapImage, m_mapDrawn.lo.x, m_mapDrawn.lo.y, m_mapDrawn.hi.x, m_mapDrawn.hi.y );
	TheDisplay->drawOpenRect( m_mapBox.lo.x - 1, m_mapBox.lo.y - 1, mapSize + 2, mapSize + 2, 1.0f, BOARD_EDGE );
	const Int spot = max( 8, scaled( 18 ) );
	const Int ring = max( 2, scaled( 3 ) );
	for( size_t each = 0; each < m_spots.size(); each++ )
	{
		const BoardSpot &mark = m_spots[ each ];
		TheDisplay->drawFillRect( mark.pos.x - spot / 2 - ring, mark.pos.y - spot / 2 - ring, spot + 2 * ring, spot + 2 * ring, BOARD_GROUND );
		TheDisplay->drawFillRect( mark.pos.x - spot / 2, mark.pos.y - spot / 2, spot, spot, mark.color );
	}
	if( m_observers )
		m_observers->draw( m_observersPos.x, m_observersPos.y, BOARD_MUTED, BOARD_SHADOW );
}


// SinglePlayerLoadScreen Class ///////////////////////////////////////////////
//-----------------------------------------------------------------------------
SinglePlayerLoadScreen::SinglePlayerLoadScreen( void )
{
	//Added By Sadullah Nader
	//Initialization(s) inserted
	m_currentObjectiveLine = 0;
	m_currentObjectiveLineCharacter = 0;
	m_finishedObjectiveText = NULL;
	m_currentObjectiveWidthOffset = 0;
	//
	m_progressBar = NULL;
	m_percent = NULL;
	m_videoStream = NULL;
	m_videoBuffer = NULL;
	m_objectiveWin = NULL;
	for(Int i = 0; i < MAX_OBJECTIVE_LINES; ++i)
		m_objectiveLines[i] = NULL;
	m_board = NULL;

}

SinglePlayerLoadScreen::~SinglePlayerLoadScreen( void )
{
	// the board's window is a child of the load screen's, gone with it in ~LoadScreen
	delete m_board;
	m_progressBar = NULL;
	m_percent = NULL;
	m_objectiveWin = NULL;
	for(Int i = 0; i < MAX_OBJECTIVE_LINES; ++i)
		m_objectiveLines[i] = NULL;
	if(m_videoBuffer)
		delete m_videoBuffer;
	m_videoBuffer = NULL;

	if ( m_videoStream )
		m_videoStream->close();
	m_videoStream = NULL;
	
	TheAudio->removeAudioEvent( m_ambientLoopHandle );
	m_ambientLoopHandle = NULL;
	
}

void SinglePlayerLoadScreen::moveWindows( Int frame )
{
	enum{
		STATE_BEGIN = 250,
		STATE_SHOW_LOCATION = 251,
		STATE_BEGIN_BREIFING = 255,
//		STATE_BEGIN_ANIMATING_TEXT = 250,
		STATE_SHOW_CAMEO_1 = 434,
		STATE_BEGIN_ANIMATING_TEXT = 356,
		STATE_HIDE_CAMEO_1 = 459,
		STATE_SHOW_CAMEO_2 = 464,
		STATE_HIDE_CAMEO_2 = 492,
		STATE_SHOW_CAMEO_3 = 497,
		STATE_HIDE_CAMEO_3 = 524,
//		STATE_END_ANIM_HEAD = 450,
		STATE_END_ANIMATING_TEXT = 730,
		STATE_END = 730
	};
	if(frame < STATE_BEGIN || frame > STATE_END)
		return;
	
	if( frame == STATE_BEGIN_BREIFING)
	{
		// add sound support here
		TheAudio->friend_forcePlayAudioEventRTS(&TheCampaignManager->getCurrentMission()->m_briefingVoice);
	}

	if( frame == STATE_BEGIN_ANIMATING_TEXT)
	{
		m_objectiveWin->winHide(FALSE);
		// animate the text and stuff
	}
	
	if( frame > STATE_BEGIN_ANIMATING_TEXT && frame <= STATE_END_ANIMATING_TEXT && !m_finishedObjectiveText)
	{
		if(m_currentObjectiveLineCharacter >= m_unicodeObjectiveLines[m_currentObjectiveLine].getLength() )
		{
			m_currentObjectiveLine++;
			m_currentObjectiveLineCharacter =0;
		}
		if(m_currentObjectiveLine >= MAX_OBJECTIVE_LINES || m_unicodeObjectiveLines[m_currentObjectiveLine].isEmpty())
		{
			m_finishedObjectiveText = TRUE;
		}
		else
		{
			WideChar wChar = m_unicodeObjectiveLines[m_currentObjectiveLine].getCharAt(m_currentObjectiveLineCharacter);
			UnicodeString text = GadgetStaticTextGetText(m_objectiveLines[m_currentObjectiveLine]);
			text.concat(wChar);
			GadgetStaticTextSetText(m_objectiveLines[m_currentObjectiveLine], text);

		}
		m_currentObjectiveLineCharacter++;
	}
	switch (frame) {
	
	case STATE_SHOW_LOCATION:
		m_location->winHide(FALSE);
		break;
	case STATE_SHOW_CAMEO_1:
		m_unitDesc[0]->winHide(FALSE);
		break;
	case STATE_HIDE_CAMEO_1:
		m_unitDesc[0]->winHide(TRUE);
		break;
	case STATE_SHOW_CAMEO_2:
		m_unitDesc[1]->winHide(FALSE);
		break;
	case STATE_HIDE_CAMEO_2:
		m_unitDesc[1]->winHide(TRUE);
		break;
	case STATE_SHOW_CAMEO_3:
		m_unitDesc[2]->winHide(FALSE);
		break;
	case STATE_HIDE_CAMEO_3:
		m_unitDesc[2]->winHide(TRUE);
		break;
	}

}
/*
	static Bool on = FALSE;
	static ICoord2D startPos, endPos;
	enum{
		STATE_BEGIN = 275,
		STATE_BEGIN_ANIM = 290,
		STATE_ANIM_CAMEO1 = 300,
		STATE_ANIM_CAMEO1_TRASITION_CAMEO2 = 350,
		STATE_ANIM_CAMEO2 = 400,
		STATE_ANIM_CAMEO2_TRASITION_CAMEO3 = 450,
		STATE_ANIM_CAMEO3 = 500,
		STATED_END_ANIM = 550,
		STATE_END = 800
	};
	if(frame < STATE_BEGIN)
		return;
	else if(frame == STATE_BEGIN )
	{
		m_cameoWindow1->winHide(FALSE);
		m_cameoWindow2->winHide(FALSE);
		m_cameoWindow3->winHide(FALSE);
		m_cameoFrame->winHide(FALSE);
	}
	else if( frame == STATE_ANIM_CAMEO1)
	{
		m_cameoWindow1->winEnable(TRUE);
		GadgetStaticTextSetText(m_cameoText, TheGameText->fetch(TheCampaignManager->getCurrentMission()->m_cameoImageName[0]));
		//save of positions
	}
	else if( frame == STATE_ANIM_CAMEO1_TRASITION_CAMEO2)
	{
		m_cameoWindow1->winEnable(FALSE);
		GadgetStaticTextSetText(m_cameoText, UnicodeString::TheEmptyString);
		ICoord2D tempPos;
		Int xOffset;
		m_cameoFrame->winGetPosition(&startPos.x, &startPos.y);
		m_cameoWindow1->winGetPosition(&tempPos.x, &tempPos.y);
		xOffset = tempPos.x - startPos.x;
		m_cameoWindow2->winGetPosition(&endPos.x, &endPos.y);
		endPos.x = endPos.x - xOffset;
		endPos.y = startPos.y;
	
	}
	else if( frame > STATE_ANIM_CAMEO1_TRASITION_CAMEO2 && frame < STATE_ANIM_CAMEO2)
	{
		
		//extrapolate between start and end pos
		Real percent = INT_TO_REAL((frame - STATE_ANIM_CAMEO1_TRASITION_CAMEO2)) / (STATE_ANIM_CAMEO2 - STATE_ANIM_CAMEO1_TRASITION_CAMEO2);
		m_cameoFrame->winSetPosition(startPos.x + (endPos.x - startPos.x) * percent, endPos.y);
	}
	else if( frame == STATE_ANIM_CAMEO2 )
	{
		m_cameoWindow2->winEnable(TRUE);
		m_cameoFrame->winSetPosition(endPos.x, endPos.y);
		GadgetStaticTextSetText(m_cameoText, TheGameText->fetch(TheCampaignManager->getCurrentMission()->m_cameoImageName[1]));
	}
	else if( frame == STATE_ANIM_CAMEO2_TRASITION_CAMEO3)
	{
		m_cameoWindow2->winEnable(FALSE);
		GadgetStaticTextSetText(m_cameoText, UnicodeString::TheEmptyString);
		ICoord2D tempPos;
		Int xOffset;
		m_cameoFrame->winGetPosition(&startPos.x, &startPos.y);
		m_cameoWindow2->winGetPosition(&tempPos.x, &tempPos.y);
		xOffset = tempPos.x - startPos.x;
		m_cameoWindow3->winGetPosition(&endPos.x, &endPos.y);
		endPos.x = endPos.x - xOffset;
		endPos.y = startPos.y;
	
	}
	else if( frame > STATE_ANIM_CAMEO2_TRASITION_CAMEO3 && frame < STATE_ANIM_CAMEO3)
	{
		
		//extrapolate between start and end pos
		Real percent = INT_TO_REAL((frame - STATE_ANIM_CAMEO2_TRASITION_CAMEO3)) / (STATE_ANIM_CAMEO3 - STATE_ANIM_CAMEO2_TRASITION_CAMEO3);
		m_cameoFrame->winSetPosition(startPos.x + (endPos.x - startPos.x) * percent, endPos.y);
	}
	else if( frame == STATE_ANIM_CAMEO3 )
	{
		m_cameoFrame->winSetPosition(endPos.x, endPos.y);
		m_cameoWindow3->winEnable(TRUE);
		GadgetStaticTextSetText(m_cameoText, TheGameText->fetch(TheCampaignManager->getCurrentMission()->m_cameoImageName[2]));
	}
	else if( frame ==STATED_END_ANIM)
	{
		m_cameoWindow3->winEnable(FALSE);
		GadgetStaticTextSetText(m_cameoText, UnicodeString::TheEmptyString);
		m_cameoFrame->winHide(TRUE);
		
	}
}*/
 
void SinglePlayerLoadScreen::init( GameInfo *game )
{
	//No music in SinglePlayerLoadScreen

	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( AsciiString( "Menus/SinglePlayerLoadScreen.wnd" ) );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the single player loadscreen"));
	m_loadScreen->winHide(FALSE);
	m_loadScreen->winBringToTop();
//	Mission *mission = TheCampaignManager->getCurrentMission();
	// Store the pointer to the progress bar on the loadscreen
	m_progressBar = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:ProgressLoad" ) ));
	DEBUG_ASSERTCRASH(m_progressBar, ("Can't initialize the progressbar for the single player loadscreen"));
	GadgetProgressBarSetProgress(m_progressBar, 0 );	

	m_percent = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:Percent" ) ));
	DEBUG_ASSERTCRASH(m_percent, ("Can't initialize the m_percent for the single player loadscreen"));
	GadgetStaticTextSetText(m_percent,UnicodeString(u"0%"));
	m_percent->winHide(TRUE);

	m_objectiveWin = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:ObjectivesWin" ) ));
	DEBUG_ASSERTCRASH(m_objectiveWin, ("Can't initialize the m_objectiveWin for the single player loadscreen"));
	m_objectiveWin->winHide(TRUE);

	
	Mission *mission = TheCampaignManager->getCurrentMission();
	AsciiString lineName;
	// i is used after the loop; VC6 for-scope let it escape.
	Int i;
	for(i = 0; i < MAX_OBJECTIVE_LINES; ++i)
	{
		lineName.format("SinglePlayerLoadScreen.wnd:StaticTextLine%d",i);
		m_objectiveLines[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( lineName ));
		DEBUG_ASSERTCRASH(m_objectiveLines[i], ("Can't initialize the m_objectiveLines[%d] for the single player loadscreen", i));
		GadgetStaticTextSetText(m_objectiveLines[i],UnicodeString::TheEmptyString);

		// translate the objective lines
		if(mission->m_missionObjectivesLabel[i].isNotEmpty())
			m_unicodeObjectiveLines[i] = TheGameText->fetch(mission->m_missionObjectivesLabel[i]);
	}

	for(i = 0; i < MAX_DISPLAYED_UNITS; ++i)
	{
		lineName.format("SinglePlayerLoadScreen.wnd:StaticTextCameoText%d",i);
		m_unitDesc[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( lineName ));
		DEBUG_ASSERTCRASH(m_unitDesc[i], ("Can't initialize the m_objectiveLines[%d] for the single player loadscreen", i));
		GadgetStaticTextSetText(m_unitDesc[i],TheGameText->fetch(mission->m_unitNames[i]));
		m_unitDesc[i]->winHide(TRUE);
	}
	m_location = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:StaticTextCameoText3" ) ));
	DEBUG_ASSERTCRASH(m_location, ("Can't initialize the m_objectiveWin for the single player loadscreen"));
	m_location->winHide(TRUE);
	GadgetStaticTextSetText(m_location, TheGameText->fetch(mission->m_locationNameLabel));
	

	
	m_currentObjectiveLine = 0;
	m_currentObjectiveWidthOffset = 0;
	m_currentObjectiveLineCharacter = 0;
	m_finishedObjectiveText = FALSE;
/*
	m_cameoWindow1 = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:WindowCameo1" ) ));
	DEBUG_ASSERTCRASH(m_cameoWindow1, ("Can't initialize the m_cameoWindow1 for the single player loadscreen"));
	m_cameoWindow1->winHide(TRUE);
	m_cameoWindow1->winEnable(FALSE);
	m_cameoWindow1->winSetEnabledImage(0, mission->m_cameoImage[0]);
	m_cameoWindow1->winSetDisabledImage(0, mission->m_cameoDisabledImage[0]);

	m_cameoWindow2 = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:WindowCameo2" ) ));
	DEBUG_ASSERTCRASH(m_cameoWindow2, ("Can't initialize the m_cameoWindow2 for the single player loadscreen"));
	m_cameoWindow2->winHide(TRUE);
	m_cameoWindow2->winEnable(FALSE);
	m_cameoWindow2->winSetEnabledImage(0, mission->m_cameoImage[1]);
	m_cameoWindow2->winSetDisabledImage(0, mission->m_cameoDisabledImage[1]);

	m_cameoWindow3 = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:WindowCameo3" ) ));
	DEBUG_ASSERTCRASH(m_cameoWindow3, ("Can't initialize the m_cameoWindow3 for the single player loadscreen"));
	m_cameoWindow3->winHide(TRUE);
	m_cameoWindow3->winEnable(FALSE);
	m_cameoWindow3->winSetEnabledImage(0, mission->m_cameoImage[2]);
	m_cameoWindow3->winSetDisabledImage(0, mission->m_cameoDisabledImage[2]);

	m_headMovie = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:WindowHead" ) ));
	DEBUG_ASSERTCRASH(m_headMovie, ("Can't initialize the m_headMovie for the single player loadscreen"));
	m_headMovie->winHide(TRUE);
	m_cameoFrame = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:WindowHiliteCameo" ) ));
	DEBUG_ASSERTCRASH(m_cameoFrame, ("Can't initialize the m_cameoFrame for the single player loadscreen"));
	m_cameoFrame->winHide(TRUE);
	m_cameoText = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:StaticTextCameoText" ) ));
	DEBUG_ASSERTCRASH(m_cameoText, ("Can't initialize the m_cameoText for the single player loadscreen"));

*/
	m_ambientLoop.setEventName("LoadScreenAmbient");
	// create the new stream
	m_videoStream = TheVideoPlayer->open( TheCampaignManager->getCurrentMission()->m_movieLabel );
	if ( m_videoStream == NULL )
	{
		m_percent->winHide(TRUE);
		return;
	}

	// Create the new buffer
	m_videoBuffer = TheDisplay->createVideoBuffer();
	if (	m_videoBuffer == NULL || 
				!m_videoBuffer->allocate(	m_videoStream->width(), 
													m_videoStream->height())
		)
	{
		delete m_videoBuffer;
		m_videoBuffer = NULL;

		if ( m_videoStream )
			m_videoStream->close();
		m_videoStream = NULL;

		return;
	}

	// format the progress bar: USA to blue, GLA to green, China to red
	// and set the background image
	AsciiString campaignName = TheCampaignManager->getCurrentCampaign()->m_name;
	GameWindow *backgroundWin = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:ParentSinglePlayerLoadScreen" ) ));
	if (campaignName.compareNoCase("USA") == 0)
	{
		backgroundWin->winSetEnabledImage( 0, TheMappedImageCollection->findImageByName("MissionLoad_USA") );
		m_progressBar->winSetEnabledImage( 6, TheMappedImageCollection->findImageByName("LoadingBar_ProgressCenter2") );
	}
	else if (campaignName.compareNoCase("GLA") == 0)
	{
		backgroundWin->winSetEnabledImage( 0, TheMappedImageCollection->findImageByName("MissionLoad_GLA") );
		m_progressBar->winSetEnabledImage( 6, TheMappedImageCollection->findImageByName("LoadingBar_ProgressCenter3") );
	}
	else if (campaignName.compareNoCase("China") == 0)
	{
		backgroundWin->winSetEnabledImage( 0, TheMappedImageCollection->findImageByName("MissionLoad_China") );
		m_progressBar->winSetEnabledImage( 6, TheMappedImageCollection->findImageByName("LoadingBar_ProgressCenter1") );
	} 
	// else leave the default background screen


	if(TheGameLODManager && TheGameLODManager->didMemPass())
	{
		Int progressUpdateCount = m_videoStream->frameCount() / FRAME_FUDGE_ADD;
		Int shiftedPercent = -FRAME_FUDGE_ADD + 1;
		while (m_videoStream->frameIndex() < m_videoStream->frameCount() - 1 )
		{
			TheGameEngine->serviceWindowsOS();

			if(!m_videoStream->isFrameReady())
			{
				sleepMilliseconds( 1 );	
				continue;
			}

			if (!TheGameEngine->isActive())
			{/*	//we are alt-tabbed out, so just increment the frame
				m_videoStream->frameNext();
				m_videoStream->frameDecompress();*/

				//Changing for MissionDisk, just skip to end.
				break;
			}
			
			m_videoStream->frameDecompress();
			m_videoStream->frameRender(m_videoBuffer);

// PULLED FROM THE MISSION DISK
//			moveWindows( m_videoStream->frameIndex());

			m_videoStream->frameNext();

			if(m_videoBuffer)
				m_loadScreen->winGetInstanceData()->setVideoBuffer(m_videoBuffer);
			if(m_videoStream->frameIndex() % progressUpdateCount == 0)
			{
				shiftedPercent++;
				if(shiftedPercent >0)
					shiftedPercent = 0;
				Int percent = (shiftedPercent + FRAME_FUDGE_ADD)/1.3;
				UnicodeString per;
				per.format(u"%d%%",percent);
				TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
				GadgetProgressBarSetProgress(m_progressBar, percent);
				GadgetStaticTextSetText(m_percent, per);
			
			}
			TheWindowManager->update();

			// redraw all views, update the GUI
			TheDisplay->draw();
		}

		// let the background image show through
		m_videoStream->close();
		m_videoStream = NULL;
		m_loadScreen->winGetInstanceData()->setVideoBuffer( NULL );
		TheDisplay->draw();
	}
	else
	{
		// if we're min spec'ed don't play a movie
		
		Int delay = mission->m_voiceLength * 1000;
		Int begin = Clock_Milliseconds();
		Int currTime = begin;
		Int fudgeFactor = 0;
		while(begin + delay > currTime )
		{		
			fudgeFactor = 30 * ((currTime - begin)/ INT_TO_REAL(delay ));
			GadgetProgressBarSetProgress(m_progressBar, fudgeFactor);

			TheWindowManager->update();
			TheDisplay->draw();
			sleepMilliseconds( 100 );
			currTime = Clock_Milliseconds();
		}
		

		TheWindowManager->update();
		TheDisplay->draw();

	}
	setFPMode();
	m_percent->winHide(TRUE);
	m_ambientLoopHandle = TheAudio->addAudioEvent(&m_ambientLoop);
	
}

void SinglePlayerLoadScreen::reset( void )
{
 setLoadScreen(NULL);
 m_progressBar = NULL;
}

void SinglePlayerLoadScreen::update( Int percent )
{
	// the briefing movie has the whole screen; the board comes with the load after it
	if( m_board == NULL )
	{
		// Zero Hour's missions name no place, so the number is the title there
		static const char *const difficultyLabels[ DIFFICULTY_COUNT ] = { "GUI:Easy", "GUI:Medium", "GUI:Hard" };
		const Mission *mission = TheCampaignManager->getCurrentMission();
		UnicodeString number;
		number.format( TheGameText->fetch( "GUI:LoadMission" ), TheCampaignManager->getCurrentMissionNumber() + 1 );
		UnicodeString title = number;
		UnicodeString subtitle = TheGameText->fetch( TheCampaignManager->getCurrentCampaign()->m_campaignNameLabel );
		if( !mission->m_locationNameLabel.isEmpty() )
		{
			title = TheGameText->fetch( mission->m_locationNameLabel );
			subtitle.concat( BOARD_SEPARATOR );
			subtitle.concat( number );
		}
		subtitle.concat( BOARD_SEPARATOR );
		subtitle.concat( TheGameText->fetch( difficultyLabels[ TheCampaignManager->getGameDifficulty() ] ) );
		m_board = NEW LoadScreenBoard;
		m_board->createMission( m_loadScreen, title, subtitle, m_unicodeObjectiveLines, MAX_OBJECTIVE_LINES );
		m_progressBar->winHide( TRUE );
		m_percent->winHide( TRUE );
	}
	percent = (percent + FRAME_FUDGE_ADD)/1.3;
	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
	m_board->setLocalProgress( percent );

	// Do this last!
	LoadScreen::update( percent );
}

void SinglePlayerLoadScreen::setProgressRange( Int min, Int max )
{

}

// ChallengeLoadScreen Class ///////////////////////////////////////////////
//-----------------------------------------------------------------------------
ChallengeLoadScreen::ChallengeLoadScreen( void )
{
	m_progressBar = NULL;
	m_videoStream = NULL;
	m_videoBuffer = NULL;

	m_bioNameLeft = NULL;
	m_bioAgeLeft = NULL;
	m_bioBirthplaceLeft = NULL;
	m_bioStrategyLeft = NULL;
	m_bioBigNameEntryLeft = NULL;
	m_bioNameEntryLeft = NULL;
	m_bioAgeEntryLeft = NULL;
	m_bioBirthplaceEntryLeft = NULL;
	m_bioStrategyEntryLeft = NULL;
	m_bioBigNameEntryRight = NULL;
	m_bioNameRight = NULL;
	m_bioAgeRight = NULL;
	m_bioBirthplaceRight = NULL;
	m_bioStrategyRight = NULL;
	m_bioNameEntryRight = NULL;
	m_bioAgeEntryRight = NULL;
	m_bioBirthplaceEntryRight = NULL;
	m_bioStrategyEntryRight = NULL;

	m_portraitLeft = NULL;
	m_portraitRight = NULL;
	m_portraitMovieLeft = NULL;
	m_portraitMovieRight = NULL;

//	m_overlayReticleCrosshairs = NULL;
//	m_overlayReticleCircleLineOuter = NULL;
//	m_overlayReticleCircleLineInner = NULL;
	m_overlayReticleCircleAlphaOuter = NULL;
	m_overlayReticleCircleAlphaInner = NULL;
	m_overlayVsBackdrop = NULL;
	m_overlayVs = NULL;
	m_wndVideoManager = NULL;
}
	
ChallengeLoadScreen::~ChallengeLoadScreen( void )
{
	m_progressBar = NULL;
	if(m_videoBuffer)
		delete m_videoBuffer;
	m_videoBuffer = NULL;
	if ( m_videoStream )
		m_videoStream->close();
	m_videoStream = NULL;
	
	m_bioNameLeft = NULL;
	m_bioAgeLeft = NULL;
	m_bioBirthplaceLeft = NULL;
	m_bioStrategyLeft = NULL;
	m_bioBigNameEntryLeft = NULL;
	m_bioNameEntryLeft = NULL;
	m_bioAgeEntryLeft = NULL;
	m_bioBirthplaceEntryLeft = NULL;
	m_bioStrategyEntryLeft = NULL;
	m_bioBigNameEntryRight = NULL;
	m_bioNameRight = NULL;
	m_bioAgeRight = NULL;
	m_bioBirthplaceRight = NULL;
	m_bioStrategyRight = NULL;
	m_bioNameEntryRight = NULL;
	m_bioAgeEntryRight = NULL;
	m_bioBirthplaceEntryRight = NULL;
	m_bioStrategyEntryRight = NULL;

	m_portraitLeft = NULL;
	m_portraitRight = NULL;
	m_portraitMovieLeft = NULL;
	m_portraitMovieRight = NULL;

//	m_overlayReticleCrosshairs = NULL;
//	m_overlayReticleCircleLineOuter = NULL;
//	m_overlayReticleCircleLineInner = NULL;
	m_overlayReticleCircleAlphaOuter = NULL;
	m_overlayReticleCircleAlphaInner = NULL;
	m_overlayVsBackdrop = NULL;
	m_overlayVs = NULL;

	if(m_wndVideoManager)
		delete m_wndVideoManager;
	m_wndVideoManager = NULL;

	TheAudio->removeAudioEvent( m_ambientLoopHandle );
	m_ambientLoopHandle = NULL;
}

// accepts the number of chars to advance, the window we're concerned with, the total text for final display, and the current position of the readout
// returns the updated position of the readout
Int updateTeletypeText( Int num_chars, GameWindow* window, UnicodeString full_text, Int current_text_pos )
{
	DEBUG_ASSERTCRASH(window, ("No window for teletype text update"));
	UnicodeString currentText = GadgetStaticTextGetText(window);
	WideChar wChar;
	for (Int i = 0; i < num_chars; i++)
	{
		if (current_text_pos < full_text.getLength())
		{
			wChar = full_text.getCharAt(current_text_pos);
			currentText.concat(wChar);
			current_text_pos++;
		}
	}	
	GadgetStaticTextSetText(window, currentText);
	return current_text_pos;
}

void ChallengeLoadScreen::activatePieces( Int frame, const GeneralPersona *generalPlayer, const GeneralPersona *generalOpponent )
{
	static Int textPosBigNameRight = 0;
	static Int textPosNameRight = 0;
	static Int textPosAgeRight = 0;
	static Int textPosBirthplaceRight = 0;
	static Int textPosStrategyRight = 0;
	static Int textPosBigNameLeft = 0;
	static Int textPosNameLeft = 0;
	static Int textPosAgeLeft = 0;
	static Int textPosBirthplaceLeft = 0;
	static Int textPosStrategyLeft = 0;

	AudioEventRTS eventLeftGeneral( generalPlayer->getNameSound() );
	AudioEventRTS eventVS("Taunts_GCAnnouncer12");
	AudioEventRTS eventRightGeneral( generalOpponent->getNameSound() );

	switch (frame)
	{
		case FRAME_TITLES_START:
			m_bioNameLeft->winHide(FALSE);
//			m_bioAgeLeft->winHide(FALSE);
			m_bioBirthplaceLeft->winHide(FALSE);
			m_bioStrategyLeft->winHide(FALSE);
			m_bioNameRight->winHide(FALSE);
//			m_bioAgeRight->winHide(FALSE);
			m_bioBirthplaceRight->winHide(FALSE);
			m_bioStrategyRight->winHide(FALSE);

			break;
		case FRAME_TELETYPE_START:
			// reinit the statics for each new load screen
			textPosBigNameRight = 0;
			textPosNameRight = 0;
			textPosAgeRight = 0;
			textPosBirthplaceRight = 0;
			textPosStrategyRight = 0;
			textPosBigNameLeft = 0;
			textPosNameLeft = 0;
			textPosAgeLeft = 0;
			textPosBirthplaceLeft = 0;
			textPosStrategyLeft = 0;

			m_bioBigNameEntryLeft->winHide(FALSE);
			m_bioNameEntryLeft->winHide(FALSE);
//			m_bioAgeEntryLeft->winHide(FALSE);
			m_bioBirthplaceEntryLeft->winHide(FALSE);
			m_bioStrategyEntryLeft->winHide(FALSE);
			GadgetStaticTextSetText( m_bioBigNameEntryLeft, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioNameEntryLeft, UnicodeString::TheEmptyString );
//			GadgetStaticTextSetText( m_bioAgeEntryLeft, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioBirthplaceEntryLeft, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioStrategyEntryLeft, UnicodeString::TheEmptyString );

			m_bioBigNameEntryRight->winHide(FALSE);
			m_bioNameEntryRight->winHide(FALSE);
//			m_bioAgeEntryRight->winHide(FALSE);
			m_bioBirthplaceEntryRight->winHide(FALSE);
			m_bioStrategyEntryRight->winHide(FALSE);
			GadgetStaticTextSetText( m_bioBigNameEntryRight, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioNameEntryRight, UnicodeString::TheEmptyString );
//			GadgetStaticTextSetText( m_bioAgeEntryRight, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioBirthplaceEntryRight, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioStrategyEntryRight, UnicodeString::TheEmptyString );
			break;
		case FRAME_PORTRAITS_START:
			
			m_wndVideoManager->playMovie( m_portraitMovieLeft, generalPlayer->getPortraitMovieLeftName(), WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME);
			m_wndVideoManager->playMovie( m_portraitMovieRight, generalOpponent->getPortraitMovieRightName(), WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME);
			m_portraitMovieLeft->winHide(FALSE);
			m_portraitMovieRight->winHide(FALSE);

			TheAudio->addAudioEvent( &eventLeftGeneral );
			
			break;
		case FRAME_OUTER_CIRCLE_LINE_SHOW:
//			m_overlayReticleCircleLineOuter->winHide(FALSE);
			break;
		case FRAME_INNER_CIRCLE_LINE_SHOW:
//			m_overlayReticleCircleLineInner->winHide(FALSE);
			break;
		case FRAME_OUTER_CIRCLE_ALPHA_SHOW:
			m_overlayReticleCircleAlphaOuter->winHide(FALSE);
			break;
		case FRAME_INNER_CIRCLE_ALPHA_SHOW:
			m_overlayReticleCircleAlphaInner->winHide(FALSE);
			break;
		case FRAME_OUTER_CIRCLE_LINE_HIDE:
//			m_overlayReticleCircleLineOuter->winHide(TRUE);
			break;
		case FRAME_INNER_BACKDROP_ALPHA_SHOW:
			m_overlayVsBackdrop->winHide(FALSE);
			break;
		case FRAME_INNER_CIRCLE_LINE_HIDE:
//			m_overlayReticleCircleLineInner->winHide(TRUE);
			break;
		case FRAME_VS_ANIM_START:
			// it's time to start the overlay movie
//					m_overlayVsBackdrop->winSetEnabledImage( 0, TheMappedImageCollection->findImageByFilename("))
			m_overlayVsBackdrop->winHide(FALSE);
			m_overlayVs->winHide(FALSE);
			m_wndVideoManager->playMovie( m_overlayVs, AsciiString("VSSmall"), WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME);

			// "Verses"
			TheAudio->addAudioEvent( &eventVS );

			break;
		case FRAME_RIGHT_VOICE:
			TheAudio->addAudioEvent( &eventRightGeneral );

			break;
	}

	// update the teletype readout
	if (frame > FRAME_TELETYPE_START && (frame % TELETYPE_UPDATE_FREQ) == 0)
	{
		textPosNameLeft = updateTeletypeText( 1, m_bioNameEntryLeft, TheGameText->fetch(generalPlayer->getBioName()), textPosNameLeft);
		textPosBigNameLeft = updateTeletypeText( 1, m_bioBigNameEntryLeft, TheGameText->fetch(generalPlayer->getBioName()), textPosBigNameLeft);
//		textPosAgeLeft = updateTeletypeText( 1, m_bioAgeEntryLeft, TheGameText->fetch(generalPlayer->getBioDOB()), textPosAgeLeft);
		textPosBirthplaceLeft = updateTeletypeText( 1, m_bioBirthplaceEntryLeft, TheGameText->fetch(generalPlayer->getBioRank()), textPosBirthplaceLeft);
		textPosStrategyLeft = updateTeletypeText( 1, m_bioStrategyEntryLeft, TheGameText->fetch(generalPlayer->getBioStrategy()), textPosStrategyLeft);
		
		textPosNameRight = updateTeletypeText( 1, m_bioNameEntryRight, TheGameText->fetch(generalOpponent->getBioName()), textPosNameRight);
		textPosBigNameRight = updateTeletypeText( 1, m_bioBigNameEntryRight, TheGameText->fetch(generalOpponent->getBioName()), textPosBigNameRight);
//		textPosAgeRight = updateTeletypeText( 1, m_bioAgeEntryRight, TheGameText->fetch(generalOpponent->getBioDOB()), textPosAgeRight);
		textPosBirthplaceRight = updateTeletypeText( 1, m_bioBirthplaceEntryRight, TheGameText->fetch(generalOpponent->getBioRank()), textPosBirthplaceRight);
		textPosStrategyRight = updateTeletypeText( 1, m_bioStrategyEntryRight, TheGameText->fetch(generalOpponent->getBioStrategy()), textPosStrategyRight);
	}
}
 
void ChallengeLoadScreen::activatePiecesMinSpec(const GeneralPersona *generalPlayer, const GeneralPersona *generalOpponent)
{
	m_bioNameLeft->winHide(FALSE);
//	m_bioAgeLeft->winHide(FALSE);
	m_bioBirthplaceLeft->winHide(FALSE);
	m_bioStrategyLeft->winHide(FALSE);
	m_bioNameRight->winHide(FALSE);
//	m_bioAgeRight->winHide(FALSE);
	m_bioBirthplaceRight->winHide(FALSE);
	m_bioStrategyRight->winHide(FALSE);
	m_bioBigNameEntryLeft->winHide(FALSE);
	m_bioNameEntryLeft->winHide(FALSE);
//	m_bioAgeEntryLeft->winHide(FALSE);
	m_bioBirthplaceEntryLeft->winHide(FALSE);
	m_bioStrategyEntryLeft->winHide(FALSE);
	GadgetStaticTextSetText( m_bioBigNameEntryLeft, TheGameText->fetch(generalPlayer->getBioName()) );
	GadgetStaticTextSetText( m_bioNameEntryLeft, TheGameText->fetch(generalPlayer->getBioName()) );
//	GadgetStaticTextSetText( m_bioAgeEntryLeft, TheGameText->fetch(generalPlayer->getBioDOB()) );
	GadgetStaticTextSetText( m_bioBirthplaceEntryLeft, TheGameText->fetch(generalPlayer->getBioRank()) );
	GadgetStaticTextSetText( m_bioStrategyEntryLeft, TheGameText->fetch(generalPlayer->getBioStrategy()) );
	m_bioBigNameEntryRight->winHide(FALSE);
	m_bioNameEntryRight->winHide(FALSE);
//	m_bioAgeEntryRight->winHide(FALSE);
	m_bioBirthplaceEntryRight->winHide(FALSE);
	m_bioStrategyEntryRight->winHide(FALSE);
	GadgetStaticTextSetText( m_bioBigNameEntryRight, TheGameText->fetch(generalOpponent->getBioName()) );
	GadgetStaticTextSetText( m_bioNameEntryRight, TheGameText->fetch(generalOpponent->getBioName()) );
//	GadgetStaticTextSetText( m_bioAgeEntryRight, TheGameText->fetch(generalOpponent->getBioDOB()) );
	GadgetStaticTextSetText( m_bioBirthplaceEntryRight, TheGameText->fetch(generalOpponent->getBioRank()) );
	GadgetStaticTextSetText( m_bioStrategyEntryRight, TheGameText->fetch(generalOpponent->getBioStrategy()) );
	m_portraitLeft->winSetEnabledImage(0, generalPlayer->getBioPortraitLarge() );
	m_portraitRight->winSetEnabledImage(0, generalOpponent->getBioPortraitLarge() );
	m_portraitLeft->winHide(FALSE);
	m_portraitRight->winHide(FALSE);
	m_overlayReticleCircleAlphaOuter->winHide(FALSE);
	m_overlayReticleCircleAlphaInner->winHide(FALSE);
	m_overlayVsBackdrop->winHide(FALSE);
	m_overlayVsBackdrop->winHide(FALSE);
	m_overlayVs->winHide(FALSE);
	m_wndVideoManager->playMovie( m_overlayVs, AsciiString("VSSmall"), WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME);
}


void ChallengeLoadScreen::init( GameInfo *game )
{
	const Campaign *campaign = TheCampaignManager->getCurrentCampaign();
	const Mission *mission = TheCampaignManager->getCurrentMission();

	// the player general is tied to the campaign
	const GeneralPersona* generalPlayer = TheChallengeGenerals->getPlayerGeneralByCampaignName( campaign->m_name );

	// the opponent general is tied to the mission
	DEBUG_ASSERTCRASH(mission->m_generalName.isNotEmpty(), ("No GeneralName associated with this mission, check Campaign.ini"));
	const GeneralPersona* generalOpponent = TheChallengeGenerals->getGeneralByGeneralName( mission->m_generalName );

	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( AsciiString( "Menus/ChallengeLoadScreen.wnd" ) );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the single player loadscreen"));
	m_loadScreen->winHide(FALSE);
	m_loadScreen->winBringToTop();

	// Store the pointer to the progress bar on the loadscreen
	m_progressBar = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "ChallengeLoadScreen.wnd:ProgressLoad" ) ));
	DEBUG_ASSERTCRASH(m_progressBar, ("Can't initialize the progressbar for the single player loadscreen"));
	GadgetProgressBarSetProgress(m_progressBar, 0 );	

	m_ambientLoop.setEventName("LoadScreenAmbient");

	// create the new background video stream
	m_videoStream = TheVideoPlayer->open( TheCampaignManager->getCurrentMission()->m_movieLabel );
	// as SinglePlayerLoadScreen::init does: a movie that does not open (none headless, or a missing file) has
	// no stream, and the buffer below read its size through NULL
	if ( m_videoStream == NULL )
		return;

	// Create the new buffer
	m_videoBuffer = TheDisplay->createVideoBuffer();
	if (m_videoBuffer == NULL || !m_videoBuffer->allocate(	m_videoStream->width(), m_videoStream->height() ))
	{
		delete m_videoBuffer;
		m_videoBuffer = NULL;

		if ( m_videoStream )
			m_videoStream->close();
		m_videoStream = NULL;

		return;
	}

	// init overlays
	NameKeyType namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:PortraitLeft"));
	m_portraitLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:PortraitRight"));
	m_portraitRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );

	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:PortraitMovieLeft"));
	m_portraitMovieLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:PortraitMovieRight"));
	m_portraitMovieRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );

//	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:ReticleCrosshairs"));
//	m_overlayReticleCrosshairs = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
/*
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:OuterCircleLine"));
	m_overlayReticleCircleLineOuter = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:InnerCircleLine"));
	m_overlayReticleCircleLineInner = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
*/
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:CircleAlphaOuter"));
	m_overlayReticleCircleAlphaOuter = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:CircleAlphaInner"));
	m_overlayReticleCircleAlphaInner = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:VersusBackdrop"));
	m_overlayVsBackdrop = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:OverlayVs"));
	m_overlayVs = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );

	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioNameLeft"));
	m_bioNameLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
//	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioDOBLeft"));
//	m_bioAgeLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioBirthplaceLeft"));
	m_bioBirthplaceLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioStrategyLeft"));
	m_bioStrategyLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BigNameEntryLeft"));
	m_bioBigNameEntryLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioNameEntryLeft"));
	m_bioNameEntryLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
//	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioDOBEntryLeft"));
//	m_bioAgeEntryLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioBirthplaceEntryLeft"));
	m_bioBirthplaceEntryLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioStrategyEntryLeft"));
	m_bioStrategyEntryLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioNameRight"));
	m_bioNameRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
//	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioDOBRight"));
//	m_bioAgeRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioBirthplaceRight"));
	m_bioBirthplaceRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioStrategyRight"));
	m_bioStrategyRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BigNameEntryRight"));
	m_bioBigNameEntryRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioNameEntryRight"));
	m_bioNameEntryRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
//	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioDOBEntryRight"));
//	m_bioAgeEntryRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioBirthplaceEntryRight"));
	m_bioBirthplaceEntryRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioStrategyEntryRight"));
	m_bioStrategyEntryRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );


	// make sure reticle stuff starts out hidden
//	m_overlayReticleCircleLineOuter->winHide(TRUE);
//	m_overlayReticleCircleLineInner->winHide(TRUE);
	m_overlayReticleCircleAlphaOuter->winHide(TRUE);
	m_overlayReticleCircleAlphaInner->winHide(TRUE);
	m_overlayVsBackdrop->winHide(TRUE);
	m_overlayVs->winHide(TRUE);

	m_wndVideoManager = NEW WindowVideoManager;
	m_wndVideoManager->init();

	if(TheGameLODManager && TheGameLODManager->didMemPass())
	{
		Int progressUpdateCount = m_videoStream->frameCount() / FRAME_FUDGE_ADD;
		Int shiftedPercent = -FRAME_FUDGE_ADD + 1;
		while (m_videoStream->frameIndex() < m_videoStream->frameCount() - 1 )
		{
			TheGameEngine->serviceWindowsOS();

			if(!m_videoStream->isFrameReady())
			{
				sleepMilliseconds( 1 );	
				continue;
			}

			if (!TheGameEngine->isActive())
			{	//we are alt-tabbed out, so just increment the frame
				m_videoStream->frameNext();
				m_videoStream->frameDecompress();
				continue;
			}
			
			m_videoStream->frameDecompress();
			m_videoStream->frameRender(m_videoBuffer);
			m_videoStream->frameNext();

			if(m_videoBuffer)
				m_loadScreen->winGetInstanceData()->setVideoBuffer(m_videoBuffer);

			Int frame = m_videoStream->frameIndex();
			if(frame % progressUpdateCount == 0)
			{
				shiftedPercent++;
				if(shiftedPercent >0)
					shiftedPercent = 0;
				Int percent = (shiftedPercent + FRAME_FUDGE_ADD)/1.3;
				UnicodeString per;
				per.format(u"%d%%",percent);
				TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
				GadgetProgressBarSetProgress(m_progressBar, percent);
			}
			TheWindowManager->update();

			activatePieces(frame, generalPlayer, generalOpponent);
			m_wndVideoManager->update();

			// redraw all views, update the GUI
			TheDisplay->draw();

			TheAudio->update();
		}
	}
	else
	{
		// if we're min speced
		m_videoStream->frameGoto(m_videoStream->frameCount()); // zero based
		while(!m_videoStream->isFrameReady())
			sleepMilliseconds( 1 );
		m_videoStream->frameDecompress();
		m_videoStream->frameRender(m_videoBuffer);
		if(m_videoBuffer)
			m_loadScreen->winGetInstanceData()->setVideoBuffer(m_videoBuffer);

		activatePiecesMinSpec(generalPlayer, generalOpponent);

		Int delay = mission->m_voiceLength * 1000;
		Int begin = Clock_Milliseconds();
		Int currTime = begin;
		Int fudgeFactor = 0;
		while(begin + delay > currTime )
		{		
			fudgeFactor = 30 * ((currTime - begin)/ INT_TO_REAL(delay ));
			GadgetProgressBarSetProgress(m_progressBar, fudgeFactor);

			TheWindowManager->update();
			TheDisplay->draw();
			sleepMilliseconds( 100 );
			currTime = Clock_Milliseconds();
		}
		
		m_wndVideoManager->update();
		TheWindowManager->update();
		TheDisplay->draw();
	}
	setFPMode();

	
	AudioEventRTS event( generalOpponent->getRandomTauntSound() );
	TheAudio->addAudioEvent( &event );

	m_ambientLoopHandle = TheAudio->addAudioEvent(&m_ambientLoop);
	TheAudio->update();
}

void ChallengeLoadScreen::reset( void )
{
 setLoadScreen(NULL);
 m_progressBar = NULL;
}

void ChallengeLoadScreen::update( Int percent )
{
	percent = (percent + FRAME_FUDGE_ADD)/1.3;
	UnicodeString per;
	per.format(u"%d%%",percent);
	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
	GadgetProgressBarSetProgress(m_progressBar, percent);

	// Do this last!
	LoadScreen::update( percent );
}

void ChallengeLoadScreen::setProgressRange( Int min, Int max )
{

}

// ShellGameLoadScreen Class //////////////////////////////////////////////////
//-----------------------------------------------------------------------------
ShellGameLoadScreen::ShellGameLoadScreen( void )
{
	m_progressBar = NULL;
	m_board = NULL;
}

ShellGameLoadScreen::~ShellGameLoadScreen( void )
{

	m_progressBar = NULL;
	delete m_board;
}

void ShellGameLoadScreen::init( GameInfo *game )
{
	static Bool firstLoad = TRUE;

	// a replay loads on the board a played match loads on, its players as they really were
	if( game && TheGameLogic->getGameMode() == GAME_REPLAY )
	{
		m_board = NEW LoadScreenBoard;
		m_loadScreen = m_board->create( game, "GUI:LoadModeReplay", FALSE );
		return;
	}

	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( AsciiString( "Menus/ShellGameLoadScreen.wnd" ) );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the ShellGame loadscreen"));
	m_loadScreen->winHide(FALSE);
	m_loadScreen->winBringToTop();

	// Store the pointer to the progress bar on the loadscreen
	m_progressBar = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "ShellGameLoadScreen.wnd:ProgressLoad" ) ));
	DEBUG_ASSERTCRASH(m_progressBar, ("Can't initialize the progressbar for the single player loadscreen"));
	GadgetProgressBarSetProgress(m_progressBar, 0 );	
	m_progressBar->winHide(TRUE);
	
	if(m_loadScreen && firstLoad && TheGameLODManager && TheGameLODManager->didMemPass())
	{
		m_loadScreen->winSetEnabledImage(0, TheMappedImageCollection->findImageByName("TitleScreen"));
		TheWritableGlobalData->m_breakTheMovie = FALSE;

//		m_videoStream = TheVideoPlayer->open( "Sizzle" );
//		if ( m_videoStream == NULL )
//		{
//			m_progressBar->winHide(FALSE);
//			return;
//		}
//
//		// Create the new buffer
//		m_videoBuffer = TheDisplay->createVideoBuffer();
//		if (	m_videoBuffer == NULL || 
//					!m_videoBuffer->allocate(	m_videoStream->width(), 
//														m_videoStream->height())
//			)
//		{
//			delete m_videoBuffer;
//			m_videoBuffer = NULL;
//
//			if ( m_videoStream )
//				m_videoStream->close();
//			m_videoStream = NULL;
//
//			return;
//		}
//		TheGlobalData->m_isBreakableMovie = TRUE;
//		TheGlobalData->m_breakTheMovie = FALSE;
//		while (m_videoStream->frameIndex() < m_videoStream->frameCount() - 1 )
//		{
//			if(TheGlobalData->m_breakTheMovie)
//			{
//				TheGlobalData->m_breakTheMovie = FALSE;
//				m_videoStream->frameGoto(m_videoStream->frameCount() - 1);
//			}
//			if(m_videoStream->frameIndex() < m_videoStream->frameCount() - 1)
//			{
//				if(!m_videoStream->isFrameReady())
//					continue;
//
//				m_videoStream->frameDecompress();
//				m_videoStream->frameRender(m_videoBuffer);
//				m_videoStream->frameNext();
//				if(m_videoBuffer)
//					m_loadScreen->winGetInstanceData()->setVideoBuffer(m_videoBuffer);
//			}
//
//			TheWindowManager->update();
//		//	TheShell->update();
//			//TheDisplay->update();
//			// redraw all views, update the GUI
//			TheDisplay->draw();
//		}
//		TheGlobalData->m_isBreakableMovie = FALSE;
//		TheGlobalData->m_breakTheMovie = FALSE;
//		GameWindow *win = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "ShellGameLoadScreen.wnd:EAGamesLogo" ) ));
//		if(win)
//			win->winHide(FALSE);

		GameWindow *win = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "ShellGameLoadScreen.wnd:StaticTextLegal" ) ));
		if(win)
			win->winHide(FALSE);
		firstLoad = FALSE;

		UnsignedInt showTime = Clock_Milliseconds();
		while(showTime + 3000 > Clock_Milliseconds())
		{	
			LoadScreen::update(0);
			sleepMilliseconds( 100 );
		}

	}
	m_progressBar->winHide(FALSE);	
}

void ShellGameLoadScreen::reset( void )
{
 setLoadScreen(NULL);
 m_progressBar = NULL;
}

void ShellGameLoadScreen::update( Int percent )
{
	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
	if( m_board )
		m_board->setLocalProgress( percent );
	else
		GadgetProgressBarSetProgress(m_progressBar, percent);

	// Do this last!
	LoadScreen::update( percent );
}

// MultiPlayerLoadScreen Class //////////////////////////////////////////////////
//-----------------------------------------------------------------------------
MultiPlayerLoadScreen::MultiPlayerLoadScreen( void )
{
	m_board = NULL;
}

MultiPlayerLoadScreen::~MultiPlayerLoadScreen( void )
{
	delete m_board;

	TheAudio->removeAudioEvent( AHSV_StopTheMusicFade );
//	TheAudio->stopAudio( AudioAffect_Music );
}

void MultiPlayerLoadScreen::init( GameInfo *game )
{
	// a skirmish has one machine loading and one bar; a LAN game gives every human his own
	const Bool network = TheGameLogic->getGameMode() == GAME_LAN;
	m_board = NEW LoadScreenBoard;
	m_loadScreen = m_board->create( game, network ? "GUI:LoadModeLAN" : "GUI:LoadModeSkirmish", network );

	// getLocalSlotNum() is -1 when no slot in the list is the local player - a game watched from
	// the observer camera rather than played - and getSlot(-1) hands back NULL.  Same guard the
	// score screen already has.
	GameSlot *lSlot = game->getSlot(game->getLocalSlotNum());
	const PlayerTemplate* pt;
	if (lSlot && lSlot->getPlayerTemplate() >= 0)
		pt = ThePlayerTemplateStore->getNthPlayerTemplate(lSlot->getPlayerTemplate());
	else
		pt = ThePlayerTemplateStore->findPlayerTemplate( TheNameKeyGenerator->nameToKey("FactionObserver") );

	AsciiString musicName = pt->getLoadScreenMusic();
	if ( ! musicName.isEmpty() )
	{
		TheAudio->removeAudioEvent( AHSV_StopTheMusicFade );
		AudioEventRTS event( musicName );
		event.setShouldFade( TRUE );

		TheAudio->addAudioEvent( &event );
		TheAudio->update();//Since GameEngine::update() is suspended until after I am gone... 

	}

	TheGameLogic->initTimeOutValues();
}

void MultiPlayerLoadScreen::reset( void )
{
	setLoadScreen(NULL);
}

void MultiPlayerLoadScreen::update( Int percent )
{
	m_board->setLocalProgress( percent );
	if (TheNetwork)
	{
		if(percent <= 100)
			TheNetwork->updateLoadProgress( percent );
		TheNetwork->liteupdate();
	}
	else
	{
		if (percent <= 100)
			TheGameLogic->processProgress( TheGameInfo->getLocalSlotNum(), percent );
	}

	//GadgetProgressBarSetProgress(m_progressBars[TheNetwork->getLocalPlayerID()], percent );	

	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);

	// Do this last!
	LoadScreen::update( percent );
}

void MultiPlayerLoadScreen::processProgress(Int playerId, Int percentage)
{
	
	// The test named every way playerId can be wrong and then indexed m_playerLookup with
	// it anyway; in Release the assert is not compiled in at all, so a -1 read past the array.
	if( percentage < 0 || percentage > 100 || playerId >= MAX_SLOTS || playerId < 0 )
	{
		DEBUG_ASSERTCRASH(FALSE, ("Percentage %d was passed in for Player %d\n", percentage, playerId));
		return;
	}
	m_board->setProgress( playerId, percentage );
}

// GameSpyLoadScreen Class //////////////////////////////////////////////////
//-----------------------------------------------------------------------------
GameSpyLoadScreen::GameSpyLoadScreen( void )
{
	m_board = NULL;
}

GameSpyLoadScreen::~GameSpyLoadScreen( void )
{
	delete m_board;
}

void GameSpyLoadScreen::init( GameInfo *game )
{
	m_board = NEW LoadScreenBoard;
	m_loadScreen = m_board->create( game, "GUI:LoadModeOnline", TRUE );

	// a player's rank beside his name, from the stats the lobby fetched
	for( Int i = 0; i < MAX_SLOTS; ++i )
	{
		GameSpyGameSlot *slot = (GameSpyGameSlot *)game->getSlot(i);
		if( !slot || !slot->isHuman() || slot->getOriginalPlayerTemplate() == PLAYERTEMPLATE_OBSERVER )
			continue;
		PSPlayerStats stats = TheGameSpyPSMessageQueue->findPlayerStatsByID(slot->getProfileID());
		m_board->setBadge( i, LookupSmallRankImage( GetFavoriteSide(stats), CalculateRank(stats) ) );
	}

	TheGameLogic->initTimeOutValues();
}

void GameSpyLoadScreen::reset( void )
{
	setLoadScreen(NULL);
}

void GameSpyLoadScreen::update( Int percent )
{
	m_board->setLocalProgress( percent );
	if(percent <= 100)
		TheNetwork->updateLoadProgress( percent );
	TheNetwork->liteupdate();

	//GadgetProgressBarSetProgress(m_progressBars[TheNetwork->getLocalPlayerID()], percent );	

	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);

	// Do this last!
	LoadScreen::update( percent );
}

void GameSpyLoadScreen::processProgress(Int playerId, Int percentage)
{
	
	// The test named every way playerId can be wrong and then indexed m_playerLookup with
	// it anyway; in Release the assert is not compiled in at all, so a -1 read past the array.
	if( percentage < 0 || percentage > 100 || playerId >= MAX_SLOTS || playerId < 0 )
	{
		DEBUG_ASSERTCRASH(FALSE, ("Percentage %d was passed in for Player %d\n", percentage, playerId));
		return;
	}
	m_board->setProgress( playerId, percentage );
}

// MapTransferLoadScreen Class //////////////////////////////////////////////////
//-----------------------------------------------------------------------------
MapTransferLoadScreen::MapTransferLoadScreen( void )
{
	//Added By Sadullah Nader
	//Initializations missing and needed
	m_oldTimeout = 0;
	//
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i] = NULL;
		m_playerNames[i] = NULL;
		m_progressText[i]= NULL;
		m_playerLookup[i] = -1;
		m_oldProgress[i] = -1;
	}
	m_fileNameText = NULL;
	m_timeoutText = NULL;
}
	
MapTransferLoadScreen::~MapTransferLoadScreen( void )
{
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i] = NULL;
		m_playerNames[i] = NULL;
		m_progressText[i]= NULL;
		m_playerLookup[i] = -1;
		m_oldProgress[i] = -1;
	}
	m_fileNameText = NULL;
	m_timeoutText = NULL;
}

void MapTransferLoadScreen::init( GameInfo *game )
{
	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( AsciiString( "Menus/MapTransferScreen.wnd" ) );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the map transfer loadscreen"));
	if (!m_loadScreen)
		return;

	m_loadScreen->winHide(FALSE);
	m_loadScreen->winBringToTop();

	DEBUG_ASSERTCRASH(TheNetwork, ("Where the Heck is the Network?!!!!"));
	DEBUG_LOG(("NumPlayers %d\n", TheNetwork->getNumPlayers()));

	AsciiString winName;
	Int i;

	// Load the Filename Text
	winName.format( "MapTransferScreen.wnd:StaticTextCurrentFile");
	m_fileNameText = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
	DEBUG_ASSERTCRASH(m_fileNameText, ("Can't initialize the filename for the map transfer loadscreen"));

	// Load the Timeout Text
	winName.format( "MapTransferScreen.wnd:StaticTextTimeout");
	m_timeoutText = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
	DEBUG_ASSERTCRASH(m_timeoutText, ("Can't initialize the timeout for the map transfer loadscreen"));

	Int netSlot = 0;
	// Loop through and make the loadscreen look all good.
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		// Load the Progress Bar
		winName.format( "MapTransferScreen.wnd:ProgressLoad%d",i);
		m_progressBars[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_progressBars[i], ("Can't initialize the progressbars for the map transfer loadscreen"));
		// set the progressbar to zero
		GadgetProgressBarSetProgress(m_progressBars[i], 0 );

		// Load the Player's name
		winName.format( "MapTransferScreen.wnd:StaticTextPlayer%d",i);
		m_playerNames[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerNames[i], ("Can't initialize the Names for the map transfer loadscreen"));

		// Load the Progress Text
		winName.format( "MapTransferScreen.wnd:StaticTextProgress%d",i);
		m_progressText[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_progressText[i], ("Can't initialize the progress text for the map transfer loadscreen"));

		// get the slot man!
		GameSlot *slot = game->getSlot(i);
		if (!slot || !slot->isHuman())
			continue;
		Color houseColor = TheMultiplayerSettings->getColor(slot->getApparentColor())->getColor();
		GadgetProgressBarSetEnabledBarColor(m_progressBars[netSlot], houseColor );

		UnicodeString name = slot->getName();
		GadgetStaticTextSetText(m_playerNames[netSlot], name );
		m_playerNames[netSlot]->winSetEnabledTextColors(houseColor, m_playerNames[netSlot]->winGetEnabledTextBorderColor());

		GadgetStaticTextSetText(m_progressText[netSlot], UnicodeString::TheEmptyString );
		m_progressText[netSlot]->winSetEnabledTextColors(houseColor, m_progressText[netSlot]->winGetEnabledTextBorderColor());

		if ((i == 0 || (TheGameInfo->getConstSlot(i)->isHuman() && TheGameInfo->getConstSlot(i)->hasMap())) && m_progressBars[netSlot])
			m_progressBars[netSlot]->winHide(TRUE);

		m_playerLookup[i] = netSlot; // save our mapping so we can update progress correctly

		netSlot++;
	}
	
	for(i = netSlot; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i]->winHide(TRUE);
		m_playerNames[i]->winHide(TRUE);
		m_progressText[i]->winHide(TRUE);
	}
}

void MapTransferLoadScreen::reset( void )
{
	setLoadScreen(NULL);
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i] = NULL;
		m_playerNames[i] = NULL;
		m_progressText[i]= NULL;
		m_playerLookup[i] = -1;
		m_oldProgress[i] = -1;
	}
	m_fileNameText = NULL;
	m_timeoutText = NULL;
}

void MapTransferLoadScreen::update( Int percent )
{
	if (TheNetwork)
	{
		TheNetwork->liteupdate();
	}

	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);

	// Do this last!
	LoadScreen::update( percent );
}

void MapTransferLoadScreen::processProgress(Int playerId, Int percentage, AsciiString stateStr)
{
	
	// The test named every way playerId can be wrong and then indexed m_playerLookup with
	// it anyway; in Release the assert is not compiled in at all, so a -1 read past the array.
	if( percentage < 0 || percentage > 100 || playerId >= MAX_SLOTS || playerId < 0 )
	{
		DEBUG_ASSERTCRASH(FALSE, ("Percentage %d was passed in for Player %d\n", percentage, playerId));
		return;
	}
	if( m_playerLookup[playerId] == -1)
		return;

	if (m_oldProgress[playerId] == percentage)
		return;
	m_oldProgress[playerId] = percentage;

	Int translatedSlot = m_playerLookup[playerId];
	if(m_progressBars[translatedSlot])
		GadgetProgressBarSetProgress(m_progressBars[translatedSlot], percentage );	
	if (m_progressText[translatedSlot])
		GadgetStaticTextSetText(m_progressText[translatedSlot], TheGameText->fetch(stateStr));
}

void MapTransferLoadScreen::processTimeout(Int secondsLeft)
{
	if (m_oldTimeout == secondsLeft)
		return;
	m_oldTimeout = secondsLeft;

	if (m_timeoutText)
	{
		UnicodeString txt;
		txt.format(TheGameText->fetch("MapTransfer:Timeout"), (secondsLeft/60), (secondsLeft%60));
		GadgetStaticTextSetText(m_timeoutText, txt);
	}
}

void MapTransferLoadScreen::setCurrentFilename(AsciiString filename)
{
	if (m_fileNameText)
	{
		UnicodeString txt;
		txt.translate(TheGameState->getMapLeafName(filename));
		txt.format(TheGameText->fetch("MapTransfer:CurrentFile"), txt.str());
		GadgetStaticTextSetText(m_fileNameText, txt);
	}
}


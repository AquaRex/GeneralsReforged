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

// FILE: ScoreScreenGraphs.cpp ////////////////////////////////////////////////////////////////////
//
// The score screen's whole content after any match, a mission or a replay: a short table of every
// player's numbers (a campaign adds its allied and enemy sides as rows of their own), and below it
// one chart at a time (money earned, army, buildings) or the build order.  A chart carries one row
// of small markers where a superweapon went up or fired, a general's power was used or a general was
// promoted; markers that fall close together share one picture and list all of theirs on hover.  The
// build order shows the first of each structure and unit until asked for every build.  Everything is
// drawn here from TheMatchHistory and the score keepers; the panel is made in code over the layout,
// whose own score table ScoreScreen.cpp hides, so no layout file changes.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/MatchHistory.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/ScoreKeeper.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "GameClient/Mouse.h"
#include "GameLogic/VictoryConditions.h"

#include <algorithm>
#include <vector>

enum GraphTab
{
	GRAPH_TAB_EARNED,
	GRAPH_TAB_ARMY,
	GRAPH_TAB_BUILDINGS,
	GRAPH_TAB_BUILD_ORDER,
	GRAPH_TAB_COUNT
};

static const char *const theTabLabels[ GRAPH_TAB_COUNT ] =
{
	"GUI:GraphEarned", "GUI:GraphArmy", "GUI:GraphBuildings", "GUI:GraphBuildOrder"
};

static const MatchSeries theTabSeries[ GRAPH_TAB_BUILD_ORDER ] =
{
	MATCH_SERIES_EARNED, MATCH_SERIES_ARMY, MATCH_SERIES_BUILDINGS
};

/// the summary's columns, in the old score table's order, under three group headings
enum SummaryColumn
{
	SUMMARY_UNITS_BUILT,
	SUMMARY_UNITS_LOST,
	SUMMARY_UNITS_DESTROYED,
	SUMMARY_BUILDINGS_BUILT,
	SUMMARY_BUILDINGS_LOST,
	SUMMARY_BUILDINGS_DESTROYED,
	SUMMARY_EARNED,
	SUMMARY_COLUMN_COUNT
};

static const char *const theColumnLabels[ SUMMARY_COLUMN_COUNT ] =
{
	"GUI:SummaryUnitsBuilt", "GUI:SummaryLost", "GUI:SummaryDestroyed",
	"GUI:SummaryBuildingsBuilt", "GUI:SummaryLost", "GUI:SummaryDestroyed",
	"GUI:SummaryEarned"
};

enum { SUMMARY_GROUP_COUNT = 3 };

static const char *const theGroupLabels[ SUMMARY_GROUP_COUNT ] = { "GUI:SummaryUnits", "GUI:GraphBuildings", "GUI:SummaryMoney" };

/// the group each column sits under
static const Int theColumnGroup[ SUMMARY_COLUMN_COUNT ] = { 0, 0, 0, 1, 1, 1, 2 };

static const char *const theEventLabels[ MATCH_EVENT_TYPE_COUNT ] =
{
	"GUI:GraphSuperweaponBuilt", "GUI:GraphSuperweaponFired", "GUI:GraphGeneralPower", "GUI:GraphPromotion"
};

/// which event's picture a group of markers shows, lowest first
static const Int theEventRank[ MATCH_EVENT_TYPE_COUNT ] = { 1, 0, 2, 3 };

/// the first five minutes take this share of the build order's width once the match ran past them
static const Real EARLY_SHARE = 0.55f;
static const UnsignedInt EARLY_FRAMES = 5 * 60 * LOGICFRAMES_PER_SECOND;

static const Color PANEL_FILL = GameMakeColor( 8, 12, 20, 255 );
static const Color PANEL_EDGE = GameMakeColor( 110, 130, 160, 255 );
static const Color TAB_FILL = GameMakeColor( 24, 32, 46, 255 );
static const Color TEXT = GameMakeColor( 220, 225, 235, 255 );
static const Color TEXT_DIM = GameMakeColor( 150, 160, 175, 255 );
static const Color GRID = GameMakeColor( 255, 255, 255, 22 );
static const Color AXIS = GameMakeColor( 255, 255, 255, 70 );
static const Color GOLD = GameMakeColor( 235, 190, 60, 255 );
static const Color SHADOW = GameMakeColor( 0, 0, 0, 255 );

struct GraphPlayer
{
	Int index;		///< the player's index, or -1 for a campaign's side, which only has a summary row
	UnicodeString name;
	UnicodeString side;
	Color color;
	Bool winner;
	Int stats[ SUMMARY_COLUMN_COUNT ];
};

struct GraphText
{
	DisplayString *text;
	Int x;
	Int y;
	Color color;
};

struct GraphTipLine
{
	UnicodeString text;
	Color color;
};

struct GraphDrop
{
	Int x;
	Color color;
};

/** Something with a picture and a tooltip: a group of event markers on a chart, a build in the build
	* order, or a plain coloured box. */
struct GraphMark
{
	IRegion2D box;
	const Image *image;
	Color edge;
	Color fill;
	Int rank;							///< theEventRank of the event whose picture it shows
	DisplayString *label;	///< the rank number on a promotion
	DisplayString *count;	///< how many events share the marker, when more than one
	std::vector< GraphTipLine > tip;
	std::vector< GraphDrop > drops;	///< a chart marker's lines down to the plot, drawn on hover
	Int dropBottom;
};

struct GraphLine
{
	Color color;
	std::vector< ICoord2D > points;
};

static GameWindow *s_panel = NULL;
static GameFont *s_font = NULL;
static GameFont *s_smallFont = NULL;
static GameFont *s_tinyFont = NULL;
static Int s_tab = GRAPH_TAB_EARNED;
static Bool s_everyBuild = FALSE;
static std::vector< GraphPlayer > s_players;
static std::vector< GraphText > s_texts;
static std::vector< GraphMark > s_marks;
static std::vector< GraphLine > s_lines;
static std::vector< IRegion2D > s_rules;		///< grid and axis lines, lo to hi
static std::vector< Color > s_ruleColors;
static IRegion2D s_tabBox[ GRAPH_TAB_COUNT ];
static DisplayString *s_tabText[ GRAPH_TAB_COUNT ];
static IRegion2D s_toggleBox;
static DisplayString *s_toggleText = NULL;
static std::vector< DisplayString * > s_tipText;
static Int s_tipMark = -1;

//-------------------------------------------------------------------------------------------------
static UnicodeString clockText( UnsignedInt frame )
{
	const UnsignedInt seconds = frame / LOGICFRAMES_PER_SECOND;
	UnicodeString text;
	text.format( u"%d:%02d", seconds / 60, seconds % 60 );
	return text;
}

static UnicodeString valueText( Int value )
{
	UnicodeString text;
	if( value >= 10000 )
		text.format( u"%dk", value / 1000 );
	else
		text.format( u"%d", value );
	return text;
}

static DisplayString *newText( const UnicodeString &text, GameFont *font )
{
	DisplayString *string = TheDisplayStringManager->newDisplayString();
	string->setFont( font );
	string->setText( text );
	return string;
}

static Int textWidth( DisplayString *string )
{
	Int width, height;
	string->getSize( &width, &height );
	return width;
}

/** align: 0 puts x at the text's left, 1 at its middle, 2 at its right.  Returns the text's width. */
static Int addText( const UnicodeString &text, GameFont *font, Int x, Int y, Color color, Int align = 0 )
{
	GraphText entry;
	entry.text = newText( text, font );
	const Int width = textWidth( entry.text );
	entry.x = x - width * align / 2;
	entry.y = y;
	entry.color = color;
	s_texts.push_back( entry );
	return width;
}

static void addRule( Int x0, Int y0, Int x1, Int y1, Color color )
{
	IRegion2D rule;
	rule.lo.x = x0;
	rule.lo.y = y0;
	rule.hi.x = x1;
	rule.hi.y = y1;
	s_rules.push_back( rule );
	s_ruleColors.push_back( color );
}

static GraphMark newMark( Int x, Int y, Int width, Int height, Color fill, Color edge )
{
	GraphMark mark;
	mark.box.lo.x = x;
	mark.box.lo.y = y;
	mark.box.hi.x = x + width;
	mark.box.hi.y = y + height;
	mark.image = NULL;
	mark.edge = edge;
	mark.fill = fill;
	mark.rank = MATCH_EVENT_TYPE_COUNT;
	mark.label = NULL;
	mark.count = NULL;
	mark.dropBottom = 0;
	return mark;
}

static void addTip( GraphMark &mark, const UnicodeString &text, Color color )
{
	GraphTipLine line;
	line.text = text;
	line.color = color;
	mark.tip.push_back( line );
}

/** The rows a player stands behind; a campaign's side rows have no history to draw. */
static std::vector< const GraphPlayer * > chartedPlayers( void )
{
	std::vector< const GraphPlayer * > players;
	for( size_t each = 0; each < s_players.size(); each++ )
	{
		if( s_players[ each ].index >= 0 )
			players.push_back( &s_players[ each ] );
	}
	return players;
}

static const GraphPlayer *playerFor( Int index )
{
	for( size_t each = 0; each < s_players.size(); each++ )
	{
		if( s_players[ each ].index == index )
			return &s_players[ each ];
	}
	return NULL;
}

/** The cameo a power is fired from; the picture lives on its command button.  InGameUI's
	* powerButton asks the same question for the superweapon timers. */
static const CommandButton *powerButton( const SpecialPowerTemplate *power )
{
	for( const CommandButton *button = TheControlBar->getCommandButtons(); button; button = button->getNext() )
	{
		if( button->getSpecialPowerTemplate() == power && button->getButtonImage() )
			return button;
	}
	return NULL;
}

/** A build's cameo: its own, or else the one on the button that builds it (the Angry Mob has none
	* of its own). */
static const Image *buildImage( const ThingTemplate *thing )
{
	if( thing->getButtonImage() )
		return thing->getButtonImage();
	for( const CommandButton *button = TheControlBar->getCommandButtons(); button; button = button->getNext() )
	{
		if( button->getThingTemplate() == thing && button->getButtonImage() )
			return button->getButtonImage();
	}
	return NULL;
}

static UnicodeString withoutHotkey( UnicodeString text )
{
	UnicodeString plain;
	for( const WideChar *c = text.str(); *c; c++ )
	{
		if( *c != u'&' )
			plain.concat( *c );
	}
	return plain;
}

//-------------------------------------------------------------------------------------------------
static void clearTip( void )
{
	for( size_t each = 0; each < s_tipText.size(); each++ )
		TheDisplayStringManager->freeDisplayString( s_tipText[ each ] );
	s_tipText.clear();
	s_tipMark = -1;
}

static void clearContent( void )
{
	for( size_t each = 0; each < s_texts.size(); each++ )
		TheDisplayStringManager->freeDisplayString( s_texts[ each ].text );
	s_texts.clear();
	for( size_t each = 0; each < s_marks.size(); each++ )
	{
		if( s_marks[ each ].label )
			TheDisplayStringManager->freeDisplayString( s_marks[ each ].label );
		if( s_marks[ each ].count )
			TheDisplayStringManager->freeDisplayString( s_marks[ each ].count );
	}
	s_marks.clear();
	s_lines.clear();
	s_rules.clear();
	s_ruleColors.clear();
	clearTip();
}

/** Lanes stop two pictures that fall on the same moment from hiding each other: each goes into the
	* first lane whose last picture it clears, or else into the one that ends soonest, moved right by
	* up to its own width to clear it.  Only a crowd wider than that still overlaps; the tooltip keeps
	* the exact time. */
static Int pickLane( std::vector< Int > &laneRight, Int &left, Int width, Int gap )
{
	Int best = 0;
	for( size_t lane = 0; lane < laneRight.size(); lane++ )
	{
		if( left >= laneRight[ lane ] )
		{
			laneRight[ lane ] = left + width + gap;
			return (Int)lane;
		}
		if( laneRight[ lane ] < laneRight[ best ] )
			best = (Int)lane;
	}
	left = min( laneRight[ best ], left + width );
	laneRight[ best ] = left + width + gap;
	return best;
}

static Int niceStep( Int raw )
{
	Int magnitude = 1;
	while( magnitude * 10 <= raw )
		magnitude *= 10;
	if( raw <= magnitude )
		return magnitude;
	if( raw <= 2 * magnitude )
		return 2 * magnitude;
	if( raw <= 5 * magnitude )
		return 5 * magnitude;
	return 10 * magnitude;
}

static Int minuteStep( UnsignedInt frames, Int most )
{
	static const Int steps[] = { 1, 2, 5, 10, 15, 20, 30, 60 };
	const Int minutes = frames / ( 60 * LOGICFRAMES_PER_SECOND ) + 1;
	for( Int each = 0; each < ARRAY_SIZE( steps ); each++ )
	{
		if( minutes / steps[ each ] <= most )
			return steps[ each ];
	}
	return 120;
}

//-------------------------------------------------------------------------------------------------
/** A row a player: a bar in its colour, the name in it, the side dimmed beside it, then the numbers
	* under their group headings, Units, Buildings and Money.  Returns where the table ends. */
static Int layoutSummary( Int top, Int width, Int pad )
{
	const Int smallLine = s_smallFont->height;
	const Int line = s_font->height;
	const Int left = pad * 2;
	const Int right = width - pad * 2;
	const Int namesRight = left + ( right - left ) / 3;
	const Int groupGap = pad * 2;
	const Int column = ( right - namesRight - ( SUMMARY_GROUP_COUNT - 1 ) * groupGap ) / SUMMARY_COLUMN_COUNT;

	// each column's right edge, where its heading and numbers are aligned
	Int columnRight[ SUMMARY_COLUMN_COUNT ];
	for( Int each = 0; each < SUMMARY_COLUMN_COUNT; each++ )
		columnRight[ each ] = namesRight + ( each + 1 ) * column + theColumnGroup[ each ] * groupGap;

	const Int subTop = top + smallLine + pad / 2;
	for( Int group = 0; group < SUMMARY_GROUP_COUNT; group++ )
	{
		Int first = SUMMARY_COLUMN_COUNT, last = 0;
		for( Int each = 0; each < SUMMARY_COLUMN_COUNT; each++ )
		{
			if( theColumnGroup[ each ] == group )
			{
				first = min( first, each );
				last = max( last, each );
			}
		}
		const Int groupLeft = columnRight[ first ] - column + pad;
		addText( TheGameText->fetch( theGroupLabels[ group ] ), s_smallFont, ( groupLeft + columnRight[ last ] ) / 2, top, TEXT, 1 );
		addRule( groupLeft, subTop - pad / 4, columnRight[ last ], subTop - pad / 4, AXIS );
	}
	for( Int each = 0; each < SUMMARY_COLUMN_COUNT; each++ )
		addText( TheGameText->fetch( theColumnLabels[ each ] ), s_smallFont, columnRight[ each ], subTop, TEXT_DIM, 2 );
	Int y = subTop + smallLine + pad / 2;
	addRule( left, y, right, y, GRID );

	// eight players in a lobby with its chat below get tighter rows
	const Int rowHeight = s_players.size() > 4 ? line + pad / 3 : line + pad;
	y += pad / 2;
	for( size_t each = 0; each < s_players.size(); each++ )
	{
		const GraphPlayer &player = s_players[ each ];
		const Int textY = y + ( rowHeight - line ) / 2;
		s_marks.push_back( newMark( left, textY + 2, 4, line - 4, player.color, player.color ) );
		Int x = left + 4 + pad;
		x += addText( player.name, s_font, x, textY, player.color ) + pad;
		const Int smallY = textY + line - smallLine - 1;
		if( !player.side.isEmpty() )
			x += addText( player.side, s_smallFont, x, smallY, TEXT_DIM ) + pad;
		if( player.winner )
			addText( TheGameText->fetch( "GUI:SummaryWinner" ), s_smallFont, x, smallY, GOLD );
		for( Int stat = 0; stat < SUMMARY_COLUMN_COUNT; stat++ )
		{
			UnicodeString value;
			value.format( u"%d", player.stats[ stat ] );
			addText( value, s_font, columnRight[ stat ], textY, TEXT, 2 );
		}
		y += rowHeight;
	}
	return y;
}

//-------------------------------------------------------------------------------------------------
/** Gives a marker the picture of event, which outranks whatever it showed before. */
static void dressMark( GraphMark &mark, const MatchEvent &event, const GraphPlayer &player, const CommandButton *button )
{
	mark.rank = theEventRank[ event.type ];
	mark.edge = player.color;
	mark.fill = TAB_FILL;
	if( mark.label )
		TheDisplayStringManager->freeDisplayString( mark.label );
	mark.label = NULL;
	switch( event.type )
	{
		case MATCH_EVENT_SUPERWEAPON_BUILT:
			mark.image = event.thing->getButtonImage();
			break;
		case MATCH_EVENT_SUPERWEAPON_FIRED:
			mark.image = button ? button->getButtonImage() : event.thing->getButtonImage();
			break;
		case MATCH_EVENT_POWER:
			mark.image = button ? button->getButtonImage() : NULL;
			break;
		default:
		{
			mark.image = NULL;
			UnicodeString rank;
			rank.format( u"%d", event.rank );
			mark.label = newText( rank, s_smallFont );
			break;
		}
	}
}

static void layoutChart( MatchSeries series, Int top, Int width, Int bottom, Int pad )
{
	const MatchHistory &history = TheMatchHistory;
	const Int count = history.getSampleCount();
	const Int smallLine = s_smallFont->height;
	const Int icon = smallLine + 6;
	const Int iconWidth = icon * 4 / 3;
	const Int stripTop = top;
	const Int plotTop = stripTop + icon + pad;
	const Int plotBottom = bottom - smallLine - pad / 2;

	if( count == 0 )
	{
		addText( TheGameText->fetch( "GUI:GraphNoData" ), s_font, pad * 2, plotTop, TEXT_DIM );
		return;
	}

	const std::vector< const GraphPlayer * > players = chartedPlayers();
	Int most = 0;
	for( Int sample = 0; sample < count; sample++ )
	{
		for( size_t each = 0; each < players.size(); each++ )
			most = max( most, history.getValue( sample, players[ each ]->index, series ) );
	}
	const Int step = niceStep( max( most, 4 ) / 4 );
	const Int ceiling = step * ( ( max( most, 1 ) + step - 1 ) / step );

	// the value labels decide where the plot starts
	DisplayString *widest = newText( valueText( ceiling ), s_smallFont );
	const Int plotLeft = pad * 3 + textWidth( widest );
	TheDisplayStringManager->freeDisplayString( widest );
	const Int plotRight = width - pad * 2 - iconWidth / 2;
	const Int plotWidth = max( plotRight - plotLeft, 1 );
	const Int plotHeight = max( plotBottom - plotTop, 1 );
	const UnsignedInt end = max( history.getSampleFrame( count - 1 ), (UnsignedInt)1 );

	for( Int value = 0; value <= ceiling; value += step )
	{
		const Int y = plotBottom - (Int)( (Real)value / ceiling * plotHeight );
		addRule( plotLeft, y, plotRight, y, value == 0 ? AXIS : GRID );
		addText( valueText( value ), s_smallFont, plotLeft - pad, y - smallLine / 2, TEXT_DIM, 2 );
	}

	const Int minutes = minuteStep( end, 8 );
	for( UnsignedInt frame = 0; frame <= end; frame += minutes * 60 * LOGICFRAMES_PER_SECOND )
	{
		const Int x = plotLeft + (Int)( (Real)frame / end * plotWidth );
		addText( clockText( frame ), s_smallFont, x, plotBottom + pad / 2, TEXT_DIM, frame == 0 ? 0 : 1 );
	}

	for( size_t each = 0; each < players.size(); each++ )
	{
		const Int index = players[ each ]->index;
		if( !history.wasSampled( index ) )
			continue;
		GraphLine graph;
		graph.color = players[ each ]->color;
		for( Int sample = 0; sample < count; sample++ )
		{
			ICoord2D point;
			point.x = plotLeft + (Int)( (Real)history.getSampleFrame( sample ) / end * plotWidth );
			point.y = plotBottom - (Int)( (Real)history.getValue( sample, index, series ) / ceiling * plotHeight );
			graph.points.push_back( point );
		}
		s_lines.push_back( graph );
	}

	// one row of markers: an event that lands on the one before it joins that marker instead of
	// stacking a second picture, and the marker shows the weightiest of its events
	const size_t firstMarker = s_marks.size();
	Int anchor = 0;
	const std::vector< MatchEvent > &events = history.getEvents();
	for( size_t each = 0; each < events.size(); each++ )
	{
		const MatchEvent &event = events[ each ];
		const GraphPlayer *player = playerFor( event.player );
		if( player == NULL )
			continue;
		const Int x = plotLeft + (Int)( (Real)min( event.frame, end ) / end * plotWidth );
		if( s_marks.size() == firstMarker || x >= anchor + iconWidth + pad / 2 )
		{
			anchor = x;
			GraphMark mark = newMark( x - iconWidth / 2, stripTop, iconWidth, icon, TAB_FILL, player->color );
			mark.dropBottom = plotBottom;
			s_marks.push_back( mark );
		}
		GraphMark &mark = s_marks.back();
		const CommandButton *button = event.power ? powerButton( event.power ) : NULL;
		if( theEventRank[ event.type ] < mark.rank )
			dressMark( mark, event, *player, button );

		UnicodeString what;
		switch( event.type )
		{
			case MATCH_EVENT_SUPERWEAPON_BUILT:
				what = event.thing->getDisplayName();
				break;
			case MATCH_EVENT_SUPERWEAPON_FIRED:
				what = button ? withoutHotkey( TheGameText->fetch( button->getTextLabel() ) ) : event.thing->getDisplayName();
				break;
			case MATCH_EVENT_POWER:
				what = button ? withoutHotkey( TheGameText->fetch( button->getTextLabel() ) ) : UnicodeString( u"?" );
				break;
			default:
				what.format( TheGameText->fetch( "GUI:GraphRank" ).str(), event.rank );
				break;
		}
		UnicodeString tip;
		tip.format( u"%s  %s  %s: %s", clockText( event.frame ).str(), player->name.str(),
			TheGameText->fetch( theEventLabels[ event.type ] ).str(), what.str() );
		addTip( mark, tip, player->color );
		GraphDrop drop;
		drop.x = x;
		drop.color = ( player->color & 0x00FFFFFF ) | 0xB4000000;
		mark.drops.push_back( drop );
	}
	for( size_t each = firstMarker; each < s_marks.size(); each++ )
	{
		if( s_marks[ each ].tip.size() > 1 )
		{
			UnicodeString number;
			number.format( u"%d", (Int)s_marks[ each ].tip.size() );
			s_marks[ each ].count = newText( number, s_tinyFont );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Frame to x on the build order's timeline: linear when the match was short, otherwise the first
	* five minutes get EARLY_SHARE of the width and the rest of the match the remainder. */
static Int buildX( UnsignedInt frame, UnsignedInt end, Int left, Int width )
{
	if( end <= EARLY_FRAMES + EARLY_FRAMES / 5 )
		return left + (Int)( (Real)frame / end * width );
	const Int early = (Int)( width * EARLY_SHARE );
	if( frame <= EARLY_FRAMES )
		return left + (Int)( (Real)frame / EARLY_FRAMES * early );
	return left + early + (Int)( (Real)( frame - EARLY_FRAMES ) / ( end - EARLY_FRAMES ) * ( width - early ) );
}

static void layoutBuildOrder( Int top, Int width, Int bottom, Int pad )
{
	const MatchHistory &history = TheMatchHistory;
	const Int smallLine = s_smallFont->height;
	const std::vector< const GraphPlayer * > players = chartedPlayers();
	if( players.empty() )
		return;

	const Int rowsTop = top + smallLine + pad / 2;
	// a lone campaign player gets the height of two rows, not the whole area
	const Int rowHeight = ( bottom - rowsTop ) / max( (Int)players.size(), 2 );
	// a row too short for the name over the side (six players and up at 1080) gets one line: the name,
	// or the side where names repeat, as eight "Hard AI" seats would
	const Bool twoLines = rowHeight >= 2 * smallLine;
	std::vector< UnicodeString > labels;
	for( size_t each = 0; each < players.size(); each++ )
	{
		Bool repeated = FALSE;
		for( size_t other = 0; other < players.size(); other++ )
			repeated = repeated || ( other != each && players[ other ]->name == players[ each ]->name );
		labels.push_back( twoLines || !repeated ? players[ each ]->name : players[ each ]->side );
	}

	UnsignedInt end = history.getSampleCount() > 0 ? history.getSampleFrame( history.getSampleCount() - 1 ) : 0;
	Int nameWidth = 0;
	for( size_t each = 0; each < players.size(); each++ )
	{
		const std::vector< MatchBuild > &builds = history.getBuilds( players[ each ]->index );
		if( !builds.empty() )
			end = max( end, builds.back().frame );
		DisplayString *name = newText( labels[ each ], s_smallFont );
		nameWidth = max( nameWidth, textWidth( name ) );
		if( twoLines )
		{
			name->setText( players[ each ]->side );
			nameWidth = max( nameWidth, textWidth( name ) );
		}
		TheDisplayStringManager->freeDisplayString( name );
	}
	end = max( end, (UnsignedInt)LOGICFRAMES_PER_SECOND );

	// the cameos at their own 4:3, two lanes of the first of each thing, three of every build, and
	// fewer lanes where that would shrink a cameo under two thirds of a line of text
	Int lanes = s_everyBuild ? 3 : 2;
	while( lanes > 1 && ( rowHeight - pad ) / lanes - 3 < smallLine * 2 / 3 )
		lanes--;
	const Int icon = min( max( ( rowHeight - pad ) / lanes - 3, 10 ), s_font->height * 3 / 2 );
	const Int iconWidth = icon * 4 / 3;
	const Int left = pad * 3 + nameWidth + iconWidth / 2;
	const Int right = width - pad * 2 - iconWidth / 2;
	const Int span = max( right - left, 1 );

	// the time axis: minutes through the first five, then a coarser step for the rest
	const Bool split = end > EARLY_FRAMES + EARLY_FRAMES / 5;
	const UnsignedInt minute = 60 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt lateStep = split ? minuteStep( end - EARLY_FRAMES, 5 ) * minute : minuteStep( end, 8 ) * minute;
	for( UnsignedInt frame = 0; frame <= end; frame += ( split && frame < EARLY_FRAMES ) ? minute : lateStep )
	{
		const Int x = buildX( frame, end, left, span );
		addRule( x, rowsTop, x, rowsTop + (Int)players.size() * rowHeight, frame == EARLY_FRAMES && split ? AXIS : GRID );
		addText( clockText( frame ), s_smallFont, x, top, TEXT_DIM, 1 );
		if( split && frame < EARLY_FRAMES && frame + minute > EARLY_FRAMES )
			frame = EARLY_FRAMES - minute;
	}

	for( size_t each = 0; each < players.size(); each++ )
	{
		const GraphPlayer &player = *players[ each ];
		const Int rowTop = rowsTop + (Int)each * rowHeight;
		if( each > 0 )
			addRule( pad * 2, rowTop, right + iconWidth / 2, rowTop, GRID );
		const Int nameTop = rowTop + ( rowHeight - ( twoLines ? 2 : 1 ) * smallLine ) / 2;
		addText( labels[ each ], s_smallFont, pad * 2, nameTop, player.color );
		if( twoLines )
			addText( player.side, s_smallFont, pad * 2, nameTop + smallLine, TEXT_DIM );

		std::vector< Int > laneRight( lanes, -iconWidth );
		std::vector< const ThingTemplate * > seen;
		const Int lanesTop = rowTop + ( rowHeight - lanes * ( icon + 3 ) ) / 2;
		const std::vector< MatchBuild > &builds = history.getBuilds( player.index );
		for( size_t built = 0; built < builds.size(); built++ )
		{
			const MatchBuild &build = builds[ built ];
			// a picture says what was built; something with none says nothing, and gets left out
			const Image *image = buildImage( build.thing );
			if( image == NULL )
				continue;
			if( !s_everyBuild )
			{
				if( std::find( seen.begin(), seen.end(), build.thing ) != seen.end() )
					continue;
				seen.push_back( build.thing );
			}
			Int x = buildX( build.frame, end, left, span ) - iconWidth / 2;
			const Int lane = pickLane( laneRight, x, iconWidth, 3 );
			x = min( x, right - iconWidth / 2 );	// a push never carries a picture off the timeline's end
			GraphMark mark = newMark( x, lanesTop + lane * ( icon + 3 ), iconWidth, icon, TAB_FILL,
				build.thing->isKindOf( KINDOF_STRUCTURE ) ? TEXT_DIM : player.color );
			mark.image = image;
			UnicodeString tip;
			tip.format( u"%s  %s", clockText( build.frame ).str(), build.thing->getDisplayName().str() );
			addTip( mark, tip, TEXT );
			s_marks.push_back( mark );
		}
	}
}

//-------------------------------------------------------------------------------------------------
static void layoutPanel( void )
{
	clearContent();
	Int width, height;
	s_panel->winGetSize( &width, &height );
	const Int pad = max( s_font->height / 2, 4 );
	const Int smallLine = s_smallFont->height;

	const Int tabsTop = layoutSummary( pad * 2, width, pad ) + pad * 2;
	Int x = pad * 2;
	for( Int tab = 0; tab < GRAPH_TAB_COUNT; tab++ )
	{
		s_tabBox[ tab ].lo.x = x - pad / 2;
		s_tabBox[ tab ].lo.y = tabsTop;
		s_tabBox[ tab ].hi.x = x + textWidth( s_tabText[ tab ] ) + pad / 2;
		s_tabBox[ tab ].hi.y = tabsTop + smallLine + pad / 2;
		x += textWidth( s_tabText[ tab ] ) + pad * 2;
	}
	const Int tabsBottom = s_tabBox[ 0 ].hi.y;
	addRule( pad * 2, tabsBottom, width - pad * 2, tabsBottom, GRID );

	s_toggleBox.lo.x = s_toggleBox.hi.x = 0;
	s_toggleBox.lo.y = s_toggleBox.hi.y = 0;
	if( s_tab == GRAPH_TAB_BUILD_ORDER )
	{
		s_toggleText->setText( TheGameText->fetch( s_everyBuild ? "GUI:BuildOrderFirsts" : "GUI:BuildOrderEvery" ) );
		s_toggleBox.hi.x = width - pad * 2;
		s_toggleBox.lo.x = s_toggleBox.hi.x - textWidth( s_toggleText ) - pad * 2;
		s_toggleBox.lo.y = tabsTop - pad / 2;
		s_toggleBox.hi.y = tabsBottom - 2;
		layoutBuildOrder( tabsBottom + pad, width, height - pad, pad );
	}
	else
		layoutChart( theTabSeries[ s_tab ], tabsBottom + pad, width, height - pad, pad );
}

//-------------------------------------------------------------------------------------------------
static void drawBox( const IRegion2D &box, Int ox, Int oy, Color fill, Color edge )
{
	const Int w = box.hi.x - box.lo.x;
	const Int h = box.hi.y - box.lo.y;
	TheDisplay->drawFillRect( ox + box.lo.x, oy + box.lo.y, w, h, fill );
	TheDisplay->drawOpenRect( ox + box.lo.x, oy + box.lo.y, w, h, 1.0f, edge );
}

static Bool isInside( const IRegion2D &box, Int ox, Int oy, Int x, Int y )
{
	return x >= ox + box.lo.x && x < ox + box.hi.x && y >= oy + box.lo.y && y < oy + box.hi.y;
}

static void drawPanel( GameWindow *window, WinInstanceData *instData )
{
	Int ox, oy, width, height;
	window->winGetScreenPosition( &ox, &oy );
	window->winGetSize( &width, &height );
	TheDisplay->drawFillRect( ox, oy, width, height, PANEL_FILL );
	TheDisplay->drawOpenRect( ox, oy, width, height, 2.0f, PANEL_EDGE );

	const Int pad = max( s_font->height / 2, 4 );
	for( Int tab = 0; tab < GRAPH_TAB_COUNT; tab++ )
	{
		const IRegion2D &box = s_tabBox[ tab ];
		s_tabText[ tab ]->draw( ox + box.lo.x + pad / 2, oy + box.lo.y, tab == s_tab ? TEXT : TEXT_DIM, SHADOW );
		if( tab == s_tab )
			TheDisplay->drawFillRect( ox + box.lo.x, oy + box.hi.y - 2, box.hi.x - box.lo.x, 3, GOLD );
	}
	if( s_toggleBox.hi.x > s_toggleBox.lo.x )
	{
		drawBox( s_toggleBox, ox, oy, TAB_FILL, PANEL_EDGE );
		s_toggleText->draw( ox + s_toggleBox.lo.x + pad, oy + s_toggleBox.lo.y + ( s_toggleBox.hi.y - s_toggleBox.lo.y - s_smallFont->height ) / 2, TEXT, SHADOW );
	}

	for( size_t each = 0; each < s_rules.size(); each++ )
	{
		const IRegion2D &rule = s_rules[ each ];
		TheDisplay->drawLine( ox + rule.lo.x, oy + rule.lo.y, ox + rule.hi.x, oy + rule.hi.y, 1.0f, s_ruleColors[ each ] );
	}
	for( size_t each = 0; each < s_texts.size(); each++ )
		s_texts[ each ].text->draw( ox + s_texts[ each ].x, oy + s_texts[ each ].y, s_texts[ each ].color, SHADOW );

	const ICoord2D mouse = TheMouse->getMouseStatus()->pos;
	Int hovered = -1;
	for( size_t each = 0; each < s_marks.size(); each++ )
	{
		if( !s_marks[ each ].tip.empty() && isInside( s_marks[ each ].box, ox, oy, mouse.x, mouse.y ) )
			hovered = (Int)each;
	}
	if( hovered >= 0 )
	{
		const GraphMark &mark = s_marks[ hovered ];
		for( size_t each = 0; each < mark.drops.size(); each++ )
			TheDisplay->drawLine( ox + mark.drops[ each ].x, oy + mark.box.hi.y, ox + mark.drops[ each ].x, oy + mark.dropBottom, 1.0f, mark.drops[ each ].color );
	}

	for( size_t each = 0; each < s_lines.size(); each++ )
	{
		const std::vector< ICoord2D > &points = s_lines[ each ].points;
		for( size_t point = 1; point < points.size(); point++ )
			TheDisplay->drawLine( ox + points[ point - 1 ].x, oy + points[ point - 1 ].y, ox + points[ point ].x, oy + points[ point ].y, 2.0f, s_lines[ each ].color );
	}

	for( size_t each = 0; each < s_marks.size(); each++ )
	{
		const GraphMark &mark = s_marks[ each ];
		drawBox( mark.box, ox, oy, mark.fill, mark.edge );
		const Int inset = 2;
		if( mark.image )
			TheDisplay->drawImage( mark.image, ox + mark.box.lo.x + inset, oy + mark.box.lo.y + inset, ox + mark.box.hi.x - inset, oy + mark.box.hi.y - inset );
		if( mark.label )
		{
			Int w, h;
			mark.label->getSize( &w, &h );
			mark.label->draw( ox + ( mark.box.lo.x + mark.box.hi.x - w ) / 2, oy + ( mark.box.lo.y + mark.box.hi.y - h ) / 2, mark.edge, SHADOW );
		}
		if( mark.count )
		{
			Int w, h;
			mark.count->getSize( &w, &h );
			const Int x = ox + mark.box.hi.x - w - 2;
			const Int y = oy + mark.box.hi.y - h;
			TheDisplay->drawFillRect( x - 2, y, w + 3, h - 1, GameMakeColor( 0, 0, 0, 200 ) );
			mark.count->draw( x, y - 1, TEXT, SHADOW );
		}
	}

	if( hovered >= 0 )
	{
		const GraphMark &mark = s_marks[ hovered ];
		if( hovered != s_tipMark )
		{
			clearTip();
			for( size_t line = 0; line < mark.tip.size(); line++ )
				s_tipText.push_back( newText( mark.tip[ line ].text, s_smallFont ) );
			s_tipMark = hovered;
		}
		drawBox( mark.box, ox, oy, 0, GOLD );
		Int w = 0;
		const Int lineHeight = s_smallFont->height + 2;
		for( size_t line = 0; line < s_tipText.size(); line++ )
			w = max( w, textWidth( s_tipText[ line ] ) );
		const Int h = (Int)s_tipText.size() * lineHeight;
		const Int x = max( min( ox + mark.box.lo.x, ox + width - w - 12 ), ox + 4 );
		const Int y = oy + mark.box.hi.y + 4 + h + 6 < oy + height ? oy + mark.box.hi.y + 4 : oy + mark.box.lo.y - h - 10;
		TheDisplay->drawFillRect( x, y, w + 8, h + 6, GameMakeColor( 0, 0, 0, 230 ) );
		TheDisplay->drawOpenRect( x, y, w + 8, h + 6, 1.0f, GOLD );
		for( size_t line = 0; line < s_tipText.size(); line++ )
			s_tipText[ line ]->draw( x + 4, y + 3 + (Int)line * lineHeight, mark.tip[ line ].color, SHADOW );
	}
}

static WindowMsgHandledType panelInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg == GWM_CHAR )
		return MSG_IGNORED;
	if( msg == GWM_LEFT_UP )
	{
		Int ox, oy;
		window->winGetScreenPosition( &ox, &oy );
		const Int x = (Int)( mData1 & 0xFFFF );
		const Int y = (Int)( ( mData1 >> 16 ) & 0xFFFF );
		for( Int tab = 0; tab < GRAPH_TAB_COUNT; tab++ )
		{
			if( tab != s_tab && isInside( s_tabBox[ tab ], ox, oy, x, y ) )
			{
				s_tab = tab;
				layoutPanel();
				return MSG_HANDLED;
			}
		}
		if( isInside( s_toggleBox, ox, oy, x, y ) )
		{
			s_everyBuild = !s_everyBuild;
			layoutPanel();
		}
	}
	return MSG_HANDLED;
}

//-------------------------------------------------------------------------------------------------
/** Makes the panel, hidden until ScoreScreenGraphsShow; anchor is the layout's OK button, whose font
	* the panel takes and above which it stops. */
void ScoreScreenGraphsInit( GameWindow *parent, GameWindow *anchor )
{
	s_players.clear();
	s_tab = GRAPH_TAB_EARNED;
	s_everyBuild = FALSE;
	s_font = anchor->winGetFont();
	s_smallFont = TheFontLibrary->getFont( s_font->nameString, max( s_font->pointSize - 2, 8 ), FALSE );
	s_tinyFont = TheFontLibrary->getFont( s_font->nameString, max( s_font->pointSize * 2 / 3, 7 ), TRUE );

	Int parentX, parentY, parentW, parentH, anchorX, anchorY, anchorW, anchorH;
	parent->winGetScreenPosition( &parentX, &parentY );
	parent->winGetSize( &parentW, &parentH );
	anchor->winGetScreenPosition( &anchorX, &anchorY );
	anchor->winGetSize( &anchorW, &anchorH );

	// inside the layout's blue frame, which sits about a twentieth of the screen in from each side
	const Int marginX = parentW / 20;
	const Int marginY = parentH / 16;
	s_panel = TheWindowManager->winCreate( parent, WIN_STATUS_ENABLED | WIN_STATUS_HIDDEN, marginX, marginY,
		parentW - 2 * marginX, anchorY - parentY - marginY - anchorH / 2, GameWinDefaultSystem );
	s_panel->winSetDrawFunc( drawPanel );
	s_panel->winSetInputFunc( panelInput );

	for( Int tab = 0; tab < GRAPH_TAB_COUNT; tab++ )
		s_tabText[ tab ] = newText( TheGameText->fetch( theTabLabels[ tab ] ), s_smallFont );
	s_toggleText = newText( UnicodeString::TheEmptyString, s_smallFont );
}

void ScoreScreenGraphsAddPlayer( Player *player, const UnicodeString &name, Color color )
{
	ScoreKeeper *score = player->getScoreKeeper();
	const PlayerTemplate *side = player->getPlayerTemplate();
	GraphPlayer entry;
	entry.index = player->getPlayerIndex();
	entry.name = name;
	entry.side = side ? side->getDisplayName() : UnicodeString::TheEmptyString;
	entry.color = color;
	entry.winner = TheVictoryConditions->hasAchievedVictory( player );
	entry.stats[ SUMMARY_UNITS_BUILT ] = score->getTotalUnitsBuilt();
	entry.stats[ SUMMARY_UNITS_LOST ] = score->getTotalUnitsLost();
	entry.stats[ SUMMARY_UNITS_DESTROYED ] = score->getTotalUnitsDestroyed();
	entry.stats[ SUMMARY_BUILDINGS_BUILT ] = score->getTotalBuildingsBuilt();
	entry.stats[ SUMMARY_BUILDINGS_LOST ] = score->getTotalBuildingsLost();
	entry.stats[ SUMMARY_BUILDINGS_DESTROYED ] = score->getTotalBuildingsDestroyed();
	entry.stats[ SUMMARY_EARNED ] = score->getTotalMoneyEarned();
	s_players.push_back( entry );
}

/** A campaign's allies or enemies of one side, summed into one row; it has numbers and no lines. */
void ScoreScreenGraphsAddSide( const UnicodeString &name, Color color, Int unitsBuilt, Int unitsLost, Int unitsDestroyed,
	Int buildingsBuilt, Int buildingsLost, Int buildingsDestroyed, Int earned )
{
	GraphPlayer entry;
	entry.index = -1;
	entry.name = name;
	entry.color = color;
	entry.winner = FALSE;
	entry.stats[ SUMMARY_UNITS_BUILT ] = unitsBuilt;
	entry.stats[ SUMMARY_UNITS_LOST ] = unitsLost;
	entry.stats[ SUMMARY_UNITS_DESTROYED ] = unitsDestroyed;
	entry.stats[ SUMMARY_BUILDINGS_BUILT ] = buildingsBuilt;
	entry.stats[ SUMMARY_BUILDINGS_LOST ] = buildingsLost;
	entry.stats[ SUMMARY_BUILDINGS_DESTROYED ] = buildingsDestroyed;
	entry.stats[ SUMMARY_EARNED ] = earned;
	s_players.push_back( entry );
}

/** Lays the panel out and shows it.  floor, when given, is a window the panel has to stop above:
	* the chat after a network match, the "game saved" line after a mission. */
void ScoreScreenGraphsShow( GameWindow *floor )
{
	if( floor )
	{
		Int panelX, panelY, floorX, floorY, width, height;
		s_panel->winGetScreenPosition( &panelX, &panelY );
		floor->winGetScreenPosition( &floorX, &floorY );
		s_panel->winGetSize( &width, &height );
		s_panel->winSetSize( width, min( height, floorY - panelY - s_font->height / 2 ) );
	}
	layoutPanel();
	s_panel->winBringToTop();
	s_panel->winHide( FALSE );
}

void ScoreScreenGraphsShutdown( void )
{
	clearContent();
	for( Int tab = 0; tab < GRAPH_TAB_COUNT; tab++ )
		TheDisplayStringManager->freeDisplayString( s_tabText[ tab ] );
	TheDisplayStringManager->freeDisplayString( s_toggleText );
	TheWindowManager->winDestroy( s_panel );
	s_panel = NULL;
	s_players.clear();
}

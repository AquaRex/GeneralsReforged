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
// The score screen's Graphs button and the panel it opens: one line a player over the match for
// money earned, the bank, the army, buildings, power and units lost, with a marker where a
// superweapon went up or fired, a general's power was used or a general was promoted; and a build
// order tab, every structure and the first of each unit on a timeline whose first five minutes take
// over half the width.  Everything is drawn here from TheMatchHistory, which the logic wrote during
// the match; the panel and its button are made in code, so no layout file changes.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/MatchHistory.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "GameClient/Mouse.h"

#include <vector>

enum GraphTab
{
	GRAPH_TAB_EARNED,
	GRAPH_TAB_BANK,
	GRAPH_TAB_ARMY,
	GRAPH_TAB_BUILDINGS,
	GRAPH_TAB_POWER,
	GRAPH_TAB_LOST,
	GRAPH_TAB_BUILD_ORDER,
	GRAPH_TAB_COUNT
};

static const char *const theTabLabels[ GRAPH_TAB_COUNT ] =
{
	"GUI:GraphEarned", "GUI:GraphBank", "GUI:GraphArmy", "GUI:GraphBuildings", "GUI:GraphPower", "GUI:GraphLost", "GUI:GraphBuildOrder"
};

static const MatchSeries theTabSeries[ GRAPH_TAB_BUILD_ORDER ] =
{
	MATCH_SERIES_EARNED, MATCH_SERIES_BANK, MATCH_SERIES_ARMY, MATCH_SERIES_BUILDINGS, MATCH_SERIES_POWER, MATCH_SERIES_LOST
};

static const char *const theEventLabels[ MATCH_EVENT_TYPE_COUNT ] =
{
	"GUI:GraphSuperweaponBuilt", "GUI:GraphSuperweaponFired", "GUI:GraphGeneralPower", "GUI:GraphPromotion"
};

/// the first five minutes take this share of the build order's width once the match ran past them
static const Real EARLY_SHARE = 0.55f;
static const UnsignedInt EARLY_FRAMES = 5 * 60 * LOGICFRAMES_PER_SECOND;

static const Color PANEL_FILL = GameMakeColor( 8, 12, 20, 255 );
static const Color PANEL_EDGE = GameMakeColor( 110, 130, 160, 255 );
static const Color TAB_FILL = GameMakeColor( 24, 32, 46, 255 );
static const Color TAB_ACTIVE = GameMakeColor( 58, 78, 112, 255 );
static const Color TEXT = GameMakeColor( 220, 225, 235, 255 );
static const Color TEXT_DIM = GameMakeColor( 150, 160, 175, 255 );
static const Color GRID = GameMakeColor( 255, 255, 255, 28 );
static const Color AXIS = GameMakeColor( 255, 255, 255, 90 );
static const Color GOLD = GameMakeColor( 235, 190, 60, 255 );
static const Color RED = GameMakeColor( 225, 60, 45, 255 );
static const Color SHADOW = GameMakeColor( 0, 0, 0, 255 );

struct GraphPlayer
{
	Int index;
	UnicodeString name;
	Color color;
};

struct GraphText
{
	DisplayString *text;
	Int x;
	Int y;
	Color color;
};

/** Something with a picture and a tooltip: an event marker on a chart or a build in the build order. */
struct GraphMark
{
	IRegion2D box;
	const Image *image;
	Color edge;
	Color fill;
	Int lineX;				///< a chart marker's drop line, -1 for none
	Int lineBottom;
	Color lineColor;
	DisplayString *label;	///< the rank number on a promotion
	UnicodeString tip;
};

struct GraphLine
{
	Color color;
	std::vector< ICoord2D > points;
};

static GameWindow *s_button = NULL;
static GameWindow *s_panel = NULL;
static GameFont *s_font = NULL;
static GameFont *s_smallFont = NULL;
static Int s_tab = GRAPH_TAB_EARNED;
static std::vector< GraphPlayer > s_players;
static std::vector< GraphText > s_texts;
static std::vector< GraphMark > s_marks;
static std::vector< GraphLine > s_lines;
static std::vector< IRegion2D > s_rules;		///< grid and axis lines, lo to hi
static std::vector< Color > s_ruleColors;
static IRegion2D s_tabBox[ GRAPH_TAB_COUNT ];
static DisplayString *s_tabText[ GRAPH_TAB_COUNT ];
static DisplayString *s_tip = NULL;
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

static void addText( const UnicodeString &text, GameFont *font, Int x, Int y, Color color )
{
	GraphText entry;
	entry.text = newText( text, font );
	entry.x = x;
	entry.y = y;
	entry.color = color;
	s_texts.push_back( entry );
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

static const GraphPlayer &playerFor( Int index )
{
	for( size_t each = 0; each < s_players.size(); each++ )
	{
		if( s_players[ each ].index == index )
			return s_players[ each ];
	}
	return s_players[ 0 ];
}

static Bool isShown( Int index )
{
	for( size_t each = 0; each < s_players.size(); each++ )
	{
		if( s_players[ each ].index == index )
			return TRUE;
	}
	return FALSE;
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
static void clearContent( void )
{
	for( size_t each = 0; each < s_texts.size(); each++ )
		TheDisplayStringManager->freeDisplayString( s_texts[ each ].text );
	s_texts.clear();
	for( size_t each = 0; each < s_marks.size(); each++ )
	{
		if( s_marks[ each ].label )
			TheDisplayStringManager->freeDisplayString( s_marks[ each ].label );
	}
	s_marks.clear();
	s_lines.clear();
	s_rules.clear();
	s_ruleColors.clear();
	s_tipMark = -1;
}

/** Lanes stop two pictures that fall on the same moment from hiding each other: each goes into the
	* first lane whose last picture it clears, or into the one that ends soonest. */
static Int pickLane( std::vector< Int > &laneRight, Int left, Int right )
{
	Int best = 0;
	for( size_t lane = 0; lane < laneRight.size(); lane++ )
	{
		if( left >= laneRight[ lane ] )
		{
			laneRight[ lane ] = right;
			return (Int)lane;
		}
		if( laneRight[ lane ] < laneRight[ best ] )
			best = (Int)lane;
	}
	laneRight[ best ] = right;
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
/** The players in the score screen's order, each in its colour, along the bottom. */
static void layoutLegend( Int left, Int y, Int right, Int pad )
{
	Int x = left;
	for( size_t each = 0; each < s_players.size(); each++ )
	{
		const GraphPlayer &player = s_players[ each ];
		const Int swatch = s_smallFont->height;
		addRule( x, y + swatch / 2, x + swatch, y + swatch / 2, player.color );
		addRule( x, y + swatch / 2 + 1, x + swatch, y + swatch / 2 + 1, player.color );
		addText( player.name, s_smallFont, x + swatch + pad / 2, y, player.color );
		x += swatch + pad / 2 + textWidth( s_texts.back().text ) + 2 * pad;
		if( x > right )
			break;
	}
}

static void markEventKinds( Int left, Int y, Int pad )
{
	static const Color edges[ MATCH_EVENT_TYPE_COUNT ] = { GOLD, RED, TEXT, TEXT };
	const Int size = s_smallFont->height;
	Int x = left;
	for( Int type = 0; type < MATCH_EVENT_TYPE_COUNT; type++ )
	{
		GraphMark key;
		key.box.lo.x = x;
		key.box.lo.y = y;
		key.box.hi.x = x + size;
		key.box.hi.y = y + size;
		key.image = NULL;
		key.edge = edges[ type ];
		key.fill = type == MATCH_EVENT_RANK ? TEXT_DIM : TAB_FILL;
		key.lineX = -1;
		key.lineBottom = 0;
		key.lineColor = 0;
		key.label = NULL;
		s_marks.push_back( key );
		addText( TheGameText->fetch( theEventLabels[ type ] ), s_smallFont, x + size + pad / 2, y, TEXT_DIM );
		x += size + pad / 2 + textWidth( s_texts.back().text ) + 2 * pad;
	}
}

//-------------------------------------------------------------------------------------------------
static void layoutChart( MatchSeries series, Int top, Int width, Int height, Int pad )
{
	const MatchHistory &history = TheMatchHistory;
	const Int count = history.getSampleCount();
	const Int line = s_smallFont->height;
	const Int legendTop = height - pad - 2 * line - pad / 2;
	const Int icon = s_font->height * 3 / 2;
	const Int lanes = 3;
	const Int stripTop = top;
	const Int plotTop = stripTop + lanes * ( icon + 2 ) + pad;
	const Int plotBottom = legendTop - line - pad;

	if( count == 0 )
	{
		addText( TheGameText->fetch( "GUI:GraphNoData" ), s_font, pad * 2, plotTop, TEXT_DIM );
		return;
	}

	Int most = 0;
	for( Int sample = 0; sample < count; sample++ )
	{
		for( size_t each = 0; each < s_players.size(); each++ )
			most = max( most, history.getValue( sample, s_players[ each ].index, series ) );
	}
	const Int step = niceStep( max( most, 4 ) / 4 );
	const Int ceiling = step * ( ( max( most, 1 ) + step - 1 ) / step );

	// the value labels decide where the plot starts
	DisplayString *widest = newText( valueText( ceiling ), s_smallFont );
	const Int plotLeft = pad * 2 + textWidth( widest );
	TheDisplayStringManager->freeDisplayString( widest );
	const Int plotRight = width - pad * 2;
	const Int plotWidth = max( plotRight - plotLeft, 1 );
	const Int plotHeight = max( plotBottom - plotTop, 1 );
	const UnsignedInt end = max( history.getSampleFrame( count - 1 ), (UnsignedInt)1 );

	for( Int value = 0; value <= ceiling; value += step )
	{
		const Int y = plotBottom - (Int)( (Real)value / ceiling * plotHeight );
		addRule( plotLeft, y, plotRight, y, value == 0 ? AXIS : GRID );
		addText( valueText( value ), s_smallFont, pad, y - line / 2, TEXT_DIM );
	}
	addRule( plotLeft, plotTop, plotLeft, plotBottom, AXIS );

	const Int minutes = minuteStep( end, 8 );
	for( UnsignedInt frame = 0; frame <= end; frame += minutes * 60 * LOGICFRAMES_PER_SECOND )
	{
		const Int x = plotLeft + (Int)( (Real)frame / end * plotWidth );
		addRule( x, plotBottom, x, plotBottom + pad / 2, AXIS );
		addText( clockText( frame ), s_smallFont, x - line, plotBottom + pad / 2, TEXT_DIM );
	}

	for( size_t each = 0; each < s_players.size(); each++ )
	{
		const Int index = s_players[ each ].index;
		if( !history.wasSampled( index ) )
			continue;
		GraphLine graph;
		graph.color = s_players[ each ].color;
		for( Int sample = 0; sample < count; sample++ )
		{
			ICoord2D point;
			point.x = plotLeft + (Int)( (Real)history.getSampleFrame( sample ) / end * plotWidth );
			point.y = plotBottom - (Int)( (Real)history.getValue( sample, index, series ) / ceiling * plotHeight );
			graph.points.push_back( point );
		}
		s_lines.push_back( graph );
	}

	std::vector< Int > laneRight( lanes, -icon );
	const std::vector< MatchEvent > &events = history.getEvents();
	for( size_t each = 0; each < events.size(); each++ )
	{
		const MatchEvent &event = events[ each ];
		if( !isShown( event.player ) )
			continue;
		const GraphPlayer &player = playerFor( event.player );
		const Int x = plotLeft + (Int)( (Real)min( event.frame, end ) / end * plotWidth );
		// a promotion is only a number, so it takes a narrow box and leaves room for the pictures
		const Int iconWidth = event.type == MATCH_EVENT_RANK ? icon * 2 / 3 : icon * 4 / 3;
		const Int lane = pickLane( laneRight, x - iconWidth / 2, x + iconWidth / 2 + 2 );

		GraphMark mark;
		mark.box.lo.x = x - iconWidth / 2;
		mark.box.lo.y = stripTop + lane * ( icon + 2 );
		mark.box.hi.x = mark.box.lo.x + iconWidth;
		mark.box.hi.y = mark.box.lo.y + icon;
		mark.image = NULL;
		mark.edge = player.color;
		mark.fill = TAB_FILL;
		mark.lineX = x;
		mark.lineBottom = plotBottom;
		mark.lineColor = ( player.color & 0x00FFFFFF ) | 0x96000000;
		mark.label = NULL;

		UnicodeString what;
		const CommandButton *button = event.power ? powerButton( event.power ) : NULL;
		switch( event.type )
		{
			case MATCH_EVENT_SUPERWEAPON_BUILT:
				mark.edge = GOLD;
				mark.image = event.thing->getButtonImage();
				what = event.thing->getDisplayName();
				break;
			case MATCH_EVENT_SUPERWEAPON_FIRED:
				mark.edge = RED;
				mark.image = button ? button->getButtonImage() : event.thing->getButtonImage();
				what = button ? withoutHotkey( TheGameText->fetch( button->getTextLabel() ) ) : event.thing->getDisplayName();
				break;
			case MATCH_EVENT_POWER:
				mark.image = button ? button->getButtonImage() : NULL;
				what = button ? withoutHotkey( TheGameText->fetch( button->getTextLabel() ) ) : UnicodeString( u"?" );
				break;
			default:
			{
				mark.fill = player.color;
				UnicodeString rank;
				rank.format( u"%d", event.rank );
				mark.label = newText( rank, s_font );
				what.format( TheGameText->fetch( "GUI:GraphRank" ).str(), event.rank );
				break;
			}
		}
		mark.tip.format( u"%s  %s - %s: %s", clockText( event.frame ).str(), player.name.str(),
			TheGameText->fetch( theEventLabels[ event.type ] ).str(), what.str() );
		s_marks.push_back( mark );
	}

	layoutLegend( pad * 2, legendTop, width - pad * 2, pad );
	markEventKinds( pad * 2, legendTop + line + pad / 2, pad );
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

static void layoutBuildOrder( Int top, Int width, Int height, Int pad )
{
	const MatchHistory &history = TheMatchHistory;
	const Int line = s_smallFont->height;
	if( s_players.empty() )
		return;

	UnsignedInt end = history.getSampleCount() > 0 ? history.getSampleFrame( history.getSampleCount() - 1 ) : 0;
	Int nameWidth = 0;
	for( size_t each = 0; each < s_players.size(); each++ )
	{
		const std::vector< MatchBuild > &builds = history.getBuilds( s_players[ each ].index );
		if( !builds.empty() )
			end = max( end, builds.back().frame );
		DisplayString *name = newText( s_players[ each ].name, s_font );
		nameWidth = max( nameWidth, textWidth( name ) );
		TheDisplayStringManager->freeDisplayString( name );
	}
	end = max( end, (UnsignedInt)LOGICFRAMES_PER_SECOND );

	const Int axisTop = top + line + pad / 2;
	const Int rowsTop = axisTop + line + pad;
	const Int rowHeight = ( height - pad - rowsTop ) / (Int)s_players.size();
	// three lanes of cameos a row, at the cameos' own 4:3, small enough that an opening's builds
	// mostly sit side by side
	const Int lanes = 3;
	const Int icon = min( max( ( rowHeight - 4 ) / lanes - 2, 10 ), s_font->height * 3 / 2 );
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
		addRule( x, axisTop + line, x, height - pad, frame == EARLY_FRAMES && split ? AXIS : GRID );
		addText( clockText( frame ), s_smallFont, x - line, axisTop, TEXT_DIM );
		if( split && frame < EARLY_FRAMES && frame + minute > EARLY_FRAMES )
			frame = EARLY_FRAMES - minute;
	}
	if( split )
		addText( TheGameText->fetch( "GUI:GraphFirstMinutes" ), s_smallFont, left, top, TEXT );

	for( size_t each = 0; each < s_players.size(); each++ )
	{
		const GraphPlayer &player = s_players[ each ];
		const Int rowTop = rowsTop + (Int)each * rowHeight;
		addRule( pad, rowTop + rowHeight - 1, right, rowTop + rowHeight - 1, GRID );
		addText( player.name, s_font, pad * 2, rowTop + ( rowHeight - s_font->height ) / 2, player.color );

		std::vector< Int > laneRight( lanes, -iconWidth );
		const Int lanesTop = rowTop + ( rowHeight - lanes * ( icon + 2 ) ) / 2;
		const std::vector< MatchBuild > &builds = history.getBuilds( player.index );
		for( size_t built = 0; built < builds.size(); built++ )
		{
			const MatchBuild &build = builds[ built ];
			const Int x = buildX( build.frame, end, left, span );
			const Int lane = pickLane( laneRight, x - iconWidth / 2, x + iconWidth / 2 + 1 );
			GraphMark mark;
			mark.box.lo.x = x - iconWidth / 2;
			mark.box.lo.y = lanesTop + lane * ( icon + 2 );
			mark.box.hi.x = mark.box.lo.x + iconWidth;
			mark.box.hi.y = mark.box.lo.y + icon;
			mark.image = build.thing->getButtonImage();
			mark.edge = build.thing->isKindOf( KINDOF_STRUCTURE ) ? TEXT_DIM : player.color;
			mark.fill = TAB_FILL;
			mark.lineX = -1;
			mark.lineBottom = 0;
			mark.lineColor = 0;
			mark.label = NULL;
			mark.tip.format( u"%s  %s", clockText( build.frame ).str(), build.thing->getDisplayName().str() );
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

	Int x = pad;
	for( Int tab = 0; tab < GRAPH_TAB_COUNT; tab++ )
	{
		const Int tabWidth = textWidth( s_tabText[ tab ] ) + 2 * pad;
		s_tabBox[ tab ].lo.x = x;
		s_tabBox[ tab ].lo.y = pad;
		s_tabBox[ tab ].hi.x = x + tabWidth;
		s_tabBox[ tab ].hi.y = pad + s_font->height + pad;
		x += tabWidth + pad / 2;
	}
	const Int top = s_tabBox[ 0 ].hi.y + pad;
	addRule( pad, top - pad / 2, width - pad, top - pad / 2, PANEL_EDGE );

	if( s_tab == GRAPH_TAB_BUILD_ORDER )
		layoutBuildOrder( top, width, height, pad );
	else
		layoutChart( theTabSeries[ s_tab ], top, width, height, pad );
}

//-------------------------------------------------------------------------------------------------
static void drawBox( const IRegion2D &box, Int ox, Int oy, Color fill, Color edge )
{
	const Int w = box.hi.x - box.lo.x;
	const Int h = box.hi.y - box.lo.y;
	TheDisplay->drawFillRect( ox + box.lo.x, oy + box.lo.y, w, h, fill );
	TheDisplay->drawOpenRect( ox + box.lo.x, oy + box.lo.y, w, h, 1.0f, edge );
}

static void drawPanel( GameWindow *window, WinInstanceData *instData )
{
	Int ox, oy, width, height;
	window->winGetScreenPosition( &ox, &oy );
	window->winGetSize( &width, &height );
	TheDisplay->drawFillRect( ox, oy, width, height, PANEL_FILL );
	TheDisplay->drawOpenRect( ox, oy, width, height, 2.0f, PANEL_EDGE );

	for( Int tab = 0; tab < GRAPH_TAB_COUNT; tab++ )
	{
		drawBox( s_tabBox[ tab ], ox, oy, tab == s_tab ? TAB_ACTIVE : TAB_FILL, tab == s_tab ? GOLD : PANEL_EDGE );
		const Int pad = ( s_tabBox[ tab ].hi.y - s_tabBox[ tab ].lo.y - s_font->height ) / 2;
		s_tabText[ tab ]->draw( ox + s_tabBox[ tab ].lo.x + pad, oy + s_tabBox[ tab ].lo.y + pad, tab == s_tab ? TEXT : TEXT_DIM, SHADOW );
	}

	for( size_t each = 0; each < s_rules.size(); each++ )
	{
		const IRegion2D &rule = s_rules[ each ];
		TheDisplay->drawLine( ox + rule.lo.x, oy + rule.lo.y, ox + rule.hi.x, oy + rule.hi.y, 1.0f, s_ruleColors[ each ] );
	}
	for( size_t each = 0; each < s_texts.size(); each++ )
		s_texts[ each ].text->draw( ox + s_texts[ each ].x, oy + s_texts[ each ].y, s_texts[ each ].color, SHADOW );

	for( size_t each = 0; each < s_marks.size(); each++ )
	{
		if( s_marks[ each ].lineX >= 0 )
			TheDisplay->drawLine( ox + s_marks[ each ].lineX, oy + s_marks[ each ].box.hi.y, ox + s_marks[ each ].lineX, oy + s_marks[ each ].lineBottom, 1.0f, s_marks[ each ].lineColor );
	}
	for( size_t each = 0; each < s_lines.size(); each++ )
	{
		const std::vector< ICoord2D > &points = s_lines[ each ].points;
		for( size_t point = 1; point < points.size(); point++ )
			TheDisplay->drawLine( ox + points[ point - 1 ].x, oy + points[ point - 1 ].y, ox + points[ point ].x, oy + points[ point ].y, 2.0f, s_lines[ each ].color );
	}

	const ICoord2D mouse = TheMouse->getMouseStatus()->pos;
	Int hovered = -1;
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
			mark.label->draw( ox + ( mark.box.lo.x + mark.box.hi.x - w ) / 2, oy + ( mark.box.lo.y + mark.box.hi.y - h ) / 2, TEXT, SHADOW );
		}
		if( !mark.tip.isEmpty() && mouse.x >= ox + mark.box.lo.x && mouse.x < ox + mark.box.hi.x && mouse.y >= oy + mark.box.lo.y && mouse.y < oy + mark.box.hi.y )
			hovered = (Int)each;
	}

	if( hovered >= 0 )
	{
		if( hovered != s_tipMark )
		{
			s_tip->setText( s_marks[ hovered ].tip );
			s_tipMark = hovered;
		}
		const GraphMark &mark = s_marks[ hovered ];
		drawBox( mark.box, ox, oy, 0, GOLD );
		Int w, h;
		s_tip->getSize( &w, &h );
		const Int x = min( ox + mark.box.lo.x, ox + width - w - 8 );
		const Int y = oy + mark.box.hi.y + 4 + h + 6 < oy + height ? oy + mark.box.hi.y + 4 : oy + mark.box.lo.y - h - 10;
		TheDisplay->drawFillRect( x, y, w + 8, h + 6, GameMakeColor( 0, 0, 0, 230 ) );
		TheDisplay->drawOpenRect( x, y, w + 8, h + 6, 1.0f, GOLD );
		s_tip->draw( x + 4, y + 3, TEXT, SHADOW );
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
		ICoord2D mouse;
		mouse.x = (Int)( mData1 & 0xFFFF );
		mouse.y = (Int)( ( mData1 >> 16 ) & 0xFFFF );
		for( Int tab = 0; tab < GRAPH_TAB_COUNT; tab++ )
		{
			const IRegion2D &box = s_tabBox[ tab ];
			if( tab != s_tab && mouse.x >= ox + box.lo.x && mouse.x < ox + box.hi.x && mouse.y >= oy + box.lo.y && mouse.y < oy + box.hi.y )
			{
				s_tab = tab;
				layoutPanel();
				break;
			}
		}
	}
	return MSG_HANDLED;
}

//-------------------------------------------------------------------------------------------------
/** The button goes to the left of anchor, the panel over the score table above it. */
void ScoreScreenGraphsInit( GameWindow *parent, GameWindow *anchor )
{
	s_players.clear();
	s_font = anchor->winGetFont();
	s_smallFont = TheFontLibrary->getFont( s_font->nameString, max( s_font->pointSize - 2, 8 ), FALSE );

	Int parentX, parentY, parentW, parentH, anchorX, anchorY, anchorW, anchorH;
	parent->winGetScreenPosition( &parentX, &parentY );
	parent->winGetSize( &parentW, &parentH );
	anchor->winGetScreenPosition( &anchorX, &anchorY );
	anchor->winGetSize( &anchorW, &anchorH );

	WinInstanceData instData;
	instData.init();
	BitSet( instData.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	s_button = TheWindowManager->gogoGadgetPushButton( parent, WIN_STATUS_ENABLED | WIN_STATUS_IMAGE,
		anchorX - parentX - anchorW - anchorW / 8, anchorY - parentY, anchorW, anchorH, &instData, s_font, TRUE );
	// dressed as its neighbour, the layout's own button, rather than the plain default look
	WinInstanceData *mine = s_button->winGetInstanceData();
	const WinInstanceData *theirs = anchor->winGetInstanceData();
	for( Int index = 0; index < MAX_DRAW_DATA; index++ )
	{
		mine->m_enabledDrawData[ index ] = theirs->m_enabledDrawData[ index ];
		mine->m_disabledDrawData[ index ] = theirs->m_disabledDrawData[ index ];
		mine->m_hiliteDrawData[ index ] = theirs->m_hiliteDrawData[ index ];
	}
	mine->m_enabledText = theirs->m_enabledText;
	mine->m_disabledText = theirs->m_disabledText;
	mine->m_hiliteText = theirs->m_hiliteText;
	s_button->winSetDrawFunc( anchor->winGetDrawFunc() );
	GadgetButtonSetText( s_button, TheGameText->fetch( "GUI:ScoreGraphs" ) );

	// inside the layout's blue frame, which sits about a twentieth of the screen in from each side
	const Int marginX = parentW / 20;
	const Int marginY = parentH / 16;
	s_panel = TheWindowManager->winCreate( parent, WIN_STATUS_ENABLED | WIN_STATUS_HIDDEN, marginX, marginY,
		parentW - 2 * marginX, anchorY - parentY - marginY - anchorH / 2, GameWinDefaultSystem );
	s_panel->winSetDrawFunc( drawPanel );
	s_panel->winSetInputFunc( panelInput );

	for( Int tab = 0; tab < GRAPH_TAB_COUNT; tab++ )
		s_tabText[ tab ] = newText( TheGameText->fetch( theTabLabels[ tab ] ), s_font );
	s_tip = newText( UnicodeString::TheEmptyString, s_smallFont );
}

void ScoreScreenGraphsAddPlayer( Int index, const UnicodeString &name, Color color )
{
	GraphPlayer player;
	player.index = index;
	player.name = name;
	player.color = color;
	s_players.push_back( player );
}

/** TRUE when control was the Graphs button, which then opens or closes the panel. */
Bool ScoreScreenGraphsSelected( GameWindow *control )
{
	if( s_button == NULL || control != s_button )
		return FALSE;
	const Bool show = s_panel->winIsHidden();
	if( show )
	{
		layoutPanel();
		s_panel->winBringToTop();
	}
	s_panel->winHide( !show );
	GadgetButtonSetText( s_button, TheGameText->fetch( show ? "GUI:ScoreTable" : "GUI:ScoreGraphs" ) );
	return TRUE;
}

void ScoreScreenGraphsShutdown( void )
{
	if( s_button == NULL )
		return;
	clearContent();
	for( Int tab = 0; tab < GRAPH_TAB_COUNT; tab++ )
		TheDisplayStringManager->freeDisplayString( s_tabText[ tab ] );
	TheDisplayStringManager->freeDisplayString( s_tip );
	TheWindowManager->winDestroy( s_panel );
	TheWindowManager->winDestroy( s_button );
	s_panel = NULL;
	s_button = NULL;
	s_players.clear();
}

/*
 * The rank ladder's arithmetic (RankLadder in GlobalData.h, docs/plans/RANK-LADDER.md in the
 * wrapper): the ratio each side's length and top give, the thresholds, the EA level on the pins, the
 * one-grade clip and the stat split, and the perks and auras each side's shipped Perk_ lines give.
 */
/* crc.h pulls winsock2.h, so it has to come before anything that drags in windows.h */
#include "Common/crc.h"

#include "test_harness.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Common/GlobalData.h"

/* BalanceReforged.ini's Veterancy block, one side at a time */
static RankLadder makeLadder( Int grades, Int veteran, Int elite, Int heroic, Int heroStart,
															Real rateOfFire, Real damage, Real health )
{
	RankLadder ladder;
	ladder.clear();
	ladder.m_grades = grades;
	ladder.m_top = 120.0f;
	ladder.m_pin[LEVEL_VETERAN] = veteran;
	ladder.m_pin[LEVEL_ELITE] = elite;
	ladder.m_pin[LEVEL_HEROIC] = heroic;
	ladder.m_heroStart = heroStart;
	ladder.m_statTop[RANK_STAT_RATE_OF_FIRE] = rateOfFire;
	ladder.m_statTop[RANK_STAT_DAMAGE] = damage;
	ladder.m_statTop[RANK_STAT_HEALTH] = health;
	ladder.finish( 1.4f );
	return ladder;
}

static RankLadder america() { return makeLadder( 28, 4, 13, 22, 23, 1.42f, 1.19f, 1.42f ); }
static RankLadder china() { return makeLadder( 19, 4, 10, 15, 15, 1.30f, 1.19f, 1.55f ); }
static RankLadder gla() { return makeLadder( 20, 4, 10, 15, 15, 1.48f, 1.14f, 1.42f ); }

TEST(rank_ladder_ratio_puts_every_top_at_120_kills)
{
	/* grade 0 is unranked on every side, so China's Private and GLA's Recruit are grade 1 */
	CHECK_NEAR( america().m_ratio, 1.0935f, 0.0005f );
	CHECK_NEAR( china().m_ratio, 1.1775f, 0.0005f );
	CHECK_NEAR( gla().m_ratio, 1.1633f, 0.0005f );

	/* a Crusader, E 100: 12,000 to General of the Army, one kill to E-2 */
	const RankLadder usa = america();
	CHECK_NEAR( usa.threshold( 100, 28 ), 12000, 1 );
	CHECK_EQ( usa.threshold( 100, 1 ), 100 );
	CHECK_EQ( usa.threshold( 100, 0 ), 0 );
	/* the doc's table: Corporal 4.6 E, Warrant Officer 23.5 E, Lieutenant Colonel 65.7 E */
	CHECK_NEAR( usa.threshold( 100, 4 ), 460, 1 );
	CHECK_NEAR( usa.threshold( 100, 13 ), 2350, 3 );
	CHECK_NEAR( usa.threshold( 100, 22 ), 6570, 5 );
	/* the Heroic pins: China's Colonel 59.7 E, GLA's Warlord 53.1 E */
	CHECK_NEAR( china().threshold( 100, 15 ), 5972, 5 );
	CHECK_NEAR( gla().threshold( 100, 15 ), 5308, 5 );
	CHECK_EQ( china().threshold( 100, 1 ), 100 );
	CHECK_EQ( gla().threshold( 100, 1 ), 100 );
}

TEST(rank_ladder_small_values_round_once_and_never_fall_back)
{
	/* the Red Guard's E of 5: each threshold is rounded from the sum, never step by step */
	const RankLadder ladder = china();
	for( Int grade = 1; grade <= ladder.m_grades; ++grade )
		CHECK( ladder.threshold( 5, grade ) >= ladder.threshold( 5, grade - 1 ) );
	CHECK_NEAR( ladder.threshold( 5, 19 ), 600, 1 );
}

TEST(rank_ladder_grade_and_level_follow_experience_and_pins)
{
	const RankLadder usa = america();
	const Int corporal = usa.threshold( 100, 4 );
	CHECK_EQ( usa.gradeOf( 100, corporal - 1 ), 3 );
	CHECK_EQ( usa.gradeOf( 100, corporal ), 4 );
	CHECK_EQ( usa.gradeOf( 100, 0 ), 0 );
	CHECK_EQ( usa.gradeOf( 100, 1000000 ), 28 );

	CHECK_EQ( (Int)usa.levelOf( 3 ), (Int)LEVEL_REGULAR );
	CHECK_EQ( (Int)usa.levelOf( 4 ), (Int)LEVEL_VETERAN );
	CHECK_EQ( (Int)usa.levelOf( 12 ), (Int)LEVEL_VETERAN );
	CHECK_EQ( (Int)usa.levelOf( 13 ), (Int)LEVEL_ELITE );
	CHECK_EQ( (Int)usa.levelOf( 21 ), (Int)LEVEL_ELITE );
	CHECK_EQ( (Int)usa.levelOf( 22 ), (Int)LEVEL_HEROIC );
	CHECK_EQ( (Int)usa.levelOf( 28 ), (Int)LEVEL_HEROIC );
	CHECK_EQ( (Int)china().levelOf( 0 ), (Int)LEVEL_REGULAR );
	CHECK_EQ( (Int)china().levelOf( 3 ), (Int)LEVEL_REGULAR );
	CHECK_EQ( (Int)china().levelOf( 4 ), (Int)LEVEL_VETERAN );
	CHECK_EQ( (Int)china().levelOf( 14 ), (Int)LEVEL_ELITE );
	CHECK_EQ( (Int)china().levelOf( 15 ), (Int)LEVEL_HEROIC );
	CHECK_EQ( (Int)gla().levelOf( 9 ), (Int)LEVEL_VETERAN );
	CHECK_EQ( (Int)gla().levelOf( 10 ), (Int)LEVEL_ELITE );
}

TEST(rank_ladder_one_gain_lifts_one_grade_at_most)
{
	const RankLadder usa = america();

	/* a Ranger (E 20) at grade 0 kills a Crusader worth 100: he stops one point short of grade 2 */
	const Int ranger = usa.clipGain( 20, 0, 100 );
	CHECK_EQ( ranger, usa.threshold( 20, 2 ) - 1 );
	CHECK_EQ( usa.gradeOf( 20, ranger ), 1 );

	/* a gain that fits is untouched */
	CHECK_EQ( usa.clipGain( 100, 0, 150 ), 150 );

	/* halfway through a grade the cap is still the second threshold above it */
	const Int mid = usa.threshold( 100, 10 ) + 50;
	CHECK_EQ( usa.clipGain( 100, mid, 100000 ), usa.threshold( 100, 12 ) - 1 - mid );
	CHECK_EQ( usa.gradeOf( 100, mid + usa.clipGain( 100, mid, 100000 ) ), 11 );

	/* from the next-to-last grade on nothing is clipped, and experience keeps growing past the top */
	CHECK_EQ( usa.clipGain( 100, usa.threshold( 100, 27 ), 100000 ), 100000 );
	CHECK_EQ( usa.clipGain( 100, usa.threshold( 100, 28 ) + 5, 777 ), 777 );
}

TEST(rank_ladder_stats_split_one_worth_curve)
{
	const RankLadder ladders[] = { america(), china(), gla() };
	for( Int side = 0; side < 3; ++side )
	{
		const RankLadder &ladder = ladders[ side ];
		const Real *bottom = ladder.m_stat[ 0 ];
		CHECK_NEAR( bottom[RANK_STAT_RATE_OF_FIRE] * bottom[RANK_STAT_DAMAGE] * bottom[RANK_STAT_HEALTH], 1.0f, 0.0001f );
		const Real *top = ladder.m_stat[ ladder.m_grades ];
		CHECK_NEAR( top[RANK_STAT_RATE_OF_FIRE] * top[RANK_STAT_DAMAGE] * top[RANK_STAT_HEALTH], 2.40f, 0.01f );
		/* worth = 1 + 1.4 sqrt(total / 120) at every grade */
		for( Int grade = 1; grade <= ladder.m_grades; ++grade )
		{
			const Real *stat = ladder.m_stat[ grade ];
			const Real worth = stat[RANK_STAT_RATE_OF_FIRE] * stat[RANK_STAT_DAMAGE] * stat[RANK_STAT_HEALTH];
			CHECK_NEAR( worth, 1.0f + 1.4f * sqrtf( ladder.m_total[ grade ] / 120.0f ), 0.01f );
			CHECK( stat[RANK_STAT_HEALTH] >= ladder.m_stat[ grade - 1 ][RANK_STAT_HEALTH] );
		}
	}
	/* the hand-rolled ln and e agree with the CRT's: each stat is its top value to ln(worth) / ln(2.4) */
	const RankLadder &usa = ladders[ 0 ];
	for( Int grade = 0; grade <= usa.m_grades; ++grade )
	{
		const double share = log( 1.0 + 1.4 * sqrt( ( usa.m_total[ grade ] < 120.0f ? usa.m_total[ grade ] : 120.0f ) / 120.0 ) ) / log( 2.4 );
		CHECK_NEAR( usa.m_stat[ grade ][RANK_STAT_HEALTH], pow( 1.42, share ), 0.00001 );
		CHECK_NEAR( usa.m_stat[ grade ][RANK_STAT_RANGE], 1.0, 0.00001 );
	}
	CHECK_NEAR( usa.m_stat[ usa.m_grades ][RANK_STAT_RATE_OF_FIRE], 1.42f, 0.0005f );

	/* the first kill is worth 1.13 everywhere */
	const Real *first = ladders[ 0 ].m_stat[ 1 ];
	CHECK_NEAR( first[RANK_STAT_RATE_OF_FIRE] * first[RANK_STAT_DAMAGE] * first[RANK_STAT_HEALTH], 1.13f, 0.005f );
}

TEST(rank_ladder_hero_insignia_runs_from_its_start_to_the_top)
{
	const RankLadder usa = america();
	CHECK_EQ( usa.insigniaIndex( 0, TRUE ), 23 );
	CHECK_EQ( usa.insigniaIndex( 28, TRUE ), 28 );
	CHECK_EQ( usa.insigniaIndex( 14, TRUE ), 25 );
	CHECK_EQ( usa.insigniaIndex( 14, FALSE ), 14 );
	CHECK_EQ( gla().insigniaIndex( 20, TRUE ), 20 );
	CHECK_EQ( china().insigniaIndex( 0, TRUE ), 15 );
	CHECK_EQ( china().insigniaIndex( 19, TRUE ), 19 );
}

TEST(rank_ladder_perks_carry_up_and_the_strongest_of_a_kind_holds)
{
	/* Turkiye's Perk_ lines: sight 110%, 120%, 130% at 3, 8, 18; the rate-of-fire aura 5% at 10, 10% at 16 */
	RankLadder ladder;
	ladder.clear();
	ladder.m_grades = 21;
	ladder.m_top = 120.0f;
	ladder.m_pin[LEVEL_VETERAN] = 4;
	ladder.m_pin[LEVEL_ELITE] = 11;
	ladder.m_pin[LEVEL_HEROIC] = 16;
	ladder.m_auraRadius = 150.0f;
	ladder.m_perkAt[ 3 ][ RANK_PERK_SIGHT ] = 1.10f;
	ladder.m_perkAt[ 8 ][ RANK_PERK_SIGHT ] = 1.20f;
	ladder.m_perkAt[ 18 ][ RANK_PERK_SIGHT ] = 1.30f;
	ladder.m_perkAt[ 10 ][ RANK_PERK_AURA_RATE_OF_FIRE ] = 0.05f;
	ladder.m_perkAt[ 16 ][ RANK_PERK_AURA_RATE_OF_FIRE ] = 0.10f;
	ladder.m_perkAt[ 17 ][ RANK_PERK_DEATH_RALLY ] = 0.15f;
	ladder.m_perkAt[ 21 ][ RANK_PERK_AURA_RADIUS ] = 2.0f;
	ladder.finish( 1.4f );

	CHECK_EQ( ladder.perk( 2, RANK_PERK_SIGHT ), 0.0f );
	CHECK_NEAR( ladder.perk( 3, RANK_PERK_SIGHT ), 1.10f, 1e-6f );
	CHECK_NEAR( ladder.perk( 7, RANK_PERK_SIGHT ), 1.10f, 1e-6f );
	CHECK_NEAR( ladder.perk( 8, RANK_PERK_SIGHT ), 1.20f, 1e-6f );
	CHECK_NEAR( ladder.perk( 21, RANK_PERK_SIGHT ), 1.30f, 1e-6f );

	/* the Albay's 10% replaces the Bascavus's 5%, it does not add to it */
	CHECK_EQ( ladder.aura( 9, RANK_AURA_RATE_OF_FIRE ), 0.0f );
	CHECK_NEAR( ladder.aura( 10, RANK_AURA_RATE_OF_FIRE ), 0.05f, 1e-6f );
	CHECK_NEAR( ladder.aura( 16, RANK_AURA_RATE_OF_FIRE ), 0.10f, 1e-6f );
	CHECK_NEAR( ladder.aura( 17, RANK_AURA_RALLY ), 0.15f, 1e-6f );
	CHECK_EQ( ladder.aura( 16, RANK_AURA_RALLY ), 0.0f );

	/* the Maresal's double reach, and a grade past the top reads the top; no line names a radius, so
	   every kind and the rally take AuraRadius */
	CHECK_NEAR( ladder.auraRadius( 20, RANK_AURA_RATE_OF_FIRE ), 150.0f, 1e-3f );
	CHECK_NEAR( ladder.auraRadius( 21, RANK_AURA_RATE_OF_FIRE ), 300.0f, 1e-3f );
	CHECK_NEAR( ladder.auraRadius( 40, RANK_AURA_RATE_OF_FIRE ), 300.0f, 1e-3f );
	CHECK_NEAR( ladder.auraRadius( 21, RANK_AURA_RALLY ), 300.0f, 1e-3f );
}

/* The side's Perk_ lines as BalanceReforged.ini ships them, through the parser's own grammar
   (RankLadder::addPerkLine), on top of that side's stats.  FALSE if the file is missing or a line
   will not parse. */
static Bool shippedLadder( RankLadder &ladder, const char *side )
{
	FILE *fp = fopen( BALANCE_REFORGED_INI, "rb" );
	if( fp == NULL )
		return FALSE;
	char key[ 32 ];
	snprintf( key, sizeof( key ), "Perk_%s", side );
	const size_t keyLength = strlen( key );
	Bool parsed = TRUE;
	char line[ 512 ];
	while( fgets( line, sizeof( line ), fp ) != NULL )
	{
		char *at = line;
		while( *at == ' ' || *at == '\t' )
			++at;
		if( strncmp( at, key, keyLength ) != 0 || ( at[ keyLength ] != ' ' && at[ keyLength ] != '=' ) )
			continue;
		char *comment = strchr( at, ';' );
		if( comment )
			*comment = '\0';
		const char *tokens[ 6 ];
		Int count = 0;
		for( char *token = strtok( strchr( at, '=' ) + 1, " \t\r\n" ); token && count < 6; token = strtok( NULL, " \t\r\n" ) )
			tokens[ count++ ] = token;
		try
		{
			ladder.addPerkLine( tokens, count );
		}
		catch( ... )
		{
			printf( "  %s line will not parse\n", key );
			parsed = FALSE;
		}
	}
	fclose( fp );
	ladder.finish( 1.4f );
	return parsed;
}

TEST(rank_ladder_america_ships_the_wiki_ladder)
{
	RankLadder usa = america();
	CHECK( shippedLadder( usa, "America" ) );

	/* self: Drilled at E-2, Steady hands then Calibrated, Precision, the Designator's 2 s then 4 s */
	CHECK_EQ( usa.perk( 0, RANK_PERK_RATE_OF_FIRE ), 0.0f );
	CHECK_NEAR( usa.perk( 1, RANK_PERK_RATE_OF_FIRE ), 0.05f, 1e-6f );
	CHECK_NEAR( usa.perk( 2, RANK_PERK_RANGE ), 0.03f, 1e-6f );
	CHECK_NEAR( usa.perk( 14, RANK_PERK_RANGE ), 0.06f, 1e-6f );
	CHECK_NEAR( usa.perk( 3, RANK_PERK_MEND ), 0.005f, 1e-6f );
	CHECK_NEAR( usa.perk( 15, RANK_PERK_SCATTER ), 0.50f, 1e-6f );
	CHECK_NEAR( usa.perk( 24, RANK_PERK_DESIGNATE ), 2.0f, 1e-6f );
	CHECK_EQ( usa.perk( 27, RANK_PERK_DESIGNATE_REVEAL ), 0.0f );
	CHECK_NEAR( usa.perk( 28, RANK_PERK_DESIGNATE ), 4.0f, 1e-6f );
	CHECK_NEAR( usa.perk( 28, RANK_PERK_DESIGNATE_REVEAL ), 1.0f, 1e-6f );

	/* the rate-of-fire aura I to VI, each with its own radius */
	CHECK_EQ( usa.aura( 3, RANK_AURA_RATE_OF_FIRE ), 0.0f );
	CHECK_NEAR( usa.aura( 4, RANK_AURA_RATE_OF_FIRE ), 0.05f, 1e-6f );
	CHECK_NEAR( usa.auraRadius( 4, RANK_AURA_RATE_OF_FIRE ), 80.0f, 1e-3f );
	CHECK_NEAR( usa.auraRadius( 10, RANK_AURA_RATE_OF_FIRE ), 100.0f, 1e-3f );
	CHECK_NEAR( usa.aura( 23, RANK_AURA_RATE_OF_FIRE ), 0.15f, 1e-6f );
	CHECK_NEAR( usa.auraRadius( 23, RANK_AURA_RATE_OF_FIRE ), 160.0f, 1e-3f );

	/* the mend aura reaches infantry at 80, then every unit at 100 from the Warrant Officer */
	CHECK_NEAR( usa.aura( 6, RANK_AURA_MEND ), 0.005f, 1e-6f );
	CHECK_EQ( (Int)usa.auraReach( 6, RANK_AURA_MEND ), (Int)RANK_REACH_INFANTRY );
	CHECK_NEAR( usa.auraRadius( 12, RANK_AURA_MEND ), 80.0f, 1e-3f );
	CHECK_NEAR( usa.aura( 13, RANK_AURA_MEND ), 0.0075f, 1e-6f );
	CHECK_EQ( (Int)usa.auraReach( 13, RANK_AURA_MEND ), (Int)RANK_REACH_UNITS );
	CHECK_NEAR( usa.auraRadius( 13, RANK_AURA_MEND ), 100.0f, 1e-3f );

	/* drill caps on the grade it names, detect is a radius, and the O-10's command net reaches 25%
	   farther with every aura but not with its detection */
	CHECK_NEAR( usa.aura( 8, RANK_AURA_DRILL ), 4.0f, 1e-6f );
	CHECK_NEAR( usa.aura( 25, RANK_AURA_DRILL ), 13.0f, 1e-6f );
	CHECK_NEAR( usa.perk( 21, RANK_PERK_DETECT ), 100.0f, 1e-3f );
	CHECK_NEAR( usa.auraRadius( 26, RANK_AURA_GUARD ), 180.0f, 1e-3f );
	CHECK_NEAR( usa.auraRadius( 27, RANK_AURA_GUARD ), 225.0f, 1e-3f );
	CHECK_NEAR( usa.aura( 28, RANK_AURA_RATE_OF_FIRE ), 0.18f, 1e-6f );
	CHECK_NEAR( usa.auraRadius( 28, RANK_AURA_RATE_OF_FIRE ), 250.0f, 1e-3f );
	CHECK_NEAR( usa.perk( 28, RANK_PERK_DETECT ), 100.0f, 1e-3f );
	CHECK_EQ( usa.aura( 28, RANK_AURA_PACE ), 0.0f );
}

TEST(rank_ladder_china_ships_the_wiki_ladder)
{
	RankLadder prc = china();
	CHECK( shippedLadder( prc, "China" ) );

	/* self: Hardy, and Last stand from the Major General */
	CHECK_EQ( prc.perk( 1, RANK_PERK_HEALTH ), 0.0f );
	CHECK_NEAR( prc.perk( 2, RANK_PERK_HEALTH ), 0.08f, 1e-6f );
	CHECK_EQ( prc.perk( 16, RANK_PERK_LAST_STAND ), 0.0f );
	CHECK_NEAR( prc.perk( 17, RANK_PERK_LAST_STAND ), 0.25f, 1e-6f );

	/* guard I to III, steady to Fearless's -50%, seize, and forced march for infantry only */
	CHECK_NEAR( prc.aura( 3, RANK_AURA_GUARD ), 0.04f, 1e-6f );
	CHECK_NEAR( prc.auraRadius( 3, RANK_AURA_GUARD ), 80.0f, 1e-3f );
	CHECK_NEAR( prc.aura( 14, RANK_AURA_GUARD ), 0.10f, 1e-6f );
	CHECK_NEAR( prc.aura( 11, RANK_AURA_STEADY ), 0.50f, 1e-6f );
	CHECK_NEAR( prc.aura( 12, RANK_AURA_CAPTURE ), 0.30f, 1e-6f );
	CHECK_NEAR( prc.aura( 16, RANK_AURA_PACE ), 0.08f, 1e-6f );
	CHECK_EQ( (Int)prc.auraReach( 16, RANK_AURA_PACE ), (Int)RANK_REACH_INFANTRY );
	CHECK_NEAR( prc.aura( 18, RANK_AURA_DRILL ), 10.0f, 1e-6f );

	/* the General's rate-of-fire IV names no radius: III's 180, then People's army's 25% */
	CHECK_NEAR( prc.aura( 19, RANK_AURA_RATE_OF_FIRE ), 0.18f, 1e-6f );
	CHECK_NEAR( prc.auraRadius( 18, RANK_AURA_RATE_OF_FIRE ), 180.0f, 1e-3f );
	CHECK_NEAR( prc.auraRadius( 19, RANK_AURA_RATE_OF_FIRE ), 225.0f, 1e-3f );
	CHECK_EQ( prc.perk( 19, RANK_PERK_DETECT ), 0.0f );
}

TEST(rank_ladder_gla_ships_the_wiki_ladder)
{
	RankLadder gla_ = gla();
	CHECK( shippedLadder( gla_, "GLA" ) );

	/* self: Light feet, Patch-up, Scavenger, Tough, Cell survives */
	CHECK_NEAR( gla_.perk( 2, RANK_PERK_SPEED ), 0.05f, 1e-6f );
	CHECK_NEAR( gla_.perk( 3, RANK_PERK_MEND ), 0.005f, 1e-6f );
	CHECK_EQ( gla_.perk( 3, RANK_PERK_SCAVENGE ), 0.0f );
	CHECK_NEAR( gla_.perk( 4, RANK_PERK_SCAVENGE ), 0.10f, 1e-6f );
	CHECK_NEAR( gla_.perk( 5, RANK_PERK_HEALTH ), 0.08f, 1e-6f );
	CHECK_EQ( gla_.perk( 17, RANK_PERK_SALVAGE_CRATE ), 0.0f );
	CHECK_NEAR( gla_.perk( 18, RANK_PERK_SALVAGE_CRATE ), 1.0f, 1e-6f );

	/* pace I is infantry's, pace II as strong for every unit replaces it, III is stronger */
	CHECK_EQ( (Int)gla_.auraReach( 6, RANK_AURA_PACE ), (Int)RANK_REACH_INFANTRY );
	CHECK_EQ( (Int)gla_.auraReach( 10, RANK_AURA_PACE ), (Int)RANK_REACH_UNITS );
	CHECK_NEAR( gla_.auraRadius( 10, RANK_AURA_PACE ), 120.0f, 1e-3f );
	CHECK_NEAR( gla_.aura( 15, RANK_AURA_PACE ), 0.12f, 1e-6f );

	/* spoils I to III, the last one paying for buildings' kills too */
	CHECK_NEAR( gla_.aura( 7, RANK_AURA_SPOILS ), 0.05f, 1e-6f );
	CHECK_EQ( (Int)gla_.auraReach( 14, RANK_AURA_SPOILS ), (Int)RANK_REACH_UNITS );
	CHECK_NEAR( gla_.aura( 17, RANK_AURA_SPOILS ), 0.15f, 1e-6f );
	CHECK_EQ( (Int)gla_.auraReach( 17, RANK_AURA_SPOILS ), (Int)RANK_REACH_STRUCTURES );
	CHECK_NEAR( gla_.auraRadius( 17, RANK_AURA_SPOILS ), 180.0f, 1e-3f );

	/* the Warlord's rate-of-fire II keeps I's 120, Liberation's 25% on top at the end */
	CHECK_NEAR( gla_.aura( 15, RANK_AURA_RATE_OF_FIRE ), 0.12f, 1e-6f );
	CHECK_NEAR( gla_.auraRadius( 15, RANK_AURA_RATE_OF_FIRE ), 120.0f, 1e-3f );
	CHECK_NEAR( gla_.aura( 19, RANK_AURA_MEND ), 0.0125f, 1e-6f );
	CHECK_NEAR( gla_.auraRadius( 20, RANK_AURA_MEND ), 225.0f, 1e-3f );
	CHECK_EQ( gla_.aura( 20, RANK_AURA_GUARD ), 0.0f );
}

/* Every aura kind a shipped ladder gives draws a ring: the file has an AuraRing_<kind> block for it,
   or InGameUI::updateAuraRings skips the kind and the leader's aura goes unseen. */
TEST(rank_ladder_every_aura_given_has_a_ring)
{
	FILE *fp = fopen( BALANCE_REFORGED_INI, "rb" );
	CHECK( fp != NULL );
	if( fp == NULL )
		return;
	static char text[ 1 << 20 ];
	text[ fread( text, 1, sizeof( text ) - 1, fp ) ] = '\0';
	fclose( fp );

	struct Side { const char *name; RankLadder ladder; } sides[] =
	{
		{ "America", america() }, { "China", china() }, { "GLA", gla() },
		{ "Turkiye", makeLadder( 21, 4, 11, 16, 16, 1.42f, 1.19f, 1.42f ) },
	};
	for( Side &side : sides )
	{
		CHECK( shippedLadder( side.ladder, side.name ) );
		for( Int kind = 0; kind < RANK_AURA_COUNT; ++kind )
		{
			Bool given = FALSE;
			for( Int grade = 0; grade <= side.ladder.m_grades; ++grade )
				given = given || side.ladder.aura( grade, (RankAura)kind ) > 0.0f;
			char block[ 64 ];
			snprintf( block, sizeof( block ), "AuraRing_%s", TheRankAuraNames[ kind ] );
			Bool ringed = FALSE;		// the block's own line, not a longer name that starts the same
			for( const char *at = strstr( text, block ); at && !ringed; at = strstr( at + 1, block ) )
				ringed = strchr( " \t\r\n", at[ strlen( block ) ] ) != NULL;
			if( given && !ringed )
				printf( "  %s gives %s and no ring draws it\n", side.name, TheRankAuraNames[ kind ] );
			CHECK( !given || ringed );
		}
	}
}

TEST(rank_ladder_perk_line_grammar_refuses_what_it_does_not_know)
{
	RankLadder ladder;
	ladder.clear();
	const char *const unknownKind[] = { "3", "AURA_NOTHING", "5%" };
	const char *const unknownReach[] = { "3", "AURA_MEND", "1%", "100", "AIRCRAFT" };
	const char *const pastTheTop[] = { "33", "HEALTH", "8%" };
	Int threw = 0;
	try { ladder.addPerkLine( unknownKind, 3 ); } catch( ... ) { ++threw; }
	try { ladder.addPerkLine( unknownReach, 5 ); } catch( ... ) { ++threw; }
	try { ladder.addPerkLine( pastTheTop, 3 ); } catch( ... ) { ++threw; }
	CHECK_EQ( threw, 3 );
}

TEST(rank_ladder_drill_teaches_up_to_its_cap_and_no_further)
{
	const RankLadder usa = america();
	/* a Crusader, E 100, 5% a second under a drill aura I (Corporal, grade 4) */
	const Int corporal = usa.threshold( 100, 4 );
	CHECK_EQ( usa.drillGain( 100, 0, 4, 0.05f ), 5 );
	CHECK_EQ( usa.drillGain( 100, corporal - 2, 4, 0.05f ), 2 );
	CHECK_EQ( usa.drillGain( 100, corporal, 4, 0.05f ), 0 );
	CHECK_EQ( usa.drillGain( 100, corporal + 300, 4, 0.05f ), 0 );
	/* the Red Guard's E of 5 still learns a point a second */
	CHECK_EQ( china().drillGain( 5, 0, 4, 0.05f ), 1 );
}

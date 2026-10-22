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

// FILE: SwarmStrikeSpecialPower.cpp /////////////////////////////////////////////////////////////
//
// Turkiye's loitering munitions.  A nuke burns one spot; this spends one munition on each of the
// most expensive targets in a wide circle, so a spread-out army hurts more than a packed base.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/FXList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/SwarmStrikeSpecialPower.h"

// ------------------------------------------------------------------------------------------------
SwarmStrikeSpecialPowerModuleData::SwarmStrikeSpecialPowerModuleData( void )
{
	m_radius = 0.0f;
	m_targetCount = 1;
	m_repeatTargets = FALSE;
	m_weapon = NULL;
	m_targetFX = NULL;
}

// ------------------------------------------------------------------------------------------------
/*static*/ void SwarmStrikeSpecialPowerModuleData::buildFieldParse( MultiIniFieldParse& p )
{
	SpecialPowerModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "Radius",				INI::parseReal,						NULL, offsetof( SwarmStrikeSpecialPowerModuleData, m_radius ) },
		{ "TargetCount",	INI::parseInt,						NULL, offsetof( SwarmStrikeSpecialPowerModuleData, m_targetCount ) },
		{ "RepeatTargets",	INI::parseBool,					NULL, offsetof( SwarmStrikeSpecialPowerModuleData, m_repeatTargets ) },
		{ "Weapon",				INI::parseWeaponTemplate,	NULL, offsetof( SwarmStrikeSpecialPowerModuleData, m_weapon ) },
		{ "TargetFX",			INI::parseFXList,					NULL, offsetof( SwarmStrikeSpecialPowerModuleData, m_targetFX ) },
		{ 0, 0, 0, 0 }
	};
	p.add( dataFieldParse );
}

// ------------------------------------------------------------------------------------------------
SwarmStrikeSpecialPower::SwarmStrikeSpecialPower( Thing *thing, const ModuleData *moduleData )
												: SpecialPowerModule( thing, moduleData )
{
}

// ------------------------------------------------------------------------------------------------
SwarmStrikeSpecialPower::~SwarmStrikeSpecialPower( void )
{
}

// ------------------------------------------------------------------------------------------------
/** One shot, at the target or else at the point.  A projectile is made at the shooter's foot when its
	* model has no launch bone, and a drone made at the foot of a building met the ground the frame it
	* left, so it starts from the roof. */
// ------------------------------------------------------------------------------------------------
static void fireShot( const WeaponTemplate *weapon, const Object *source, Object *target, const Coord3D *pos )
{
	Weapon *w = TheWeaponStore->allocateNewWeapon( weapon, PRIMARY_WEAPON );
	w->loadAmmoNow( source );
	ObjectID projectileID = INVALID_ID;
	if( target )
		w->fireWeapon( source, target, &projectileID );
	else
		w->fireWeapon( source, pos, &projectileID );
	w->deleteInstance();

	Object *projectile = TheGameLogic->findObjectByID( projectileID );
	if( projectile == NULL )
		return;		// a hit with no projectile, the rank tree's munitions
	Coord3D roof = *projectile->getPosition();
	roof.z += source->getGeometryInfo().getMaxHeightAbovePosition();
	projectile->setPosition( &roof );
}

// ------------------------------------------------------------------------------------------------
void SwarmStrikeSpecialPower::doSpecialPowerAtLocation( const Coord3D *loc, Real angle, UnsignedInt commandOptions )
{
	if( getObject()->isDisabled() )
		return;

	SpecialPowerModule::doSpecialPowerAtLocation( loc, angle, commandOptions );

	const SwarmStrikeSpecialPowerModuleData *data = getSwarmStrikeSpecialPowerModuleData();
	std::vector<Object*> targets;
	Weapon_findMostValuableEnemies( getObject(), loc, data->m_radius, data->m_targetCount, targets );

	for( std::vector<Object*>::iterator it = targets.begin(); it != targets.end(); ++it )
	{
		DEBUG_LOG(( "SWARM '%s' picks '%s' %d\n", getPowerName().str(), (*it)->getTemplate()->getName().str(), (*it)->getID() ));
		FXList::doFXPos( data->m_targetFX, (*it)->getPosition() );
		fireShot( data->m_weapon, getObject(), *it, NULL );
	}

	if( !data->m_repeatTargets )
		return;

	// the spare shots: round the same targets again, dearest first, or at the click when none stood there
	for( Int shot = (Int)targets.size(); shot < data->m_targetCount; ++shot )
		fireShot( data->m_weapon, getObject(), targets.empty() ? NULL : targets[ shot % targets.size() ], loc );
	DEBUG_LOG(( "SWARM '%s' frame %d sends %d at %d targets around (%.0f,%.0f)\n", getPowerName().str(),
		TheGameLogic->getFrame(), data->m_targetCount, (Int)targets.size(), loc->x, loc->y ));
}

// ------------------------------------------------------------------------------------------------
void SwarmStrikeSpecialPower::doSpecialPowerAtObject( Object *obj, UnsignedInt commandOptions )
{
	if( getObject()->isDisabled() )
		return;

	doSpecialPowerAtLocation( obj->getPosition(), INVALID_ANGLE, commandOptions );
}

// ------------------------------------------------------------------------------------------------
void SwarmStrikeSpecialPower::crc( Xfer *xfer )
{
	SpecialPowerModule::crc( xfer );
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void SwarmStrikeSpecialPower::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	SpecialPowerModule::xfer( xfer );
}

// ------------------------------------------------------------------------------------------------
void SwarmStrikeSpecialPower::loadPostProcess( void )
{
	SpecialPowerModule::loadPostProcess();
}

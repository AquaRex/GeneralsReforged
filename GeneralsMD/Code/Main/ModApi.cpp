// Zero Hour Reforged Local: the engine calls the mod manager (dinput8.dll) makes, exported from
// generals.exe by name so that it does not have to find them in the machine code, which moves with
// every build.  The manager looks them up with GetProcAddress( GetModuleHandle( NULL ), ... ).

#include <windows.h>
#include "Lib/BaseType.h"
#include "Common/AsciiString.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/Shell.h"

extern "C" __declspec(dllexport) unsigned int ZHGameNameKey( const char *name )
{
	return TheNameKeyGenerator->nameToKey( name );
}

extern "C" __declspec(dllexport) void ZHGamePushScreen( const char *layout )
{
	TheShell->push( AsciiString( layout ) );
}

extern "C" __declspec(dllexport) void ZHGamePopScreen( void )
{
	TheShell->popImmediate();
}

/** Code mods change the simulation without touching an INI file, so the INI CRC two machines compare
	* on joining (GameDataMatch.h) would let a game with different mods or settings start and fall out
	* of step.  The manager stamps what it runs into that CRC; a stamp of 0 leaves it as it is. */
extern "C" __declspec(dllexport) void ZHGameSetModStamp( unsigned int stamp )
{
	static UnsignedInt s_dataCRC = 0;
	static Bool s_known = FALSE;
	if( !s_known )
	{
		s_dataCRC = TheGlobalData->m_iniCRC;
		s_known = TRUE;
	}
	TheWritableGlobalData->m_iniCRC = s_dataCRC ^ stamp;
}

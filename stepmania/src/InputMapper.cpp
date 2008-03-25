#include "global.h"
#include "InputMapper.h"
#include "IniFile.h"
#include "MessageManager.h"
#include "RageLog.h"
#include "InputFilter.h"
#include "RageUtil.h"
#include "PrefsManager.h"
#include "RageInput.h"
#include "SpecialFiles.h"
#include "LocalizedString.h"
#include "Foreach.h"

static Preference<RString> g_sLastSeenInputDevices( "LastSeenInputDevices", "" );
static Preference<bool> g_bAutoMapOnJoyChange( "AutoMapOnJoyChange", true );

namespace
{
	// lookup for efficiency from a DeviceInput to a GameInput
	// This is repopulated every time m_PItoDI changes by calling UpdateTempDItoPI().
	map<DeviceInput, GameInput> g_tempDItoGI;

	PlayerNumber g_JoinControllers;
};

InputMapper*	INPUTMAPPER = NULL;	// global and accessable from anywhere in our program

InputMapper::InputMapper()
{
	g_JoinControllers = PLAYER_INVALID;
	m_pInputScheme = NULL;
}


InputMapper::~InputMapper()
{
	SaveMappingsToDisk();
	g_tempDItoGI.clear();
}

void InputMapper::ClearAllMappings()
{
	FOREACH_GameController( i )
		FOREACH_GameButton(j)
			for( int k=0; k<NUM_GAME_TO_DEVICE_SLOTS; k++ )
				m_GItoDI[i][j][k].MakeInvalid();
	UpdateTempDItoGI();
}

struct AutoJoyMapping
{
	const char *szGame;
	const char *szDriverRegex;	// reported by InputHandler
	const char *szControllerName;	// the product name of the controller
	InputMapping maps[32];
};
#define END_MARKER	{-1, DeviceButton_Invalid, GameButton_Invalid, false },	// end marker
const InputMapping g_DefaultKeyMappings[] = 
{
	{ 0, KEY_LEFT,		GAME_BUTTON_MENULEFT,	false },
	{ 0, KEY_RIGHT,		GAME_BUTTON_MENURIGHT,	false },
	{ 0, KEY_UP,		GAME_BUTTON_MENUUP,	false },
	{ 0, KEY_DOWN,		GAME_BUTTON_MENUDOWN,	false },
	{ 0, KEY_ENTER,		GAME_BUTTON_START,	false },
	{ 0, KEY_SLASH,		GAME_BUTTON_SELECT,	false },
	{ 0, KEY_ESC,		GAME_BUTTON_BACK,	false },
	{ 0, KEY_KP_C4,		GAME_BUTTON_MENULEFT,	true },
	{ 0, KEY_KP_C6,		GAME_BUTTON_MENURIGHT,	true },
	{ 0, KEY_KP_C8,		GAME_BUTTON_MENUUP,	true },
	{ 0, KEY_KP_C2,		GAME_BUTTON_MENUDOWN,	true },
	{ 0, KEY_KP_ENTER,	GAME_BUTTON_START,	true },
	{ 0, KEY_KP_C0,		GAME_BUTTON_SELECT,	true },
	{ 0, KEY_NUMLOCK,	GAME_BUTTON_BACK,	true },
	{ 0, KEY_F1,		GAME_BUTTON_COIN,	false },
	{ 0, KEY_SCRLLOCK,	GAME_BUTTON_OPERATOR,	false },
	END_MARKER
};

void InputMapper::AddDefaultMappingsForCurrentGameIfUnmapped()
{
	// Clear default mappings.  Default mappings are in the third slot.
	FOREACH_GameController( i )
		FOREACH_GameButton(j)
			ClearFromInputMap( GameInput(i, j), 2 );

	vector<InputMapping> aMaps;
	aMaps.reserve( 32 );

	for( int k=0; !g_DefaultKeyMappings[k].IsEndMarker(); k++ )
		aMaps.push_back( g_DefaultKeyMappings[k] );
	for( int k=0; !m_pInputScheme->m_Maps[k].IsEndMarker(); k++ )
		aMaps.push_back( m_pInputScheme->m_Maps[k] );

	/* There may be duplicate GAME_BUTTON maps.  Process the list backwards,
	 * so game-specific mappings override g_DefaultKeyMappings. */
	std::reverse( aMaps.begin(), aMaps.end() );

	FOREACH( InputMapping, aMaps, m )
	{
		DeviceButton key = m->deviceButton;
		DeviceInput DeviceI( DEVICE_KEYBOARD, key );
		GameInput GameI( m->SecondController? GAME_CONTROLLER_2:GAME_CONTROLLER_1, m->gb );
		if( !IsMapped(DeviceI) )	// if this key isn't already being used by another user-made mapping
			SetInputMap( DeviceI, GameI, 2 );   
	}
}

const AutoJoyMapping g_AutoJoyMappings[] = 
{
	{
		"dance",
		"GIC USB Joystick",
		"Boom USB convertor (black/gray)",
		{
			{ 0, JOY_BUTTON_16,	DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_BUTTON_14,	DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_BUTTON_13,	DANCE_BUTTON_UP,	false },
			{ 0, JOY_BUTTON_15,	DANCE_BUTTON_DOWN,	false },
			{ 1, JOY_BUTTON_4,	DANCE_BUTTON_LEFT,	false },
			{ 1, JOY_BUTTON_2,	DANCE_BUTTON_RIGHT,	false },
			{ 1, JOY_BUTTON_1,	DANCE_BUTTON_UP,	false },
			{ 1, JOY_BUTTON_3,	DANCE_BUTTON_DOWN,	false },
			{ 0, JOY_BUTTON_7,	DANCE_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_8,	DANCE_BUTTON_UPRIGHT,	false },
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_12,	GAME_BUTTON_START,	false },
			END_MARKER
		}
	},
	{
		"dance",
		"4 axis 16 button joystick",
		"EMS USB2",
		{
			{ 0, JOY_BUTTON_16,	DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_BUTTON_14,	DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_BUTTON_13,	DANCE_BUTTON_UP,	false },
			{ 0, JOY_BUTTON_15,	DANCE_BUTTON_DOWN,	false },
			{ 1, JOY_BUTTON_4,	DANCE_BUTTON_LEFT,	false },
			{ 1, JOY_BUTTON_2,	DANCE_BUTTON_RIGHT,	false },
			{ 1, JOY_BUTTON_1,	DANCE_BUTTON_UP,	false },
			{ 1, JOY_BUTTON_3,	DANCE_BUTTON_DOWN,	false },
			{ 0, JOY_BUTTON_7,	DANCE_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_8,	DANCE_BUTTON_UPRIGHT,	false },
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_10,	GAME_BUTTON_START,	false },
			END_MARKER
		}
	},
	{
		"dance",
		"GamePad Pro USB ",	// yes, there is a space at the end
		"GamePad Pro USB",
		{
			{ 0, JOY_LEFT,		DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_RIGHT,		DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_UP,		DANCE_BUTTON_UP,	false },
			{ 0, JOY_DOWN,		DANCE_BUTTON_DOWN,	false },
			{ 1, JOY_BUTTON_1,	DANCE_BUTTON_LEFT,	false },
			{ 1, JOY_BUTTON_3,	DANCE_BUTTON_RIGHT,	false },
			{ 1, JOY_BUTTON_4,	DANCE_BUTTON_UP,	false },
			{ 1, JOY_BUTTON_2,	DANCE_BUTTON_DOWN,	false },
			{ 0, JOY_BUTTON_5,	DANCE_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_6,	DANCE_BUTTON_UPRIGHT,	false },
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_10,	GAME_BUTTON_START,	false },
			END_MARKER
		}
	},
	{
		"dance",
		"SideWinder Game Pad USB version 1.0",
		"SideWinder Game Pad USB",
		{
			{ 0, JOY_LEFT,		DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_RIGHT,		DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_UP,		DANCE_BUTTON_UP,	false },
			{ 0, JOY_DOWN,		DANCE_BUTTON_DOWN,	false },
			{ 1, JOY_BUTTON_4,	DANCE_BUTTON_LEFT,	false },
			{ 1, JOY_BUTTON_2,	DANCE_BUTTON_RIGHT,	false },
			{ 1, JOY_BUTTON_5,	DANCE_BUTTON_UP,	false },
			{ 1, JOY_BUTTON_1,	DANCE_BUTTON_DOWN,	false },
			{ 0, JOY_BUTTON_7,	DANCE_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_8,	DANCE_BUTTON_UPRIGHT,	false },
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_10,	GAME_BUTTON_START,	false },
			END_MARKER
		}
	},
	{
		"dance",
		"4 axis 12 button joystick with hat switch",
		"Super Joy Box 5",
		{
			{ 0, JOY_LEFT,		DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_RIGHT,		DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_UP,		DANCE_BUTTON_UP,	false },
			{ 0, JOY_DOWN,		DANCE_BUTTON_DOWN,	false },
			{ 1, JOY_BUTTON_4,	DANCE_BUTTON_LEFT,	false },
			{ 1, JOY_BUTTON_2,	DANCE_BUTTON_RIGHT,	false },
			{ 1, JOY_BUTTON_1,	DANCE_BUTTON_UP,	false },
			{ 1, JOY_BUTTON_3,	DANCE_BUTTON_DOWN,	false },
			{ 0, JOY_BUTTON_7,	DANCE_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_8,	DANCE_BUTTON_UPRIGHT,	false },
			{ 0, JOY_BUTTON_10,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_START,	false },
			END_MARKER
		}
	},
/*	{
		"dance",
		"MP-8866 Dual USB Joypad",
		"Super Dual Box (from Lik-Sang, 2004)",
		//NEEDS_DANCE_PAD_MAPPING_CODE,
		{
			{ 0, JOY_LEFT,		DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_RIGHT,		DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_UP,		DANCE_BUTTON_UP,	false },
			{ 0, JOY_DOWN,		DANCE_BUTTON_DOWN,	false },
			{ 1, JOY_BUTTON_4,	DANCE_BUTTON_LEFT,	false },
			{ 1, JOY_BUTTON_2,	DANCE_BUTTON_RIGHT,	false },
			{ 1, JOY_BUTTON_1,	DANCE_BUTTON_UP,	false },
			{ 1, JOY_BUTTON_3,	DANCE_BUTTON_DOWN,	false },
			{ 0, JOY_BUTTON_5,	DANCE_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_6,	DANCE_BUTTON_UPRIGHT,	false },
			{ 1, JOY_BUTTON_7,	DANCE_BUTTON_UPLEFT,	false },
			{ 1, JOY_BUTTON_8,	DANCE_BUTTON_UPRIGHT,	false },
			{ 0, JOY_BUTTON_10,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_START,	false },
			END_MARKER
		}
	},
*/
	{
		"dance",
		"MP-8866 Dual USB Joypad",
		"Super Dual Box (from DDRGame.com, Feb 2008)",
		//NEEDS_DANCE_PAD_MAPPING_CODE,
		{
			{ 0, JOY_BUTTON_3,	DANCE_BUTTON_UP,	false },
			{ 0, JOY_BUTTON_2,	DANCE_BUTTON_DOWN,	false },
			{ 0, JOY_BUTTON_1,	DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_BUTTON_4,	DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_BUTTON_7,	DANCE_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_8,	DANCE_BUTTON_UPRIGHT,	false },
			//{ 0, JOY_BUTTON_5,	DANCE_BUTTON_DOWNLEFT,	false },
			//{ 0, JOY_BUTTON_6,	DANCE_BUTTON_DOWNRIGHT,	false },
			{ 0, JOY_BUTTON_10,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_START,	false },
			END_MARKER
		}
	},
	{
		"dance",
		"NTPAD",
		"NTPAD",
		{
			{ 0, JOY_BUTTON_13,	DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_BUTTON_15,	DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_BUTTON_16,	DANCE_BUTTON_UP,	false },
			{ 0, JOY_BUTTON_14,	DANCE_BUTTON_DOWN,	false },
			{ 1, JOY_BUTTON_1,	DANCE_BUTTON_LEFT,	false },
			{ 1, JOY_BUTTON_3,	DANCE_BUTTON_RIGHT,	false },
			{ 1, JOY_BUTTON_4,	DANCE_BUTTON_UP,	false },
			{ 1, JOY_BUTTON_2,	DANCE_BUTTON_DOWN,	false },
			{ 0, JOY_BUTTON_5,	DANCE_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_6,	DANCE_BUTTON_UPRIGHT,	false },
			{ 1, JOY_BUTTON_7,	DANCE_BUTTON_UPLEFT,	false },
			{ 1, JOY_BUTTON_8,	DANCE_BUTTON_UPRIGHT,	false },
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_10,	GAME_BUTTON_START,	false },
			END_MARKER
		}
	},
	{
		"dance",
		"Psx Gamepad",
		"PSXPAD",
		{
			{ 0, JOY_LEFT,		DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_RIGHT,		DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_UP,		DANCE_BUTTON_UP,	false },
			{ 0, JOY_DOWN,		DANCE_BUTTON_DOWN,	false },
			{ 1, JOY_BUTTON_2,	DANCE_BUTTON_LEFT,	false },
			{ 1, JOY_BUTTON_1,	DANCE_BUTTON_RIGHT,	false },
			{ 1, JOY_BUTTON_4,	DANCE_BUTTON_UP,	false },
			{ 1, JOY_BUTTON_3,	DANCE_BUTTON_DOWN,	false },
			{ 0, JOY_BUTTON_7,	DANCE_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_5,	DANCE_BUTTON_UPRIGHT,	false },
			{ 1, JOY_BUTTON_8,	DANCE_BUTTON_UPLEFT,	false },
			{ 1, JOY_BUTTON_6,	DANCE_BUTTON_UPRIGHT,	false },
			{ 0, JOY_BUTTON_10,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_START,	false },
			END_MARKER
		}
	},
	{
		"dance",
		"XBOX Gamepad Plugin V0.01",
		"X-Box gamepad",
		{
			{ 0, JOY_LEFT,		DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_RIGHT,		DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_UP,		DANCE_BUTTON_UP,	false },
			{ 0, JOY_DOWN,		DANCE_BUTTON_DOWN,	false },
			{ 1, JOY_BUTTON_1,	DANCE_BUTTON_DOWN,	false },	// A
			{ 1, JOY_BUTTON_2,	DANCE_BUTTON_RIGHT,	false },	// B
			{ 1, JOY_BUTTON_3,	DANCE_BUTTON_LEFT,	false },	// X
			{ 1, JOY_BUTTON_4,	DANCE_BUTTON_UP,	false },	// Y
			{ 0, JOY_BUTTON_7,	DANCE_BUTTON_UPLEFT,	false },	// L shoulder
			{ 0, JOY_BUTTON_8,	DANCE_BUTTON_UPRIGHT,	false },	// R shoulder
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_START,	false },
			{ 0, JOY_BUTTON_10,	GAME_BUTTON_BACK,	false },
			END_MARKER
		}
	},
	{
		"dance",
		"0b43:0003", // The EMS USB2 doesn't provide a model string, so Linux 
			     // just gives us the VendorID and ModelID in hex.
		"EMS USB2",
		{
			// Player 1.
			{ 0, JOY_BUTTON_16,	DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_BUTTON_14,	DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_BUTTON_13,	DANCE_BUTTON_UP,	false },
			{ 0, JOY_BUTTON_15,	DANCE_BUTTON_DOWN,	false },
			{ 1, JOY_BUTTON_4,	DANCE_BUTTON_LEFT,	false },
			{ 1, JOY_BUTTON_2,	DANCE_BUTTON_RIGHT,	false },
			{ 1, JOY_BUTTON_1,	DANCE_BUTTON_UP,	false },
			{ 1, JOY_BUTTON_3,	DANCE_BUTTON_DOWN,	false },
			{ 0, JOY_BUTTON_7,	DANCE_BUTTON_UPLEFT,    false },
			{ 0, JOY_BUTTON_8,	DANCE_BUTTON_UPRIGHT,   false },
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_SELECT,	false },
			{ 0, JOY_BUTTON_10,	GAME_BUTTON_START,	false },
			{ 0, JOY_BUTTON_5,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_6,	GAME_BUTTON_COIN,	false },
			// Player 2.
			{ 0, JOY_BUTTON_32,	DANCE_BUTTON_LEFT,	true },
			{ 0, JOY_BUTTON_30,	DANCE_BUTTON_RIGHT,	true },
			{ 0, JOY_BUTTON_29,	DANCE_BUTTON_UP,	true },
			{ 0, JOY_BUTTON_31,	DANCE_BUTTON_DOWN,	true },
			{ 1, JOY_BUTTON_20,	DANCE_BUTTON_LEFT,	true },
			{ 1, JOY_BUTTON_18,	DANCE_BUTTON_RIGHT,	true },
			{ 1, JOY_BUTTON_17,	DANCE_BUTTON_UP,	true },
			{ 1, JOY_BUTTON_19,	DANCE_BUTTON_DOWN,	true },
			{ 0, JOY_BUTTON_23,	DANCE_BUTTON_UPRIGHT,	true },
			{ 0, JOY_BUTTON_24,	DANCE_BUTTON_UPLEFT,	true },
			{ 0, JOY_BUTTON_25,	GAME_BUTTON_SELECT,	true },
			{ 0, JOY_BUTTON_26,	GAME_BUTTON_START,	true },
			{ 0, JOY_BUTTON_21,	GAME_BUTTON_BACK,	true },
			{ 0, JOY_BUTTON_22,	GAME_BUTTON_COIN,	true },
			END_MARKER
		}
	},
	{
		"dance",
		"Dance ", //Notice extra space at end
		"LevelSix USB Pad (DDR638)",	// "DDR638" is the model number of the pad
		{
			{ 0, JOY_BUTTON_1,	DANCE_BUTTON_UP,	false },
			{ 0, JOY_BUTTON_2,	DANCE_BUTTON_DOWN,	false },
			{ 0, JOY_BUTTON_3,	DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_BUTTON_4,	DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_BUTTON_7,	DANCE_BUTTON_UPRIGHT,	false },
			{ 0, JOY_BUTTON_8,	DANCE_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_10,	GAME_BUTTON_START,	false },
			END_MARKER
		}
	},
	{
		"dance",
		"SmartJoy PLUS Adapter",
		"SmartJoy PLUS Adapter",
		{
			{ 0, JOY_LEFT,		/* dpad L */	DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_RIGHT,		/* dpad R */	DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_UP,		/* dpad U */	DANCE_BUTTON_UP,	false },
			{ 0, JOY_DOWN,		/* dpad D */	DANCE_BUTTON_DOWN,	false },
			{ 1, JOY_BUTTON_4,	/* Square */	DANCE_BUTTON_LEFT,	false },
			{ 1, JOY_BUTTON_2,	/* Circle */	DANCE_BUTTON_RIGHT,	false },
			{ 1, JOY_BUTTON_1,	/* Tri */	DANCE_BUTTON_UP,	false },
			{ 1, JOY_BUTTON_3,	/* X */		DANCE_BUTTON_DOWN,	false },
			{ 0, JOY_BUTTON_7,	/* L1 */	DANCE_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_8,	/* R1 */	DANCE_BUTTON_UPRIGHT,	false },
			{ 0, JOY_BUTTON_10,	/* Select */	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_9,	/* Start */	GAME_BUTTON_START,	false },
			{ 0, JOY_BUTTON_5,	/* R1 */	GAME_BUTTON_SELECT,	false },
			{ 0, JOY_BUTTON_6,	/* R2 */	GAME_BUTTON_COIN,	false },
			END_MARKER
		}
	},
	{
		"dance",
		"RedOctane USB Pad|XBOX DDR", // "RedOctane USB Pad" is Ignition 3s and newer Afterburners.  "XBOX DDR" is older Afterburners.
		"RedOctane Ignition 3 or Afterburner",
		{
			{ 0, JOY_BUTTON_1,	/* dpad L */	DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_BUTTON_4,	/* dpad R */	DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_BUTTON_3,	/* dpad U */	DANCE_BUTTON_UP,	false },
			{ 0, JOY_BUTTON_2,	/* dpad D */	DANCE_BUTTON_DOWN,	false },
			//{ 0, JOY_BUTTON_5,	/* Tri */	GAME_BUTTON_BACK,	false },
			//{ 0, JOY_BUTTON_6,	/* Square */	GAME_BUTTON_BACK,	false },
			//{ 1, JOY_BUTTON_7,	/* X */		GAME_BUTTON_START,	false },
			{ 0, JOY_BUTTON_8,	/* O */		GAME_BUTTON_START,	false },
			{ 1, JOY_BUTTON_9,	/* Start */	GAME_BUTTON_START,	false },
			{ 0, JOY_BUTTON_10,	/* Sel */	GAME_BUTTON_BACK,	false },
			END_MARKER
		}
	},
	{
		"dance",
		"Joypad to USB converter",
		"EMS Trio Linker",
		{
			{ 0, JOY_BUTTON_16,	/* dpad L */	DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_BUTTON_14,	/* dpad R */	DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_BUTTON_13,	/* dpad U */	DANCE_BUTTON_UP,	false },
			{ 0, JOY_BUTTON_15,	/* dpad D */	DANCE_BUTTON_DOWN,	false },
			//{ 0, JOY_BUTTON_5,	/* Tri */	GAME_BUTTON_BACK,	false },
			//{ 0, JOY_BUTTON_6,	/* Square */	GAME_BUTTON_BACK,	false },
			//{ 1, JOY_BUTTON_7,	/* X */		GAME_BUTTON_START,	false },
			{ 0, JOY_BUTTON_2,	/* O */		GAME_BUTTON_START,	false },
			{ 1, JOY_BUTTON_10,	/* Start */	GAME_BUTTON_START,	false },
			{ 0, JOY_BUTTON_9,	/* Sel */	GAME_BUTTON_BACK,	false },
			END_MARKER
		}
	},
	{
		"dance",
		"Positive Gaming Impact USB pad",
		"Positive Gaming Impact USB pad",
		{
			{ 0, JOY_BUTTON_1,	/* dpad L */	DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_BUTTON_4,	/* dpad R */	DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_BUTTON_3,	/* dpad U */	DANCE_BUTTON_UP,	false },
			{ 0, JOY_BUTTON_2,	/* dpad D */	DANCE_BUTTON_DOWN,	false },
			//{ 0, JOY_BUTTON_5,	/* Tri */	GAME_BUTTON_BACK,	false },
			//{ 0, JOY_BUTTON_6,	/* Square */	GAME_BUTTON_BACK,	false },
			//{ 1, JOY_BUTTON_7,	/* X */		GAME_BUTTON_START,	false },
			{ 0, JOY_BUTTON_8,	/* O */		GAME_BUTTON_START,	false },
			{ 1, JOY_BUTTON_9,	/* Start */	GAME_BUTTON_START,	false },
			{ 0, JOY_BUTTON_10,	/* Sel */	GAME_BUTTON_BACK,	false },
			END_MARKER
		}
	},
	{
		"dance",
		"USB Dance Pad",
		"DDRGame Energy Dance Pad",
		{
			{ 0, JOY_BUTTON_13,	DANCE_BUTTON_UP,	false },
			{ 0, JOY_BUTTON_15,	DANCE_BUTTON_DOWN,	false },
			{ 0, JOY_BUTTON_16,	DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_BUTTON_14,	DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_BUTTON_3,	DANCE_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_2,	DANCE_BUTTON_UPRIGHT,	false },
			//{ 0, JOY_BUTTON_1,	DANCE_BUTTON_DOWNLEFT,	false },
			//{ 0, JOY_BUTTON_4,	DANCE_BUTTON_DOWNRIGHT,	false },
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_10,	GAME_BUTTON_START,	false },
			END_MARKER
		}
	},
	{
		"dance",
		"Dual USB Vibration Joystick",
		"PC Multi Hub Double Power Box 4",
		{
			{ 0, JOY_BUTTON_13,	DANCE_BUTTON_UP,	false },
			{ 0, JOY_BUTTON_15,	DANCE_BUTTON_DOWN,	false },
			{ 0, JOY_BUTTON_16,	DANCE_BUTTON_LEFT,	false },
			{ 0, JOY_BUTTON_14,	DANCE_BUTTON_RIGHT,	false },
			{ 0, JOY_BUTTON_3,	DANCE_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_2,	DANCE_BUTTON_UPRIGHT,	false },
			//{ 0, JOY_BUTTON_1,	DANCE_BUTTON_DOWNLEFT,	false },
			//{ 0, JOY_BUTTON_4,	DANCE_BUTTON_DOWNRIGHT,	false },
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_10,	GAME_BUTTON_START,	false },
			END_MARKER
		}
	},
	{
		"dance",
		"Controller \\(Harmonix Drum Kit for Xbox 360\\)",
		"Rock Band drum controller (Xbox 360, Windows driver)",
		{
			{ 0, JOY_BUTTON_3,	DANCE_BUTTON_UP,	false },	// blue drum
			{ 0, JOY_BUTTON_4,	DANCE_BUTTON_DOWN,	false },	// yellow drum
			{ 0, JOY_BUTTON_2,	DANCE_BUTTON_LEFT,	false },	// red drum
			{ 0, JOY_BUTTON_1,	DANCE_BUTTON_RIGHT,	false },	// green drum
			{ 0, JOY_HAT_LEFT,	GAME_BUTTON_MENULEFT,	false },	// d-pad left
			{ 0, JOY_HAT_RIGHT,	GAME_BUTTON_MENURIGHT,	false },	// d-pad right
			{ 0, JOY_HAT_UP,	GAME_BUTTON_MENUUP,	false },	// d-pad up
			{ 0, JOY_HAT_DOWN,	GAME_BUTTON_MENUDOWN,	false },	// d-pad down
			{ 0, JOY_BUTTON_8,	GAME_BUTTON_START,	false },	// start button
			{ 0, JOY_BUTTON_7,	GAME_BUTTON_BACK,	false },	// back button
			END_MARKER
		}
	},
	{
		"pump",
		"Pump USB",
		"Pump USB pad",
		{
			{ 0, JOY_BUTTON_1,	PUMP_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_2,	PUMP_BUTTON_UPRIGHT,	false },
			{ 0, JOY_BUTTON_3,	PUMP_BUTTON_CENTER,	false },
			{ 0, JOY_BUTTON_4,	PUMP_BUTTON_DOWNLEFT,	false },
			{ 0, JOY_BUTTON_5,	PUMP_BUTTON_DOWNRIGHT,	false },
			{ 0, JOY_BUTTON_6,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_7,	PUMP_BUTTON_UPLEFT,	true },
			{ 0, JOY_BUTTON_8,	PUMP_BUTTON_UPRIGHT,	true },
			{ 0, JOY_BUTTON_9,	PUMP_BUTTON_CENTER,	true },
			{ 0, JOY_BUTTON_10,	PUMP_BUTTON_DOWNLEFT,	true },
			{ 0, JOY_BUTTON_11,	PUMP_BUTTON_DOWNRIGHT,	true },
			END_MARKER
		}
	},
	{
		"pump",
		"GamePad Pro USB ",	// yes, there is a space at the end
		"GamePad Pro USB",
		{
			{ 0, JOY_BUTTON_5,	PUMP_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_6,	PUMP_BUTTON_UPRIGHT,	false },
			{ 0, JOY_BUTTON_7,	PUMP_BUTTON_DOWNLEFT,	false },
			{ 0, JOY_BUTTON_8,	PUMP_BUTTON_DOWNRIGHT,	false },
			{ 0, JOY_LEFT,		GAME_BUTTON_MENULEFT,	false },
			{ 0, JOY_RIGHT,		GAME_BUTTON_MENURIGHT,	false },
			{ 0, JOY_UP,		GAME_BUTTON_MENUUP,	false },
			{ 0, JOY_DOWN,		GAME_BUTTON_MENUDOWN,	false },
			{ 1, JOY_BUTTON_1,	PUMP_BUTTON_CENTER,	false },
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_10,	GAME_BUTTON_START,	false },
			END_MARKER
		}
	},
	{
		"pump",
		"Controller (Harmonix Drum Kit for Xbox 360)",
		"Rock Band drum controller (Xbox 360, Windows driver)",
		{
			{ 0, JOY_BUTTON_5,	PUMP_BUTTON_CENTER,	false },	// bass pedal
			{ 0, JOY_BUTTON_3,	PUMP_BUTTON_UPRIGHT,	false },	// blue drum
			{ 0, JOY_BUTTON_4,	PUMP_BUTTON_UPLEFT,	false },	// yellow drum
			{ 0, JOY_BUTTON_2,	PUMP_BUTTON_DOWNLEFT,	false },	// red drum
			{ 0, JOY_BUTTON_1,	PUMP_BUTTON_DOWNRIGHT,	false },	// green drum
			{ 0, JOY_HAT_LEFT,	GAME_BUTTON_MENULEFT,	false },	// d-pad left
			{ 0, JOY_HAT_RIGHT,	GAME_BUTTON_MENURIGHT,	false },	// d-pad right
			{ 0, JOY_HAT_UP,	GAME_BUTTON_MENUUP,	false },	// d-pad up
			{ 0, JOY_HAT_DOWN,	GAME_BUTTON_MENUDOWN,	false },	// d-pad down
			{ 0, JOY_BUTTON_8,	GAME_BUTTON_START,	false },	// start button
			{ 0, JOY_BUTTON_7,	GAME_BUTTON_BACK,	false },	// back button
			END_MARKER
		}
	},
	{
		"para",
		"ParaParaParadise Controller",
		"ParaParaParadise Controller",
		{
			{ 0, JOY_BUTTON_5,	PARA_BUTTON_LEFT,	false },
			{ 0, JOY_BUTTON_4,	PARA_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_3,	PARA_BUTTON_UP,		false },
			{ 0, JOY_BUTTON_2,	PARA_BUTTON_UPRIGHT,	false },
			{ 0, JOY_BUTTON_1,	PARA_BUTTON_RIGHT,	false },
			{ 0, JOY_BUTTON_10,	GAME_BUTTON_START,	false },
			{ 0, JOY_BUTTON_11,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_12,	GAME_BUTTON_MENULEFT,	false },
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_MENURIGHT,	false },
			END_MARKER
		}
	},
	{
		"techno",
		"Dance ", //Notice the extra space at end
		"LevelSix USB Pad (DDR638)",	// "DDR638" is the model number of the pad
		{
			{ 0, JOY_BUTTON_1,	TECHNO_BUTTON_UP,	false },
			{ 0, JOY_BUTTON_2,	TECHNO_BUTTON_DOWN,	false },
			{ 0, JOY_BUTTON_3,	TECHNO_BUTTON_LEFT,	false },
			{ 0, JOY_BUTTON_4,	TECHNO_BUTTON_RIGHT,	false },
			{ 0, JOY_BUTTON_5,	TECHNO_BUTTON_DOWNRIGHT,false },
			{ 0, JOY_BUTTON_6,	TECHNO_BUTTON_DOWNLEFT,	false },
			{ 0, JOY_BUTTON_7,	TECHNO_BUTTON_UPRIGHT,	false },
			{ 0, JOY_BUTTON_8,	TECHNO_BUTTON_UPLEFT,	false },
			{ 0, JOY_BUTTON_9,	GAME_BUTTON_BACK,	false },
			{ 0, JOY_BUTTON_10,	GAME_BUTTON_START,	false },
			END_MARKER
		}
	},
};

void InputMapper::Unmap( InputDevice id )
{
	FOREACH_GameController( i )
	{
		FOREACH_GameButton(j)
		{
			for( int k=0; k<NUM_USER_GAME_TO_DEVICE_SLOTS; k++ )
			{
				DeviceInput &di = m_GItoDI[i][j][k];
				if( di.device == id )
					di.MakeInvalid();
			}
		}
	}

	UpdateTempDItoGI();
}

void InputMapper::ApplyMapping( const InputMapping *maps, GameController gc, InputDevice id )
{
	map<GameInput, int> MappedButtons;

	for( int k=0; !maps[k].IsEndMarker(); k++ )
	{
		GameController map_gc = gc;
		if( maps[k].SecondController )
		{
			map_gc = (GameController)(map_gc+1);

			/* If that pushed it over, then it's a second controller for a joystick
			 * that's already a second controller, so we'll just ignore it.  (This
			 * can happen if eg. two primary Pump pads are connected.) */
			if( map_gc >= NUM_GameController )
				continue;
		}

		DeviceInput di( id, maps[k].deviceButton );
		GameInput gi( map_gc, maps[k].gb );
		int iSlot = MappedButtons[gi];
		++MappedButtons[gi];
		SetInputMap( di, gi, iSlot );//maps[k].iSlotIndex );
	}
}

void InputMapper::AutoMapJoysticksForCurrentGame()
{
	vector<InputDeviceInfo> vDevices;
	INPUTMAN->GetDevicesAndDescriptions(vDevices);

	int iNumJoysticksMapped = 0;

	for( unsigned i=0; i<vDevices.size(); i++ )
	{
		InputDevice id = vDevices[i].id;
		const RString &sDescription = vDevices[i].sDesc;
		for( unsigned j=0; j<ARRAYLEN(g_AutoJoyMappings); j++ )
		{
			const AutoJoyMapping& mapping = g_AutoJoyMappings[j];

			if( RString(m_pInputScheme->m_szName).CompareNoCase(mapping.szGame) )
				continue;	// games don't match

			RString sDriverRegex = mapping.szDriverRegex;
			Regex regex( sDriverRegex );
			if( !regex.Compare(sDescription) )
				continue;	// driver names don't match

			//
			// We have a mapping for this joystick
			//
			GameController gc = (GameController)iNumJoysticksMapped;
			if( gc >= NUM_GameController )
				break;	// stop mapping.  We already mapped one device for each game controller.

			LOG->Info( "Applying default joystick mapping #%d for device '%s' (%s)",
				iNumJoysticksMapped+1, mapping.szDriverRegex, mapping.szControllerName );

			Unmap( id );
			ApplyMapping( mapping.maps, gc, id );

			iNumJoysticksMapped++;
		}
	}
}

void InputMapper::SetInputScheme( const InputScheme *pInputScheme )
{
	m_pInputScheme = pInputScheme;

	ReadMappingsFromDisk();
}

const InputScheme *InputMapper::GetInputScheme() const
{
	return m_pInputScheme;
}

static const RString DEVICE_INPUT_SEPARATOR = ":";	// this isn't used in any key names

void InputMapper::ReadMappingsFromDisk()
{
	ClearAllMappings();

	IniFile ini;
	if( !ini.ReadFile( SpecialFiles::KEYMAPS_PATH ) )
		LOG->Trace( "Couldn't open mapping file \"%s\": %s.",
					SpecialFiles::KEYMAPS_PATH.c_str(), ini.GetError().c_str() );

	const XNode *Key = ini.GetChild( m_pInputScheme->m_szName );

	if( Key  )
	{
		FOREACH_CONST_Attr( Key, i )
		{
			const RString &name = i->first;
			RString value;
			i->second->GetValue( value );

			GameInput GameI;
			GameI.FromString( m_pInputScheme, name );

			vector<RString> sDeviceInputStrings;
			split( value, DEVICE_INPUT_SEPARATOR, sDeviceInputStrings, false );

			for( unsigned i=0; i<sDeviceInputStrings.size() && i<unsigned(NUM_GAME_TO_DEVICE_SLOTS); i++ )
			{
				DeviceInput DeviceI;
				DeviceI.FromString( sDeviceInputStrings[i] );
				if( DeviceI.IsValid() )
					SetInputMap( DeviceI, GameI, i );
			}
		}
	}

	AddDefaultMappingsForCurrentGameIfUnmapped();
}

void InputMapper::SaveMappingsToDisk()
{
	IniFile ini;
	ini.ReadFile( SpecialFiles::KEYMAPS_PATH );
	
	// erase the key so that we overwrite everything for this game
	ini.DeleteKey( m_pInputScheme->m_szName );

	XNode *pKey = ini.GetChild( m_pInputScheme->m_szName );
	if( pKey != NULL )
		ini.RemoveChild( pKey );
	pKey = ini.AppendChild( m_pInputScheme->m_szName );

	// iterate over our input map and write all mappings to the ini file
	FOREACH_GameController( i )
	{
		FOREACH_GameButtonInScheme( m_pInputScheme, j )
		{
			GameInput GameI( i, j );
			RString sNameString = GameI.ToString( m_pInputScheme );
			
			vector<RString> asValues;
			for( int slot = 0; slot < NUM_USER_GAME_TO_DEVICE_SLOTS; ++slot )	// don't save data from the last (keyboard automap) slot
				asValues.push_back( m_GItoDI[i][j][slot].ToString() );
		
			while( asValues.size() && asValues.back() == "" )
				asValues.erase( asValues.begin()+asValues.size()-1 );
			
			RString sValueString = join( DEVICE_INPUT_SEPARATOR, asValues );

			pKey->AppendAttr( sNameString, sValueString );
		}
	}

	ini.WriteFile( SpecialFiles::KEYMAPS_PATH );
}

static LocalizedString CONNECTED			( "InputMapper", "Connected" );
static LocalizedString DISCONNECTED			( "InputMapper", "Disconnected" );
static LocalizedString AUTOMAPPING_ALL_JOYSTICKS	( "InputMapper", "Auto-mapping all joysticks." );
bool InputMapper::CheckForChangedInputDevicesAndRemap( RString &sMessageOut )
{
	// Only check for changes in joysticks since that's all we know how to remap.

	// update last seen joysticks
	vector<InputDeviceInfo> vDevices;
	INPUTMAN->GetDevicesAndDescriptions( vDevices );

	// Strip non-joysticks.
	vector<RString> vsLastSeenJoysticks;
	split( g_sLastSeenInputDevices, ",", vsLastSeenJoysticks );

	vector<RString> vsCurrent;
	vector<RString> vsCurrentJoysticks;
	for( int i=vDevices.size()-1; i>=0; i-- )
	{
		vsCurrent.push_back( vDevices[i].sDesc );
		if( IsJoystick(vDevices[i].id) )
		{
			vsCurrentJoysticks.push_back( vDevices[i].sDesc );
		}
		else
		{
			vector<RString>::iterator iter = find( vsLastSeenJoysticks.begin(), vsLastSeenJoysticks.end(), vDevices[i].sDesc );
			if( iter != vsLastSeenJoysticks.end() )
				vsLastSeenJoysticks.erase( iter );
		}
	}


	bool bJoysticksChanged = vsCurrentJoysticks != vsLastSeenJoysticks;
	if( !bJoysticksChanged )
		return false;

	vector<RString> vsConnects, vsDisconnects;
	GetConnectsDisconnects( vsLastSeenJoysticks, vsCurrentJoysticks, vsDisconnects, vsConnects );

	sMessageOut = RString();
	if( !vsConnects.empty() )
		sMessageOut += CONNECTED.GetValue()+": " + join( "\n", vsConnects ) + "\n";
	if( !vsDisconnects.empty() )
		sMessageOut += DISCONNECTED.GetValue()+": " + join( "\n", vsDisconnects ) + "\n";

	if( g_bAutoMapOnJoyChange )
	{
		sMessageOut += AUTOMAPPING_ALL_JOYSTICKS.GetValue();
		AutoMapJoysticksForCurrentGame();
		SaveMappingsToDisk();
		MESSAGEMAN->Broadcast( Message_AutoJoyMappingApplied );
	}

	LOG->Info( sMessageOut );

	g_sLastSeenInputDevices.Set( join(",",vsCurrent) );
	PREFSMAN->SavePrefsToDisk();

	return true;
}

void InputMapper::SetInputMap( const DeviceInput &DeviceI, const GameInput &GameI, int iSlotIndex )
{
	// remove the old input
	ClearFromInputMap( DeviceI );
	ClearFromInputMap( GameI, iSlotIndex );
	
	ASSERT_M( GameI.controller < NUM_GameController,
		  ssprintf("controller: %u >= %u", GameI.controller, NUM_GameController) );
	ASSERT_M( GameI.button < NUM_GameButton,
		  ssprintf("button: %u >= %u", GameI.button, NUM_GameButton) );
	ASSERT_M( iSlotIndex < NUM_GAME_TO_DEVICE_SLOTS,
		  ssprintf("slot: %u >= %u", iSlotIndex, NUM_GAME_TO_DEVICE_SLOTS) );
	m_GItoDI[GameI.controller][GameI.button][iSlotIndex] = DeviceI;


	UpdateTempDItoGI();
}

void InputMapper::ClearFromInputMap( const DeviceInput &DeviceI )
{
	// search for where this DeviceI maps to

	FOREACH_GameController( p )
	{
		FOREACH_GameButton(b)
		{
			for( int s=0; s<NUM_GAME_TO_DEVICE_SLOTS; s++ )
			{
				if( m_GItoDI[p][b][s] == DeviceI )
					m_GItoDI[p][b][s].MakeInvalid();
			}
		}
	}
	
	UpdateTempDItoGI();
}

bool InputMapper::ClearFromInputMap( const GameInput &GameI, int iSlotIndex )
{
	if( !GameI.IsValid() )
		return false;

	DeviceInput &di = m_GItoDI[GameI.controller][GameI.button][iSlotIndex];
	if( !di.IsValid() )
		return false;
	di.MakeInvalid();

	UpdateTempDItoGI();
	return true;
}

bool InputMapper::IsMapped( const DeviceInput &DeviceI )
{
	return g_tempDItoGI.find(DeviceI) != g_tempDItoGI.end();
}

void InputMapper::UpdateTempDItoGI()
{
	// repopulate g_tempDItoGI
	g_tempDItoGI.clear();
	FOREACH_GameController( n )
	{
		FOREACH_GameButton( b )
		{
			for( int s=0; s<NUM_GAME_TO_DEVICE_SLOTS; s++ )
			{
				const DeviceInput &DeviceI = m_GItoDI[n][b][s];
				if( !DeviceI.IsValid() )
					continue;

				g_tempDItoGI[DeviceI] = GameInput( n, b );
			}
		}
	}
}

bool InputMapper::DeviceToGame( const DeviceInput &DeviceI, GameInput& GameI ) // return true if there is a mapping from device to pad
{
	GameI = g_tempDItoGI[DeviceI];
	return GameI.controller != GameController_Invalid;
}

bool InputMapper::GameToDevice( const GameInput &GameI, int iSlotNum, DeviceInput& DeviceI )	// return true if there is a mapping from pad to device
{
	DeviceI = m_GItoDI[GameI.controller][GameI.button][iSlotNum];
	return DeviceI.device != InputDevice_Invalid;
}

PlayerNumber InputMapper::ControllerToPlayerNumber( GameController controller )
{
	if( controller == GameController_Invalid )
		return PLAYER_INVALID;
	else if( g_JoinControllers != PLAYER_INVALID )
		return g_JoinControllers;
	else
		return (PlayerNumber) controller;
}

GameButton InputMapper::GameButtonToMenuButton( GameButton gb )
{
	return m_pInputScheme->GameButtonToMenuButton( gb );
}

/* If set (not PLAYER_INVALID), inputs from both GameControllers will be mapped
 * to the specified player.  If PLAYER_INVALID, GameControllers will be mapped
 * individually. */
void InputMapper::SetJoinControllers( PlayerNumber pn )
{
	g_JoinControllers = pn;
}


void InputMapper::MenuToGame( GameButton MenuI, PlayerNumber pn, vector<GameInput> &GameIout )
{
	if( g_JoinControllers != PLAYER_INVALID )
		pn = PLAYER_INVALID;

	m_pInputScheme->MenuButtonToGameInputs( MenuI, pn, GameIout );
}


bool InputMapper::IsBeingPressed( const GameInput &GameI, MultiPlayer mp, const DeviceInputList *pButtonState )
{
	for( int i=0; i<NUM_GAME_TO_DEVICE_SLOTS; i++ )
	{
		DeviceInput DeviceI;

		if( GameToDevice( GameI, i, DeviceI ) )
		{
			if( mp != MultiPlayer_Invalid )
				DeviceI.device = MultiPlayerToInputDevice(mp);
			if( INPUTFILTER->IsBeingPressed(DeviceI, pButtonState) )
				return true;
		}
	}

	return false;
}

bool InputMapper::IsBeingPressed( GameButton MenuI, PlayerNumber pn )
{
	vector<GameInput> GameI;
	MenuToGame( MenuI, pn, GameI );
	for( size_t i=0; i<GameI.size(); i++ )
		if( IsBeingPressed(GameI[i]) )
			return true;

	return false;
}

void InputMapper::RepeatStopKey( const GameInput &GameI )
{
	for( int i=0; i<NUM_GAME_TO_DEVICE_SLOTS; i++ )
	{
		DeviceInput DeviceI;

		if( GameToDevice( GameI, i, DeviceI ) )
			INPUTFILTER->RepeatStopKey( DeviceI );
	}
}

void InputMapper::RepeatStopKey( GameButton MenuI, PlayerNumber pn )
{
	vector<GameInput> GameI;
	MenuToGame( MenuI, pn, GameI );
	for( size_t i=0; i<GameI.size(); i++ )
		RepeatStopKey( GameI[i] );
}

float InputMapper::GetSecsHeld( const GameInput &GameI, MultiPlayer mp )
{
	float fMaxSecsHeld = 0;

	for( int i=0; i<NUM_GAME_TO_DEVICE_SLOTS; i++ )
	{
		DeviceInput DeviceI;
		if( GameToDevice( GameI, i, DeviceI ) )
		{
			if( mp != MultiPlayer_Invalid )
				DeviceI.device = MultiPlayerToInputDevice(mp);
			fMaxSecsHeld = max( fMaxSecsHeld, INPUTFILTER->GetSecsHeld(DeviceI) );
		}
	}

	return fMaxSecsHeld;
}

float InputMapper::GetSecsHeld( GameButton MenuI, PlayerNumber pn )
{
	float fMaxSecsHeld = 0;

	vector<GameInput> GameI;
	MenuToGame( MenuI, pn, GameI );
	for( size_t i=0; i<GameI.size(); i++ )
		fMaxSecsHeld = max( fMaxSecsHeld, GetSecsHeld(GameI[i]) );

	return fMaxSecsHeld;
}

void InputMapper::ResetKeyRepeat( const GameInput &GameI )
{
	for( int i=0; i<NUM_GAME_TO_DEVICE_SLOTS; i++ )
	{
		DeviceInput DeviceI;
		if( GameToDevice( GameI, i, DeviceI ) )
			INPUTFILTER->ResetKeyRepeat( DeviceI );
	}
}

void InputMapper::ResetKeyRepeat( GameButton MenuI, PlayerNumber pn )
{
	vector<GameInput> GameI;
	MenuToGame( MenuI, pn, GameI );
	for( size_t i=0; i<GameI.size(); i++ )
		ResetKeyRepeat( GameI[i] );
}

float InputMapper::GetLevel( const GameInput &GameI )
{
	float fLevel = 0;
	for( int i=0; i<NUM_GAME_TO_DEVICE_SLOTS; i++ )
	{
		DeviceInput DeviceI;

		if( GameToDevice( GameI, i, DeviceI ) )
			fLevel = max( fLevel, INPUTFILTER->GetLevel(DeviceI) );
	}
	return fLevel;
}

float InputMapper::GetLevel( GameButton MenuI, PlayerNumber pn )
{
	vector<GameInput> GameI;
	MenuToGame( MenuI, pn, GameI );

	float fLevel = 0;
	for( size_t i=0; i<GameI.size(); i++ )
		fLevel = max( fLevel, GetLevel(GameI[i]) );

	return fLevel;
}

InputDevice InputMapper::MultiPlayerToInputDevice( MultiPlayer mp )
{
	if( mp == MultiPlayer_Invalid )
		return InputDevice_Invalid;
	return enum_add2( DEVICE_JOY1, mp );
}

MultiPlayer InputMapper::InputDeviceToMultiPlayer( InputDevice id )
{
	if( id == InputDevice_Invalid )
		return MultiPlayer_Invalid;
	return enum_add2( MultiPlayer_P1, id - DEVICE_JOY1 );
}

GameButton InputScheme::ButtonNameToIndex( const RString &sButtonName ) const
{
	for( GameButton gb=(GameButton) 0; gb<m_iButtonsPerController; gb=(GameButton)(gb+1) ) 
		if( stricmp(GetGameButtonName(gb), sButtonName) == 0 )
			return gb;

	return GameButton_Invalid;
}

void InputScheme::MenuButtonToGameInputs( GameButton MenuI, PlayerNumber pn, vector<GameInput> &GameIout ) const
{
	ASSERT( MenuI != GameButton_Invalid );

	vector<GameButton> aGameButtons;
	MenuButtonToGameButtons( MenuI, aGameButtons );
	FOREACH( GameButton, aGameButtons, gb )
	{
		if( pn == PLAYER_INVALID )
		{
			GameIout.push_back( GameInput(GAME_CONTROLLER_1, *gb) );
			GameIout.push_back( GameInput(GAME_CONTROLLER_2, *gb) );
		}
		else
		{
			GameIout.push_back( GameInput((GameController)pn, *gb) );
		}
	}
}

void InputScheme::MenuButtonToGameButtons( GameButton MenuI, vector<GameButton> &aGameButtons ) const
{
	ASSERT( MenuI != GameButton_Invalid );

	if( MenuI == GameButton_Invalid )
		return;

	FOREACH_GameButton(gb)
	{
		if( PREFSMAN->m_bOnlyDedicatedMenuButtons && gb >= GAME_BUTTON_NEXT )
			break;

		const GameButtonInfo *pGameButtonInfo = GetGameButtonInfo( gb );
		if( pGameButtonInfo->m_SecondaryMenuButton != MenuI )
			continue;
		aGameButtons.push_back( gb );
	}
}

GameButton InputScheme::GameButtonToMenuButton( GameButton gb ) const
{
	if( gb == GameButton_Invalid )
		return GameButton_Invalid;
	if( gb >= GAME_BUTTON_NEXT && PREFSMAN->m_bOnlyDedicatedMenuButtons )
		return GameButton_Invalid;
	return GetGameButtonInfo(gb)->m_SecondaryMenuButton;
}

static const InputScheme::GameButtonInfo g_CommonGameButtonInfo[] =
{
	{ "MenuLeft",	GAME_BUTTON_MENULEFT },
	{ "MenuRight",	GAME_BUTTON_MENURIGHT },
	{ "MenuUp",	GAME_BUTTON_MENUUP },
	{ "MenuDown",	GAME_BUTTON_MENUDOWN },
	{ "Start",	GAME_BUTTON_START },
	{ "Select",	GAME_BUTTON_SELECT },
	{ "Back",	GAME_BUTTON_BACK },
	{ "Coin",	GAME_BUTTON_COIN },
	{ "Operator",	GAME_BUTTON_OPERATOR },
	{ "EffectUp",	GAME_BUTTON_EFFECT_UP },
	{ "EffectDown",	GAME_BUTTON_EFFECT_DOWN },
};

const InputScheme::GameButtonInfo *InputScheme::GetGameButtonInfo( GameButton gb ) const
{
	COMPILE_ASSERT( GAME_BUTTON_NEXT == ARRAYLEN(g_CommonGameButtonInfo) );
	if( gb < GAME_BUTTON_NEXT )
		return &g_CommonGameButtonInfo[gb];
	else
		return &m_GameButtonInfo[gb-GAME_BUTTON_NEXT];
}

const char *InputScheme::GetGameButtonName( GameButton gb ) const
{
	if( gb == GameButton_Invalid )
		return "";
	return GetGameButtonInfo(gb)->m_szName;
}

/*
 * (c) 2001-2003 Chris Danford
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

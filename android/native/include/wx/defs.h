#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's <wx/defs.h>, for what the engine uses.
// The constants have wxWindows 2.4.2's values (include/wx/defs.h, wxWindows Library Licence):
// the engine and its data may hold them as numbers. A Unicode, non-debug build, as the engine's
// Windows library (src/platform/WxWidgets.h), with the MSW port's handle types; the message
// parameters are pointer-sized, as FAF's x64 build of wxWindows has them
// (dependencies/patches/wxwindows_2_4_2_faf_x64_pointers.patch).

#include <cstddef>
#include <cstdint>

#define wxMAJOR_VERSION 2
#define wxMINOR_VERSION 4
#define wxRELEASE_NUMBER 2

#define wxUSE_UNICODE 1
#define wxUSE_GUI 1
#define wxUSE_THREADS 1

#define WXDLLEXPORT
#define WXDLLEXPORT_DATA(decl) decl
#define WXDEBUG_NEW new
#define WXUNUSED(identifier)

using wxChar = wchar_t;
#define wxT(text) L##text
#define wxTRANSLATE(text) wxT(text)

using wxWindowID = int;
using wxCoord = int;
using wxInt8 = std::int8_t;
using wxUint8 = std::uint8_t;
using wxInt16 = std::int16_t;
using wxUint16 = std::uint16_t;
using wxInt32 = std::int32_t;
using wxUint32 = std::uint32_t;

// The MSW port's stand-ins for the Windows types. A handle is an unsigned long, as wxWindows
// declares it: 64 bits on Android, which hold an HWND of the message layer (include/win32).
using WXHWND = unsigned long;
using WXHANDLE = unsigned long;
using WXHICON = unsigned long;
using WXHFONT = unsigned long;
using WXHMENU = unsigned long;
using WXHBRUSH = unsigned long;
using WXHCURSOR = unsigned long;
using WXHBITMAP = unsigned long;
using WXHDC = unsigned long;
using WXHINSTANCE = void*;
using WXUINT = unsigned int;
using WXDWORD = unsigned long;
using WXWORD = unsigned short;
using WXMSG = void*;
using WXWidget = WXHWND;
using WXWPARAM = std::uint64_t;
using WXLPARAM = std::int64_t;

// A non-debug build: the checks compile to nothing, as in the engine's Windows library.
#define wxASSERT(cond) ((void)0)
#define wxASSERT_MSG(cond, msg) ((void)0)
#define wxFAIL ((void)0)
#define wxFAIL_MSG(msg) ((void)0)
#define wxCHECK_RET(cond, msg) \
  if (!(cond)) {               \
    return;                    \
  }
#define wxCHECK_MSG(cond, rc, msg) \
  if (!(cond)) {                   \
    return rc;                     \
  }

#define DECLARE_NO_COPY_CLASS(classname) \
private:                                 \
  classname(const classname&) = delete;  \
  classname& operator=(const classname&) = delete;

/// What Find and its kind return when there is nothing.
#define wxNOT_FOUND (-1)

// --- Ids -----------------------------------------------------------------------------------------

enum
{
  wxID_ANY = -1
};

enum
{
  wxID_SEPARATOR = -1
};

enum
{
  wxID_LOWEST = 4999,
  wxID_OPEN,
  wxID_CLOSE,
  wxID_NEW,
  wxID_SAVE,
  wxID_SAVEAS,
  wxID_REVERT,
  wxID_EXIT,
  wxID_UNDO,
  wxID_REDO,
  wxID_HELP,
  wxID_PRINT,
  wxID_PRINT_SETUP,
  wxID_PREVIEW,
  wxID_ABOUT,
  wxID_HELP_CONTENTS,
  wxID_HELP_COMMANDS,
  wxID_HELP_PROCEDURES,
  wxID_HELP_CONTEXT,
  wxID_CLOSE_ALL,
  wxID_CUT = 5030,
  wxID_COPY,
  wxID_PASTE,
  wxID_CLEAR,
  wxID_FIND,
  wxID_DUPLICATE,
  wxID_SELECTALL,
  wxID_FILE1 = 5050,
  wxID_FILE2,
  wxID_FILE3,
  wxID_FILE4,
  wxID_FILE5,
  wxID_FILE6,
  wxID_FILE7,
  wxID_FILE8,
  wxID_FILE9,
  wxID_OK = 5100,
  wxID_CANCEL,
  wxID_APPLY,
  wxID_YES,
  wxID_NO,
  wxID_STATIC,
  wxID_FORWARD,
  wxID_BACKWARD,
  wxID_DEFAULT,
  wxID_MORE,
  wxID_SETUP,
  wxID_RESET,
  wxID_CONTEXT_HELP,
  wxID_YESTOALL,
  wxID_NOTOALL,
  wxID_ABORT,
  wxID_RETRY,
  wxID_IGNORE,
  wxID_SYSTEM_MENU = 5200,
  wxID_CLOSE_FRAME,
  wxID_MOVE_FRAME,
  wxID_RESIZE_FRAME,
  wxID_MAXIMIZE_FRAME,
  wxID_ICONIZE_FRAME,
  wxID_RESTORE_FRAME,
  wxID_FILEDLGG = 5900,
  wxID_HIGHEST = 5999
};

// --- Orientations, sizer flags, borders ----------------------------------------------------------

enum wxOrientation
{
  wxHORIZONTAL = 0x0004,
  wxVERTICAL = 0x0008,
  wxBOTH = (wxVERTICAL | wxHORIZONTAL)
};

enum wxDirection
{
  wxLEFT = 0x0010,
  wxRIGHT = 0x0020,
  wxUP = 0x0040,
  wxDOWN = 0x0080,
  wxTOP = wxUP,
  wxBOTTOM = wxDOWN,
  wxNORTH = wxUP,
  wxSOUTH = wxDOWN,
  wxWEST = wxLEFT,
  wxEAST = wxRIGHT,
  wxALL = (wxUP | wxDOWN | wxRIGHT | wxLEFT)
};

enum wxAlignment
{
  wxALIGN_NOT = 0x0000,
  wxALIGN_CENTER_HORIZONTAL = 0x0100,
  wxALIGN_CENTRE_HORIZONTAL = wxALIGN_CENTER_HORIZONTAL,
  wxALIGN_LEFT = wxALIGN_NOT,
  wxALIGN_TOP = wxALIGN_NOT,
  wxALIGN_RIGHT = 0x0200,
  wxALIGN_BOTTOM = 0x0400,
  wxALIGN_CENTER_VERTICAL = 0x0800,
  wxALIGN_CENTRE_VERTICAL = wxALIGN_CENTER_VERTICAL,
  wxALIGN_CENTER = (wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL),
  wxALIGN_CENTRE = wxALIGN_CENTER,
  wxALIGN_MASK = 0x0f00
};

enum wxStretch
{
  wxSTRETCH_NOT = 0x0000,
  wxSHRINK = 0x1000,
  wxGROW = 0x2000,
  wxEXPAND = wxGROW,
  wxSHAPED = 0x4000,
  wxADJUST_MINSIZE = 0x8000,
  wxTILE = 0xc000
};

enum wxBorder
{
  wxBORDER_DEFAULT = 0,
  wxBORDER_NONE = 0x00200000,
  wxBORDER_STATIC = 0x01000000,
  wxBORDER_SIMPLE = 0x02000000,
  wxBORDER_RAISED = 0x04000000,
  wxBORDER_SUNKEN = 0x08000000,
  wxBORDER_DOUBLE = 0x10000000,
  wxBORDER_MASK = 0x1f200000
};

// --- Window styles -------------------------------------------------------------------------------

#define wxVSCROLL 0x80000000
#define wxHSCROLL 0x40000000
#define wxCAPTION 0x20000000
#define wxDOUBLE_BORDER wxBORDER_DOUBLE
#define wxSUNKEN_BORDER wxBORDER_SUNKEN
#define wxRAISED_BORDER wxBORDER_RAISED
#define wxBORDER wxBORDER_SIMPLE
#define wxSIMPLE_BORDER wxBORDER_SIMPLE
#define wxSTATIC_BORDER wxBORDER_STATIC
#define wxNO_BORDER wxBORDER_NONE
#define wxNO_3D 0x00800000
#define wxCLIP_CHILDREN 0x00400000
#define wxCLIP_SIBLINGS 0x20000000
#define wxTRANSPARENT_WINDOW 0x00100000
#define wxTAB_TRAVERSAL 0x00080000
#define wxWANTS_CHARS 0x00040000
#define wxRETAINED 0x00000000
#define wxNO_FULL_REPAINT_ON_RESIZE 0x00010000

#define wxWS_EX_VALIDATE_RECURSIVELY 0x00000001
#define wxWS_EX_BLOCK_EVENTS 0x00000002
#define wxWS_EX_TRANSIENT 0x00000004

#define wxSTAY_ON_TOP 0x8000
#define wxICONIZE 0x4000
#define wxMINIMIZE wxICONIZE
#define wxMAXIMIZE 0x2000
#define wxSYSTEM_MENU 0x0800
#define wxMINIMIZE_BOX 0x0400
#define wxMAXIMIZE_BOX 0x0200
#define wxTINY_CAPTION_HORIZ 0x0100
#define wxTINY_CAPTION_VERT 0x0080
#define wxRESIZE_BORDER 0x0040
#define wxDIALOG_NO_PARENT 0x0001
#define wxFRAME_NO_TASKBAR 0x0002
#define wxFRAME_TOOL_WINDOW 0x0004
#define wxFRAME_FLOAT_ON_PARENT 0x0008
#define wxFRAME_SHAPED 0x0010
#define wxDIALOG_MODAL 0x0020
#define wxDIALOG_MODELESS 0x0000
#define wxRESIZE_BOX wxMAXIMIZE_BOX
#define wxTHICK_FRAME wxRESIZE_BORDER

#define wxDEFAULT_FRAME_STYLE (wxSYSTEM_MENU | wxRESIZE_BORDER | wxMINIMIZE_BOX | wxMAXIMIZE_BOX | wxCAPTION | wxCLIP_CHILDREN)
// The MSW port's dialog style, which the engine was built against.
#define wxDEFAULT_DIALOG_STYLE (wxSYSTEM_MENU | wxCAPTION)

// Centre().
enum
{
  wxCENTRE = 0x0001,
  wxCENTER = wxCENTRE
};
#define wxCENTER_FRAME 0x0000
#define wxCENTRE_FRAME wxCENTER_FRAME
#define wxCENTRE_ON_SCREEN 0x0002
#define wxCENTER_ON_SCREEN wxCENTRE_ON_SCREEN

// SetSize().
#define wxSIZE_AUTO_WIDTH 0x0001
#define wxSIZE_AUTO_HEIGHT 0x0002
#define wxSIZE_AUTO (wxSIZE_AUTO_WIDTH | wxSIZE_AUTO_HEIGHT)
#define wxSIZE_USE_EXISTING 0x0000
#define wxSIZE_ALLOW_MINUS_ONE 0x0004
#define wxSIZE_NO_ADJUSTMENTS 0x0008

// --- Keys ----------------------------------------------------------------------------------------

enum wxKeyCode
{
  WXK_BACK = 8,
  WXK_TAB = 9,
  WXK_RETURN = 13,
  WXK_ESCAPE = 27,
  WXK_SPACE = 32,
  WXK_DELETE = 127,

  WXK_START = 300,
  WXK_LBUTTON,
  WXK_RBUTTON,
  WXK_CANCEL,
  WXK_MBUTTON,
  WXK_CLEAR,
  WXK_SHIFT,
  WXK_ALT,
  WXK_CONTROL,
  WXK_MENU,
  WXK_PAUSE,
  WXK_CAPITAL,
  WXK_PRIOR,
  WXK_NEXT,
  WXK_END,
  WXK_HOME,
  WXK_LEFT,
  WXK_UP,
  WXK_RIGHT,
  WXK_DOWN,
  WXK_SELECT,
  WXK_PRINT,
  WXK_EXECUTE,
  WXK_SNAPSHOT,
  WXK_INSERT,
  WXK_HELP,
  WXK_NUMPAD0,
  WXK_NUMPAD1,
  WXK_NUMPAD2,
  WXK_NUMPAD3,
  WXK_NUMPAD4,
  WXK_NUMPAD5,
  WXK_NUMPAD6,
  WXK_NUMPAD7,
  WXK_NUMPAD8,
  WXK_NUMPAD9,
  WXK_MULTIPLY,
  WXK_ADD,
  WXK_SEPARATOR,
  WXK_SUBTRACT,
  WXK_DECIMAL,
  WXK_DIVIDE,
  WXK_F1,
  WXK_F2,
  WXK_F3,
  WXK_F4,
  WXK_F5,
  WXK_F6,
  WXK_F7,
  WXK_F8,
  WXK_F9,
  WXK_F10,
  WXK_F11,
  WXK_F12,
  WXK_F13,
  WXK_F14,
  WXK_F15,
  WXK_F16,
  WXK_F17,
  WXK_F18,
  WXK_F19,
  WXK_F20,
  WXK_F21,
  WXK_F22,
  WXK_F23,
  WXK_F24,
  WXK_NUMLOCK,
  WXK_SCROLL,
  WXK_PAGEUP,
  WXK_PAGEDOWN,

  WXK_NUMPAD_SPACE,
  WXK_NUMPAD_TAB,
  WXK_NUMPAD_ENTER,
  WXK_NUMPAD_F1,
  WXK_NUMPAD_F2,
  WXK_NUMPAD_F3,
  WXK_NUMPAD_F4,
  WXK_NUMPAD_HOME,
  WXK_NUMPAD_LEFT,
  WXK_NUMPAD_UP,
  WXK_NUMPAD_RIGHT,
  WXK_NUMPAD_DOWN,
  WXK_NUMPAD_PRIOR,
  WXK_NUMPAD_PAGEUP,
  WXK_NUMPAD_NEXT,
  WXK_NUMPAD_PAGEDOWN,
  WXK_NUMPAD_END,
  WXK_NUMPAD_BEGIN,
  WXK_NUMPAD_INSERT,
  WXK_NUMPAD_DELETE,
  WXK_NUMPAD_EQUAL,
  WXK_NUMPAD_MULTIPLY,
  WXK_NUMPAD_ADD,
  WXK_NUMPAD_SEPARATOR,
  WXK_NUMPAD_SUBTRACT,
  WXK_NUMPAD_DECIMAL,
  WXK_NUMPAD_DIVIDE
};

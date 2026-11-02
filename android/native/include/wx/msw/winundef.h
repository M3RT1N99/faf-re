// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's <wx/msw/winundef.h>, which swaps the
// A/W macros of <windows.h> that clash with wx member names (CreateDialog, LoadMenu, GetCharWidth,
// FindWindow, ...) for inline functions. The message layer's <windows.h> (include/win32) defines
// none of them: there is nothing to undo.

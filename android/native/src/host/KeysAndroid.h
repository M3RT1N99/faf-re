#pragma once

// Android's keys as a PC keyboard's, for the host (GameHost.cpp): the Windows virtual key, the
// scan code of set 1 the key sends, and whether it is one of the extended (E0) keys. Windows
// names the letter, digit and punctuation keys by what they type, as Android's key codes do, so
// those follow the keyboard's layout; their scan codes are positions, which a physical keyboard's
// Linux scan code gives (KEY_ESC to KEY_F12 are set 1's own numbers).

#include <Windows.h>

struct WindowsKey
{
  UINT virtualKey = 0;
  UINT scanCode = 0;
  bool extended = false;
};

/// The PC key for Android's `keyCode` (AKEYCODE_*), with the event's `linuxScanCode` (0 for a
/// key without one, such as an on-screen keyboard's). False for a key a PC keyboard has not.
bool KeyFromAndroid(int keyCode, int linuxScanCode, WindowsKey* key);

/// The character a key's press types, as TranslateMessage makes it: `androidCharacter` is what
/// Android says, which for some keys is another character or none.
wchar_t KeyCharacter(const WindowsKey& key, int androidCharacter);

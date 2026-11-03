// Android's keys as a PC keyboard's (KeysAndroid.h).

#include "KeysAndroid.h"

#include <android/keycodes.h>

namespace
{
  struct Entry
  {
    int keyCode;
    UINT virtualKey;
    UINT scanCode; // set 1, at the US keyboard's position
    bool extended;
  };

  // VK_BACK is Backspace; Android's Back is the key that leaves, which a PC keyboard has as Esc.
  constexpr Entry kKeys[] = {
    {AKEYCODE_BACK, VK_ESCAPE, 0x01, false},
    {AKEYCODE_ESCAPE, VK_ESCAPE, 0x01, false},
    {AKEYCODE_0, '0', 0x0B, false},
    {AKEYCODE_1, '1', 0x02, false},
    {AKEYCODE_2, '2', 0x03, false},
    {AKEYCODE_3, '3', 0x04, false},
    {AKEYCODE_4, '4', 0x05, false},
    {AKEYCODE_5, '5', 0x06, false},
    {AKEYCODE_6, '6', 0x07, false},
    {AKEYCODE_7, '7', 0x08, false},
    {AKEYCODE_8, '8', 0x09, false},
    {AKEYCODE_9, '9', 0x0A, false},
    {AKEYCODE_A, 'A', 0x1E, false},
    {AKEYCODE_B, 'B', 0x30, false},
    {AKEYCODE_C, 'C', 0x2E, false},
    {AKEYCODE_D, 'D', 0x20, false},
    {AKEYCODE_E, 'E', 0x12, false},
    {AKEYCODE_F, 'F', 0x21, false},
    {AKEYCODE_G, 'G', 0x22, false},
    {AKEYCODE_H, 'H', 0x23, false},
    {AKEYCODE_I, 'I', 0x17, false},
    {AKEYCODE_J, 'J', 0x24, false},
    {AKEYCODE_K, 'K', 0x25, false},
    {AKEYCODE_L, 'L', 0x26, false},
    {AKEYCODE_M, 'M', 0x32, false},
    {AKEYCODE_N, 'N', 0x31, false},
    {AKEYCODE_O, 'O', 0x18, false},
    {AKEYCODE_P, 'P', 0x19, false},
    {AKEYCODE_Q, 'Q', 0x10, false},
    {AKEYCODE_R, 'R', 0x13, false},
    {AKEYCODE_S, 'S', 0x1F, false},
    {AKEYCODE_T, 'T', 0x14, false},
    {AKEYCODE_U, 'U', 0x16, false},
    {AKEYCODE_V, 'V', 0x2F, false},
    {AKEYCODE_W, 'W', 0x11, false},
    {AKEYCODE_X, 'X', 0x2D, false},
    {AKEYCODE_Y, 'Y', 0x15, false},
    {AKEYCODE_Z, 'Z', 0x2C, false},
    {AKEYCODE_GRAVE, VK_OEM_3, 0x29, false},
    {AKEYCODE_MINUS, VK_OEM_MINUS, 0x0C, false},
    {AKEYCODE_EQUALS, VK_OEM_PLUS, 0x0D, false},
    {AKEYCODE_LEFT_BRACKET, VK_OEM_4, 0x1A, false},
    {AKEYCODE_RIGHT_BRACKET, VK_OEM_6, 0x1B, false},
    {AKEYCODE_BACKSLASH, VK_OEM_5, 0x2B, false},
    {AKEYCODE_SEMICOLON, VK_OEM_1, 0x27, false},
    {AKEYCODE_APOSTROPHE, VK_OEM_7, 0x28, false},
    {AKEYCODE_COMMA, VK_OEM_COMMA, 0x33, false},
    {AKEYCODE_PERIOD, VK_OEM_PERIOD, 0x34, false},
    {AKEYCODE_SLASH, VK_OEM_2, 0x35, false},
    {AKEYCODE_SPACE, VK_SPACE, 0x39, false},
    {AKEYCODE_TAB, VK_TAB, 0x0F, false},
    {AKEYCODE_ENTER, VK_RETURN, 0x1C, false},
    {AKEYCODE_DEL, VK_BACK, 0x0E, false},
    {AKEYCODE_FORWARD_DEL, VK_DELETE, 0x53, true},
    {AKEYCODE_INSERT, VK_INSERT, 0x52, true},
    {AKEYCODE_MOVE_HOME, VK_HOME, 0x47, true},
    {AKEYCODE_MOVE_END, VK_END, 0x4F, true},
    {AKEYCODE_PAGE_UP, VK_PRIOR, 0x49, true},
    {AKEYCODE_PAGE_DOWN, VK_NEXT, 0x51, true},
    {AKEYCODE_DPAD_UP, VK_UP, 0x48, true},
    {AKEYCODE_DPAD_DOWN, VK_DOWN, 0x50, true},
    {AKEYCODE_DPAD_LEFT, VK_LEFT, 0x4B, true},
    {AKEYCODE_DPAD_RIGHT, VK_RIGHT, 0x4D, true},
    {AKEYCODE_SHIFT_LEFT, VK_SHIFT, 0x2A, false},
    {AKEYCODE_SHIFT_RIGHT, VK_SHIFT, 0x36, false},
    {AKEYCODE_CTRL_LEFT, VK_CONTROL, 0x1D, false},
    {AKEYCODE_CTRL_RIGHT, VK_CONTROL, 0x1D, true},
    {AKEYCODE_ALT_LEFT, VK_MENU, 0x38, false},
    {AKEYCODE_ALT_RIGHT, VK_MENU, 0x38, true},
    {AKEYCODE_META_LEFT, VK_LWIN, 0x5B, true},
    {AKEYCODE_META_RIGHT, VK_RWIN, 0x5C, true},
    {AKEYCODE_MENU, VK_APPS, 0x5D, true},
    {AKEYCODE_CAPS_LOCK, VK_CAPITAL, 0x3A, false},
    // Windows marks Num Lock extended, and Pause not, whatever the keyboard sends for them.
    {AKEYCODE_NUM_LOCK, VK_NUMLOCK, 0x45, true},
    {AKEYCODE_SCROLL_LOCK, VK_SCROLL, 0x46, false},
    {AKEYCODE_SYSRQ, VK_SNAPSHOT, 0x37, true},
    {AKEYCODE_BREAK, VK_PAUSE, 0x45, false},
    {AKEYCODE_F1, VK_F1, 0x3B, false},
    {AKEYCODE_F2, VK_F2, 0x3C, false},
    {AKEYCODE_F3, VK_F3, 0x3D, false},
    {AKEYCODE_F4, VK_F4, 0x3E, false},
    {AKEYCODE_F5, VK_F5, 0x3F, false},
    {AKEYCODE_F6, VK_F6, 0x40, false},
    {AKEYCODE_F7, VK_F7, 0x41, false},
    {AKEYCODE_F8, VK_F8, 0x42, false},
    {AKEYCODE_F9, VK_F9, 0x43, false},
    {AKEYCODE_F10, VK_F10, 0x44, false},
    {AKEYCODE_F11, VK_F11, 0x57, false},
    {AKEYCODE_F12, VK_F12, 0x58, false},
    {AKEYCODE_NUMPAD_0, VK_NUMPAD0, 0x52, false},
    {AKEYCODE_NUMPAD_1, VK_NUMPAD1, 0x4F, false},
    {AKEYCODE_NUMPAD_2, VK_NUMPAD2, 0x50, false},
    {AKEYCODE_NUMPAD_3, VK_NUMPAD3, 0x51, false},
    {AKEYCODE_NUMPAD_4, VK_NUMPAD4, 0x4B, false},
    {AKEYCODE_NUMPAD_5, VK_NUMPAD5, 0x4C, false},
    {AKEYCODE_NUMPAD_6, VK_NUMPAD6, 0x4D, false},
    {AKEYCODE_NUMPAD_7, VK_NUMPAD7, 0x47, false},
    {AKEYCODE_NUMPAD_8, VK_NUMPAD8, 0x48, false},
    {AKEYCODE_NUMPAD_9, VK_NUMPAD9, 0x49, false},
    {AKEYCODE_NUMPAD_DIVIDE, VK_DIVIDE, 0x35, true},
    {AKEYCODE_NUMPAD_MULTIPLY, VK_MULTIPLY, 0x37, false},
    {AKEYCODE_NUMPAD_SUBTRACT, VK_SUBTRACT, 0x4A, false},
    {AKEYCODE_NUMPAD_ADD, VK_ADD, 0x4E, false},
    {AKEYCODE_NUMPAD_DOT, VK_DECIMAL, 0x53, false},
    {AKEYCODE_NUMPAD_ENTER, VK_RETURN, 0x1C, true},
  };

  // The Linux key codes that are set 1's numbers: KEY_ESC (1) to KEY_F12 (88), the keys a
  // keyboard sends without the E0 prefix.
  constexpr int kLastSet1LinuxScanCode = 0x58;
} // namespace

bool KeyFromAndroid(const int keyCode, const int linuxScanCode, WindowsKey* const key)
{
  for (const Entry& entry : kKeys) {
    if (entry.keyCode != keyCode) {
      continue;
    }
    key->virtualKey = entry.virtualKey;
    key->scanCode = entry.scanCode;
    key->extended = entry.extended;
    // A physical key's own position. A key of the number pad that Num Lock turns into Home or an
    // arrow is the number pad's key, which is not an extended one, as on a PC.
    if (linuxScanCode >= 1 && linuxScanCode <= kLastSet1LinuxScanCode && entry.virtualKey != VK_NUMLOCK &&
        entry.virtualKey != VK_PAUSE) {
      key->scanCode = static_cast<UINT>(linuxScanCode);
      key->extended = false;
    }
    return true;
  }
  return false;
}

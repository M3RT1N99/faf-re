// The phone's screen keyboard for the game's text fields (android/README.md, Game). SDL would bring it whenever text input
// starts, which the engine starts in every window it makes (src/platform/sdl/WindowSdl.cpp), so SDL's own showing is off
// (GameMainAndroid.cpp). It comes as on the earlier host: when a finger's press lands on one of FAF's text fields (the
// lobby's chat, a profile's name, the console's line; MenuFingers.cpp's NoteTapOnField there), and for FAF's Menu tab's
// Keyboard (UiPackageAndroid.cpp). It is SDL's keyboard, whose keys come to the engine as a keyboard's, with their text.
// It goes by its own key and by Back, which it takes for itself, so the game gets no Esc of it; and after Enter, which
// goes to the game first.
//
// The window system's events reach this through the hook that src/platform/sdl/WindowSdlPlatform.h has for the developer
// windows, on the program's thread before the engine takes them: Android has one window, so no developer window takes it.
// The host (src/host/HostSdl.cpp) takes its tasks' wakes from it first, and gives a finger's press here itself, as the
// engine gets that press from the host and SDL never sees it (FingerPressed).

#include "ScreenKeyboardAndroid.h"

#include <exception>

#include <SDL3/SDL.h>

#include "../host/HostSdl.h"
#include "GameAndroid.h"
#include "gpg/core/utils/Logging.h"
#include "moho/ui/UiPrivate.h"
#include "platform/sdl/WindowSdlPlatform.h"

namespace
{
  /// Android's InputType.TYPE_NULL: the keyboard sends plain keys, as the earlier host's did, which SDL hands on with their
  /// text; no suggestion or correction of the keyboard's stands between a key and the game's field.
  constexpr Sint64 kPlainKeys = 0;

  using platform::android::GameWindow;

  // SDL shows and hides its keyboard with text input while its hint says so: on for the one call.

  void Show(SDL_Window* const window)
  {
    const SDL_PropertiesID properties = SDL_CreateProperties();
    (void)SDL_SetNumberProperty(properties, SDL_PROP_TEXTINPUT_ANDROID_INPUTTYPE_NUMBER, kPlainKeys);
    (void)SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "1");
    (void)SDL_StartTextInputWithProperties(window, properties);
    (void)SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
    SDL_DestroyProperties(properties);
  }

  void Hide(SDL_Window* const window)
  {
    (void)SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "1");
    (void)SDL_StopTextInput(window);
    (void)SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
    // Text goes on coming from a keyboard of the phone's own.
    (void)SDL_StartTextInput(window);
  }

  /// The control the engine's input goes to first: the top of its capture stack, else the UI's first frame.
  [[nodiscard]] moho::CMauiControl* InputRoot()
  {
    if (moho::CMauiControl* const capture = ResolveTopInputCaptureControl()) {
      return capture;
    }
    return moho::g_UIManager != nullptr && !moho::g_UIManager->mFrames.Empty() ? moho::g_UIManager->mFrames[0].get() : nullptr;
  }

  /// Whether a press here lands on one of FAF's text fields that shows and takes events. A field can share its depth with
  /// what lies under it, which then takes the press (the lobby's chat): it counts as deep as what the press hits, or above.
  [[nodiscard]] bool OnTextField(const float x, const float y)
  {
    moho::CMauiControl* const root = InputRoot();
    moho::CMauiControl* const top = root != nullptr ? moho::CMauiControl::GetTopmostControl(root, x, y) : nullptr;
    if (top == nullptr) {
      return false;
    }
    for (moho::CMauiControl* control = root; control != nullptr; control = control->DepthFirstSuccessor(root)) {
      if (dynamic_cast<moho::CMauiEdit*>(control) != nullptr && !control->IsHidden() && !control->IsHitTestDisabled() &&
          control->HitTest(x, y) && control->mDepth >= top->mDepth) {
        return true;
      }
    }
    return false;
  }

  /// A finger's press in the engine's pixels: the keyboard comes where it lands on a text field.
  void ShowForField(SDL_Window* const window, const float x, const float y)
  {
    if (window == nullptr || SDL_ScreenKeyboardShown(window)) {
      return;
    }
    try {
      if (OnTextField(x, y)) {
        Show(window);
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Screen keyboard: %s", exception.what());
    }
  }

  bool OnEvent(const void* const sdlEvent)
  {
    if (host::OnSdlEvent(sdlEvent)) {
      return true;
    }
    const SDL_Event& event = *static_cast<const SDL_Event*>(sdlEvent);
    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      // A finger's press, which SDL gives as the left button; a mouse's goes with a keyboard of its own.
      SDL_Window* const window = SDL_GetWindowFromID(event.button.windowID);
      if (event.button.which != SDL_TOUCH_MOUSEID || event.button.button != SDL_BUTTON_LEFT || window == nullptr) {
        break;
      }
      // The engine's pixels, as src/platform/sdl/WindowSdl.cpp takes them.
      const float density = SDL_GetWindowPixelDensity(window);
      const float scale = density > 0.0f ? density : 1.0f;
      ShowForField(window, event.button.x * scale, event.button.y * scale);
      break;
    }
    case SDL_EVENT_KEY_UP:
      // Enter went to the game as it went down: the chat has sent its line, the console has run it.
      if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
        SDL_Window* const window = SDL_GetWindowFromID(event.key.windowID);
        if (window != nullptr && SDL_ScreenKeyboardShown(window)) {
          Hide(window);
        }
      }
      break;
    case SDL_EVENT_SCREEN_KEYBOARD_HIDDEN:
      // Back took it away, and SDL stopped text input with it (SDLDummyEdit.onKeyPreIme; the patched
      // onNativeKeyboardFocusLost says so on every Android version): typed text goes on coming.
      if (SDL_Window* const window = GameWindow()) {
        (void)SDL_StartTextInput(window);
      }
      break;
    default:
      break;
    }
    return false;
  }
} // namespace

SDL_Window* platform::android::GameWindow()
{
  if (SDL_Window* const focus = SDL_GetKeyboardFocus()) {
    return focus;
  }
  int count = 0;
  SDL_Window** const windows = SDL_GetWindows(&count);
  SDL_Window* const first = windows != nullptr && count > 0 ? windows[0] : nullptr;
  SDL_free(windows);
  return first;
}

void screenkeyboard::Install()
{
  platform::window::sdl::SetToolEventHook(&OnEvent);
}

void screenkeyboard::Toggle()
{
  SDL_Window* const window = GameWindow();
  if (window == nullptr) {
    return;
  }
  if (SDL_ScreenKeyboardShown(window)) {
    Hide(window);
  } else {
    Show(window);
  }
}

bool screenkeyboard::Shown()
{
  SDL_Window* const window = GameWindow();
  return window != nullptr && SDL_ScreenKeyboardShown(window);
}

void screenkeyboard::Dismiss()
{
  SDL_Window* const window = GameWindow();
  if (window != nullptr && SDL_ScreenKeyboardShown(window)) {
    Hide(window);
  }
}

void screenkeyboard::FingerPressed(const float x, const float y)
{
  ShowForField(GameWindow(), x, y);
}

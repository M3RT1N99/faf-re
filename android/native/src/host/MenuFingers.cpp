// What FAF's UI and the touch layer tell each other about the fingers in the menus (android/TOUCH_UI.md). FAF's Lua declares a
// screen's page, whose the fingers are, the HUD's factor and what to bring into view, by four functions added to its UI's
// globals, and the layer hears of it in a task of its own (SetListener) and reads what is new. The phone's keyboard is SDL's
// (src/app/ScreenKeyboardAndroid.cpp), which a finger's tap on a text field brings. A finger that scrolls asks what list is
// under it, and a list takes its drag on the program's thread, where FAF's lists live, in the order of the rest of the input;
// what a list cannot take goes back to the layer for the page.

#include "MenuFingers.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>

#include <ListDrag.h>

#include "../app/ScreenKeyboardAndroid.h"
#include "HostSdl.h"
#include "TaskPlaces.h"
#include "gpg/core/utils/Logging.h"
#include "moho/lua/CScrLuaBinder.h"
#include "moho/ui/UiPrivate.h"

namespace
{
  using hosttask::Argument;

  // --- The touch layer --------------------------------------------------------------------------

  /// Who hears what is new (menufingers::SetListener).
  std::atomic<void (*)()> gHeard{nullptr};

  void HeardTask(const Argument)
  {
    if (void (*const heard)() = gHeard.load()) {
      heard();
    }
  }

  /// The layer reads what is new in a task of its own, after the task or the Lua that has it.
  void TellLayer()
  {
    host::RunNewestOnGameThread(&HeardTask, 0);
  }

  // --- What FAF's Lua declares ------------------------------------------------------------------

  using menufingers::Page;

  std::mutex gPageMutex;
  Page gPage;
  /// Whose the fingers are: the front end is the menus' from the start, and a session says otherwise.
  std::atomic<bool> gMenuFingers{true};
  /// The session's UI factor, for the app's buttons; 0: nothing has said yet.
  std::atomic<float> gHudFactor{0};

  constexpr char kSetPageHelp[] = "FafreSetPage([left, top, right, bottom]) - the picture's part the view fits to the screen's width, or none";
  constexpr char kSetFingersHelp[] = "FafreSetFingers(menu) - whether fingers act as in the menus or as in a game";
  constexpr char kSetHudHelp[] = "FafreSetHud(factor) - the UI factor of the session's HUD, by which the app places its buttons";
  constexpr char kShowLinesHelp[] = "FafreShowLines(top, bottom) - the page goes where the screen shows these lines of the picture";

  int SetPage(lua_State* const lua)
  {
    Page page;
    if (lua_gettop(lua) >= 4) {
      page.shown = true;
      page.left = static_cast<int>(lua_tonumber(lua, 1));
      page.top = static_cast<int>(lua_tonumber(lua, 2));
      page.right = static_cast<int>(lua_tonumber(lua, 3));
      page.bottom = static_cast<int>(lua_tonumber(lua, 4));
    }
    {
      const std::lock_guard<std::mutex> lock(gPageMutex);
      gPage = page;
    }
    TellLayer();
    return 0;
  }

  int SetFingers(lua_State* const lua)
  {
    const bool menu = lua_toboolean(lua, 1) != 0;
    if (gMenuFingers.exchange(menu) != menu) {
      TellLayer();
    }
    return 0;
  }

  int SetHud(lua_State* const lua)
  {
    gHudFactor.store(static_cast<float>(lua_tonumber(lua, 1)));
    return 0;
  }

  moho::CScrLuaBinder gSetPageBinder(moho::SCR_UserLuaInitSet(), "FafreSetPage", &SetPage, nullptr, "<global>", kSetPageHelp);
  moho::CScrLuaBinder gSetFingersBinder(moho::SCR_UserLuaInitSet(), "FafreSetFingers", &SetFingers, nullptr, "<global>", kSetFingersHelp);
  moho::CScrLuaBinder gSetHudBinder(moho::SCR_UserLuaInitSet(), "FafreSetHud", &SetHud, nullptr, "<global>", kSetHudHelp);

  // --- What is yet to be brought into view -------------------------------------------------------

  using menufingers::kFieldAskedFor;
  using menufingers::kFieldTapped;
  using menufingers::kLinesToShow;
  using menufingers::kNoPlace;
  using menufingers::ToShow;

  std::mutex gToShowMutex;
  ToShow gToShow;

  void PublishToShow(const ToShow& toShow)
  {
    {
      const std::lock_guard<std::mutex> lock(gToShowMutex);
      gToShow = toShow;
    }
    TellLayer();
  }

  int ShowLines(lua_State* const lua)
  {
    PublishToShow(ToShow{kLinesToShow, static_cast<int>(lua_tonumber(lua, 1)), static_cast<int>(lua_tonumber(lua, 2))});
    return 0;
  }

  moho::CScrLuaBinder gShowLinesBinder(moho::SCR_UserLuaInitSet(), "FafreShowLines", &ShowLines, nullptr, "<global>", kShowLinesHelp);


  // --- The keyboard -----------------------------------------------------------------------------

  constexpr char kKeyboardHelp[] = "FafreKeyboard() - the phone's keyboard comes, or goes where it is up";

  /// FAF's Menu tab's Keyboard (the hook of ui/game/tabs.lua) and the bar's Keys (fafre/bar.lua): SDL's keyboard comes, or
  /// goes where it is up; the text field with the focus comes into view above it (AskForField).
  int Keyboard(lua_State*)
  {
    if (!screenkeyboard::Shown()) {
      menufingers::AskForField();
    }
    screenkeyboard::Toggle();
    return 0;
  }

  moho::CScrLuaBinder gKeyboardBinder(moho::SCR_UserLuaInitSet(), "FafreKeyboard", &Keyboard, nullptr, "<global>", kKeyboardHelp);

  // --- The controls -----------------------------------------------------------------------------

  [[nodiscard]] moho::CMauiControl* InputRoot()
  {
    moho::CMauiControl* const capture = ResolveTopInputCaptureControl();
    if (capture != nullptr) {
      return capture;
    }
    return moho::g_UIManager != nullptr && !moho::g_UIManager->mFrames.Empty() ? moho::g_UIManager->mFrames[0].get() : nullptr;
  }

  [[nodiscard]] moho::CMauiControl* ControlAt(const float x, const float y)
  {
    moho::CMauiControl* const root = InputRoot();
    return root != nullptr ? moho::CMauiControl::GetTopmostControl(root, x, y) : nullptr;
  }

  /// Whether it has anywhere to go: some of FAF's lists say that they scroll whatever they hold, and an ItemList is the engine's,
  /// whose Lua answers IsScrollable with Control's false.
  [[nodiscard]] bool Scrolls(moho::CMauiControl* const control)
  {
    if (dynamic_cast<moho::CMauiItemList*>(control) == nullptr && !control->IsScrollable(moho::MSA_Vert)) {
      return false;
    }
    const moho::SMauiScrollValues values = control->GetScrollValues(moho::MSA_Vert);
    return values.mMaxRange > values.mMaxVisible || values.mMinVisible > values.mMinRange;
  }

  /// The first thing that scrolls from this control up, and for a scroll bar what it scrolls.
  [[nodiscard]] moho::CMauiControl* ScrollableFrom(moho::CMauiControl* control)
  {
    for (; control != nullptr; control = control->GetParent()) {
      if (auto* const bar = dynamic_cast<moho::CMauiScrollbar*>(control)) {
        moho::CMauiControl* const scrolled = bar->mScrollable.GetObjectPtr();
        if (scrolled != nullptr && Scrolls(scrolled)) {
          return scrolled;
        }
      }
      if (Scrolls(control)) {
        return control;
      }
    }
    return nullptr;
  }

  // --- A finger on a list -----------------------------------------------------------------------

  using menufingers::kList;
  using menufingers::kNoList;
  using menufingers::kNotKnownYet;

  /// The serial of the finger that has asked last, and the answer with the serial it is for.
  std::atomic<std::uint32_t> gListSerial{0};
  std::atomic<std::uint64_t> gListAnswer{0};
  /// What no list took, in the picture's pixels.
  std::mutex gPageDragMutex;
  float gPageDrag = 0;

  moho::WeakPtr<moho::CMauiControl> gList;
  listdrag::ListDrag gListDrag;

  void AddPageDrag(const float pixels)
  {
    {
      const std::lock_guard<std::mutex> lock(gPageDragMutex);
      gPageDrag += pixels;
    }
    TellLayer();
  }

  /// On the program's thread: what list a finger that has come down here would scroll.
  void LookForList(const Argument place)
  {
    gListDrag = listdrag::ListDrag{};
    moho::CMauiControl* list = nullptr;
    try {
      list = ScrollableFrom(ControlAt(static_cast<float>(hosttask::Coordinate(place, 0)), static_cast<float>(hosttask::Coordinate(place, 1))));
    } catch (const std::exception& exception) {
      gpg::Warnf("MenuFingers: %s", exception.what());
    }
    gList.ResetFromObject(list);
    const std::uint64_t serial = static_cast<std::uint32_t>(place >> 32);
    gListAnswer.store(serial << 8 | static_cast<std::uint64_t>(list != nullptr ? kList : kNoList));
    TellLayer();
  }

  /// On the program's thread: this much of a finger's drag, as a float's bits; the list takes what it can.
  void ScrollList(const Argument dragBits)
  {
    float drag = 0;
    std::memcpy(&drag, &dragBits, sizeof(drag));
    float rest = drag;
    try {
      if (moho::CMauiControl* const list = gList.GetObjectPtr()) {
        const moho::SMauiScrollValues values = list->GetScrollValues(moho::MSA_Vert);
        const float lines = values.mMaxVisible - values.mMinVisible;
        const float height = moho::CScriptLazyVar_float::GetValue(&list->mBottomLV) - moho::CScriptLazyVar_float::GetValue(&list->mTopLV);
        if (lines > 0 && height > 0) {
          rest = gListDrag.Take(drag, listdrag::Position{values.mMinRange, values.mMaxRange, values.mMinVisible, values.mMaxVisible}, height / lines,
            [list, &values](const int by) {
              list->ScrollLines(moho::MSA_Vert, static_cast<float>(by));
              return static_cast<int>(list->GetScrollValues(moho::MSA_Vert).mMinVisible - values.mMinVisible);
            });
        }
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("MenuFingers: %s", exception.what());
    }
    if (rest != 0) {
      AddPageDrag(rest);
    }
  }

  // --- The text field ---------------------------------------------------------------------------

  void PublishField(moho::CMauiControl* const control, const int why)
  {
    if (dynamic_cast<moho::CMauiEdit*>(control) != nullptr) {
      PublishToShow(ToShow{
        why, static_cast<int>(moho::CScriptLazyVar_float::GetValue(&control->mTopLV)),
        static_cast<int>(moho::CScriptLazyVar_float::GetValue(&control->mBottomLV))
      });
    }
  }

  /// On the program's thread: the keyboard is coming, and typing goes to the field with the focus.
  void LookForField(const Argument)
  {
    PublishField(moho::Maui_CurrentFocusControl.GetObjectPtr(), kFieldAskedFor);
  }
} // namespace

void menufingers::SetListener(void (*const heard)())
{
  gHeard.store(heard);
}

void menufingers::NoteTapOnField(const float x, const float y)
{
  try {
    moho::CMauiControl* const root = InputRoot();
    moho::CMauiControl* const top = ControlAt(x, y);
    if (root == nullptr || top == nullptr) {
      return;
    }
    // A field can share its depth with what lies under it, which then takes the press: the lobby's chat.
    for (moho::CMauiControl* control = root; control != nullptr; control = control->DepthFirstSuccessor(root)) {
      if (dynamic_cast<moho::CMauiEdit*>(control) != nullptr && !control->IsHidden() && !control->IsHitTestDisabled()
          && control->HitTest(x, y) && control->mDepth >= top->mDepth) {
        PublishField(control, kFieldTapped);
        screenkeyboard::FingerPressed(x, y);
        return;
      }
    }
  } catch (const std::exception& exception) {
    gpg::Warnf("MenuFingers: %s", exception.what());
  }
}

bool menufingers::OnScrollBar(const float x, const float y)
{
  try {
    return dynamic_cast<moho::CMauiScrollbar*>(ControlAt(x, y)) != nullptr;
  } catch (const std::exception& exception) {
    gpg::Warnf("MenuFingers: %s", exception.what());
    return false;
  }
}

menufingers::Page menufingers::PageNow()
{
  const std::lock_guard<std::mutex> lock(gPageMutex);
  return gPage;
}

bool menufingers::MenuFingers()
{
  return gMenuFingers.load();
}

float menufingers::HudFactor()
{
  return gHudFactor.load();
}

void menufingers::LookForList(const int x, const int y)
{
  const std::uint32_t serial = gListSerial.fetch_add(1) + 1;
  host::RunOnGameThread(&::LookForList, hosttask::Places(x, y) | static_cast<Argument>(serial) << 32);
}

int menufingers::ListAnswer()
{
  const std::uint64_t answer = gListAnswer.load();
  return static_cast<std::uint32_t>(answer >> 8) == gListSerial.load() ? static_cast<int>(answer & 0xFFU) : kNotKnownYet;
}

void menufingers::ScrollList(const float drag)
{
  Argument bits = 0;
  std::memcpy(&bits, &drag, sizeof(drag));
  host::RunOnGameThread(&::ScrollList, bits);
}

float menufingers::TakePageDrag()
{
  const std::lock_guard<std::mutex> lock(gPageDragMutex);
  const float drag = gPageDrag;
  gPageDrag = 0;
  return drag;
}

void menufingers::AskForField()
{
  host::RunOnGameThread(&LookForField, 0);
}

menufingers::ToShow menufingers::TakeToShow()
{
  const std::lock_guard<std::mutex> lock(gToShowMutex);
  const ToShow toShow = gToShow;
  gToShow = ToShow{};
  return toShow;
}

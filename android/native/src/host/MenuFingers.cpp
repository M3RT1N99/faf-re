// What FAF's UI and the app tell each other about the fingers in the menus (android/TOUCH_UI.md). FAF's Lua declares a
// screen's page, whose the fingers are, the HUD's factor and what to bring into view, by four functions added to its UI's
// globals, and the activity follows on its own thread (Game.fromProgram wakes it, and it reads what is new). A finger that
// scrolls asks what list is under it, and a list takes its drag on the program's thread, where FAF's lists live, in the
// order of the rest of the input; what a list cannot take goes back to the activity for the page.

#include "MenuFingers.h"

#include <jni.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>

#include <ListDrag.h>
#include <Win32AndroidHost.h>

#include "TaskPlaces.h"
#include "moho/lua/CScrLuaBinder.h"
#include "moho/ui/UiPrivate.h"

namespace
{
  // --- The activity -----------------------------------------------------------------------------

  JavaVM* gVm = nullptr;
  jclass gGameClass = nullptr;
  jmethodID gFromProgram = nullptr;

  /// The activity reads what is new when it can.
  void WakeActivity()
  {
    JNIEnv* env = nullptr;
    if (gVm == nullptr || gVm->AttachCurrentThreadAsDaemon(&env, nullptr) != JNI_OK) {
      return;
    }
    env->CallStaticVoidMethod(gGameClass, gFromProgram);
    if (env->ExceptionCheck() == JNI_TRUE) {
      env->ExceptionDescribe();
      env->ExceptionClear();
    }
  }

  // --- What FAF's Lua declares ------------------------------------------------------------------

  /// A screen's page, in the picture's pixels; none: the view is the whole picture.
  struct Page
  {
    bool shown = false;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
  };

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
    WakeActivity();
    return 0;
  }

  int SetFingers(lua_State* const lua)
  {
    const bool menu = lua_toboolean(lua, 1) != 0;
    if (gMenuFingers.exchange(menu) != menu) {
      WakeActivity();
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

  // --- What the activity has yet to bring into view ----------------------------------------------

  constexpr jint kNoPlace = 0;
  constexpr jint kFieldTapped = 1;
  constexpr jint kFieldAskedFor = 2;
  constexpr jint kLinesToShow = 3;

  /// Why, and the place in the picture's lines.
  struct ToShow
  {
    jint why = kNoPlace;
    jint top = 0;
    jint bottom = 0;
  };

  std::mutex gToShowMutex;
  ToShow gToShow;

  void PublishToShow(const ToShow& toShow)
  {
    {
      const std::lock_guard<std::mutex> lock(gToShowMutex);
      gToShow = toShow;
    }
    WakeActivity();
  }

  int ShowLines(lua_State* const lua)
  {
    PublishToShow(ToShow{kLinesToShow, static_cast<jint>(lua_tonumber(lua, 1)), static_cast<jint>(lua_tonumber(lua, 2))});
    return 0;
  }

  moho::CScrLuaBinder gShowLinesBinder(moho::SCR_UserLuaInitSet(), "FafreShowLines", &ShowLines, nullptr, "<global>", kShowLinesHelp);

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

  constexpr jint kNotKnownYet = 0;
  constexpr jint kNoList = 1;
  constexpr jint kList = 2;

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
    WakeActivity();
  }

  /// On the program's thread: what list a finger that has come down here would scroll.
  void LookForList(const WPARAM place)
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
    WakeActivity();
  }

  /// On the program's thread: this much of a finger's drag, as a float's bits; the list takes what it can.
  void ScrollList(const WPARAM dragBits)
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

  void PublishField(moho::CMauiControl* const control, const jint why)
  {
    if (dynamic_cast<moho::CMauiEdit*>(control) != nullptr) {
      PublishToShow(ToShow{
        why, static_cast<jint>(moho::CScriptLazyVar_float::GetValue(&control->mTopLV)),
        static_cast<jint>(moho::CScriptLazyVar_float::GetValue(&control->mBottomLV))
      });
    }
  }

  /// On the program's thread: the keyboard is coming, and typing goes to the field with the focus.
  void LookForField(const WPARAM)
  {
    PublishField(moho::Maui_CurrentFocusControl.GetObjectPtr(), kFieldAskedFor);
  }
} // namespace

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

JNIEXPORT jint JNI_OnLoad(JavaVM* const vm, void*)
{
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }
  const jclass game = env->FindClass("io/github/m3rt1n99/fafre/Game");
  if (game == nullptr) {
    return JNI_ERR;
  }
  gGameClass = static_cast<jclass>(env->NewGlobalRef(game));
  gFromProgram = env->GetStaticMethodID(game, "fromProgram", "()V");
  env->DeleteLocalRef(game);
  if (gFromProgram == nullptr) {
    return JNI_ERR;
  }
  gVm = vm;
  return JNI_VERSION_1_6;
}

/// The page FAF's Lua has declared, into left, top, right, bottom; false when there is none.
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_m3rt1n99_fafre_Game_page(JNIEnv* const env, jclass, const jintArray rect)
{
  Page page;
  {
    const std::lock_guard<std::mutex> lock(gPageMutex);
    page = gPage;
  }
  const jint values[4] = {page.left, page.top, page.right, page.bottom};
  env->SetIntArrayRegion(rect, 0, 4, values);
  return page.shown ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL Java_io_github_m3rt1n99_fafre_Game_menuFingers(JNIEnv*, jclass)
{
  return gMenuFingers.load() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jfloat JNICALL Java_io_github_m3rt1n99_fafre_Game_hudFactor(JNIEnv*, jclass)
{
  return gHudFactor.load();
}

/// A finger came down here: what list would it scroll? The answer comes as `listAnswer`.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_lookForList(JNIEnv*, jclass, const jint x, const jint y)
{
  const std::uint32_t serial = gListSerial.fetch_add(1) + 1;
  Win32Android_RunOnWindowThread(&LookForList, hosttask::Places(x, y) | static_cast<WPARAM>(serial) << 32);
}

/// 0: not known yet; 1: no list is under the last finger; 2: one is.
extern "C" JNIEXPORT jint JNICALL Java_io_github_m3rt1n99_fafre_Game_listAnswer(JNIEnv*, jclass)
{
  const std::uint64_t answer = gListAnswer.load();
  return static_cast<std::uint32_t>(answer >> 8) == gListSerial.load() ? static_cast<jint>(answer & 0xFFU) : kNotKnownYet;
}

/// `drag`: in the picture's pixels, positive downwards, for the list of the last finger.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_scrollList(JNIEnv*, jclass, const jfloat drag)
{
  WPARAM bits = 0;
  std::memcpy(&bits, &drag, sizeof(drag));
  Win32Android_RunOnWindowThread(&ScrollList, bits);
}

/// What no list took since the last call, in the picture's pixels.
extern "C" JNIEXPORT jfloat JNICALL Java_io_github_m3rt1n99_fafre_Game_takePageDrag(JNIEnv*, jclass)
{
  const std::lock_guard<std::mutex> lock(gPageDragMutex);
  const float drag = gPageDrag;
  gPageDrag = 0;
  return drag;
}

/// The keyboard is coming: the answer comes as `takeToShow`, if a text field has the focus.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_askForField(JNIEnv*, jclass)
{
  Win32Android_RunOnWindowThread(&LookForField, 0);
}

/// The lines the activity has yet to bring into view, into top, bottom; 0: none, 1: a finger tapped a text field, 2:
/// askForField's text field, 3: FAF's Lua says so.
extern "C" JNIEXPORT jint JNICALL Java_io_github_m3rt1n99_fafre_Game_takeToShow(JNIEnv* const env, jclass, const jintArray place)
{
  ToShow toShow;
  {
    const std::lock_guard<std::mutex> lock(gToShowMutex);
    toShow = gToShow;
    gToShow = ToShow{};
  }
  const jint values[2] = {toShow.top, toShow.bottom};
  env->SetIntArrayRegion(place, 0, 2, values);
  return toShow.why;
}

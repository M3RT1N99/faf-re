# The game's UI under a finger

FAF's UI is drawn in fixed pixels for a monitor and a mouse. On a phone it comes out too small to hit, so
this port measures every control and makes the small ones larger. The look stays FAF's: no control is
redrawn and no Android menu replaces one. FAF's own Lua files stay untouched and nothing here reaches the
simulation, so play against a PC is not affected.

## What a finger needs

The picture is at least 768 lines high, as FAF's front end asks for. On a Galaxy S22 Ultra in HD+ that is
1647x768 on a short side of about 73 mm: **10.5 game pixels to the millimetre**. A finger needs **7-9 mm**;
with help from the app (below), small controls down to about 4 mm are usable.

FAF has a UI scale (`ui_scale`, `LayoutHelpers.SetPixelScaleFactor`), and every panel of the game honours
it. But above 100 % FAF's front end loses its lower part: at 125 % at 768 lines the main menu has no Options
and Exit, and the lobby no Launch Game. So the two parts are handled differently:

- **In the game** the UI scale goes up, only there: a session starts in a fresh Lua state. This is built, see below.
- **In the menus** the UI scale stays. Instead a page is shown larger at a fixed size and a finger scrolls it
  down. There is no zoom gesture (the user's decision). This is a later job.

## The measurements

`android/tools/ui_census` is the measuring tool: a Lua hook that wraps the engine's control constructors, so
it knows which Lua line made each control, and logs each control's size and whether it takes events. It has a folder
and a hook entry of its own (`/fafre_census`, `mount_in_init.py`), beside the app's package. The tables here are from a
run at 1647x768 without the package, sizes in game pixels at FAF's scale of 100 %; how the game's panels came out is under
"The game's panels now".

### The game's panels

| Control | Lua | Pixels | mm | At 1.6 |
|---|---|---|---|---|
| Order buttons | `ui/game/orders.lua:1190` | 46x46 | 4.4 | 7.0 |
| Build icons | `ui/game/construction.lua:702` | 48x48 | 4.6 | 7.3 |
| Construction's side tabs | `ui/game/construction.lua:211` | 74x54 | 5.1 | 8.2 |
| Tech level tabs | `ui/game/construction.lua:214` | 40x25 | 2.4 | 3.8 |
| Pause and small construction buttons | `ui/game/construction.lua:211` | 30x30 | 2.9 | 4.6 |
| Construction's scroll buttons | `ui/game/construction.lua:305` | 22x50 | 2.1 | 3.4 |
| Top tabs (menu, diplomacy, pause) | `ui/game/tabs.lua:448` | 54x46 | 4.4 | 7.0 |
| Multifunction panel's buttons | `ui/game/multifunction.lua:218` | 42x30 | 2.9 | 4.6 |
| Idle and commander avatars | `ui/game/avatars.lua:123` | 62x66 | 5.9 | 9.4 |
| Collapse arrows of every panel | `tabs.lua:441`, `economy.lua:82`, `multifunction.lua:209`, `score.lua:91`, `avatars.lua:54` | 18x26 | 1.7 | 2.7 |
| Score panel's small icons | `ui/game/score.lua:123` | 14x14 | 1.3 | 2.1 |

With the UI scale at 1.6 every panel of the game grows as one, with sharp text and the same look.

### The menus

| Screen | Lua | Its size | What is small |
|---|---|---|---|
| Main menu | `ui/menus/main.lua` | central column | nothing: buttons 358x70, 6.7 mm |
| Options | `ui/dialogs/options.lua` | 698x598 | rows 34 high (3.2 mm), choices 18 (1.7 mm), slider thumbs 30x22 (2.1 mm), tabs 178x58, buttons 170x50 |
| Lobby | `ui/lobby/lobby.lua` | 1024 wide, all 768 lines | slot rows 22 apart (2.1 mm), choices 18 (1.7 mm), scroll bars 14 wide (1.3 mm), buttons 130x42 (4.0 mm), small toggles 98x42 |
| LAN screen | `ui/lobby/gameselect.lua` | 974x734 | edit boxes 19 high, list rows |
| Replay, load and save | `ui/dialogs/replay.lua`, `saveload.lua`, `ui/controls/filepicker.lua` | 702x586 | rows 20-22 high (2 mm), buttons 198x70 |
| Profile | `ui/dialogs/profile.lua` | 578x446 | scroll buttons 34x26 |
| Choices everywhere | `ui/controls/combo.lua` | | 18 high, and the same when opened |

The in-game menu is a counter example: its buttons are large already, so at 1.6 its list runs off the
screen. A control's scale has to follow what it needs, not a single number.

## How it is built

Lua hooks: the app ships `android/app/src/main/assets/fafre_ui`, `GameFiles.gameInit` copies it beside the game's
files before a game starts, replacing what is there, and the game's init file mounts it and adds it to the `hook`
table. A replay's run stays without it, as it pins the simulation. The engine joins each script in it to the end of
the game's script of the same path (`SCR_LuaDoFileConcat`; the log says `Hooked /lua/... with /fafre_ui/lua/...`).
Traps:

- An error in a hook ends the import of the whole file and with it the game's UI ("unable to start main UI
  script"), so each hook keeps its work in a `pcall` and logs what failed with `WARN`: FAF's Lua changes under it.
  A syntax error is beyond that: the joined chunk does not load.
- A hook is the same chunk as the file: it sees the file's locals and can set them, it adds to the file's 200 locals
  if it declares any outside a function, and it starts with an empty line, as a file can end in a comment.
- `/lua/ui/uimain.lua` is imported in a session's state too, before that state can run threads
  (`ForkThread: Lua state has not been set up for multiple threads`).
- `SessionIsActive()` is still false while a session's state loads. `/lua/sessioninit.lua` runs only in a
  session's fresh state, so that is where the game's scale is set.
- Reading a global that does not exist is an error (`lua/system/config.lua`): use `rawget(_G, name)`.
- `UI_GetCommandMode` imports `/lua/ui/game/commandmode.lua`, which fails in the menus' state: ask it only with a world
  view up.
- Hooks must not reach what the simulation loads. Thirty seconds into a game the simulation's `__modules` held of
  `/lua/ui` only `lobby/lobbyoptions.lua` and `lobby/unitsanalyzer.lua`, and of `/lua/maui` nothing; neither is hooked.

### The scale

`HudScale` works out the factor at which FAF's 48-pixel build icon is 7.3 mm: the picture's lines over the screen's short
side, at least 1.0, and at most what leaves 460 of FAF's lines. On the S22 Ultra's 768 lines on 73 mm that is 1.6; at
1080 lines 2.25, at the same size on the glass and sharper. The short side is the panel's, which takes some working out:
a phone set to a lower resolution, as the S22 Ultra in HD+ has an active mode of 720x1544 (`CLIENT_PLAN.md`), reports a
real density that may stay the panel's (about 501, which with that mode would say half the size) or follow the mode (about
250); which of them this phone does is not known. So the size is worked out from the active mode and from the largest
supported one, at the density reported, and the one nearer what the logical size says is taken: logical pixels over the
logical density, 61 mm there in every resolution, and a panel is about 15 % larger than that. Where the result is more
than a factor of two from the logical size, the density is wrong and the logical size is all there is. At 460 lines the
panels (365 lines from the multifunction panel down to the unit view) still leave the app's buttons about 6 mm each.
The value goes to the game as `/fafre_hud_scale`, which `fafre/scale.lua` reads, and the report has a line with it, what
the size was taken from and what there was to choose from: only a phone can prove this. On the emulator `wm size` leaves
its millimetres as they were, so a test gives the phone's factor: `am start ... --ef hudScale 1.6`.

FAF's other readers of the `ui_scale` option in a session:

| Reader | What it does | Here |
|---|---|---|
| `lua/ui/dialogs/hotstats.lua:5` | A local factor, for the margins of the score dialog's graphs | Set to the score dialog's factor by the hook |
| `lua/maui/effecthelpers.lua:32` | A local factor with a getter and a setter that nothing reads | Nothing to do |
| `lua/ui/dialogs/myeffecthelpers.lua:20` | The same, and it starts at 1.0 | Nothing to do |
| `lua/ui/help/tooltips.lua:850` | The option's text in the Options dialog | Stays: the option still rules the menus |

The option itself stays in Game.prefs as the user set it, and a session does not write it.

### A factor per panel

`fafre_ui/lua/fafre/scale.lua` is the one helper. FAF's layout functions read the factor when they are evaluated, which
is after they are set, so a factor has to be there for as long as what it laid out can be evaluated:

- `Dialog`: a dialog that covers the game and builds more as it is used (tabs, rows), from local functions that a hook
  cannot reach. Its factor stays until its root control is gone, and a thread watches for that, as a dialog's
  `OnDestroy` is its own to set. Dialogs nest, and go in any order.
- `Built`: a panel that leaves the HUD in use and builds nothing later, the Menu tab: its factor stays only while it is
  built, and all it made is evaluated before the HUD's factor returns. What a later change makes evaluate again gets the
  HUD's factor: a button's next texture would size it anew, so the hook fixes the buttons' sizes.
- `AtHud`: what the game calls for the HUD runs at the HUD's factor whatever dialog is open: `OnSelectionChanged`,
  `OnQueueChanged`, `OnBeat`, `OnPause`, `OnResume`, `OnSync` and the unit view's `OnFrame`.
- `Try`: what a wrapper does beyond calling FAF runs later than the hook's own `pcall`, so it goes through this, which
  logs a failure with `WARN`.

What could not be ruled out: while a dialog is open, HUD code reached in other ways gets the dialog's smaller factor,
such as a thread's or a panel's own `OnFrame` that builds a control, and a layout function of the HUD that some change
makes evaluate again. The second heals at its next change, as the numbers of the economy panel do; the first stays
until that control is built again. An announcement that starts while Options is open is the likely case. Closing a
dialog showed the HUD as before, also after units had gone on building while it was open, and with the game paused. The
reverse should not arise: of a dialog only what `OnBeat` and `OnSync` build would run at the HUD's factor, and none of the
dialogs below was seen to build from there.

### The hooks

| Hook | Why |
|---|---|
| `lua/sessioninit.lua` | The HUD's factor, in the session's fresh state, and `OnSync` at the HUD's factor |
| `lua/ui/game/gamemain.lua` | `OnSelectionChanged`, `OnQueueChanged`, `OnBeat`, `OnPause`, `OnResume` at the HUD's factor |
| `lua/ui/game/unitview.lua` | Nothing hovers on a phone: FAF's "Single Unit Selected Info" is on, or a selected unit's view would go with the pointer; the abilities list, which stands over the middle of the screen, only for a finger that rests on a unit |
| `lua/ui/game/tabs.lua` | The Menu tab's ten large buttons fit at 0.92: `Built`, 837 of FAF's lines |
| `lua/ui/game/controlgroups.lua` | FAF's place, 368 lines down, is under the construction panel: below the avatars, which grow |
| `lua/ui/game/minimap.lua`, `lua/ui/game/chat.lua` | Their default places lie under the app's buttons, the multifunction panel and the unit view: a new window goes where it covers no panel, and the reset button brings it back there; a place the user chose stays |
| `lua/ui/dialogs/options.lua` | `Dialog`, 646 lines |
| `lua/ui/dialogs/keybindings.lua`, `lua/ui/notify/customiser.lua` | `Dialog`, 740 lines |
| `lua/ui/dialogs/saveload.lua` | `Dialog`, 640 lines |
| `lua/ui/game/connectivity.lua` | `Dialog`, 640 lines, which hold nine players |
| `lua/ui/dialogs/score.lua` | `Dialog`, 646 lines; its root is `dialog`, as setting the skin rebuilds the HUD before it makes its first control |
| `lua/ui/dialogs/hotstats.lua` | Sets its local factor (above) |

### The pointer

A mouse's pointer goes on being somewhere, and the game shows what it is over. So 300 ms after the last finger has
lifted (`TouchInput.leaveSoon`), `Game.pointerLeave` moves it off the window, to -1000,-1000. wx-lite's idle handler then
sends the leave a Windows program gets, the event mapper tells the control it left, and rollovers and tooltips go. While a
finger rests, the pointer is there and they show, until the hold's right button comes at 350 ms, which FAF's buttons take
for a press. A new finger takes the leave back, first thing and also when it is already on its way to the game: the
leave carries a count of the fingers that have come down, and is dropped if the count has moved (`Game.pointerStay`). It
stays with a building's ghost over the world (`UI_GhostWaitsAt`), and leaves a build icon that started the ghost.

Everything the activity gives, a finger's or a mouse's, reaches the program's thread as a task
(`Win32Android_RunOnWindowThread`), and the input a task gives comes right behind it in the queue, ahead of what was given
after it. So the order holds as it was given: a tap's press and release come before the next finger's move, which could
otherwise leave the cursor at the old place, and nothing falls between them; and a tap's choice sees what the tap before it
has done, also when a long frame has held them all back. A drag is no later for it: the program takes its queue until it is
empty before each frame, and a move replaces one still waiting last in the queue, as Windows coalesces them.

### Tap help

A left button's tap is `Game.tap`, decided on the window thread by `UI_TapTarget` in the patch of
`CUIWorldViewEvents.cpp`; the choice itself is `include/TapTarget.h`, with its tests in `tests/wx`. Nothing changes if the
place's topmost control takes events and is not small, if it is the minimap (the place on it is what the tap says), or if it
is the world and a command mode is armed, an order, a ping or a building, which wants that very place. A tap that lands
on the HUD gets the help with a command mode armed too. Otherwise it looks 2.5 mm around for small controls that take
events (a side under 5 mm) and takes the nearest by distance to its rectangle, except that a small control gives way to
a small one within it: a row of Options around its choice. It clicks the pixel of the control nearest to the tap, not its
centre, as sliders and scroll bars act on the place; a tap on the control stays where it landed. That pixel has to find the
control or a part of it, not what lies over it.

"Takes events" is the census's: a `HandleEvent` of its own other than `Control`'s, which plain Text, Bitmap and Group have,
or an ItemList, Edit or Scrollbar, whose handler is the engine's. C++ reads it through the control's Lua object, as it runs
only at a tap. Two kinds of handler do not count. `tooltip.lua`'s wrappers keep what they wrap in `oldHandleEvent`, and a
label with a tooltip has the plain one there, while a button with a tooltip has the button's. And a window's title and its
resize edges (`lua/maui/window.lua`, by the names it gives them) take a drag: a tap on one, with nothing else in reach, clicks
nothing, as a press that moves nothing would do no more than save the window's place, which then counts as the place the user
chose. A drag from them still works. A control with an alpha of 0 is no candidate, as nobody aims at what cannot be seen: the
resize edges again, and panels that stand faded out, such as the unit view with nothing selected.

An order by tap (*Order*) and a two-finger tap stay where they landed, except that *Order*'s two-finger tap, which is the left
button, gets the help too. It works in the menus as well.

### The app's buttons

In a game (`Game.gamePlaying`: the main world view is up and the game is not over) they are 3 + 2 buttons of up to 9 mm at
the left edge between the multifunction panel and the unit view, 138 and 230 of FAF's lines from the two ends. The right
edge, which has more height, belongs to the avatars with their idle lists and to the control groups, and both grow.
In the menus and on the score screen only *Keys* stays, 6.5 mm, in the upper left corner, which no menu uses. A toggle
that is on goes off when the game ends.

### The game's panels now

Measured by the census at 1.6 on the phone's size; mm at 10.5 pixels to the millimetre. "Reach" is what tap help adds
where nothing larger that takes events is in the way.

| Control | Lua | Pixels | mm | |
|---|---|---|---|---|
| Order buttons | `ui/game/orders.lua:1190` | 73x73 | 7.0 | |
| Build icons | `ui/game/construction.lua:702` | 76x76 | 7.2 | |
| Construction's side tabs | `ui/game/construction.lua:211` | 118x86 | 8.2 | |
| Tech level tabs | `ui/game/construction.lua:214` | 64x40 | 3.8 | reach |
| Pause and small construction buttons | `ui/game/construction.lua:211` | 48x48 | 4.6 | reach |
| Construction's scroll buttons | `ui/game/construction.lua:257` | 35x80 | 3.3 | reach |
| Top tabs | `ui/game/tabs.lua:448` | 86x73 | 7.0 | |
| Menu tab's buttons | `ui/game/tabs.lua:600` | 269x64 | 6.1 | at 0.92 |
| Multifunction panel's buttons | `ui/game/multifunction.lua:218` | 67x48 | 4.6 | reach |
| Commander's avatar | `ui/game/avatars.lua:123` | 99x105 | 9.4 | |
| Idle avatars | `ui/game/avatars.lua:237` | 86x80 | 7.6 | |
| Idle list's rows | `ui/game/avatars.lua:533` | 137x80 | 7.6 | |
| The idle avatars' expand arrow | `ui/game/avatars.lua:256` | 16x48 | 1.5 | reach |
| Control groups | `ui/game/controlgroups.lua:83` | 73x54 | 5.1 | |
| Collapse arrows of every panel | `tabs.lua:441`, `economy.lua:82`, `multifunction.lua:209`, `score.lua:91`, `avatars.lua:54` | 28x41 | 2.7 | reach, at the screen's edge |
| Score panel's rows and small icons | `ui/game/score.lua:280`, `:315` | 480x22, 22x22 | 2.1 | reach; tooltips and the display's switch |

The arrows and tech tabs keep their size: they sit in FAF's outline, and a larger one would cover what is beside it. A
tap beside them reaches them; where a large button lies directly beside an arrow (the avatar, the Diplomacy tab under
the top arrow), only the strip at the screen's edge is the arrow's, and that edge a finger finds.

| Dialog | Lua | Factor at 768 lines | Fits | What is small |
|---|---|---|---|---|
| Options | `ui/dialogs/options.lua` | 1.19 | yes | rows 715x40 (3.8 mm), choices and slider thumbs 26 high (2.5 mm): reach, and the menus' later job |
| Key Bindings | `ui/dialogs/keybindings.lua` | 1.04 | yes | buttons 207x43 (4.1 mm), scroll bar 14 wide (1.3 mm) |
| Notify Management | `ui/notify/customiser.lua` | 1.04 | yes | buttons 156x43 (4.1 mm), scroll bar 14 wide (1.3 mm) |
| Save, Load | `ui/dialogs/saveload.lua` | 1.20 | yes | scroll bar 40 wide (3.8 mm), sort tabs 26 high (2.5 mm) |
| Scenario | `ui/game/objectivedetail.lua` | 1.60 | yes | |
| Restart, End Game, Exit questions | `ui/uiutil.lua` | 1.60 | yes | |
| Connectivity | `ui/game/connectivity.lua` | 1.20 | yes; tried with two players, and by its slots' height nine fit | |
| Diplomacy tab | `ui/game/diplomacy.lua` | 1.60 | yes, tried with three players | |
| Score, Continue | `ui/dialogs/score.lua`, `hotstats.lua` | 1.19 | yes | |
| Chat | `ui/game/chat.lua` | 1.60 | yes, at its new place | window's small buttons: reach |
| Minimap and its options | `ui/game/minimap.lua`, `multifunction.lua` | 1.60 | yes, at its new place | |
| Transmission log | `ui/game/transmissionlog.lua` | 1.60 | yes; FAF has no key or button that opens it | |

Not tried: the campaign's objectives panel in a campaign (it opens at the top right, as the score panel, which a campaign
has not), and more than three players.

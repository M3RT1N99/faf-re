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

- **In the game** the UI scale goes up, only there: a session starts in a fresh Lua state.
- **In the menus** the UI scale stays at 1. A screen that needs it is shown larger at a fixed size, its page, which a
  finger scrolls down, and lists scroll inside it. There is no zoom gesture (the user's decision). See "The menus".

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
| `lua/ui/help/tooltips.lua:850` | The option's text in the Options dialog | Says what the option does now (below) |
| `lua/ui/controls/textarea.lua:8`, `lua/ui/lobby/unitstooltip.lua:18`, `lua/ui/dialogs/createunit.lua:2` | A local factor from `GetPixelScaleFactor`, as they are first imported | What it was: in the front end 1, in a session the factor at that moment. The option's value does not reach them by itself any more |

The option is the user's own adjustment of the HUD. The front end does not read it: the hook of `layouthelpers.lua` sets
FAF's factor to 1, so that a value above 100 % cannot cut off the menus. `StartSession` multiplies the app's factor by it, within
the same limit of lines as `HudScale` (`/fafre_hud_most`); on the S22 Ultra's 768 lines that is 1.6 at 100 %, 1.28 at 80 %, and 1.67 from
125 % up. The app's buttons go by the result (`FafreSetHud`). A session reads it as it starts, so the option needs no restart any more,
which FAF's dialog would have asked for (the hook of `options.lua` takes that off), and it counts from the next game on. It stays in
Game.prefs as the user set it.

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

- `DialogAt`: as `Dialog`, for a dialog whose factor is its own, larger than the HUD's, and which holds in the front end too.
- `Page`: see "The menus".

### The hooks

| Hook | Why |
|---|---|
| `lua/sessioninit.lua` | The HUD's factor, in the session's fresh state, and `OnSync` at the HUD's factor |
| `lua/ui/game/gamemain.lua` | `OnSelectionChanged`, `OnQueueChanged`, `OnBeat`, `OnPause`, `OnResume` at the HUD's factor |
| `lua/ui/game/unitview.lua` | Nothing hovers on a phone: FAF's "Single Unit Selected Info" is on, or a selected unit's view would go with the pointer; the abilities list, which stands over the middle of the screen, only for a finger that rests on a unit |
| `lua/ui/game/tabs.lua` | The Menu tab's ten large buttons fit at 0.92: `Built`, 837 of FAF's lines |
| `lua/ui/game/controlgroups.lua` | FAF's place, 368 lines down, is under the construction panel: below the avatars, which grow |
| `lua/ui/game/minimap.lua`, `lua/ui/game/chat.lua` | Their default places lie under the app's buttons, the multifunction panel and the unit view: a new window goes where it covers no panel, and the reset button brings it back there; a place the user chose stays |
| `lua/ui/dialogs/options.lua` | `DialogAt`, in the front end too: shown larger, with fewer rows (see "The menus"); the UI scale needs no restart |
| `lua/ui/uimain.lua` | A new front end takes the fingers and the view back from what the last state declared |
| `lua/maui/layouthelpers.lua` | The front end's factor is 1, whatever the UI scale says |
| `lua/maui/slider.lua` | A press on a slider's bar also takes the thumb, for the drag that may follow |
| `lua/ui/controls/combo.lua` | A dropdown's rows are 5 mm on the glass, and it stays on the picture |
| `lua/ui/lobby/lobby.lua` | `Page`; the slot rows' distance |
| `lua/ui/lobby/gameselect.lua` | `Page` |
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
button, gets the help too. It works in the menus as well, and there a drag's press has it too (`Game.pressNear`). Reach and "small" are
millimetres on the glass, so in the picture's pixels they follow the page's scale. A control that takes a press only by its own wheel
can say so, as a tooltip's wrapper does (the rows of Options, which at their size would count as large: a tap beside a choice finds the
choice).

### The app's buttons

In a game (`Game.gamePlaying`: the main world view is up and the game is not over) they are 3 + 2 buttons of up to 9 mm at
the left edge between the multifunction panel and the unit view, 138 and 230 of FAF's lines from the two ends. The right
edge, which has more height, belongs to the avatars with their idle lists and to the control groups, and both grow.
In the menus, on the score screen and over a dialog in a game, whose fingers are the menus', only *Keys* stays, 6.5 mm, in the upper left
corner, which no menu uses for anything but a title. A toggle that is on goes off then.

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
| Options | `ui/dialogs/options.lua` | 1.75 | yes, reflowed: see "The menus" | |
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

## The menus

No zoom in the menus: a screen is shown once at a fixed larger size, one finger scrolls it up and down, and lists scroll inside. FAF's
look stays: the same textures and fonts, no new art.

### Who decides

FAF's Lua declares a screen's page and whose the fingers are, through three functions the app adds to the UI's globals
(`android/native/src/host/MenuFingers.cpp`): `FafreSetPage([left, top, right, bottom])`, `FafreSetFingers(menu)` and `FafreShowLines(top,
bottom)`. The native side keeps what they say; `Game.fromProgram` wakes the activity, which reads it on its own thread. `fafre/scale.lua` is
the only one that calls them. The front end's fingers are the menus' from the start. A session's are the game's (`TouchInput` as it
was), except while a `Dialog`, `DialogAt` or `Page` is open.

A new screen takes a line or two of Lua, as `ui/lobby/gameselect.lua`, which gives its sides as well:

```lua
CreateUI = Scale.Page(CreateUI, function(made) return made[1] end)
```

`Page` wraps what builds the screen. `find` gets the controls it made and says which control the page goes by, and has the place of,
and if a side of the page is not that control's, a table with `left`, `top`, `right`, `bottom`. When that control is gone so is the page.
Pages nest: the one under comes back as it stood.

### The view

`PageView` works out how the picture lies on the screen. With a page the picture is larger, the page's width fitted to the screen's, at the
page's left edge; a finger moves it up and down within the page's height and not sideways. The surface is laid out at that size,
larger than the screen, and moved by `translationY`. `setFixedSize` keeps its buffers, so the swap chain does not notice: no new surface, and a
scroll needs no frame of the game. On the emulator's composition two long swipes took 86 frames of the view system, 2 of them late, and
Home and back leave the page as it stood; a screenshot cannot show a tear, so what the eye says on a phone is still to come. Fingers land on a layer of their own, which does not move, so their places stay the screen's, and `PageView` turns them
into the picture's. The cost is sharpness: the picture stays 1647x768 and the compositor scales it up. A surface as high as the page
would be sharp, and is not tried.

With the on-screen keyboard up a page can go up that much further. A tap on a text field opens the keyboard, and *Keys* asks
which field has the focus; either way the page goes where the field shows above the keyboard. Without a page the keyboard covers
what it covers, and what is typed shows over the picture, as before.

### The fingers

`PageInput`, beside `TouchInput`:

- a tap is a click, through `Game.tap`, so with tap help; also after a long rest, as a careful finger is slow;
- a finger that moves mainly up or down past the slop scrolls, presses nothing, and the pointer leaves at once;
- a finger that moves mainly sideways past the slop is a mouse's drag from where it came down: sliders;
- a finger that rests 300 ms, which it feels, and then moves is a mouse's drag in any direction: scroll bars, windows;
- two fingers do nothing;
- the pointer leaves 300 ms after the last finger, as in a game.

There is no way to a right click here: FAF has two in the lobby, for the host, on an observer in the list (kick) and on a start position
on the map (close it). A slot's own choices do the same for a player's slot.

### The list first

A drag first moves the list under the finger, and what that list cannot take moves the page. When a finger comes down the activity asks what
list is under it (`Game.lookForList`): on the program's thread `MenuFingers.cpp` walks up from the topmost control until
something scrolls, by FAF's own interface (`IsScrollable`, `GetScrollValues`, `ScrollLines`; an `ItemList` is the engine's, and for a scroll bar
what it scrolls). The answer says *none* or *one*.

- With none the activity moves the page itself, at once.
- With one, each move goes over as a task (`Game.scrollList`), which runs in the order of all other input, so it does not matter that a frame can be
  long. `include/ListDrag.h`, with its tests, gives the list whole lines by its own pixels per line (its height over its visible lines), so
  that it follows the finger one to one, carries the remainder, and gives what is left at the list's end back: `Game.takePageDrag`.
- Until the answer has come, what the finger has gone waits (`mUnsentDrag`).

### The screens

| Screen | How | Why |
|---|---|---|
| Main menu | As FAF's | Its buttons are 358x70, 6.7 mm |
| Lobby, also a network game's | A page of its panel's width and all of FAF's lines: 1024 pixels fill the screen's width, the picture is 1.61 times as large, and 290 of FAF's lines are to scroll | Above a factor of 1 FAF's lobby loses its lower part, and it holds 16 slots, chat, the options list and the map on its 768 lines. The panel's 1024 are what a finger needs of the 1647; the rest is decoration |
| Slot rows | 44 apart with up to 8 players, down to FAF's 22 with 16: the map's start spots share the 352 lines; the art of the row's background stretches, a plain strip | Combos stay 18 high, but with 26 free lines between rows tap help reaches the whole row |
| Map and game options (`ui/dialogs/mapselect.lua`) | On the lobby's page: 956 wide, it fits | The next job can give it a page of its own |
| LAN screen | A page of its panel's width and its Create button, which stands out of it: 1014 wide, 1.62 times as large | 974x734 |
| Options | **Reflowed**, no page: a factor of about 1.75, for rows of 5.8 mm as far as 440 of FAF's lines allow, and as many rows as fit, 5 at 768 lines; the list scrolls by finger. On a screen so large that FAF's whole dialog fits at that factor it stays as FAF has it | It stays sharp, and all of it is larger, not only what a finger hits. See below |
| Choices everywhere | Rows 5 mm by a larger size of FAF's font; the list grows to the left for its longer rows, up to the picture's edge, opens upwards if there is more room there, and has as many rows as show at a time, which scroll by finger; a page goes where it shows | |
| Choices of pictures (colour, faction) | Rows 5 mm, the picture filling them; they have no scroll bar, so if they do not fit below they go in columns to the left | 19 colours |

**Options' frame.** FAF's background is one Bitmap of 698x598 with the list's art in the middle. MAUI's `SetUV` shows a part of a picture, so
two Bitmaps of the same picture show its top and its bottom, with the same part of the middle left out, and the dialog's own Bitmap is
transparent and keeps FAF's anchors. The list's art is even from top to bottom, so nothing shows of the cut. The hook
gets at what it moves by FAF's locals of the file: `Button`, `Grid` and `CreateOption` are wrapped while the dialog is built. The buttons stand by the
dialog's bottom now, and the rows' height says how many are shown.

### What it came to

Measured by the census (`android/tools/ui_census`) at the phone's size; pixels in the picture, mm on the glass, with the scale of the screen: the
lobby's page shows 6.5 of the picture's pixels to the mm, the LAN screen's 6.5, and without a page it is 10.5. "Reach" is what tap help adds, 2.5 mm
around, where nothing larger that takes events is in the way.

| Screen | Control | Lua | Pixels | mm | |
|---|---|---|---|---|---|
| Lobby | Slot rows, 8 players | `ui/lobby/lobby.lua:2780` | 44 apart | 6.7 | the whole row is a choice's by reach |
| | Slot rows, 16 players | | 22 apart | 3.4 | reach picks the nearest |
| | A slot's name, colour, faction, team | `:2864`, `:2884`, `:2922`, `:2946` | 257x18, 59x18 | 2.8 | reach; the list opens larger |
| | A dropdown's rows | `ui/controls/combo.lua:194`, `:733` | 33 | 5 | scroll by finger; colours in columns |
| | Ready | `:2999` | 51 wide, about 14 high | 2.1 | reach, and the row's free part |
| | The faction you play | `:6393` | 58x58, 22x22 | 8.9, 3.4 | reach |
| | Choose AI, the teams' number | `:3193`, `:3208` | 263x18, 44x18 | 2.8 | reach |
| | Fill Slots, Clear Slots | `:3200`, `:3204` | 129x30 | 4.6 | reach |
| | The small toggles | `:3783`..`:3953` | 98x42 | 6.4 | |
| | Settings, Patchnotes, Options, Load, Back | `:3135`, `:3329`, `:3352`, `:3719`, `:3764` | 130x42 | 6.4 | |
| | Launch Game | `:3708` | 294x78 | 11.9 | |
| | Hide default options | `:3317` | 121x26 | 4.0 | reach |
| | The map's zoom | `:3306` | 30x30 | 4.6 | reach |
| | Chat's field | `:4447` | 474x22 | 3.4 | reach; a tap opens the keyboard |
| | Scroll bars | `:3704`, `:4393`, `:4457` | 14 wide | 2.1 | not needed: the lists scroll by finger, and reach finds them for a drag |
| LAN screen | Name, address, port | `ui/lobby/gameselect.lua:155` | 375x19, 290x19, 79x19 | 2.9 | reach; a tap opens the keyboard |
| | The list's rows, Join and Observe | `:527`, `:536`, `:566` | 58 high, 63 apart | 9.7 | |
| | Sort tabs | `:342` | 60..291x22 | 3.4 | reach |
| | Connect, Back, Create | `:299`, `:178`, `:203` | 198x70, 358x70 | 10.8 | |
| Options | Rows | `ui/dialogs/options.lua:213` | 1050x59 | 5.6 | |
| | Choices | `:44` | 436x31 | 3.0 | reach, in the row's free part too |
| | A choice's rows | `ui/controls/combo.lua:194` | 54 | 5.1 | scroll by finger |
| | Sliders and their thumbs | `:141`, `:143` | 345x38, 52x38 | 3.6 | a drag from anywhere on the bar |
| | Tabs | `:476` | 310x101 | 9.6 | |
| | Reset, Ok, Cancel, Apply | `:311`..`:346` | 296x87 | 8.3 | |

What is still not comfortable: the slot rows at 16 players (22 apart, 3.4 mm, choices 2.8 mm: tap help picks the nearest); the
combos' own buttons, which keep their art at 2.8 mm in the lobby and 3 mm in Options and rely on tap help; the score panel's and observers'
small texts. The picture is scaled up on a page, so it is a little soft. 19 colours in two columns or three cover what is beside them while open.

Not tried on a real phone: all of this is the emulator's composition and its touch screen, at the phone's size. A real LAN's server list:
the emulator's network shows none, so the list was tried with made-up entries, one of which pointed at the PC's game (`Join` worked), and
the PC's game by address.

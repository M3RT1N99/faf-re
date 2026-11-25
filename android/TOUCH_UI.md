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
  finger scrolls down, and lists scroll inside it; a dialog that fits gets a factor of its own, which leaves it sharp. There is no
  zoom gesture (the user's decision). See "The menus".

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
| Replay, load and save | `ui/dialogs/replay.lua`, `saveload.lua`, `ui/controls/filepicker.lua` | 702x586 | rows 20 high at any factor (1.9 mm), buttons 198x70 |
| Profile | `ui/dialogs/profile.lua` | 578x446 | rows 18 high, scroll buttons 34x26 |
| Mods Manager | `ui/lobby/modsmanager.lua` | 850 wide, the screen's height | filters 180x33 (3.1 mm), a star 25 wide |
| Map and game options | `ui/dialogs/mapselect.lua` | 956x692 | map rows 18 high, choices 18 |
| Unit Manager | `ui/lobby/unitsmanager.lua` | all of the picture | cells 43 (4.1 mm), buttons 130x42 |
| Licence | `ui/menus/eula.lua` | 718x598 | its font of 12 points |
| A question | `ui/uiutil.lua` | 428 wide | buttons 130x42 (4 mm) |
| LAN's Create | `ui/lobby/gamecreate.lua` | 478x318 | fields 16 high |
| Campaign | `ui/campaign/selectcampaign.lua`, `operationbriefing.lua` | all of the picture | rows 370x34, a film's buttons 34x34, a choice's rows 20 |
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
- No hook may stop doing its work without a word on a later FAF (`fafre/scale.lua`). What a hook wraps or reads of FAF's goes
  through `Scale.Needed` before the hook changes anything: a field of a class or of a control, or a table's entry, is nil where it
  is gone, which is an error then, and the hook's `pcall` says it (`fafre_ui: <file>: this FAF has no <name>`); the hook leaves
  FAF's file as it is, where wrapping nil would have broken FAF's own code. A name that is gone, a global, a local of FAF's file
  or a module's, is an error by itself where it is read (FAF's `lua/system/config.lua` and `import.lua`; a hook is the file's chunk,
  below), and `Needed` only reads it early. A local that is no value until FAF sets it, as the score dialog's `dialog`, is read
  plainly. What a hook's wrappers read later goes through `Try`, and each failure is said once a state (`Scale.Warn`), however
  often FAF builds. A file FAF no longer has runs no hook at all: as each UI state is set up (`SetupUI`, which the engine runs for
  the front end, a lobby from the command line or from the FAF client, and a session alike), `CheckHookedFiles` lists the
  package's hooks (`DiskFindFiles` of `/fafre_ui/lua`) and says once for each whose FAF file is gone (`fafre_ui: lua/<file>: this
  FAF has no such file, so its hook does nothing`), but where FAF replaced it by another file that has a hook of its own
  (`replaced`: FAF 3841's chat in `ui/game/chat/`, its patch notes in `ui/lobby/changelog/`). A file FAF keeps without what a
  hook wants of it is beyond that check: its hook says what it misses where it runs, unless it steps aside for the newer file's
  hook (`Scale.Replaced`), as the hook of 3841's `ui/game/chat.lua`, a shim the engine imports at Enter, does.
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
than a factor of two from the logical size, the density is wrong and the logical size is all there is. The activity keeps
itself where the screen's pixels or panel change (its `configChanges`: a resolution switched in a game, a foldable unfolded,
`wm size`), so the short side is worked out anew whenever the layer that takes the fingers changes its size, and with it
Griff's millimetre: its slops and reaches, the loupe's, the sizes the program measures by (`Game.setGriff`), and the rail's
slop at each move. The mouse scheme's slop (the system's) and the factor as the game started (used only until the game says
its own) stay as they were then. At 460 lines the
panels (372 lines from the multifunction panel down to the unit view) still leave the mouse's buttons about 6 mm each; Griff's rail, which FAF's UI draws (see "Griff"), grows with FAF's
panels.
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
| `lua/ui/controls/textarea.lua:8`, `lua/ui/dialogs/createunit.lua:2` | A local factor from `GetPixelScaleFactor`, as they are first imported | What it was: in the front end 1, in a session the factor at that moment. The option's value does not reach them by itself any more |
| `lua/ui/lobby/unitstooltip.lua:18` | The same, which its tooltip undoes to get at its height, and by which it goes above a unit or below it | Set by the hook as each tooltip is made: the file is imported before the Unit Manager's factor holds |

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
  cannot reach. Its factor stays until its root control goes. A child of the root says that, in that moment, as a dialog's
  `OnDestroy` is its own to set and what a dialog does as it closes (`MenuShow`, the next screen) has to be at the factor from
  before it. Dialogs nest, and go in any order. In the front end it holds too: there is no HUD, and the factor is what makes it as
  large on the glass as a session's (`GlassFactor`), which on a page that shows the picture as large already, the lobby's, is 1.
- `Built`, `BuiltAt`: a panel that leaves the HUD in use and builds nothing later, the Menu tab, or a question in the front end: its
  factor stays only while it is built, and all it made is evaluated before the factor from before it returns. What a later change makes
  evaluate again gets the factor there is then.
- `AtHud`: what the game calls for the HUD runs at the HUD's factor whatever dialog is open: `OnSelectionChanged`,
  `OnQueueChanged`, `OnBeat`, `OnPause`, `OnResume`, `OnSync` and the unit view's `OnFrame`.
- `AtFrontEnd`: the same for a screen of the front end that goes on under a dialog with a factor of its own, the lobby under the Unit Manager, at the
  front end's factor of 1. The dialog's factor is back when it returns, so what is due under the screen's root is evaluated there, but for what the dialogs made.
- `Try`: what a wrapper does beyond calling FAF runs later than the hook's own `pcall`, so it goes through this, which
  logs a failure with `WARN`, once for each name in a state (`Warn`).

What could not be ruled out: while a dialog is open, HUD code reached in other ways gets the dialog's smaller factor,
such as a thread's or a panel's own `OnFrame` that builds a control, and a layout function of the HUD that some change
makes evaluate again. The second heals at its next change, as the numbers of the economy panel do; the first stays
until that control is built again. An announcement that starts while Options is open is the likely case. Closing a
dialog showed the HUD as before, also after units had gone on building while it was open, and with the game paused. The
reverse should not arise: of a dialog only what `OnBeat` and `OnSync` build would run at the HUD's factor, and none of the
dialogs below was seen to build from there.

- `DialogAt`: as `Dialog`, for a dialog whose factor is its own, larger than the HUD's.
- `Page`: see "The menus". Inside a `DialogAt` it is at that factor.

The factor is one for all, so what lies under a dialog that holds one must not move. The main menu hides for its dialogs, and a question
holds none. The lobby goes on under the Unit Manager, which holds the HUD's size on the glass: what the host sends and a peer that goes fill slots and set fonts, offsets, the map
and the options, the ping's thread names a peer who has come, and what is typed goes to the chat. All of that runs through `AtFrontEnd` (the hook of `ui/lobby/lobby.lua`). What else
the lobby does by itself does not change its layout: the ping's bars, the letters of the map's name. One thing does change under any dialog: the button that opened it, which the
finger leaves after the dialog is there, and which FAF then sizes anew with its next texture at the dialog's factor. So a bitmap keeps the size it was made at (the hook of `maui/bitmap.lua`).

### The hooks

| Hook | Why |
|---|---|
| `lua/sessioninit.lua` | The HUD's factor, in the session's fresh state, and `OnSync` at the HUD's factor |
| `lua/ui/game/gamemain.lua` | `OnSelectionChanged`, `OnQueueChanged`, `OnBeat`, `OnPause`, `OnResume` at the HUD's factor; the game's UI starts Griff's report of the selection and its pieces in FAF's UI (`fafre/griff.lua`, `fafre/griffhud.lua`, see "Griff"), or where a finger is the mouse the bar (`fafre/bar.lua`: *Order*, *Shift*, *Ctrl*, *Del* and *Keys* under the multifunction panel, as large as the room above the unit view allows, up to 9 mm; toggles of the touch layer, `FafreTouchBar`) |
| `lua/ui/game/construction.lua` | Griff: a build icon's click (FAF's `OnClickHandler`, which the panel's buttons take when they are made) puts its building into the draft (`fafre/griff.lua` `BuildIconClicked`, after FAF's own handler), not FAF's ghost; FAF's build mode keys arm the build mode themselves (`buildmode.lua`), and a template's key (no mouse button) and click (no build item) reach the handler but stay FAF's |
| `lua/ui/game/unitview.lua` | Nothing hovers on a phone: FAF's "Single Unit Selected Info" is on, or a selected unit's view would go with the pointer; the abilities list, which stands over the middle of the screen, only for a finger that rests on a unit |
| `lua/ui/game/tabs.lua` | The Menu tab has the app's *Keyboard* after Key Bindings, in both touch schemes: it brings the phone's keyboard as the bar's *Keys* does (`FafreKeyboard`, `MenuFingers.cpp`; Griff's rail has no *Keys*). The large buttons are as large as the list FAF shows fits: `Built` with 137 of FAF's lines and 70 a button of the list as `BuildContent` picks it, on 768 lines a single-player game's eleven at 0.85, a replay's ten at 0.92, a network game's eight at 1.10. What a tab shows is told to `fafre/opened.lua`: nothing of Griff's covers it (see "The rail, the control groups and the keyboard") |
| `lua/ui/game/multifunction.lua` | Its dropouts (the overlay filters' list right of the panel, the map's options, the team colours) are told to `fafre/opened.lua` as their backgrounds are made: nothing of Griff's covers them |
| `lua/ui/game/controlgroups.lua` | FAF's place, 368 lines down, is under the construction panel: below the avatars, which grow. With Griff, groups 1, 2 and 3 always show in FAF's column: FAF's own controls, or for an empty group the same frame with its number (see "Griff") |
| `lua/ui/game/avatars.lua` | A finger's pointer leaves after each tap (below), which FAF's avatar took for the mouse going and started its round of the idle units over, so that each tap took the first: the round goes on while the unit it took last still stands just before it in the avatar's list, which FAF makes anew each beat without the units ordered since, and starts over at the best idle unit where it does not, as a mouse's click after the order does |
| `lua/ui/game/minimap.lua`, `lua/ui/game/chat.lua` | Their default places lie under the multifunction panel, Griff's rail and the unit view: a new window goes where it covers no panel and none of Griff's pieces, the minimap right of the screen's middle and the chat window above it ("The rail, the control groups and the keyboard"), and the reset button brings it back there (the chat's lines made again for its size); a place the user chose stays. From FAF 3841 on `chat.lua` only forwards to the new chat (the engine imports it at Enter): its hook does nothing there |
| `lua/ui/game/chat/chatinterface.lua` | FAF 3841's chat window: its `DefaultRect`, the new window's place and its reset button's, lies at the HUD's factor below the picture; it is the chat's home, 8 of FAF's pixels wider for the title this window has |
| `lua/ui/dialogs/options.lua` | `DialogAt`: shown larger, with fewer rows (see "The menus"); the UI scale needs no restart |
| `lua/maui/bitmap.lua` | A bitmap keeps the size it was made at (above) |
| `lua/maui/window.lua` | Each window (the chat's, its options', the minimap's) is told to `fafre/opened.lua` as it is made: nothing of Griff's covers it |
| `lua/ui/uiutil.lua` | A question in the front end is `BuiltAt`, as large on the glass as a session's. It holds no factor, as the lobby asks one (that it is connecting) just before it builds all the rest |
| `lua/ui/controls/popups/popup.lua` | A popup shows where the page stands, from its top if it is higher than the screen. In a game nothing of Griff's lies over it: it is told to `fafre/opened.lua` with the screen's box, which its shadow takes |
| `lua/ui/controls/filepicker.lua`, `lua/ui/dialogs/replay.lua`, `saveload.lua` | `Dialog` for the three dialogs around a file picker, with the frame cut as Options' is; rows 5 mm, where FAF sets 20 pixels at any factor |
| `lua/ui/dialogs/profile.lua` | `Dialog`, 510 lines; its list's rows |
| `lua/ui/menus/eula.lua` | `Dialog`, with the frame cut |
| `lua/ui/dialogs/modmanager.lua` | FAF hands the function that shows the main menu again to where the Mods Manager goes through the other players' mods, which calls it as the mods load: the menu came back under the dialog, at the dialog's factor. Here it comes when the dialog has closed |
| `lua/ui/lobby/modsmanager.lua` | `Dialog`: FAF lays it out by the screen's height at any factor |
| `lua/ui/lobby/unitsmanager.lua` | `DialogAt` and `Page` of the whole picture; a resting finger reads a unit's tooltip and changes nothing |
| `lua/ui/lobby/unitstooltip.lua` | Its local factor as each tooltip is made (above) |
| `lua/ui/dialogs/mapselect.lua`, `lua/ui/lobby/changelog.lua`, `lua/ui/lobby/changelog/changelogdialog.lua` | The rows of the maps and of the patches; the patch notes show from their top. FAF 3841 has its patch notes in `changelog/ChangelogDialog.lua` and `changelog.lua` no more |
| `lua/ui/lobby/gamecreate.lua` | `Dialog` |
| `lua/ui/campaign/selectcampaign.lua` | Skipping the timeline's film (a tap, or Back) went to a call of FAF's with arguments it no longer takes, which left a black screen: it goes where the film's end goes |
| `lua/ui/campaign/operationbriefing.lua` | The rows of the choice of difficulty and faction, `BuiltAt` a factor that makes them 5 mm |
| `lua/ui/uimain.lua` | A new front end takes the fingers and the view back from what the last state declared; each state, as it is set up (`SetupUI`), says which hooks this FAF runs no more, as their files are gone (`CheckHookedFiles`, above). The front end's first screen gets the bar's *Keys* in its top left corner (`fafre/bar.lua`, 6.5 mm), over FAF's screens to come, while the touch layer takes fingers |
| `lua/maui/layouthelpers.lua` | The front end's factor is 1, whatever the UI scale says |
| `lua/maui/slider.lua` | A press on a slider's bar also takes the thumb, for the drag that may follow |
| `lua/ui/controls/combo.lua` | A dropdown's rows are 5 mm on the glass, it stays on what the screen shows, and it opens with its choice in view |
| `lua/ui/lobby/lobby.lua` | `Page`; the slot rows' distance; the row under a finger in the list of observers; what goes on under a dialog with a factor of its own stays at the lobby's (`AtFrontEnd`) |
| `lua/ui/lobby/gameselect.lua` | `Page` |
| `lua/ui/dialogs/keybindings.lua`, `lua/ui/notify/customiser.lua` | `Dialog`, 740 lines |
| `lua/ui/game/connectivity.lua` | `Dialog`, 640 lines, which hold nine players |
| `lua/ui/dialogs/score.lua` | `Dialog`, 646 lines; its root is `dialog`, as setting the skin rebuilds the HUD before it makes its first control |
| `lua/ui/dialogs/hotstats.lua` | Sets its local factor (above) |
| `lua/ui/game/announcement/titleannouncement.lua`, `titletextannouncement.lua` | FAF puts an announcement's title 250 of its lines above the screen's middle (*Replay over.*, a player defeated, the pause, *Unit Cap Reached*), which at 1.6 on 768 lines is 400 of the picture's 384 to its top: the title stood above the screen, and of a title with text only the end of the text showed. It stands no further above the middle than FAF's least screen has it, 250 of its 768 lines (`fafre/scale.lua` `AboveMiddle`: its middle 134 of the picture's lines down, under the tabs), its text below it, and the content's bottom follows the text, which FAF takes once where the title stood before. On a screen of 768 of FAF's lines or more FAF's own place |

### The pointer

A mouse's pointer goes on being somewhere, and the game shows what it is over. So 300 ms after the last finger has
lifted (`TouchInput.leaveSoon`), `Game.pointerLeave` moves it off the window, to -1000,-1000. wx-lite's idle handler then
sends the leave a Windows program gets, the event mapper tells the control it left, and rollovers and tooltips go. While a
finger rests, the pointer is there and they show, until the hold's right button comes at 350 ms, which FAF's buttons take
for a press. A new finger takes the leave back, first thing and also when it is already on its way to the game: the
leave carries a count of the fingers that have come down, and is dropped if the count has moved (`Game.pointerStay`). It
stays with a building's ghost over the world (`UI_GhostWaitsAt`), and leaves a build icon that started the ghost. While
Griff's Distribute orders holds the cursor at the group's middle until a frame has drawn it there, the leave waits and comes
after it, by the same count (`grifftap::Leave`, tested).

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
events (a side under 5 mm) and takes the nearest by distance to its rectangle; what the tap is on stays ahead of what is
only near, also of a child: the lobby's AI choice holds its Fill Slots button. It clicks the pixel of the control nearest to the tap, not its
centre, as sliders and scroll bars act on the place; a tap on the control stays where it landed. That pixel has to find the
control or a part of it, not what lies over it. Of each control the walk reads Left and Right first, and Top and Bottom only
where it can be the one, 2 pixels wide or more and within reach across (`Nearest::MayWant`, tested): the engine's own hit test
reads a control's rows only where the place lies between its Left and Right, and FAF lays out no more than that. FAF's border
group of the game (`borders.lua`), 0 wide, has no Top at all; reading it at a session's first tap failed with the engine's
warning `Evaluating LazyVar failed` (`control.lua` Bottom and Top), which a PC's mouse never meets.

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
the left edge between the multifunction panel and the unit view, 138 and 234 of FAF's lines from the two ends (the unit view's
top while a selection shows the construction panel, `HudScale`). The right
edge, which has more height, belongs to the avatars with their idle lists and to the control groups, and both grow.
In the menus, on the score screen and over a dialog in a game, whose fingers are the menus', only *Keys* stays, 6.5 mm, in the upper left
corner, which no menu uses for anything but a title. A toggle that is on goes off then. With the touch scheme Griff the rail is FAF's
UI's in a game, a row of FAF's order buttons at the top left (below, "The rail, the control groups and the keyboard"), and *Keys*
is the menus' only: in a game the keyboard is *Keyboard* in FAF's Menu tab, in both schemes. A thumb on a rail button taps within
Griff's slop of 2.5 mm.

### Griff

The touch scheme Griff (`TouchScheme`, chosen on the game card for the game's next start; buildstage/touch-concept-v2.md, scheme A2,
and its mockup buildstage/touch-preview, whose numbers it takes: buildstage/griff-v3-spec.md) plays a game with FAF's own inputs only:
each gesture ends in a click of the left or right button, a drag of the left or middle one, Shift, a command mode as FAF's orders panel
arms it, or a console key action FAF binds to keys, what a PC player gives with a mouse and a keyboard. FAF decides what they order,
and nothing else reaches the simulation. The camera is the UI's, which FAF's Lua moves as a player's keys and minimap do. The activity
keeps the fingers (`TouchInput` with `setGriff`: slop, timing, the tap ladder, the stroke, two fingers, the draft's gestures; `Rail` for the
rail; `Stroke` for what a stroke gives; `Draft` for the draft's plans as the screen shows them; `Undo` for the undo's button). All Griff
shows is FAF's UI's, in FAF's own look at the HUD's factor (Griff v4, the user's verdict on 0.14.0: every element as an original one of
FA's), each piece checked side by side with the element of FAF's it follows (2026-11-15, on the emulator at the phone's size and HUD 1.6,
in the four factions' skins where the skin changes it, but for the stroke's label, FAF's tooltip, whose colours follow the skin and which
was seen in UEF's only). `fafre/griffhud.lua` has the rail's *Box*, *Plus*, *Groups* and in a replay *Time*, the undo's button
and the draft's tick and the cross, Griff's Esc, as FAF's order buttons, the hints where FAF prints its own messages and a waypoint marked for deletion
under FAF's Cancel icon. `fafre/griffmap.lua`, on the map's view as FAF's reclaim labels are, has the handle as the brackets FAF draws
about a selected unit, a resting finger's ring as a ring of FAF's pings, a stroke as FAF's command graph draws the route once it is
ordered, the draft's plans as the ghosts of FAF's build preview, *Box*'s box over deposits as FAF's selection box and the loupe's frame
as FAF's marker ring. `fafre/griffgroups.lua` has the groups' icons in FAF's strategic icon art ("The groups"). The activity says where its
pieces are (`Game.setGriffShown`, `native/src/host/GriffShown.cpp`) and lays a view of its own over *Box*, *Plus*, the undo's button,
the tick and the cross where FAF's UI reports them (`Game.griffHudPlaces`, `HudTarget`), which takes their fingers. Only the loupe's
picture is the activity's own (`LoupeView`, framed by FAF's ring): FAF's UI has no picture of the game's picture to magnify, and a
second world view cannot show the 6 mm about a finger at twice their size, as every camera stops at the console's nearest zoom
(`cam_NearZoom`) and shows at its focus the width that zoom gives. While the player hides FAF's UI (`gamemain.lua` HideGameUI), all of
Griff's pieces hide with it but a resting finger's ring and what a finger draws, and the views over its buttons take no finger.
What a gesture clicks is decided on the program's thread, which knows what lies under the finger (`native/src/host/GriffAndroid.cpp`),
with `fafre/griff.lua` for what only FAF's Lua knows. A finger stays within 2.5 mm, the mockup's slop:

| Gesture | FAF's input |
|---|---|
| Tap on the map | On one's own unit the left click, which selects it. One's own are the focus army's, as the engine selects no other (`CanSelectUnit`; FAF's world view leaves the click to it), which in a replay is the army that the score panel's row or the replay's army list focuses; an observer without a focus army, as a replay begins or after a defeat, selects nothing, by a PC's click as by a tap, which is the left click there too. On anything else, with a selection, the right click, which FAF makes the order for what is there; without one the left click. `TapKind` reads what the cursor is over (`GetRolloverInfo`, `GetRightMouseButtonOrder`) once the cursor has rested there, 120 ms as for every tap. A tap that misses a unit by a little still takes it: FAF's own pick reaches 3.5 mm about the cursor with nothing selected that takes orders, 2 mm with troops (mobile units, or factories alone), so that a tap for a move beside one's own units does not take the selection from them; the program sets FAF's console variable `ui_SelectTolerance` (an option of FAF's) at a first finger's down where it differs, but not while a command mode is armed, which picks as FAF sets it (its reclaim mode reaches further), and puts back the value it found when the game ends; both in as many digits as the console needs to read the very float back (`include/GriffTap.h` `FloatText`, tested). *Box* counts for the tap as it did at the finger's down, which set the reach. With a command mode armed (FAF's attack-move button's too): its left click. *Plus*: with Shift. *Box*: Shift's left click on one's own unit, which adds it or takes it out; on anything else nothing, and *Box* stays lit. A tap uses the rail's buttons that counted for it (`Rail`): *Box* only where it took or let go a unit, so a tap on FAF's UI or an armed mode's puts *Plus* out and leaves *Box* lit, and *Box*'s tap on nothing uses none (`include/GriffTap.h` `Used`, tested). With *Plus* and troops selected, a tap on one's own unit is the order there too (FAF's right click: assist or guard); adding a unit is *Box*'s |
| Tap on a panel, or on the map within reach of a small control | The tap help's left click (above), as with the mouse |
| Taps in a row | The tap ladder (`TouchInput.TapLadder`): a tap within 350 ms of the last one's lift and 6 mm of it climbs a rung by what the ladder's first tap was, whatever lies under the next, which the program says of each tap (`Game.griffSaid`). After a tap that selected one's own unit: all of its type on the screen (FAF's key action `UI_SelectByCategory +inview` with the type's own category, which the engine gives each blueprint: a double click at the first tap's place could land on the panels its selection brought up), then all of it on the map (`UI_ExpandCurrentSelection`, FAF's select_all_units_of_same_type). After a plain move (FAF's right click there was a move, without *Plus*): an attack-move at the first tap's place, which replaces the move, as FAF cannot change an order's kind (where FAF offers no attack-move, as for factories alone, the right click again: the rally point). After a tap on nothing with nothing selected: all one's own mobile units on the screen (`UI_SelectByCategory +inview MOBILE`). A tap on FAF's UI is clicked again where the first went, within the game's own double-click reach, so the message layer makes it its double click. *Plus* and *Box*, a drag, a rest, a stroke and two fingers start the ladder anew. The handle leaves a finger to the ladder where it may climb (`TapLadder.climbing`: soon and near enough, below the third rung, not with *Box* or *Plus* counting); where its lift does not climb after all, as when it came too late or the program had not said what the first tap was, it is the handle's tap, never FAF's click at the handle's place. Only that quick lift is the ladder's: a rest of the finger or a move past the slop is the handle's, from where the finger came down, as for a finger after the ladder's time (`TouchInput` `takeForHandle`): a rest the handle's rest, FAF's Distribute orders with its ring and label where the program says so (below), else the handle's tap at its lift, never the map's ring or right button at the handle's place, which would be a move or a formation there; a move the stroke |
| Rest 300 ms and lift | A ring about the finger shows the rest, and the program hears of it (`Game.griffRest`, Griff v4): a ring of FAF's pings (`marker/ring_*02-blur.dds`, as large as two order buttons), in the look of what a drag from the rest gives, as the program says once it has found it (`Game.REST_`): the move ping's blue for FAF's formation, the alert ping's yellow for the left button's drag, as a mouse's there (FAF's box on the map, an armed command mode's own drag, the minimap's camera, a panel's), the attack ping's red where it gives nothing: on a waypoint marked for deletion, and on a Checkbox of FAF's, whose press a slide leaves alone (below), though the lift there stays the panel's right click. On a waypoint of the selection's orders within the tap help's reach (one FAF draws in its command graph, `griffundo::Removable`), Shift goes down so that FAF draws its waypoints and FAF's cursor goes onto it; while FAF's cursor hovers that very command, FAF's own Cancel icon (`orders-panel/no-parking_bmp.dds`, the Cancel of its mouse tooltips) marks it (`griffhud.lua`, `FafreGriffMarked`), and the lift takes that order out with FAF's Shift+Ctrl right release, which strips it from every unit it went to, as the undo does (`DeleteMarked`); where FAF's cursor did not hover it then, a hint says *Not deleted* (FAF keeps a factory's last rally point so). Waypoints show while Shift is down, as for a PC player: with *Plus* held. With units selected, on the ground where FAF's right click is a plain move and no command mode is armed, the rest is the right button's, as the mouse scheme's hold: its lift is FAF's right click there (*Plus*: Shift), the order, watched for its undo. On a panel the tap help's right click (a factory's icon: one less; an avatar: select and go to it). Elsewhere on the map nothing: the cursor stood there, so FAF showed what is under it (on one's own unit its abilities, the hook of `unitview.lua`). With *Box* no rest (the mockup's): no ring, and the lift on the map is *Box*'s tap; *Box* as it counted at the finger's down. A second finger, or the system taking the fingers, ends a rest with nothing (`Game.griffRestGone`), a mark's Shift with it |
| One finger that moves | On the map the middle button's drag, FAF's pan, pressed where the finger passed the slop, so the map does not jump by it. After a rest on the ground with mobile units selected (above) the right button's drag from where it rested, FA's formation: FAF turns it to face the way the finger goes, as its right-button drag does, and orders it at the lift (*Plus*: Shift), watched for its undo. A second finger gives it up and says *Cancelled*, also one that came before the program said the drag is FAF's formation (`Game.SAID_FORMATION`), at the word, and so, without a word, does the system taking the fingers or a first finger that comes while the gesture still runs: the world views forget the order a release would give and the formation FAF shows, then the right button goes up (`GiveUpDrag`), as only a lifted finger orders. That runs in a task of its own, which comes after the right button's press also where the give-up was asked before the drag's own task had run (`GiveUpFormationTask`). On the emulator (2026-11-15, the final build) Home in the middle of the drag, which gives the activity the system's cancel (0.2 s after the drag's first move, 0.6 s before the finger lifted), and a second finger after ten moves each left the ten tanks selected with no order, where the same drag alone ordered all ten. One constant chooses what this drag gives (`GriffAndroid.cpp` `kRestDragGivesFormation`, the user's to decide: FAF's formation as the mouse scheme's hold, or 0.14.0's box); a selection without mobile units (factories alone, whose right click there is the rally point) boxes either way, and the rest's lift stays FAF's right click. After a rest on a waypoint nothing: the finger slid off, and its mark goes with Shift. After any other rest, and with *Box* at once, the left button's, FAF's box, which *Plus* makes add (Shift): so with a selection *Box* arms the next drag as a box, where nothing is selected a rest and a drag box, and a two fingers' tap still clears the selection. On a panel or the minimap the left button's drag, as a mouse's, but from a Checkbox of FAF's nothing, also after a rest on it: FAF's Checkbox acts on the press (`maui/checkbox.lua`, found by its `ToggleCheck`), so a thumb that slid from the orders panel onto the map fired the button it started on, Stop or a toggle, which no mouse does by sliding, and a rest that reads a tooltip had no way off it that did nothing. That is the orders panel's buttons and toggles, the construction panel's tabs and the panels' collapse arrows; a mark over one that takes hit tests of its own (an order toggle's ring, its question mark for a mixed selection) counts as the button, as the press goes on to it (`include/GriffTap.h` `DragPressesNothing`, tested). The pointer goes with the finger, and a rest that lifts without sliding stays the panel's right click (above). FAF's buttons, which act at the release (the build icons, the factory queue's reorder by its drag), the minimap, windows, scroll bars and sliders keep their press. A pan that lifts while moving (its last move within 70 ms) glides on (not while the program's clicks run, below) at 0.9 of the finger's speed, slowing by e^-6t until under 0.7 mm a second: `griff.lua` moves the focus each frame of the UI by the ground the map's velocity takes past the view's middle, at FAF's own pan rate (`CameraPan`: the ground's breadth a pixel across the middle, both ways), on the system's clock, so a paused game glides too. A finger on the map or the minimap stops it, as do a key or the minimap moving the camera, and FAF's bounds of the camera |
| Two fingers | They zoom at their middle and move the camera with it at once, through the UI's camera (`griff.lua` `Pinch`, `SaveSettings`/`RestoreSettings`): the ground under their middle when they began stays under it, the zoom by how far apart they are from then, within FAF's own limits (`GetMinZoom`, `GetMaxZoom`), and the camera tilts for its zoom as FAF's does. Each move is measured on the camera the last frame drew, so it holds however many come in a frame, one frame behind the finger as FAF's own pan is; `griff.lua` takes that camera each frame apart from the glide's and the flight's steps, which an error stops alone, and where it has none the fingers do nothing. The finger left of the two moves the camera on, and a finger again beside it pinches anew. A second finger on a pan ends the pan and the two pinch, once the program has said the drag is a pan (`Game.SAID_PAN`, by the drag's number); on a box, a panel's drag or the minimap's it does nothing, and the drag goes on. One that came before the program's word, which comes a program frame and a post after the drag began, waits for it and is no gesture's until then: the word for a pan makes two fingers of it, whose lift is no tap of two, and the word for FAF's formation gives the formation up; with no word, on a box or a panel's drag, it waits for its lift. A quick tap of two, within 350 ms of the first finger down, each finger within 1.5 mm of where it was as the second came down and their distance within 1.5 mm of what it was then, ends an armed command mode (`EndCommandMode`), else `SelectUnits(nil)`, also where the first finger came down on the handle, which gives no stroke up then. Two fingers that twist about a still middle pinch, which moves nothing. Two that began on a pan or a pinch, where the first had travelled already, are no tap |
| Tap on the handle | The camera flies to the group's middle, 1 - e^-14t of the way each frame (`ToGroup`), its zoom and turn kept, until it is within a pixel of it, or where FAF's bounds hold the camera (it moved less than half the step asked); at once where the system's animations are off |
| Rest 300 ms on the handle and lift | FAF's Distribute orders, its key action G and Shift-G (`spreadattack`), which FAF's UI has no button for, where the program says the selection can (`Game.handleRest`, `griff.lua` `CanDistribute`, `SAID_DISTRIBUTES`): two of the selected units or more that are no factory, and of them the one FAF itself picks with the cursor at the group's middle, the nearest to the world place under it (`distribute-queue.lua` `DistributeOrders`), holds two orders or more. Then a ring shows, the move ping's blue, and 14 mm above the finger FAF's tooltip with FAF's own words for G (`<LOC key_desc_distribute_orders>Distribute orders`, localized), the label alone. The lift within the slop puts the cursor on the group's middle and, once a frame drew it there (FAF reads the world place under the cursor, `GetMouseWorldPos`), runs FAF's own action string, which clears every queue and deals the unit's orders out over the selection (moves and reclaims in batches, attacks with full redundancy, builds one per engineer where plans and engineers match, else in batches: several plans to an engineer, or several engineers to a plan) and says *Distributing orders* where FAF prints its messages; the undo's record ends, as every queue is given anew. The pointer's leave that the lift asks for waits for that frame (*The pointer*, above); without that, on the emulator's frames of about 130 ms (one of 700 ms), the leave came first, took the cursor off the group and the lift only said *Cancelled* (2026-11-18). Where the group's middle has left the map by the lift, or a finger moved the cursor before the frame, *Cancelled*. A move past the slop is the stroke as ever, which a short loop back gives up. Where the program does not say so (one unit, one order or none, the group out of view or under a panel), nothing shows and the lift is the handle's tap, so a slow tap never deals queues out. So a spread move is a route or *Plus*-held taps, then the rest; a spread attack *Plus*-held taps on enemy units, then the rest; spread builds the draft's tick, then the rest |
| Stroke from the handle | Waypoints about every 15 mm and at the corners (`Stroke.waypoints`); each the right click at its place, in a task of its own so that it sees what the one before did; the first replaces unless *Plus* counts, the rest go with Shift. Each picks what is under it, as the world view updates its pick at each event (`CUIWorldView::HandleEvent`), so the last one gives the order for what the stroke ends on, also on the minimap. While it is drawn, the cursor hovers at its end every 0.1 s, so FAF shows its order cursor there; FAF's UI shows the route close to how FAF's command graph draws it once it is ordered (`griffmap.lua`, `FafreGriffWaypoint`, the engine's own sizes): its order line from where the stroke began through each waypoint, a band of the order's colour as wide as FAF's for the selection's count (`commandwaypoint.lua` CalculateWaypointLineWidth: the middle half of that width and the brighter edges its texture shows on either side), straight from waypoint to waypoint as FAF's Lua draws lines on the map (`UI_DrawLine`), where the command graph rounds its corners and ends the line in its arrowhead, and a patrol's and an attack-move's the same band, where FAF's run as chevrons (`orderline_arrow02.dds`); FAF's waypoint icon at each waypoint, laid on the ground as large as the engine draws one there (its units counted up to `ui_CommandGraphMaxNodeUnits`, within `ui_MinWaypointSize` and `ui_MaxWaypointSize`); and a label 14 mm above the finger, FAF's tooltip (`tooltip.lua` CreateToolTip, in the skin's colours), says what lifting gives, from what FAF's right click there would order: Route, Attack, Assist (with engineers) or Guard, Reclaim, Capture, Order for FAF's others, Attack move with FAF's attack-move armed, Minimap, Patrol, Cancel. The handle stays where the stroke began. The lift counts where the finger was 50 ms before, as fingers wobble as they lift. A stroke that ends within 8 mm of where it began is a loop: longer than 40 mm, FAF's patrol (its orders panel's mode, `StartCommandMode('order', {name = 'RULEUCC_Patrol'})`, for each waypoint, armed again where FAF ended it), where FAF offers one (for factories alone a loop is their rally route, as any stroke); shorter, the finger took it back. Waypoints over a panel are left out, and a stroke that ends on a panel other than the minimap gives nothing. With FAF's attack-move armed as it began (its orders panel's button, `AttackMoveArmed`) each is an attack-move, armed again where FAF ended the mode after a replacing order, as a PC player's clicks with Shift in that mode give; any other mode armed goes first. A stroke given up, by a short loop, a second finger or an end on a panel, says Cancelled; a second finger before the stroke has left the slop is the two fingers' gesture instead, whose quick tap lets the selection go (below) |
| *Plus* held | Shift held, in the order of the clicks. When the app's Shift goes up, FAF's check of each beat runs at once (`ShiftLetGo`, `commandmode.lua` `OnCommandModeBeat`): a command mode that an order with Shift used ends, which FAF otherwise does at its next beat, and a paused game has none. So a lit *Plus* appends one order in a mode and the mode ends with it, an attack route's mode ends with the route, and a held *Plus* keeps the mode until it goes up |

Attack-move is FAF's own (Griff v4): its orders panel's button (`orders.lua`: `AttackMove`, slot 1, offered for engineers, or mobile
units with a fire state), which a tap or a route takes as any armed mode, and the tap ladder's double tap on the ground, as a hotkey;
the rail has no copy of it. For a factory alone the ladder's attack-move stays the right click: FAF's `AttackMove` sim callback fails on
structures, on every client. While FAF's own ghost waits (its build mode keys, hotbuild, a template), the fingers place it as in the mouse
scheme, and with no game behind the UI any more (its end) a tap and a rest are the UI's clicks wherever they land, but on the map a drag
still pans and two fingers still zoom (`GriffAndroid.cpp` `CameraAt`): a replay that is over, or a game the player stays in after its end,
is still looked over, as a PC's middle button and wheel move its camera then too. Until 0.16.2 its end left the map still, as each drag
was a panel's and two fingers nothing. Griff keeps the game's
cursor off the picture's edge pixel: FAF scrolls the view while its cursor is on the window's edge (`ui_ScreenEdgeScrollView`), where a
mouse is pushed on purpose and a finger at the glass's edge would hold it, which took a stroke's route elsewhere than it was drawn.

FAF's split screen (the map's options or the minimap's: a second map view on the right, its camera `WorldCamera2`; `worldview.lua`
`CreateMainWorldView`) takes the fingers on either view, as a PC's middle button and wheel move the camera of the view under the mouse:
a pan, its glide and two fingers move the camera of the view they are on (`include/GriffCamera.h`, tested). A pan's is the view its
press came to, whose camera FAF's middle-button drag moves, and its glide goes on there; a pinch's the view under it where it began,
also while the finger left of it goes on, wherever that finger is now, and so does its glide; a finger again beside it pinches anew. Two
fingers on the minimap or on a panel over the map move the main view's, as in FAF's one view, and so do the flight to the group, the
tick's and the undo's looks: the handle and the draft are the main map's. A glide whose view FAF has put away, as the right one when the screen is
one view again, is over. Until 0.16.3 `griff.lua` moved the main view's camera alone (`WorldCamera`): two fingers on the right view
moved the left one, and so did the glide after a pan there.

A hint says a word where FAF prints its own messages (Griff v4, no Android toast): `textdisplay.lua` `PrintToScreen`, as userInit.lua's
`PrintText` prints a word, in FAF's font at its size 14, white, in the screen's middle, fading as FAF's do, after 2 s (`griffhud.lua`
`Hint`, by Game's HINT_, which `GriffAndroid.cpp`, the activity and the control groups' hook name): Cancelled for a stroke given up, the
draft's words below, Undone in part for an undo that could not take out every order of the gesture's or give back every one it replaced,
Not undone for one that clicked nothing of it, Not deleted for a marked waypoint FAF's cursor did not hover, and the control groups' words
(below).

#### The draft

A build icon of FAF's construction panel opens Griff's draft (buildstage/griff-v3-spec.md, T2) instead of FAF's ghost: the hook of
`construction.lua` calls `griff.lua` `BuildIconClicked` after FAF's own click handler, which ends the build mode FAF started
(`EndCommandMode`) and hands the blueprint to the program (`FafreGriffDraft`). Only a build item's click with the left button does: FAF's
build mode keys (`buildmode.lua`) and hotbuild arm the build mode themselves and never reach the handler; a template's key reaches it
without a mouse button (`BuildTemplate`) and a template's click with an item that is no build item's. So all of them keep FAF's ghost. The
selection's engineers become the draft's builders, if any are selected. The draft lives on the program's thread
(`include/GriffDraft.h`, its rules, tested in `tests/wx/GriffDraftTests.cpp`; `GriffAndroid.cpp` for what only the engine knows): each plan
a world place and its building, from the camera's own unprojection of the finger's place (`CameraScreenToSurface`, as FAF's `UnProject`).
After each frame's paint the program projects the plans through the map's camera (as `CUIWorldView:Project`) and the activity reads them
with the frame (`Game.griffDraft`, `Draft`) and knows which a finger is on; FAF's UI draws them (`griffmap.lua`, `FafreGriffPlans`). While
the draft is open there is no handle, and a finger makes plans (`TouchInput`'s `DRAFT_` states):

| Gesture | What it does |
|---|---|
| Tap on the map | A plan of the picked building where the finger came down, on FAF's grid: the footprint's first cell the nearest whole one, as FAF places it. An extractor or hydrocarbon plant goes on the free deposit of its kind nearest to the finger within 5 mm, as FAF's build check knows the deposits (`ISimResources`), else "No free resource here". Ground that a plan or FAF's own build check refuses (`USERUNIT_CanBeBuiltAt`, by which FAF's ghost turns red: the structures and queued buildings it sees, the ground) says "Taken"; a 31st plan "Draft is full" |
| Tap on a plan | It goes: the newest under the finger, within the box of the ground it takes, whose edge the map shows (below), the box at least 2.3 mm wide; nothing beyond, where a tap places a plan against it; a plan under a panel is the panel's |
| Tap on FAF's UI | The UI's click, with the tap help; a lit *Plus* stays for the tick (a held *Plus* is Shift as ever) |
| Drag from the newest plan | A line of its building from it towards the finger: in 8 directions (an axis counts with at least tan 22.5 degrees of the other, in the building's steps), whole steps of its skirt, so that the buildings stand side by side as FAF's own drag line puts them for adjacency, none before half a step and no more than 30 places, which FAF's own check is asked of at each move of the finger; shown as the finger moves, added where it was 50 ms before the lift, past taken places, up to the limit: the lift adds only places the line showed. A second finger gives it up and the two pinch |
| Drag from another plan | The whole draft follows by whole world units, plans on deposits stay; kept where the finger was 50 ms before the lift; a second finger puts it back and the two pinch |
| Drag with *Box* and a deposit's building picked | From anywhere: a box, FAF's selection box as its world view draws a box drag (`SelectionDragger2D`: a dark fill, a light edge 2 pixels wide), and at the lift every free deposit in it gets a plan; *Box* is used |
| Drag elsewhere | A pan, whatever the rail says |
| Rest 400 ms on the map, on no plan, without *Box* | The plan its lift places shows under the finger, also where its ground is taken, then red, as FAF's ghost over ground its check refuses, and the lift says "Taken" (a deposit's building shows none where no free deposit is within reach), with a loupe 14 mm above it (below where the top has no room): a circle of 12 mm, the 6 mm about the finger at twice their size, copied from the game's surface (`PixelCopy`, at most 30 times a second; on the emulator 0.6 to 15 ms, median 1.4 ms, for its 71 pixels), with the plan in it, framed by FAF's marker ring (`marker/ring_yellow02.dds`, which FAF's UI lays about the circle where the activity says it is). The copy is kept on the picture (`Draft.loupeFrom`, tested), and the loupe shows it where it was taken: within 3 mm of the picture's edge, or where the finger has moved on since the copy, beside the finger over the game's black, so the finger's spot is always the circle's middle. A copy that fails or comes back blank (the emulator's at the main menu did) shows no loupe, and the game's report says so once. A rest on the map at 300 ms waits the other 100 ms for it |
| Rest 300 ms on FAF's UI | Griff's rest, as outside the draft, once the program has said at 300 ms that the finger is on FAF's UI (`Game.draftRest`, `SAID_DRAFT_REST`): the ring and the buzz of the panel's rest, its lift FAF's right click there, its slide the panel's drag; no plan and no loupe, which are the map's. As the tap there, its lift and its slide use none of the rail: a lit *Plus* stays for the tick (a held *Plus* is Shift as ever). Where the word comes after the aim began, the aim's lift there is the right click all the same |
| Two fingers' tap | The draft goes; one of two plans or more comes back by the undo's button (The undo) |

Plans are the ghosts of FAF's build preview (`CBuildDragPreview`: the building's mesh through the UnitPlace shader, `FafreGriffGhost`), in
its green, red where the ground is taken (another plan's, or FAF's check). The shader draws every ghost at a fifth of its colour, whatever
alpha the colour has (`mesh.fx` UnitPlacePS), so plans differ by the colour alone: the newest, which a drag draws a line from, and the one
a resting finger aims at in FAF's own colours, as its ghost under the cursor; the other plans and a line's places at half of them.

While the draft is open the map shows the ground each building takes, as FAF's build mode shows it while it is armed (the user's verdict
on 0.15.0, 2026-11-16: while placing, the other buildings were not to be seen, and their skirts should show). The draft ends FAF's build
mode, so the world view's own overlay draws nothing; by a build-time patch (`patches/src/moho/ui/CUIWorldView.cpp`, the draft given by
`include/GriffSkirts.h` `gDraft`, `GriffAndroid.cpp` `DraftSkirts`) the map's view draws the same for the draft, with the engine's own
`DrawUnitSkirt` (at least `ui_FootprintMinThickness` wide) and the rules of its `DrawBuildDragAdjacencyHighlights`: the skirt of each
building the orders will build in FAF's blue unless Shift is held (`DrawAllUnitSkirts`); of each structure in view in FAF's green, as
far out as the picked building's mesh is drawn, bright green where a plan, a line's place or the aim in the colour of a free place gains
adjacency from it (FAF's `gamemain.lua` `OnDetectAdjacencyBonus`), the first of them that no structure before it in the camera's list
lit, and only that one (`include/GriffSkirts.h` `Light`, tested in `tests/wx/GriffSkirtsTests.cpp`); and of each plan, a line's place
and the aim in its colour, green, red, or that bright green. The ghosts keep their colours there, FAF's own as a plan's: FAF's overlay
gives a build drag's mesh the bright green after the frame's meshes are drawn (`WRenViewport::Render`: the mesh passes, then the view's
overlay at 0x007F95EA), and the next frame's preview update, at the frame's head, gives the mesh its place's colour again
(`CUIWorldView::Func1`, `UpdateDragPreview`'s store at 0x008533DB), so only the skirt shows the bright green. On the emulator
(2026-11-17, a land factory beside a power generator, real fingers) FAF's ghost from its build mode and the newest plan in its place showed
the same pixels about the factory, and within its skirt the ghost's and a plan's, the newest's or an older one's, stayed the same when the
generator went, while the skirts' edges went from bright green to green. Where FAF's build mode is armed (its keys, hotbuild, the
tick's clicks), its own overlay draws instead. The skirt's edge is what a tap on a plan is within: just
past it a tap or a rest places a building against the plan. In 0.15.0 a finger was on a plan within the box of its ground and 1.5 mm
past it, and the screen showed neither, while the ground reaches past the ghost's mesh (a factory's 1.5 world units each way): a tap or
a rest meant to place a building against a plan took the plan away (on the emulator a rest beside a factory's plan with power
generators picked: the factory's plan went), and a rest there showed no aim. On the emulator (2026-11-16, at the phone's size and HUD 1.6, real fingers) the draft opened with a factory's and two
power generators' skirts in green; a factory's plan below the factory lit both skirts bright; with power generators picked a rest just
past the plan's edge showed the aim red over its ground, and the lift said "Taken" and left the plan; a rest below the power
generators showed the aim green and lit the lower one bright, and its lift placed the plan; after the tick the next draft showed both
ordered buildings' skirts in blue. The tick and the cross are
FAF's order buttons (`griffhud.lua`: the orders panel's empty slot at the HUD's factor, as large as its order buttons, with FAF's tick of the
skin's map options, `temp_textures/checkmark.dds`, and its Cancel, `orders-panel/no-parking_bmp.dds`, both of the faction's skin; FAF's
glow while a finger holds one), 12 of FAF's pixels above the construction panel, the tick 12 left of the column at the right (FAF's
avatars), where `griff.lua` reads them each frame, and against the map's edges where FAF shows neither.

The cross is Griff's Esc (the decision of 2026-11-17 on the user's verdict on 0.15.0, that a button was missing to put away what was
chosen, the building being placed and the selection, as Esc and the right button do on the PC). It shows while the draft is open and
wherever a PC player's Esc would act in a game (FAF's `eschandler.lua` `HandleEsc`: an armed command mode, else the selection), in its
place beside the tick's, also where the tick does not show, and a tap puts away one of them, in this order (`include/GriffTap.h`
`EscapeFor`, tested): the draft goes, with nothing ordered (one of two plans or more comes back by the undo's button); else the armed command mode ends (`EndCommandMode`, FAF's build mode of its
keys or hotbuild too); else the selection goes (`SelectUnits(nil)`). With none of them it does not show. The program finds each frame
after its paint whether there is any (`EscapeNow`: the draft, `UI_GetCommandMode`, the session's selection), by which FAF's UI draws
the cross (`FafreGriffEscapes`) and the activity lays the view that takes its finger (`Game.griffEscapes`); the tap goes by what there
is when it comes (`EscapeTask`). The mode and the selection are not put away while the program's clicks run, as for two fingers' tap,
which stays as it was. FAF's own UI has nothing for this (lua.nx2, FAF 3835): Esc is a key only (`defaultKeyMap.lua` `Esc` = `escape`,
`keyactions.lua` to `uimain.lua` `EscapeHandler` and `HandleEsc`; FAF's alternative key map makes it Stop); its top bar has the menu,
diplomacy and pause (`tabs.lua`); its orders panel's buttons end only their own order's mode when tapped again (`orders.lua`
`StandardOrderBehavior`), and Stop is an order to the units; nothing else calls `HandleEsc` or `SelectUnits(nil)` from a control
(`borders.lua` splits the map, `gamemain.lua` begins a film). On the emulator (2026-11-17, real fingers, the phone's size, HUD 1.6)
with nothing selected no cross showed; with four engineers selected it showed where it stands in the draft, and its tap let them go
and it went; with the orders panel's Move armed its tap ended the mode and kept the four; in a draft with a plan its tap dropped the
draft and kept them.

The tick (`DraftCommitTask`): "No plan yet" without plans; "N plans were not ordered" at once, the
draft kept, where the program knows no map's view to click on (`griff.lua`'s report of it failed); the builders are the selected engineers,
else the draft's that still live ("No engineer" without), selected for the clicks (`griff.lua` `CommitBegin`) and the selection put back
after (`CommitEnd`); plans FAF's check refuses now are left out ("N plans were on buildings", "All plans are on buildings" when none is
left). Then per plan, a task each: FAF's build mode for its building (`StartCommandMode('build', ...)`, as the panel arms it for a build
icon, FAF's build templates cleared first: one the player picked and put away by a right click stays the session's, and FAF's click would
build it in place of the plan) and a mouse's left click in its cell, the first replacing what the builders do unless *Plus* counts, the
others with Shift. A finger's press would only take FAF's ghost along, so the clicks are a mouse's (`Win32Android_MouseButton`). FAF takes a
click at a whole pixel and builds in the cell of the world point under it, so the click goes to a pixel whose world point is in the plan's
cell, for a deposit's building one whose deposit FAF's ghost snaps to is the plan's and, as FAF's click drags a building of its DRAGBUILD
category (every extractor) from the snapped deposit to the world point under the release, one that keeps that drag to one step, which would
else order an extractor on a free deposit beside (`include/GriffDraft.h` `ClickPixel`, `LandsAt`, `DragsOnePlace`, tested against a
transcription of FAF's drag): of the four pixels about the plan's middle on the picture, the nearest first. Zoomed far out a pixel spans more than a cell (on
Seton's Clutch fully zoomed out 1.9 world units on the phone's picture), and where none of them lands the camera comes closer for that plan
(to a world unit's 4 pixels, not past FAF's nearest zoom) and the click comes after the next frame; a plan that cannot land then is not
ordered. A plan not on the free map gets the camera first (`LookAt`: its focus, as high and turned as it was) and its click after the next
frame drew it (griff.lua counts each frame of the session's UI for it, `FafreGriffFrame`, apart from its report, which an error stops: an
idle task would come before the frame); the camera goes back after the last (`LookBack`).

The program's clicks, the tick's and the undo's (`ClickStepTask`, which both use), have FAF's mode, the selection, Shift, the cursor and the
camera to themselves:
- They wait while a finger's pan, box or panel drag holds FAF's dragger, which would take the cursor's moves they aim with and end between
  them (a box's release selects): they begin after its lift, from the frame after it. The undo's button says *Undo...* meanwhile. A tick tapped then,
  or while the undo's clicks run or it waits for the sim, comes after them, after an undo that waits as well (its record is a gesture's from
  before the tick, whose orders would replace it); the cross takes a waiting tick back.
- As they begin the camera stops (`griff.lua` `StopCamera`: a glide, a flight) and no glide starts while they run, and a line of buildings a
  finger draws on FAF's ghost is given up, as its own cancel does: FAF's build drag holds the cursor as the others do.
- While they run the fingers rest: no gesture starts (`Game.griffClicking`), the rail and the undo's button take no tap. A gesture they came
  into is given up (`TouchInput`, by the runs begun, `Game.griffClickRuns`): nothing more of it goes to the game, also once they are over,
  where a tap would give the tick's builders another order; its fingers rest until they lift, and only what showed of it goes (a stroke
  says Cancelled, the draft's line, move, aim or box goes as a second finger gives it up). The tasks that would act between them do nothing
  (also the stroke's hover), and what a finger gave in the moment before the activity knew of them the program drops (`GameHost.cpp`'s
  input tasks, the pointer's leave too).
- Clicks that wait more than 2 s for a frame, as where griff.lua's frames stopped, end at the next task that would act between them (a
  finger's down is one, which goes also while the fingers rest) as their last click would end them, the plans left counted as not
  ordered; a tap on the tick then ends the tick's so, and its "N plans were not ordered" is the last word. The 2 s count only time without
  a frame: a frame whose paint takes long has come.

Picking another building keeps the plans, a
change of selection does not end the draft, and an order mode armed while plans wait (the orders panel, a key) ends at once with "Tick or
cross the draft first" (`griff.lua`'s start behaviour), while an empty draft ends and lets the mode be. The free map's cross (`griffplace::CrossAt`: how far the
free map reaches from the view's middle up, left and right) gives its place and the stroke's label's bounds.

The handle: `fafre/griff.lua`, started by the hook of `ui/game/gamemain.lua`, reports each frame of the session's UI the map's view and,
apart from it, where the selection's units are on the screen, with their size (`view:Project`, blueprint sizes; the mobile units, or all of
a selection without any, such as factories): an error in the selection's walk stops that walk and the handle for the game, never the map's
view, which the clicks and the plans go by. While the player hides FAF's UI the selection is not reported, and there is no handle. The program places it above the units, else below, right, left, 2 mm from their blueprint box (`include/GriffPlace.h`, tested in
`tests/wx/GriffPlaceTests.cpp`), on the free map: the main map's view less what covers it, which one walk over the UI's controls each frame finds: the controls that
can be seen at the map's depth or above (the HUD is drawn after the map's picture; below the map's depth are what holds it and the
paused game's tint of the whole screen), the minimap, FAF's panels, also those that let a click through to the map beneath (the
unit view, the map's sibling at its depth, and the multifunction panel do), and the app's own pieces (`Game.setGriffCovers`, `Covers`:
the undo's button's place a moment after it went; FAF's UI holds the rail, the undo's button and the draft's tick and cross themselves,
which the walk finds, and passes by what is drawn on a map, such as Griff's pieces of `griffmap.lua`). Only layouts that FAF has worked out count: the walk reads what a layout holds and evaluates none, as an
evaluation before FAF lays a control out fails and leaves 0 in the layout's cache, where nothing clears it. So the walk runs after the
frame is drawn, which works out what FAF's Lua changed in the frame (the unit view places its panel anew in each). A group out of view or under a panel gets the handle on the free map towards the view's middle, far enough from the edges that its
arrow at the group stays on the screen. Where that way is covered up to the middle, as by FAF's announcement in the screen's middle,
which stays up while a game is paused at its start, or by a window there, the handle goes to the free place nearest the view's middle
that is as far from the edges, of two as near the one below (`FreeMap::NearestDiscFree`: rows 2 pixels apart outward from the middle,
exactly on each row, so that it finds a place however little room the map leaves for it across); only where the free map has no room
for the handle at all is there none. The activity reads the handle each frame of the screen's (`Game.griff`)
and keeps it (`GriffHandle`): where a stroke began while the stroke is drawn, and none once the frames are not read, at the game's end or
under a dialog that takes the fingers, also while a stroke is still drawn. FAF's UI draws it as the activity keeps it (`griffmap.lua`):
the brackets FAF draws about a selected unit (`selection/selection_brackets_player.dds`) as large as an order button, with the
selection's count in FAF's font, white with a shadow, as FAF writes on its buttons, faint while *Box* is lit (a box may start under it);
away from its group, FAF's arrow of a ping off the screen (`ping_edge/ping_edge_blue_*`), on the handle's edge towards the group. A
finger is on it within half an order button and 1.5 mm of its middle. On the emulator a frame's placement took 0.2 to 1.1 ms (the
worst frame of each 10 s, one unit selected).

#### The undo

What a gesture orders is watched (buildstage/griff-v3-spec.md, T3.1 and T3.2), from a tap's click, a rest's right click on a panel, a
route's first waypoint or the draft's tick to the task after their input. The program keeps the queues of the selected units as FAF's UI
holds them (`native/src/host/GriffAndroid.cpp` `StoreSelection`, from the engine's own command queues: each order by its command's own
id, whole, with its type, its waypoint, the entity it targets, the building it builds; a factory's own queue, which holds what it builds,
and its rally route apart) and the building each was building, and FAF's UI tells each order it gives (`commandmode.lua`
`OnCommandIssued`, which `griff.lua` wraps and hands on with its units' ids; on every path a gesture takes, `ISSUE_Command` and
`ISSUE_FactoryCommand`, the engine has put the order's edit in the queues by then): whether it replaced what they did (FAF's Clear) and
which units it went to. Which of a factory's queues an order went into the program tells by the queue's newest edit
(`UserCommandQueue::issueQueue`; `include/GriffUndo.h` `TookOrder`, tested): `ISSUE_FactoryCommand` gives the rally route the moves,
patrols and transport loads that FAF's dispatch sends a selection's factories, `ISSUE_Command` gives their own queue everything else, as
the orders panel's Stop and the attack-move's script; the program keeps each order's command as FAF's UI gave it (`given`). The
queues as the gesture began are FAF's UI's, the sim's primary links with the UI's edits over them: an order given just before may not
have reached the sim yet (a paused game holds an order back until it resumes or something marks the session busy, as the attack-move's
sim callback does; a network game for its delay). The program keeps those orders apart with the beat each goes to the sim at
(`StoreSelection`; `include/GriffUndo.h` `UnsyncedOf`, tested: the queue's pending Adds of orders FAF's UI shows that the sim's primary
links lack): what the sim makes of them in the beat it takes them is the unit's own, the commands FAF's sim callbacks for an order add
(the sim's own, by their ids): an attack-move script's AggressiveMove, which FAF's `AttackMove` callback adds at the script's very
place; the building a Repair on a wreck rebuilds (FAF's `Rebuild`, which with Clear clears the Repair); the storages FAF's
assist-to-cap rings an extractor with for an engineer's Guard on it (`AssistToCap`'s `RingWithStorages`, its option
`structure_capping_feature_01`; also about a radar or an artillery structure, by its other options); the copy of an engineer's
queue for a Guard on it with Ctrl (`OnGuardCopy`'s `CopyOrders`, its option `assist_to_copy_command_queue`), which only a
keyboard's Ctrl gives, the program noting each Guard FAF's UI gives with Ctrl down; the attacks FAF's area attack adds about
the place of an Attack on the ground whose press dragged in its attack mode (`area-attack-order.lua`, `ExtendAttackOrder` at the
lift), as Griff's rested or *Box* drag on the map does with that mode armed. FAF's UI makes none of a move, a patrol, a reclaim,
a capture, a building or a Stop, and none of a Guard on a unit its capping never rings (not a structure that extracts mass, a
radar, an omni sensor or an artillery structure) given without Ctrl (`include/GriffUndo.h` `AnswerOf`, tested; the program tells
the unit's kind by the game's categories, `CappingRings`). From the first order of the gesture's that
replaced a queue the sim still runs the queue as it was, as long as the network delay: until the sync reaches the beat of the Reset
that order put at the queue's front, the program reads each frame what the sim holds of it (its primary links), and keeps that from
then on (`ReadTaken`, also where the replacement left the queue empty or the sim gave orders of its own after it): what the unit had
not done yet when the replacement landed. With each read it notes which of the orders not taken as the gesture began the sync has
reached by then (the helper of each one's command keeps that beat's events until it does), and the game's tick: one a read came after
shows in what the sim held; one the sim took in the replacement's own beat was in the queue the replacement emptied; one it took in a
beat no read came after was there unless the unit did it in between, which the program cannot tell (`include/GriffUndo.h`
`PendingOf`, tested). A frame that took long brings several beats, and where the one that brought the replacement's beat brought the
beat before it too and the game ticked in it, the last read may be a tick old: the unit may have done what it shows since
(`include/GriffUndo.h` `Stale`, tested, from the game's tick at the read and as the replacement showed it landed; a paused game makes
no tick; where the frame brought beats after the replacement's too it may say so of a read that was not old). A gesture that ordered is the record the undo's
button takes back, one level: the next one's replaces it, and any other order ends it, as only the last order counts: FAF's ghost from a build key or
a template, the keyboard's hotkeys, the construction panel's own queue (a factory's units, an upgrade, an enhancement), none a
gesture's to take back (`include/GriffUndo.h` `Takeable`); the button goes with the record. FAF's assist-to-upgrade (its options
`assist_to_upgrade` and `assist_to_upgrade_radar`) orders an extractor's or a radar's upgrade a frame or more after a Guard on it
(`OnGuardUpgrade`'s `UpgradeUnit`, which unpauses a paused one first and waits five of the game's ticks), the upgrade of that one unit
with Clear, which no upgrade of the player's has (the construction panel's, the upgrade key's, hotbuild's and the build mode keys' go
without: `include/GriffUndo.h` `AssistUpgrade`, tested): such an Upgrade of the target of one of the record's Guards neither ends nor
joins the record, which notes it, nor does one of the target of a Guard the undo that runs gives back, the unit's own order's again;
the player's own upgrade of that unit ends the record as any other order. The tap ladder's
attack-move in place of a move belongs to the move's record.

The record offers its undo only where that saves work (Griff v4, the user's rule of 2026-11-14; `include/GriffUndo.h` `SavesWork`,
tested): where an order of the gesture's replaced a queue of one of its units that held, as the gesture began, a building to give back
or two orders or more that come back (a route, a patrol, a line of reclaims), and for the draft's tick always. Orders to idle units, or to
units with one plain move or one assist, offer none, as a Stop or the next order does as much: such an order ends the record before it,
as any order does, and the activity hears record 0. A factory's own builds and what the orders panel has no mode for count for nothing,
as no click brings them back; an attack-move's script and its AggressiveMove are one order.

Two records are Griff's own, of what a stray gesture throws away that a PC player only throws away by deliberate keys (Esc, Ctrl
and a number), and order nothing (the user's choice of 2026-11-18): a draft of two plans or more that the cross or two fingers' tap
dropped (`include/GriffUndo.h` `DropSavesWork`, tested; a second thumb that brushes the glass in quick taps is two fingers' tap), and a
control group a rest stored over other units it held, where the store took out a unit that lived (`StoreSavesWork`, tested: not a
store on an empty slot, nor one of the group's units and more, nor *Plus*'s append, which takes none out). While FAF's option *Steal
from other control groups* is On (Off by default), a store takes the stored units out of their other groups without a word, and
so would the undo's (`selection.lua` `AddSelectionSet`): the hook of `controlgroups.lua` reports every store then as *Plus*'s
append, and none offers an undo. Their undo comes at the tap, with nothing clicked: the draft is open again as it was, its plans
where they stood and its builders with it, and an order mode armed since the drop ends, as none stays armed while plans wait
(`DraftBack`), with nothing for the sim; the group holds again those of its units that live, by FAF's `AddSelectionSet`, as Ctrl
and the number store a selection, whose callbacks show its control and, as for any store, send the sim FAF's
`OnControlGroupAssign`, a tutorial's note that orders nothing (`griff.lua` `GroupBack`; with none alive *Not undone*, and the
group stays as the store left it). They are records as a gesture's are: one level, the next replaces one, any order ends it, the
button offers it for its 3 s. So that the undo never undoes what came after, a draft that opens ends a dropped draft's record
(`OpeningEndsRecord`), and the hook's next store on a group's slot that changes the group ends that group's record, where a
store of the very units keeps it (`OutcomeOfStore`, both tested). A store by a keyboard's Ctrl and number goes past the hook and
ends nothing: the undo would then put the record's units back over it.

The undo's button (Griff v4) is FAF's UI's (`griffhud.lua`): a button of FAF's orders panel (its empty slot's frame,
`orders/basic-empty_bmp.dds`, at the HUD's factor as its order buttons) with *Undo* in FAF's font, white with a shadow, as FAF writes
on its order buttons, and at its bottom FAF's avatar bar of the time it has left (`avatar/health-bar.dds` of the faction's skin on its
back, as the commander's avatar shows its health); *Undo...* while it waits. It stands for 3 s after the order (`Undo` holds its time)
right of the rail, 12 of FAF's pixels after *Groups*. The activity lays a view of its own over it where FAF's UI reports it, so a finger
on it is exactly its own, never the map's. Its cover stays 600 ms after it went, where it last stood (`Covers.Lingering`, tested): a tap
meant for it whose finger was on its way as its time ran out lands on nothing, never on the map beneath (the program checks a tap's place
at its click, a settle after the finger's down: on the emulator 88 to 188 ms, and taps 74 to 389 ms late were covered, one 542 ms late
not). A dialog over the game takes it away.
A tap on it takes the record back with a PC player's inputs (`include/GriffUndo.h`, tested in `tests/wx/GriffUndoTests.cpp`), once the
sim has taken the gesture: the undo waits until none of the record's units has an edit of its queues that the sim has not taken yet (the
engine's `UserCommandQueue::issueQueue`), however long that takes (a network game takes an order its lag later, a paused game once it
resumes), so that it plans on the queues the sim holds, never on the UI's own; meanwhile the
fingers are free and the button says *Undo...*, also past its 3 s, as it does while a finger's pan or box the undo waits for still goes on.
An order meanwhile ends the record as ever, and so does the game's end: a hint says *Not undone*. Then, for each queue an order of the
gesture's went into:

| What the gesture did to a unit | Its way back |
|---|---|
| Gave it orders (in its queue now, not before; also what the sim made of them, as the AggressiveMove of FAF's attack-move or the rings FAF puts about an extractor the gesture's Guard went to) | Each taken out by FAF's Shift+Ctrl right click on its waypoint (`CUIWorldView::HandleEvent`'s right release), which strips that command from all of its units, by the engine's own command id: the units selected, Shift held a frame before (FAF draws the waypoints only while Shift is down), the cursor on the waypoint and the click only once FAF's cursor hovers that very command, a waypoint off the free map with the camera there first, as the tick's. Checked in its units' queues after (the edit FAF sends the sim; a factory's rally route shows it once the sim took it). An order its units carried out before the undo's removal reached the sim (a move to a place close by may be done within a second of the tap) stays done and counts nothing: there is nothing left to take out |
| Appended to its queue | Nothing more: its queue before stays as it was, a building it had begun goes on, nothing of it is clicked again; what it finished since stays done. What the sim made since of the unit's own orders it had not taken as the gesture began (an attack-move's AggressiveMove, a wreck's rebuild, an extractor's rings) is no command of the gesture's: the sim took those orders, and made it, before the gesture's, appending each, so whatever stands ahead of the gesture's first command stays, and all from that command on is the gesture's, its own attack-move's AggressiveMove at the very place of a pending attack-move of the unit's too (`include/GriffUndo.h` `GesturesFirst`). Where that command is gone (done at the queue's head, its target gone), what stands ahead of the next may be what it made: it goes where none of the unit's pending orders is one FAF's UI makes commands of (above, `AnswerOf`), else whose it is cannot be told and it stays, counted; so does the AggressiveMove where the gesture gave an attack-move to the very place of a pending one of the unit's (the first at its script's place that its queue before did not hold is the unit's) |
| Replaced its queue | What it lost (in its queue as the gesture began and still in the sim's when the replacement landed, not now: what it did in the network delay between stays done) given again as a player gives it, every click with Shift onto the emptied queue, so a click FAF refuses loses only its own order: a group of units whose orders come back alike selected, then for each order FAF's mode for it (the orders panel's move, attack-move, patrol, attack, assist, repair, reclaim or capture; the construction panel's build mode for a building, which lands in its cell as the tick's plans do) and a mouse's left click at its place, a frame after the cursor went there in that mode. An order on a unit at the place that unit is now, read again for each frame, and only while FAF's cursor picks that very unit, else counted missed, as is an order FAF gives on another entity. A building the unit had begun comes back by its repair. A patrol's first point comes back as a move to it, as FAF anchors a patrol given with Shift at the queue's last place and would add a point of its own. What the unit holds still the sim kept, as a commander's enhancement under way, which nothing of the undo stops. An order the sim had not taken as the gesture began was lost as far as a read after the sim took it shows; one it took in the replacement's own beat was lost with it (a paused game holds both back to one beat), but what FAF's UI made of it then no read showed: where no click of the undo's makes that again (an extractor's rings, an engineer's copied queue, an area attack: `AnswerOf`'s once), it comes back counted; one it took in a beat no read came after comes back, counted. Such an attack-move comes back as one attack-move at its place, whether its script, its AggressiveMove or nothing of it was read; a Repair on a wreck by its repair, which FAF's Rebuild answers with the rebuild again. What such a read shows the sim made of those orders (its own commands, ahead of the gesture's: the rebuild alone where FAF's Rebuild with Clear cleared the Repair, an extractor's rings) comes back by its own way, in its place (the rebuild and the storages by FAF's build mode at their places), counted where the order it stands behind comes back too and FAF makes it again of that one (its Rebuild: a second rebuild order, which finds the building there). What a read that may be a tick old shows comes back counted; one the sim took in the replacement's own beat is as exact as above still. A Guard comes back as FAF's Guard with Shift, which FAF's capping would take for the player's next tap on the structure, by what it remembers of the player's taps: on an upgrading T1 or T2 extractor or radar its double tap, which rings it with buildings the engineer never had; on a structure it rang in the last five game ticks (a paused game holds the tick), nothing. While the undo's clicks run `griff.lua` holds FAF's capping off (`AssistToCap`), as a PC player who wants only the Guard turns its options off: the Guard comes back alone, what the engineer held of FAF's rings comes back by its own clicks, and FAF's memory of the player's taps stays as the player left it. FAF's other answers to a Guard (its assist-to-upgrade, its unpause) stay |
| Set a factory's rally point | As any unit's; but a rally route the gesture replaced comes back by a first click that replaces the gesture's rally point, the rest with Shift: the sim rallies an emptied route to the factory's own place again, which an appended route would follow |
| Replaced a factory's own queue (the orders panel's Stop; an attack-move to a selection with the factory, whose script FAF authorizes for every selected unit) | Its builds cannot come back by a click: counted; its rally route, which the order did not touch, stays |
| Nothing (its queue is as it was) | Nothing |

Where two waypoints of the selection's lie within the reach of FAF's cursor of each other, FAF's cursor may hover either
(`UICommandGraph::ResolveCursorHighlightCommandId` takes the last of them), as at the anchor of a patrol given with Shift, which FAF puts
at the queue's last place. Another of the gesture's is taken out in its turn; an own order on a place that comes last in its units'
queues but for the gesture's (`ComesBackAfter`) is taken out too, from every unit it went to, the gesture's or not, as FAF's release
strips it from all of them, and given back to all of them after the gesture's, by its mode's click with Shift, so it stands where it
stood; where one of them holds a later order of its own it stays, and so does the gesture's waypoint, counted; any other leaves the
gesture's order there, counted. FAF draws a command it was asked to take out until the sim took the removal, and keeps the waypoint of
one it let go until it draws its graph anew: the undo waits for the one (2 s at most) and lifts Shift's key for a frame for the other,
whatever else holds Shift (a held *Plus*). A move's click goes only where FAF's cursor hovers no move that FAF's move order would restart
as a patrol instead of ordering (`CanRestartMoveCommandAsPatrol`: a move that every selected unit holds, not the last of their queues,
which hold moves alone). FAF's move cursor (`worldview.lua` `OnCursorMove`) would keep it from hovering waypoints at all (its ignore
mode), but the console variable it sets, `ui_CommandClickScale`, is not the one this engine's hover reads.

FAF's UI has `DeleteCommand` for one command of a queue, by its id, which the spec proposed for an appended order. But the ids FAF's UI
Lua reads are no command's own. The ids of the second army's commands on are 2^24 and more, and the game's Lua numbers are floats
(`LUA_NUMBER`), which hold those to an even number only: the phone as the second player of a game against the PC's deleted none of its
appended orders (twice; its queues showed only even ids, two commands one), where it did as the first in a skirmish. And FAF's UI gives a
finished command's id to the next, while its queue shows the finished one until the sim says so. So the undo uses neither the Lua ids
nor `DeleteCommand`: the program reads the queues in the engine, where each command's id is whole, and takes a command out by FAF's
Shift+Ctrl right click on its waypoint, which strips it by the engine's own id, for every army (the second player of a LAN game took its
orders out so).

What cannot come back or go is counted, and a hint says *Undone in part*: an order its orders panel has no mode for (a transport's, a
missile's, an enhancement the gesture's order took, a factory's builds), one on an entity that is gone, a building whose name the order did
not carry, an order of the gesture's that FAF draws no waypoint for (a script other than the attack-move's, a dive; the script FAF's
attack-move gives beside its AggressiveMove where it stays behind an order the unit kept, its queue's last command until that is done, at
whose place FAF anchors a patrol given with Shift; at the queue's head it is done by itself), a waypoint FAF's cursor never hovered or a
target it never picked, a place no frame brought onto the free map, a click FAF ordered nothing for, a unit FAF selected as the transport
that carries it (left out, `SelectUnits` selects a carried unit's transport), an upgrade FAF ordered for an assist of the gesture's (it
goes on), units the gesture ordered that were not selected as it began, and what the plan cannot tell (`Plan::unsure`: an order the sim
took in a beat no read came after, one a read that may be a tick old showed, one it took in the replacement's own beat that FAF's UI
answered with what no click of the undo's makes again, an AggressiveMove at a place where both the unit's own pending attack-move and
the gesture's went once the gesture's script is gone, what stands ahead of the gesture's commands where its first is gone and one of
the unit's pending orders FAF's UI makes commands of may have made it, what the sim made of a pending order that comes back with it and
that FAF makes again of it). Where `griff.lua` could not hold FAF's capping off (FAF's Lua changed under it: `UndoBegin` says so, and
it warns once), each Guard the undo gives back on a structure FAF's capping may ring counts too.
An upgrade FAF's assist-to-upgrade orders only after the undo said it took all of the gesture back (a paused extractor's, in a paused
game, where the sim takes the undo meanwhile and the five game ticks wait for the game to resume) makes it in part after all: the
program keeps the Guards of each undo for the game's next six ticks after it finished, which a paused game holds (`gUndone`,
`include/GriffUndo.h` `Undones`, tested), and an upgrade of one of their targets makes that undo's word in part then, a hint later,
also where more undos finished meanwhile; it neither ends nor joins any record (the activity's undo and pointer stay as they are), nor
does one for a Guard an undo gave back, the unit's own order's again. None of them outlives the game. An undo that had something to take out or give back and made
no release and no click, as where its clicks ended before their first step, says *Not undone*: nothing was clicked, so the record holds as
it was and the button offers it again for its 3 s, as it does where the program knows no map's view to click on (`griff.lua`'s report of it
failed). The clicks come one a task, as the tick's (`ClickStepTask`, which both use), the tick's first where those run, after the lift of
a finger whose drag holds FAF's dragger; while they run the camera holds still and the fingers rest, as for the tick's, and clicks that
wait more than 2 s for a frame end as the tick's: in part, or not undone where none of their steps acted. A click comes a frame after the cursor went to its place, where FAF's cursor is found right, at the pixel that
decides where it lands (`include/GriffUndo.h` `AimedClick`, tested): a release and a click on a unit where the cursor is, as FAF's hover
there decides them (a unit, or a waypoint on it, may walk on meanwhile); a building at the pixel that lands it in its cell now; a click on
the ground only while its place is shown where the cursor is still, else the cursor goes there again within the step's frames, as the
picture still moves a frame after the camera went to a place. No press of the program's, the undo's or the tick's, makes a double
click with the press before it, whoever gave that (a finger, the program): FAF's world view would take it as its double click, whose
left one gives the first press's order again. Where the message layer says a press there would pair with the last one
(`Win32Android_DoubleClickWait`, its own rule of the double-click time and rectangle, tested in `tests/wx/MessageLayerTests.cpp`), the
step comes again after the next frame until the double-click time has passed, as a PC player who wants two single clicks there waits;
none of those frames counts against the step's (two give-backs within 2 px, zoomed out, waited 465 ms; a release at the very pixel a
finger had pressed 22 ms). After them the selection and the camera are as they were, and the
next tap climbs no ladder from the order taken back. The sim takes the undo's inputs a network delay later, as a player's, after the
gesture's.

No pins (Griff v4): FAF's own feedback blip marks the place of every order its UI gives (`commandmode.lua` `OnCommandIssued`,
`AddCommandFeedbackByType`: a move's marker, a crosshair, a building's mesh), Griff's included; 0.14.0's pins at the same places were a
copy of it.

#### The rail, the control groups and the keyboard

The rail (buildstage/griff-v3-spec.md T3.5; Griff v4) is FAF's UI's (`fafre/griffhud.lua`): a row at the top left, the user's decision of
2026-11-13, of *Box*, *Plus* and *Groups*, made as FAF's orders panel makes its buttons and as large: each the orders panel's empty slot
(`/game/orders/basic-empty_bmp.dds`, as FAF's order buttons 73 of the picture's pixels at 1.6, 7.0 mm on the S22 Ultra, where 0.14.0's
buttons were 11 mm) with an icon of FAF's own: *Box* the construction panel's icon of the selection (`construction-tab_btn/units-select_icon_bmp.dds`,
which FAF's mouse tooltips show for Select), *Plus* Shift's + in FAF's font, white with a shadow, as `orders.lua` writes on its
buttons, *Groups* the icons it shows and hides: three of the groups' strategic icons in one's own colour (`griffgroups.lua`'s art, at a
selected land unit's size; one alone in red would read as a stop sign), each icon's glyph as wide as FAF's order icons are. They follow the
faction's skin where it has them (UEF's and Aeon's; Cybran and Seraphim take UEF's, as FAF's mouse tooltips do), lazily, as the session sets
the player's skin only at its first update (`gamemain.lua` `OnFirstUpdate`). Held, a button shows FAF's glow over an order button under the
mouse (`orders/glow-02_bmp.dds`), pulsing from nothing up to 0.4 and back at 1.2 a second as `orders.lua` CreateOrderGlow does; lit for
the next action, the edge of FAF's art of a selected order (`orders/move_btn_up_sel.dds`, its frame cut out of its middle, so the edge is
FAF's soft one in its own colour, not the faction's text colour); *Groups* is lit while Griff's group icons show. A finger coming onto
*Box* or *Plus* gives FAF's press sound of an order button. Checked side by side in the four factions' skins (2026-11-15, switched in a
game by `uiutil.lua` SetCurrentSkin): the frames, the glow and the lit edge are FAF's common art, the same in all of them; *Box*'s glyph is
UEF's or Aeon's, and the draft's tick is each skin's own. The row stands 145 of FAF's lines down (an H under the multifunction panel's 138) and 10 in, its buttons 2 apart as FAF's
orders grid has them, the undo's button after it. It covers less of the map than 0.14.0's (three buttons of 7 mm and one of the same
size, against three of 11 mm and a pill of up to 34 mm): FAF's overlay filters' list, which lay across 0.14.0's row, now opens beside it.
Side by side with FAF's orders panel on the emulator at the phone's size and HUD 1.6, frame and size are the same (68 of the screen's
pixels, the views laid over *Box* and *Plus* as large).

FAF's orders panel has Stop and the attack-move (`orders.lua`: `AttackMove` in slot 1, `AttackMoveBehavior`), so the rail has neither:
0.14.0's *Attack* went (the user's rule: no copy of what the game has, checked in 3835's lua.nx2 before anything was added). *Box* (FAF
selects by the mouse's drag), *Plus* (Shift is a key) and the undo have no counterpart in FAF's UI, nor has *Groups*, which shows and hides
Griff's group icons (`griffhud.lua` `GroupsShown`, which `griffgroups.lua` asks each frame: "The groups"). The activity lays a view of its own over *Box* and *Plus* where FAF's
UI reports them (`Game.griffHudPlaces`; FAF's UI draws them as the activity says each looks, `Game.setGriffHud`, `Rail.look`, tested), as a
thumb may hold *Plus* while another finger taps; *Groups* is FAF's click's, a tap toggles it. What keeps clear of the rail is FAF's UI's
walk: the handle's free map finds its buttons as FAF's controls.

In a replay the rail has *Time* after *Groups* (2026-11-25), FAF's clock of a unit's build time (`unit-over/icon-clock_large_bmp.dds`) in
the same frame, FAF's click's as *Groups*: a tap pins FAF's timeline bar at the screen's bottom or lets it go, FAF's own `Toggle` of
`ui/game/replay_timeline.lua`, which its Ctrl-T calls. FAF's UI has no button or menu entry for the bar: it shows while a move to a chosen
time is under way, while Ctrl-T pins it, and while the mouse is over the map within 48 of FAF's lines of the screen's bottom (77 of the
picture's at 1.6), where the place it had last over the map stands while it is elsewhere. A Griff finger's tap there puts the cursor on it, so the bar
comes and stays (on the emulator at the phone's size, during a replay and after its end), but its tap is also a click at that place, which
on the bar moves the replay; a phone has no Ctrl. The bar is FAF's own from 3841 on; 3840 and before have the engine's own
(`data/fafengine`, whose hook of `gamemain.lua` makes it where FAF has none), which the app puts beside the program its command line
names (`<version>/bin/fafengine`, `GameFiles.engineLua`), where the patched engine looks for it, as a desktop build puts it beside the
program. *Time* takes the bar the engine takes, FAF's where there is one (`CWldSession::ResetForNewReplaySim`); it is not made where
neither is there, nor where the file has no `Toggle`, which a warning says.

Nothing of Griff's covers what the player opened of FAF's (2026-11-14). FAF's overlay filters' list, which the arrow of the multifunction
panel's military button opens right of the panel (from 356 of the picture's pixels across and 160 down at 1.6, 18 rows down the screen),
lies across the rail's row; the Menu's list (699 to 947 across at 0.85), the map's options and a question box, which FAF puts in the
middle, lie under where 0.14.0's undo pill ended. So the hooks of the files that make them tell `fafre/opened.lua` each one as it is made: FAF's
windows (`maui/window.lua`: the chat, its options, the minimap), its popups (`ui/controls/popups/popup.lua`: each question box, whose shadow
over the whole screen takes every press outside it and closes it, so that its box is the screen's), the dropouts of its multifunction
panel (`ui/game/multifunction.lua`: the overlay filters, the map's options and the team colours, one background each) and the content
of its tabs (`ui/game/tabs.lua`: the Menu, the diplomacy). `fafre/griff.lua` reports the boxes of those that show (not destroyed, neither
they nor what holds them hidden) where they changed (`FafreGriffOpened`), and the activity reads them each frame (`Game.griffOpened`,
`Opened`, tested): each of the rail's buttons and the undo's button, in FAF's UI (`griffhud.lua`), and the draft's tick and cross that one
lies under, by more than an edge, steps aside until it has gone. It stays unseen in its place (`View.INVISIBLE`), so it takes no finger, the program hears no cover of it
and the pieces beside it stand where they did; the undo's time runs on, and its cover that stays after it went goes at once
(`Covers.Lingering.forget`, tested). On the emulator at the phone's size by real fingers (0.14.0's row, with its *Attack* and its pill): as the list grew over the row after an order,
*Plus*, *Attack* and the pill stepped aside and *Box* stayed; a tap where *Attack* stands was FAF's click on the list (its *Build
Preview* unchecked), and the pieces came back as the list folded away (it opened to 356 to 567 across and 160 to 656 down); with the
Menu open the pill that an order 0.45 s before had brought stepped aside and the rail stayed; a question box (the Menu's *Restart Game*) took all three buttons away until its *No*. Not
told, and so still covered where one meets a piece: the simulation's dialogues (a mission's, some mods'), the construction panel's
template menu, and what a mod opens by other means. A dialog with a factor of its own, as the connectivity panel's, gives the menus the
fingers, which puts the rail away anyway.

FAF's minimap and chat window, for a player who has not placed them (the hooks of `ui/game/minimap.lua` and `chat.lua`), have homes
that cover none of FAF's panels and none of Griff's pieces: the rail's row with 0.14.0's undo pill at its widest, the group slots, the draft's tick
and cross, and the handle's reach (7 mm) about the view's middle, where the handle goes when no place beside its group is free (above).
Measured on the phone's picture (1647x768 at 1.6, the covers that the handle's free map counts) in a skirmish and a replay, with
nothing and with a unit selected, the largest room clear of all of them lies right of the middle: below the score panel (93 of FAF's lines down
in a game of two armies, 129 in a replay, whose panel has the observer's line and the speed, and 14 more each army), above the
construction panel (338), left of the avatars' idle list where it opens (868) and the draft's cross (748). The minimap's window goes
from 576 to 744 of FAF's pixels across and 166 to 328 down (921 to 1190 and 265 to 524 of the picture's): as high as that room is
below the score panel of a game of up to seven armies and of a replay of up to four, and about as wide, since a map is square: 168 x
162 of FAF's pixels (26 x 25 mm on the S22 Ultra), where FAF's own window is 227 x 210. The chat window goes from 472 to 662 across and
72 to 186 down (755 to 1059 and 115 to 297), above the handle's reach between 0.14.0's undo pill at its widest and the score panel, under FAF's
tabs; open, its bottom lies over the minimap's top. FAF's drag handles at the windows' corners reach a little past them: the
minimap's lower right one under the draft's cross, the chat's lower left one under 0.14.0's pill's end and its upper left one 2 of the
picture's lines into the bottom of FAF's tabs. The chat's lower left one's box, drawn clear but for its corner, reaches 7 of the
picture's lines into the handle's reach (713 to 837 and 159 to 309), which the free map counts: with the chat open and no place beside
the group free, the handle stands 8 lines below the middle (2026-11-14, on 3835 and 3841 alike). Where the phone's keyboard is up for the chat, it covers all of the screen but its top,
wherever the window stands. The homes are the phone's picture's: on a screen of another shape or at another factor one can meet a
panel or the handle's reach about the middle (at 1.67, the factor of a screen under 70 mm, the open chat window reaches into it, 8
of the picture's lines on the emulator's screen; on a 21:9 picture the minimap's home does at either factor), and a window the player
moves stays where they put it. The handle then goes to the free place nearest the middle (above): with the chat open at 1.67 on the
emulator's screen, 10 lines below the middle. A piece of Griff's that such a window lies over steps aside (above).

FAF 3841 made its chat anew (`ui/game/chat/`; the hook of `chat/chatinterface.lua`, as `chat.lua` only forwards to it). FAF's place for
its window, `DefaultRect` (8 to 430 across and 460 to 720 down), from which its reset button sets the window too, lay at 1.6 below the
picture, 736 of its 768 lines down. Its window has a title, *Chat dialog*, which at the old home its title buttons cut: its home is the
old one 6 of FAF's pixels further left and 2 further right, 466 to 664 across and 72 to 186 down (745 to 1062 and 115 to 297 of the
picture's), 5.6 lines right of 0.14.0's pill at its widest. Measured as above on 3841 at the phone's size (the covers of 32 walks in a
skirmish, with nothing and with the commander selected, under the Menu and the overlay filters' list): FAF's panels stand where they
stood on 3835, and the window covers none of them, none of Griff's pieces and not the handle's reach. Drawn with its drag handles it
takes 703 to 1097 and 102 to 309: its upper left handle 6 lines into the bottom of FAF's tabs, its lower left one under 0.14.0's pill's end
and 7 lines into the handle's reach as above, its right ones 4 lines short of the score panel. Its reset button brings it back there
(dragged down to 201, then back at 115) and FAF's reset makes its lines anew. Open with the minimap, the minimap's top lies over the
chat's lower right corner, its camera button: 3841 makes the chat before the minimap, where 3835's chat stood over the minimap.

The control groups (T3.3; the hook of `ui/game/controlgroups.lua`, under Griff only): Griff has no Ctrl to make a group with, so groups 1,
2 and 3 always show in FAF's own column of control groups (Griff v4): a group's slot is FAF's own control for it (`CreateGroup`: the
control group's frame `avatar/avatar-control-group_bmp.dds` of the faction's skin, its best unit's icon and its number, 73x54 of the
picture's pixels at 1.6), an empty one the same frame with its number alone, dimmed (0.45). FAF's other groups (4 to 0, which a
keyboard's Ctrl makes) stand below them as FAF lays them out (`controlgroups_mini.lua` LayoutGroups: 10 into the panel and 15 from its
right, each 8 over the one before), and the bracket closes under the last. FAF does not tell its panel when a group's units die, and its
own check each second takes its control away: the slots look each second too, and the column is laid out anew where a slot's control
changed, never each second, which would open a panel the player folded away (its arrow).

| On a slot | FAF's input |
|---|---|
| Tap | FAF's left press: the group is the selection (`ApplySelectionSet`); with *Plus* (Shift) it joins the selection |
| Two taps | FAF's double click (the tap ladder clicks a second tap on FAF's UI where the first went, and the message layer makes it its double click): the group again, whose second selection soon after the first moves the camera to it as the player's option `selection_sets_double_tap_behavior` says |
| Tap on an empty slot | Where FAF's selection set of the group still holds a unit that lives (`selectionSets`, `ValidateUnitsList`), FAF's number key: `ApplySelectionSet`, whose callback makes FAF's control anew, with *Plus* (Shift) `AppendSetToSelection`. FAF's check each second takes its control away once the units it was handed are gone, while the set may hold others: a grouped factory that was upgraded joins the group so with FAF's default options (`AddUnitToSelectionSet`, which tells no callback), and its slot showed empty while the PC's number key still selected it. Else nothing, and a hint says *Empty group: rest to store* |
| Rest | FAF's right press, which the hook makes the key action of Ctrl and the number, `UI_MakeSelectionSet`: the selection is the group from now on (FAF's own right press would append); where that put other units in place of those it held that live, the undo's button gives them back (The undo). With *Plus*: FAF's own `AppendSelectionToSet`, the selection joins the group. A hint says *Group N stored*, and the phone buzzes once for it (the mockup's one buzz): as the rest's ring came, and the store adds none; with *Box* counting the rest rang for nothing (above), so the store buzzes, as does one that no rest made, a mouse's right press (`TouchInput.storeBuzzes`, tested; on the emulator one buzz each, with *Box* lit 28 ms after the lift, without it 303 ms after the finger's down). With nothing selected a hint *Nothing selected*, and the group stays as it was |

The words are hints, where FAF prints its messages (`griff.lua` `Hint`); the buzz of a group stored comes from the hook through
`FafreGriffGroupStored` (`GriffAndroid.cpp`). A group is the UI's: nothing of it orders, so a tap or a rest on a slot neither ends the
undo's record nor takes its button away, but for the group's records a store makes or ends (The undo), and the handle follows the selection it
makes as any other. The rest is the panels' one rest time, not the mockup's 400 ms for groups. The mouse scheme keeps FAF's own groups
panel and its right press, as a PC's; the scheme is the game's, read when its activity starts (`FafreGriffWanted`).

The keyboard (T3.4): the hook of `ui/game/tabs.lua` adds *Keyboard* to the Menu tab's list, after Key Bindings, in both schemes. A tap
closes the menu as its other buttons do and calls `FafreKeyboard()` (`MenuFingers.cpp`), which the activity takes as a tap on *Keys* when
it next reads what is new: the phone's keyboard comes. It would go the same way, but at the phone's size the keyboard, once up, covers
all of the screen but its top, and with it the Menu's list: the Menu tab still opens, and a tap where *Keyboard* stands types a letter
into the game instead (a T, FAF's key to track the selected unit, on the emulator). So the keyboard goes by its own key or by Back, which
the keyboard takes for itself: the game gets no Esc of it (2026-11-14, on 3841); Enter puts it away too, after it has gone to the game,
and with the keyboard down Back is Esc again, which closes FAF's chat. The menu is as large as the list FAF shows fits (the
hook's `Built`, with 137 of FAF's lines for the tabs and their gap and 70 a button of the list as `BuildContent` picks it: the FAF
client's, a network game's, a replay's, else a single-player game's, less the buttons a ranked game hides; where FAF's Lua has changed
under the hook, those of its longest list): on 768 lines a single-player game's eleven buttons at 0.85, a replay's ten at 0.92, a
network game's eight at 1.10. The frame's bottom bracket reaches below the picture's edge.

FAF's avatars (T3.8) stay FAF's: a tap selects the next of an idle avatar's units, a double tap all of them, a rest (the right press)
selects the next and goes to it, the arrow opens the idle list. FAF starts the round over when the pointer leaves the avatar, which a
finger's pointer does after each tap (The pointer): each tap took the first idle unit. The hook of `ui/game/avatars.lua` keeps the round
where it was when the pointer leaves, so that taps take the idle units in turn as a mouse's clicks do, but only while it fits the
avatar's list, which FAF makes anew each beat, best first, without the units ordered since: at the next press the round goes on where
the unit it took last still stands just before it, and starts over at the best idle unit where it does not, as once the player has
ordered that unit, which a mouse's click after it went to give the order does too. So taps with an order between take the idle units in
FAF's order, and a rest after an order goes to the best idle unit left. Where the list has not changed, a mouse that leaves goes on with
the round too, where FAF starts it over, and after a double tap the round goes on from the unit after it, as FAF's index does for a
mouse that stays.

#### The groups

Supreme Commander 2's groups on a console (Griff v4, the user's wish on 0.14.0): the units one gesture ordered together, and the units
one factory order made, are a group, which one round icon shows in the world at their middle. Only one's own units are in groups (the
focus army's mobile units that live), and nothing of a group orders. The program keeps them (`native/src/host/GriffAndroid.cpp`;
`include/GriffGroups.h` `Book`, tested in `tests/wx/GriffGroupsTests.cpp`): what a watched gesture orders (its taps, strokes and rests,
a tap on FAF's orders panel included; the orders `griffundo::Takeable` counts) takes its units out of their groups into the gesture's;
the units a factory makes of one kind while its queue does not run empty, as FAF's construction panel shows one entry with its count,
are the group of that run until a gesture orders them (`griffgroups::Runs`, tested: a unit counts for the run when it is finished, a
kind of its own begins a run, a repeating queue never runs empty, and a queue the sync shows empty a frame before its last unit is
finished still gives that unit to the run). A unit leaves its group when another gesture orders it, when it dies or is no longer one's
own, and a group of fewer than two units is none. The draft's tick, the undo and the keyboard's keys make no group: they are no gesture's. An undo that takes a gesture back whole
puts its units back in the groups they were in before it (`Book::Undone`, tested), where no other gesture has taken them since.

The icon is FAF's strategic icon art (`fafre/griffgroups.lua`): the experimentals' round icon
(`strategicicons/icon_experimental_generic_rest.dds`, its `_selected` while all of the group is selected, as the game draws a selected
unit's icon) in the colour the game gives one's own icons (the army's, or FAF's team colour), with the group's count in FAF's font,
white with a shadow, written smaller where it would be wider than 0.8 of the icon, as a count of 100 or more is. Its disc is 20 of the picture's pixels at FAF's `strat_icon_scale` 1, as large as FAF's largest unit icon (a
selected land unit's; 16 at rest). It shows where the game draws strategic icons (not with FAF's icons off) and from the zoom on at
which the game draws one of the group's units as its icon (`CWldSession::RenderStrategicIcons`: the mesh's `IconFadeInZoom`, at the
latest 0.89 of the farthest zoom; `griffgroups::DrawsIcon`, tested): zoomed in, where the units are meshes, it has gone. It is a picture
on the map's view that takes no event, as FAF's reclaim labels are, so a pan, rest or stroke over it is the map's. *Groups* on the rail
shows and hides the icons (shown as a game begins, not kept in the prefs); hidden, there is no icon to tap.

| On a group's icon | FAF's input |
|---|---|
| Tap | The group is the selection (`griff.lua` `SelectGroup`: `SelectUnits`); with *Plus* it joins the selection (`AddSelectUnits`); with *Box* it joins it or, where all of it is selected, leaves it, as *Box*'s tap does with one's own unit. FAF's UI and an armed command mode come first; then the program asks what FAF's cursor is over, and an icon takes the tap within the tap help's reach of its disc, but with troops selected (the selection that takes orders) only within FAF's pick for them, 2 mm, and not where FAF's cursor is over a unit that is not one's own, which the tap orders on (`griffgroups::IconReach`, `IconTakesTap`, `IconAt`, tested). On the emulator, zoomed out with engineers selected, a tap 34 pixels from the middle of a group's icon (beyond 2 mm of its disc, within the tap help's reach) moved the engineers there, and a tap on the disc selected the group; *Box* and a tap on a selected group's icon took it out of the selection, and again put it back |
| Two taps | The tap ladder's second rung (`TouchInput.STEP_GROUP_AGAIN`, tested): the group again, and the camera goes to it as for FAF's control group selected twice, by the player's option `selection_sets_double_tap_behavior` (`selection.lua` `DoubleTapBehavior`'s steps, which `SelectGroup` takes since that function is local to its file) |

Checked in FAF 3835's `lua.nx2` before it was added: FAF shows its control groups and selection sets only in its panel
(`controlgroups.lua`, `selection.lua`), and nothing it draws in the world (`reclaim.lua`'s labels, `unittext.lua`, `notifyoverlay.lua`,
`OffScreenIndicator.lua`, the casting mouse) stands for a group. On the emulator at the phone's size and HUD 1.6 (2026-11-15, a skirmish
whose units cheats made): two moves gave two icons of 10; a tap on one selected its 10 units, two taps on the other selected them and
brought the camera down to them, where the icons had gone; *Groups* hid and showed them; a factory's four tanks of one order showed an
icon of 4. The groups' work in each frame, Lua and program, timed over 200 calls with 10 icons over 180 grouped units (255 in the
game): 0.23 ms with the icons shown, 0.15 ms hidden (the program's part 0.03 ms); the emulator's frames, about 70 ms with those units,
took as long with the icons as without within the measure's spread (medians 67.6 and 67.1 ms over 20 s each).

#### Past the map's edges

At the phone's HUD factor FAF's panels cover much of the view along its edges, and the engine held the camera so that the map's edges
stayed under them: scrolled to its bottom, the map's last third lay under the construction panel (the user's verdict on 0.14.0). The
camera's clamp (`CameraImpl::ClampTargetPos`, the engine's, past which FAF's Lua cannot move the camera) now lets the main map's camera
come so near each edge that the edge comes out from under the panels into the view clear of them, at any zoom: the build-time patch
`native/patches/src/moho/render/camera/CameraImpl.cpp.patch`, its rules `include/CameraClear.h`, tested in
`tests/wx/CameraClearTests.cpp`. The program measures what the panels cover (`GriffAndroid.cpp` `MeasureMargins`, one walk over FAF's
controls each 30 frames): the controls over the map that overlap by more than 0.4 % of the view's height each way are one panel; a panel
standing at an edge (within 1 %) counts for the edge it reaches in from least, so a panel in a corner, as the construction panel, counts
for the edge it lies along; one covering more than half of the view from each edge it stands at counts for none (a dialog's shadow), nor
does one sliding in from more than 2 % past an edge, nor what the player opened (`fafre/opened.lua`: windows, dialogs, dropouts, the
tabs' content). Of all that showed in a game the larger margin at each edge stays: the panels come and go with the selection, and a
margin that shrank would pull the camera back from an edge it went to. The clamp takes the ground under the corners of the view clear of
the margins, as the camera's last frame showed it, on the plane of its focus, and lets each edge come in as far as the nearest of those
corners towards it; a corner that shows no ground (above the horizon, as a camera turned far up could show it) bounds no edge, so each
edge goes by the corners towards it that show ground alone and keeps the engine's span where none does (`cameraclear::Allowed`; the
whole measure, `ClearSpans`, tested on a synthetic camera's rays: its corners, a zoom other than the frame's, a focus below the target);
never less far than the engine's own clamp lets it, and the minimap's camera keeps the engine's. Zooming and panning are the engine's as
before.

Measured on the emulator at the phone's size and HUD 1.6 (2026-11-15, Seton's Clutch, hover tanks along the four edges as markers): the
margins came to 33.7 % of the width on the left (the unit view), 19.4 % of the height at the top (the score panel), 7.4 % on the right
(the avatars) and 29.6 % at the bottom (the construction panel with the orders panel). Scrolled as far as it goes, the bottom edge lay
just above the construction panel, at the play's zoom and zoomed out; the top edge below the top panels; the left edge right of the unit
view. On the right the engine's own clamp already lets the view go past the edge (its span is the zoom's, narrower than the phone's
wide picture), so the right edge comes in further than the avatars need, as FA does on a wide screen. Only Griff measures the margins:
the mouse scheme keeps the engine's clamp. The ground is taken on the plane of the focus, so where an edge's land lies much higher or
lower than the focus it comes out from under a panel a little short of or past the panel's line. Again on Open Palms, whose edges are
land and snow (2026-11-15, at HUD 1.67, the largest factor on the phone's screen, and at 1.0), scrolled as far as it goes: at 1.67 the
south edge lay just above the construction panel, the west edge at the unit view's right side and the north edge just below the top
panels; at 1.0 the south edge lay above the construction panel, and the engine's own clamp let the view go past the west and north
edges further than the smaller panels need.

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
| Menu tab's buttons | `ui/game/tabs.lua:600` | 248x59, 269x64, 323x77 | 5.6, 6.1, 7.3 | at 0.85 with a single-player game's eleven, 0.92 with a replay's ten, 1.10 with a network game's eight, the app's *Keyboard* with them |
| Multifunction panel's buttons | `ui/game/multifunction.lua:218` | 67x48 | 4.6 | reach |
| Commander's avatar | `ui/game/avatars.lua:123` | 99x105 | 9.4 | |
| Idle avatars | `ui/game/avatars.lua:237` | 86x80 | 7.6 | |
| Idle list's rows | `ui/game/avatars.lua:533` | 137x80 | 7.6 | |
| The idle avatars' expand arrow | `ui/game/avatars.lua:256` | 16x48 | 1.5 | reach |
| Control groups | `ui/game/controlgroups.lua:83` | 73x54 | 5.1 | Griff's slots 1 to 3 are FAF's own controls, and an empty one their frame |
| Collapse arrows of every panel | `tabs.lua:441`, `economy.lua:82`, `multifunction.lua:209`, `score.lua:91`, `avatars.lua:54` | 28x41 | 2.7 | reach, at the screen's edge |
| Score panel's rows and small icons | `ui/game/score.lua:280`, `:315` | 480x22, 22x22 | 2.1 | reach; tooltips and the display's switch |

The arrows and tech tabs keep their size: they sit in FAF's outline, and a larger one would cover what is beside it. A
tap beside them reaches them; where a large button lies directly beside an arrow (the avatar, the Diplomacy tab under
the top arrow), only the strip at the screen's edge is the arrow's, and that edge a finger finds.

| Dialog | Lua | Factor at 768 lines | Fits | What is small |
|---|---|---|---|---|
| Options | `ui/dialogs/options.lua` | 1.75 | yes, reflowed: see "The menus" | |
| Key Bindings | `ui/dialogs/keybindings.lua` | 1.04 | yes | rows 986x22 (2.1 mm), buttons 207x43 (4.1 mm), scroll bar 14 wide (1.3 mm). Keys cannot be pressed on a phone: a finger reaches the rows, opens the groups and scrolls the list |
| Notify Management | `ui/notify/customiser.lua` | 1.04 | yes | buttons 156x43 (4.1 mm), scroll bar 14 wide (1.3 mm) |
| Save, Load, Load Replay | `ui/dialogs/saveload.lua`, `replay.lua` | 1.60 | yes, with the frame cut: see "The menus" | sort tabs 35 high (3.3 mm) |
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

FAF's Lua declares a screen's page and whose the fingers are, through four functions the app adds to the UI's globals
(`android/native/src/host/MenuFingers.cpp`): `FafreSetPage([left, top, right, bottom])`, `FafreSetFingers(menu)`, `FafreShowLines(top,
bottom)` and `FafreSetHud(factor)`. The native side keeps what they say; `Game.fromProgram` wakes the activity, which reads it on its own thread. `fafre/scale.lua` is
the only one that calls them. The front end's fingers are the menus' from the start. A session's are the game's (`TouchInput` as it
was), except while a `Dialog`, `DialogAt` or `Page` is open.

A new screen takes a line or two of Lua, as `ui/lobby/gameselect.lua`, which gives its sides as well:

```lua
CreateUI = Scale.Page(CreateUI, function(made) return made[1] end)
```

`Page` wraps what builds the screen. `find` gets the controls it made and says which control the page goes by, and has the place of,
and if a side of the page is not that control's, a table with `left`, `top`, `right`, `bottom`. When that control is gone so is the page.
Pages nest: the one under comes back as it stood, and one that has closed opens at its top when it opens again.

### The view

`PageView` (`native/src/touch`) works out how FAF's UI lies on the screen. With a page it is larger, the page's width fitted to the
screen's, at the page's left edge; a finger moves it up and down within the page's height and not sideways. The engine draws it so: the
touch layer gives it `PageView`'s mapping as the UI's view, a scale and an offset that every UI pass draws with (`UI_SetView`, the patched
`CD3DPrimBatcher.cpp`, `native/patches/README.md`). The picture keeps its size and the swap chain its images, and the text is drawn at the
larger size, sharp, as its atlases are distances (MSDF), whose edge the patched `CD3DFont.cpp` keeps a pixel wide. What the engine draws
beside FAF's UI (the world in a game, the build's label) is not zoomed. Fingers go back through `PageView`, so a tap lands where the
control is drawn; so does a mouse on a page, whose wheel scrolls the list under it or else the page, as a finger's drag does. The bar
(`fafre/bar.lua`) undoes the view (`FafreView`) and stays where it is on the glass, as large. Without the touch layer the view is the
identity and FAF's UI is drawn as ever.

With the on-screen keyboard up a page can go up that much further: the activity says how much of the screen it covers (the window's
insets, `Game.keyboardCovers`; before Android 11 half while SDL's keyboard is up). A tap on a text field opens the keyboard, and *Keys*
asks which field has the focus; either way the page goes where the field shows above the keyboard. Without a page the keyboard covers
what it covers.

### The fingers

`PageInput`, beside `TouchInput`:

- a tap is a click, through `Game.tap`, so with tap help; also after a rest, as a careful finger is slow;
- a finger that rests 500 ms, which it feels a second time, and lifts without moving is a right click where it rests, with tap help as well
  (`Game.rightClickNear`), as what FAF's right clicks are for is small: the host's kick of an observer in the lobby's list, closing a start
  position on the map. FAF's buttons take a right click for a click, so a slower tap still works, and it opens the keyboard on a text field;
- a finger that moves mainly up or down past the slop scrolls, presses nothing, and the pointer leaves at once;
- a finger that moves mainly sideways past the slop is a mouse's drag from where it came down: sliders;
- a finger that rests 300 ms, which it feels, and then moves is a mouse's drag in any direction: scroll bars, windows. Also after the
  500 ms. A press on a scroll bar stays where it landed, where a tap's help would give it to the arrow beside the thumb;
- two fingers do nothing;
- the pointer leaves 300 ms after the last finger, as in a game.

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

A mouse can click only what the view shows, so its wheel on a page goes the same way (`PageInput.wheel`): a notch is a drag of the system's scroll factor, the list under
the pointer first and then the page. It asks for the list as it starts after a pause, or when the pointer has moved. Where no page is larger than the screen the wheel stays a wheel for FAF's lists and the world view.

### The screens

| Screen | How | Why |
|---|---|---|
| Main menu | As FAF's | Its buttons are 358x70, 6.7 mm |
| Lobby, also a network game's | A page of its panel's width and all of FAF's lines: 1024 pixels fill the screen's width, the picture is 1.61 times as large, and 290 of FAF's lines are to scroll | Above a factor of 1 FAF's lobby loses its lower part, and it holds 16 slots, chat, the options list and the map on its 768 lines. The panel's 1024 are what a finger needs of the 1647; the rest is decoration |
| Slot rows | 44 apart with up to 8 players, down to FAF's 22 with 16: the map's start spots share the 352 lines; the art of the row's background stretches, a plain strip | Combos stay 18 high, but with 26 free lines between rows tap help reaches the whole row |
| Map and game options, Settings, Patchnotes, Presets, the large map, Load, the Mods Manager from there | On the lobby's page, at FAF's factor: all are narrower than its 1024 and a finger scrolls them; a popup comes into view from its top | They stand over the lobby, whose page is as large as they need. Rows a finger picks from are larger: maps 4 mm, patches 3.5 mm |
| Unit Manager | **A page of the whole picture** over the lobby's, so no zoom, at the HUD's factor: FAF's grid of all units fills any width, and a page has no way sideways | On the lobby's page it was cut off. Its cells stay about 4 mm whatever the factor; the rest grows, and a tooltip can be read |
| Replay, Save, Load, and their question boxes | **The frame cut**, as Options': the HUD's size on the glass, 1.6, and what is too high goes from the middle of the list and of the picture. 4 rows of 5 mm at a time. On the lobby's page it is whole, at a factor of 1. A session's is the same dialog at the HUD's factor | FAF's whole dialog fits only up to 1.2, with half of the screen's width unused |
| Licence | **The frame cut**, at 1.6: 17 lines at a time | Its text is what is read |
| Mods Manager from the main menu | The HUD's size on the glass, 1.6: FAF lays it out by the screen's height at any factor, 2 rows with their pictures or 6 without | Sharp, and all on one screen |
| Profile and Enter your Name | 1.51, as far as its 510 lines go, no cut needed; a row 4.5 mm by a larger font | Also the first thing a new player sees |
| LAN's Create | 1.6: its picture is small | |
| Questions (`UIUtil.QuickDialog`) | 1.6 in the front end, 1 on the lobby's page; as they were in a session | Buttons 6.4 mm |
| Main menu, Extras | As FAF's | |
| Credits, and the timeline | A film: a tap skips it | |
| Campaign | As FAF's, full screen: rows 3.2 mm and 4.1 mm apart, with reach; the choice of difficulty 5 mm a row | The art fits the picture, and the buttons do |
| LAN screen | A page of its panel's width and its Create button, which stands out of it: 1014 wide, 1.62 times as large | 974x734 |
| Options | **Reflowed**, no page: a factor of about 1.75, for rows of 5.8 mm as far as 440 of FAF's lines allow, and as many rows as fit, 5 at 768 lines; the list scrolls by finger. On a screen so large that FAF's whole dialog fits at that factor it stays as FAF has it | It stays sharp, and all of it is larger, not only what a finger hits. See below |
| Choices everywhere | Rows 5 mm by a larger size of FAF's font; the list grows to the left for its longer rows, as far as the screen shows, opens upwards if there is more room there, and has as many rows as show at a time, which scroll by finger, with the choice among them; a page goes where it shows | |
| Choices of pictures (colour, faction) | Rows 5 mm, the picture filling them; they have no scroll bar, so if they do not fit below they go in columns to the left, as far as the screen shows | 19 colours |

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
| Replay, Save, Load | Rows | `ui/controls/filepicker.lua:213` | 945x52 | 5.0 | a list by finger, a double tap opens one |
| | Sort tabs, Show only my files | `:502`, `:186` | 696x35, 41x41 | 3.3, 3.9 | reach |
| | Filename | `:167` | 952x16 | 1.5 | reach; a tap opens the keyboard |
| | Delete, Ok, Cancel | `ui/dialogs/replay.lua:97`..`:107` | 316x112 | 10.7 | |
| Licence | Its lines | `ui/menus/eula.lua:45` | 1008x22 | 2.1 | a list by finger |
| Mods Manager, main menu | Rows | `ui/lobby/modsmanager.lua:1068` | 1296x136, without pictures 56 | 13, 5.3 | |
| | Star, filters | `:1087`, `:506` | 66x66, 288x53 | 6.3, 5.0 | |
| | Search, its X, sort | `:421`, `:404`, `:1396` | 1263x38, 17x32, 120x22 | 3.6, 1.6, 2.1 | reach; a tap opens the keyboard |
| Profile | Rows, buttons | `ui/dialogs/profile.lua:75`, `:41`.. | 47 high, 298x105 | 4.5, 10 | |
| | Name | `:224` | 527x35 | 3.3 | reach; a tap opens the keyboard |
| LAN's Create | Name, port, Auto Port | `ui/lobby/gamecreate.lua:22`, `:71` | 544x28, 41x41 | 2.7, 3.9 | reach |
| | OK, Cancel | `:119`, `:41` | 316x112 | 10.7 | |
| A question | Buttons | `ui/uiutil.lua:1083` | 208x67 | 6.4 | 42 high on the lobby's page, 6.4 mm too |
| Map and game options | Map rows | `ui/dialogs/mapselect.lua:631` | 26 high | 4.0 | a list by finger |
| | Filters, the options' choices | `:312`, `:361`, `:769` | 80x18, 60x18, 266x18 | 2.8 | reach |
| | The options' rows | `:784` | 276x46 | 7.1 | the row's free part is the choice's by reach |
| | Mods, Restrictions, Random Map, OK, Cancel | `:477`..`:543` | 130x42 | 6.4 | |
| Unit Manager | Cells, presets | `ui/lobby/unitsmanager.lua:596`, `:783` | 41x41 | 3.9 | a grid by finger; a resting finger reads the tooltip |
| | Ok, Cancel, Reset | `:456`..`:464` | 208x67 | 6.4 | |
| Settings | Rows, sliders | `ui/lobby/lobby.lua:6550`.., `:6565` | 26 high, 198x22 | 4.0, 3.4 | reach; a drag from anywhere on the bar |
| Patchnotes | Patches, X, footer | `ui/lobby/changelog.lua:210`, `:124`, `:148` | 23 high, 22x22, 130x42 | 3.5, 3.4, 6.4 | a list by finger |
| Presets | Rows, X, buttons | `ui/lobby/presets.lua:189`, `:207`, `:212`.. | 18 high, 18x18, 130x42 | 2.8, 2.8, 6.4 | a list by finger; a double tap loads one |
| Observers | Rows | `ui/lobby/lobby.lua:4344` | 15 high | 2.3 | a list by finger; the row under a resting finger lights up |
| Start positions | Marker | `ui/controls/acubutton.lua:51` | 14x14 | 2.2 | reach, for a tap and for a right click |
| Campaign | Rows | `ui/campaign/selectcampaign.lua:116` | 370x34, 43 apart | 3.2 | reach |
| | Back, Load, Select | `:77`, `:95`, `:91` | 198x70, 294x70 | 6.7 | |
| | A film's buttons, Hide Log | `ui/campaign/operationbriefing.lua:539`.., `:383` | 34x34, 174x30 | 3.2, 2.9 | reach |
| | Difficulty, its rows | `:830`, `:893` | 98x34, 263x52 | 3.2, 5.0 | reach |

What is still not comfortable: the slot rows at 16 players (22 apart, 3.4 mm, choices 2.8 mm: tap help picks the nearest); the
combos' own buttons, which keep their art at 2.8 mm in the lobby and 3 mm in Options and rely on tap help; the score panel's and observers'
small texts; Unit Manager's cells, which at 3.9 mm lie side by side, so that reach cannot help; Key Bindings' rows, 2.1 mm; a Mods Manager row
of FAF's pictures, of which 2 show at a time. The picture is scaled up on a page, so it is a little soft. 19 colours in two columns or three cover what is beside them while open.

Not tried on a real phone: all of this is the emulator's composition and its touch screen, at the phone's size; a mouse's wheel is the emulator's injected one. A real LAN's server list:
the emulator's network shows none, so the list was tried with made-up entries, one of which pointed at the PC's game (`Join` worked), and
the PC's game by address. The emulator's game has one map and no campaign beyond the first operation: long lists of maps, and the campaign's
list of operations, were tried with made-up entries beside the real one, and the Credits' film is not there at all, so it was seen to give way to the
menu and not to play. A kick and a real observer: the observers were made up, and the question was answered with Cancel.

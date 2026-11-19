# Android test app

An Android app that runs the engine's simulation on a phone, without graphics: the sim-math tests
and FAF vault replays played to the end on the headless simulation (`src/headless`), with every
checksum the players' games recorded compared against the phone's. It is how a phone's CPU is
checked before the game itself runs there. The game card starts the whole engine: the engine's own
POSIX client on SDL3, in SDL's activity. The engine's programs and libraries come from the Android
native build ([native/README.md](native/README.md)), which builds the engine for Android without
changing its files and the client on top.

## Building

Needs the Android SDK (platform 35, build-tools 35), the NDK release in
`cmake/toolchains/android-pins.cmake` (`sdkmanager "ndk;29.0.14206865"`), JDK 17, CMake 3.25+, Ninja
and Python 3. `ANDROID_HOME` names the SDK.

```sh
# The engine (dependencies once, then the native build's preset):
cmake -DFAF_DEPENDENCIES_ONLY="WildMagic3p8;zlib-1.2.3;vulkan;ffmpeg" -P cmake/FafDependencies.cmake
cd android/native
cmake --preset android-arm64 && cmake --build --preset android-arm64   # build/android-arm64/bin

# The app (JAVA_HOME = JDK 17):
cd ..
./gradlew assembleRelease    # app/build/outputs/apk/release/faf-re-android-<version>-arm64-v8a.apk
```

For the emulator, build the `android-x64` preset too and add its ABI:
`./gradlew assembleRelease -PfafAbis=arm64-v8a,x86_64`. The emulator runs x86_64 natively and arm64
under its ARM translation (an arm64-only APK).

- `stageFafNative` packages `headless` and `faf_sim_math_tests` as `libfafheadless.so` and
  `libfafsimmath.so`: Android extracts only `lib*.so` files into the app's native library folder, the
  one place an app may start a program from. It adds the game's `libfafgame.so` and DXC's
  `libdxcompiler.so`. `gradle.properties` names the build folders.
- SDL's Java side (`org.libsdl.app`, the game's activity's base) comes from the SDL release the native
  build links, `dependencies/SDL3/android-project` (the native configure downloads it), so that both
  halves are the same release.
- `stageFafAssets` puts `tests/sim_math/golden.txt` into the assets.
- `versionCode` is `git rev-list --count HEAD`, so a newer commit installs over an older one, or what
  `-PfafVersionCode=<n>` names (1 to 2100000000), for a build that must install over one of another branch
  (`VersionCodeTest` checks both); `versionName` is in `gradle.properties`.
- Signing: `FAF_ANDROID_KEYSTORE` (with `FAF_ANDROID_KEYSTORE_PASSWORD`, `FAF_ANDROID_KEY_ALIAS`,
  `FAF_ANDROID_KEY_PASSWORD`), else the standard debug key `~/.android/debug.keystore`. Android installs
  an update only with the installed app's key; the testers' releases are signed with the debug key of
  the machine that built them.

## What the app does

The application id is the earlier launcher releases' (`io.github.m3rt1n99.fafre`), so an update keeps
the game data they imported. Everything lives in the app's external files folder
(`/sdcard/Android/data/io.github.m3rt1n99.fafre/files`):

| Folder | What |
|---|---|
| `scfa/` | The user's own *Supreme Commander: Forged Alliance*: `gamedata/*.scd`, `fonts/`, `sounds/`, `movies/` (card 2's *Import SCFA folder…* or *Import SCFA ZIP…*, which copy what an earlier release's import has not, as the setup's folder and ZIP do, below), `maps/` if imported; `.zip-import/` while a ZIP's import runs |
| `parity/game/<version>/` | FAF's game files for one game version, packed as `tools/parity-data` packs them for CI: `gamedata/*.nx2` whole, as FAF's updater publishes them (pack format 3), `bin/init_ci.lua`, `fa_path.lua` pointing at `scfa/` |
| `parity/replays/`, `maps/`, `m28/` | Downloaded replays, vault maps and M28AI versions |
| `parity/runs/<id>/` | One replay's run: `replay.scfareplay`, its own vault, `headless.out`, `engine.log`, `speed.csv` |
| `parity/results/` | The results zips |
| `game/` | The game's: `home/` (its HOME: the preferences, the documents), `vault/`, `engine.sclog` |

- **Math check**: `faf_sim_math_tests golden.txt --known known_android-<arch>.txt` (the overlay only
  where `tests/sim_math` has one for the ABI: arm64's records the square root of a negative number,
  whose NaN is positive on ARM). PASS means all golden cases (recorded on the x87 of windows-x64),
  with the overlay's recorded differences, equal the phone's results. On a failure the app also
  writes every differing case (`sim_math_known.txt`, the overlay format).
- **Replay parity** follows `tools/parity-data/shard.py`: the replay from FAF's vault, decoded
  (zstd or zlib) and checked (featured mod `faf`, engine version, M28AI as the only sim mod); FAF's
  game files of its version from FAF's updater (about 700 MB once per version, kept whole for the
  game); its map from `scfa/maps` or FAF's map vault; then `headless --replay`. The verdicts are shard.py's:
  *pass*, *desync* (with the first beat whose checksum differs; a desync one live client had alone is
  not counted), *crash*, *incomplete*. Requests to FAF go one at a time, a second apart.
- **Where the simulation ran**: once a second during a replay, the busiest thread of the headless
  process (the simulation's) from `/proc/<pid>/task/<tid>/stat` (CPU time, last CPU), that CPU's
  class (little, mid, big by `cpuinfo_max_freq`), clock (`scaling_cur_freq`) and cap
  (`scaling_max_freq`), Android's thermal status, the battery's temperature and charger, the screen
  and whether the app is in front: a line under each replay, `speed` in `summary.json`, the rows in
  `speed.csv`. The screen stays on while a test runs.
- **Performance hint** (the box under the replay IDs, on by default): the app opens an Android
  performance hint session (`PerformanceHintManager`, Android 12+) for the simulation thread and
  reports its run time every 100 ms against a 50 ms target, as a game reports its frames; the
  speed line says whether the phone granted it. The manifest files the app as a game
  (`android:appCategory`); `summary.json` `game` shows the category and game mode the phone gives
  it. A run first downloads every replay, map and M28AI version it needs, then plays them, so a
  long run needs no network once the games start; downloads are retried for about ten minutes
  when the network drops; a failure on the phone's side, such as a full disk, ends a download at
  once.
- **Renderer test** (card 4) is retired: it brought up the engine's Vulkan device on a surface of its own
  before the game could draw, on the earlier host's window layer, which the move to SDL3 retired. The
  game card's report names what it showed (below).
- **Game** (card 5, `GameActivity`): `libfafgame.so`, the engine's own POSIX client on SDL3, starts as
  the PC starts it, with the SCFA import and FAF's game files of the newest version on the phone (the
  highest whole one; a launcher's start names its own, below): `ForgedAlliance.exe /init <its init file>
  /log game/engine.sclog /notelemetry /fafre_hud_scale <factor> /fafre_hud_most <factor> /fullscreen <the picture's size>`, the program's path
  in the game files' `bin` folder. `GameActivity` is SDL's activity (`SDLActivity`), which loads the library
  and starts the program on a thread of its own; it runs in a process of its own (`:game`), which ends with
  the program, as a Windows program's does, and goes on while the activity is in the back, its simulation
  at the pace it has in front (on the emulator a skirmish for a minute after Home kept real time, with the game the
  last app in front, and a LAN game between two emulators its 0.8 of real time, which `NetworkGameService` keeps going
  there: Network games, below). When Android destroys the stopped activity, as it does when it wants the memory and at
  once under the developer option *Don't keep activities*, the program goes on and the next activity shows it. The
  emulator's Android 16 froze the process once it had become a cached one, with the activity destroyed outside a
  network game: the game waited there until it was back in front. Whether a system freezes cached processes depends on
  its version and the device; where it does not, the program goes on in a cached process until Android ends it. SDL gives the program the surface (Vulkan), the fingers as the mouse (a tap is a click, a
  finger that moves drags with the left button), the keys and the sound; Back is Esc. The sound pauses in the back
  and while another app has the audio focus (a call), and plays again in front with the focus. The game's movies
  play (FFmpeg; `/nomovie` only when the import has none; a tap skips the logos, another the film).
  - The game draws at a size of its own (`PictureSize`, the card's *UI size*): the surface's buffers have
    the picture's size (`SurfaceHolder.setFixedSize`), the compositor scales them to the screen, and
    `GameSurface` hands SDL the fingers and the mouse in the picture's pixels, so a tap lands where the
    finger is at every size. FAF's UI has a fixed size in pixels: 768 lines on a phone make it larger.
  - The UI package for fingers (`app/src/main/assets/fafre_ui`, [TOUCH_UI.md](TOUCH_UI.md)) is mounted and
    hooked as FAF's init file allows, with the HUD's factor from the screen (`HudScale`), so FAF's dialogs,
    choices and HUD come out at the sizes TOUCH_UI.md gives. The pages (the lobby and the LAN screen shown
    larger, scrolled by a finger) and the touch schemes come back with SDL's input in the next stage: until
    then those screens keep FAF's size, with choices of about 3 mm, as the package sizes them for the page's
    zoom; the touch controls' button stays at *a finger is the mouse*, and Griff's Lua does nothing.
  - A tap on a text field (the lobby's chat, a profile's name, the console's line) brings the phone's
    keyboard, and so does *Keyboard* in the game's Menu tab; typed text goes to the field, Back hides the
    keyboard before it is Esc again, Enter goes to the game and then hides it.
  - The report (`game.txt`) names the GPU, each swap chain, whether BC textures are the GPU's or decoded
    on the CPU, the sound's output or why the sound did not start, and the whole command line. Tests' extras
    of the activity (`am start -S ... --ef hudScale 1.6 --es arguments /spewsound --ez bcOnCpu true`): the phone's HUD factor on the emulator, more engine
    arguments, and the phones' way with BC textures on a GPU that has BC. Only the program's start reads them:
    without `-S` a running program goes on as it was, and the report says so.
  - A launcher, the FAF client, an app of its own, starts the game through `LaunchGameActivity` for a result (the
    launcher's contract, below), which checks the start and forwards it here. `GameActivity` is not exported: it takes the
    same extras from the app itself, from the trampoline after its checks and from a test through a root shell, as the
    [native README](native/README.md)'s GPGNet check does, where only the activity's own switches are refused among the
    launcher's arguments. Those go after the activity's arguments and a test's, before `/fullscreen`. What a start needs
    first, its map and mods fetched and its replay copied, the activity gets on SDL's thread before the program starts
    (`Preparation`), with the step over the black surface; Back cancels it. A start that fails before the program runs,
    the Game card's too, ends with the result the contract gives; the report has its code and line, and its end, like the
    toast of a start that waits for no result, keeps the card's words. Any start but a launcher's while the program runs
    shows it, as above.

  What follows is the game card of release 0.14, on the earlier host (`LegacyGameActivity`, the
  Win32 message layer and wx-lite): the bar, the touch controls and the pages come back on SDL's activity
  in the next stage. There a mouse was the
  mouse, keys were the keyboard and Back was Esc. Fingers acted as a mouse (`TouchInput`):
  in a game a tap is a click, a finger that moves drags, a finger that stays holds the right button until it
  lifts (an order), a quick tap of two fingers is a right click where the second came down, two
  fingers that move together pan the camera and two that move apart or closer zoom it. While a
  building's ghost waits to be placed, a tap puts it there and a tap on it places the building; a
  finger that moves takes it along, from wherever on the screen it landed, and one that stays a moment
  away from it moves it by the smallest steps, to set it against another building; a finger that stays
  on the ghost until the tick and then moves draws a line of buildings, which go up when it lifts, and a
  second finger gives that up; a quick tap of two fingers cancels. A bar at the screen's edge has what fingers have not: *Order* swaps the left and right
  buttons, so that taps give orders, *Shift* and *Ctrl* stay down until tapped again, *Del* makes a tap
  delete the queued order under it, and *Keys* opens the on-screen keyboard, which plain keys go to the
  game from, with what it typed shown over the picture, as it covers the game's text field; a tap on a text field opens it, and
  so does *Keyboard* in the game's Menu tab (after Key Bindings, in both touch controls). In the menus, and over a
  dialog in a game, a tap is a click, a finger up or down scrolls the list under it and then the page, a finger sideways is a drag (sliders), one that rests and
  then moves drags anywhere (scroll bars), one that rests half a second, which it feels twice, and lifts is a right click (the host's kick of an observer, closing a
  start position on the map), and two fingers do nothing; the lobby and the LAN screen are shown larger as a page, which a mouse's wheel moves too, Options, Replay, Save and Load have fewer
  rows at a larger size, and FAF's other dialogs are sized to what they need. The game's HUD is scaled to the screen, a tap beside a small control
  hits it, and the pointer leaves when the fingers have, so that rollovers go ([TOUCH_UI.md](TOUCH_UI.md), where the bar
  is, and what each hook of the UI package `app/src/main/assets/fafre_ui` is for). The game draws at a size of its own (`PictureSize`, a button on the
  card): its UI has a fixed size in pixels, so 768 lines on a phone make it larger, with fewer pixels
  to fill, and the compositor scales the picture to the panel. The host writes `game.txt` in the
  app's files; when the process is gone, the app adds how it ended (the exit code, a fatal error's
  message, or what `ApplicationExitInfo` says, the tombstone of a native crash) and why the renderer
  could not start, from the engine's log. A new game keeps the last game's as `game-previous.txt`
  and `game-previous-tombstone.pb`. A crash also leaves the engine's own report in the game's HOME
  (`faf_crash_latest.txt`).
- **Touch controls**: a button on the game card chooses how fingers play a game, *mouse* (the default, above) or *Griff*
  (`TouchScheme`), from the game's next start on (a game that runs, also in the back, keeps the controls it started with),
  and the menus are the same in both. With Griff a finger works the map directly, and every gesture is one of FAF's own
  inputs, as a PC player gives them ([TOUCH_UI.md](TOUCH_UI.md), "Griff"):
  - A tap on your own unit selects it, also when it misses by a little (3.5 mm, 2 mm with troops selected, so that a tap
    for a move beside your units does not take them). A tap anywhere else gives the selection FAF's order there, as its
    right click does: move, attack an enemy, assist a factory with engineers. In a replay your own are those of the army
    you focus by its row in the score panel; with none focused, as a replay begins, a tap on a unit selects nothing, as a
    PC's click does.
  - Taps in a row climb a ladder: two quick taps on a unit take all of its type on the screen, three all of it on the map;
    two on the ground turn the move of the first into an attack-move; two on nothing with nothing selected take all your
    mobile units on the screen.
  - One finger that moves pans the camera, and the map glides on when it lifts moving; one that rests a moment (a ring
    shows it) and then moves draws a selection box. Two fingers zoom where they are and move the map with them at once, and
    a quick tap of two fingers lets the selection go (or ends an order picked in the orders panel). A finger that rests on a
    unit shows what it is, and lifts with nothing done.
  - A round handle with the selection's count stands beside it, clear of its units, or at the screen's edge with an arrow
    when the group is out of view or under a panel. A tap on it flies the camera to the group. A stroke from it lays a
    route: moves queued along the way, and at its end FAF's order for what it ends on (an enemy: attack, a factory: assist,
    the minimap: that place), which a label above the finger names while it draws, with a dot at each waypoint. A long loop
    back to the handle is a patrol; for factories alone the route is their rally point. A second finger, a short loop or an
    end on a panel gives the route up, and the screen says Cancelled.
  - The rail is a row at the top left, just under FAF's top panels, of *Box* (a drag is a box at once, a tap on a unit adds
    it or takes it out, a tap on nothing leaves *Box* lit), *Plus* (orders are queued, as with Shift) and *Attack* (taps and
    routes are FAF's attack-move, where FAF's orders panel offers it), side by side, each as large as the room above FAF's
    unit view where it stands highest (a replay) allows, 11 mm on the S22 Ultra and 9 mm at least. A tap lights one for
    the next action (a yellow edge and "1×"); held, it counts until it is let go (a white edge). An order picked in the
    orders panel ends after the one that a lit *Plus* queues; a held *Plus* keeps it until it is let go, as Shift does. With
    *Plus* or *Attack* and troops selected, a tap on one of your units is the order there too. In a game the keyboard is
    *Keyboard* in FAF's Menu tab; *Keys* is the menus' only. Each of Griff's buttons (the rail's, the pill, the tick and the
    cross) steps aside while something you opened of FAF's lies under it, such as the overlay filters' list beside the
    multifunction panel, the Menu, a window or a question box, and comes back when that closes.
  - On FAF's panels a tap is a click and a rest a right click (on an avatar: go to it), with the same help beside small
    controls as with the mouse. Taps on an idle avatar take its idle units in turn, as a mouse's clicks do; once you have
    given the one it took an order, the next tap takes the best idle unit left. A double tap takes them all.
  - A build icon of the construction panel opens a draft instead of FAF's ghost: a tap on the map places a plan of that
    building, on FAF's grid (an extractor on the free mass spot within 5 mm, else "No free resource here"), a tap on a plan
    takes it away, a drag from the newest plan draws a line of them side by side, a drag from another plan moves them all
    (extractors stay on their spots), and a drag elsewhere pans. A finger that rests shows the plan its lift places, with a
    loupe above it of what the finger covers. With an extractor picked, *Box* and a drag give every free mass spot in the
    box a plan. Another icon picks another building for the next plans; plans on taken ground are red. The tick orders them
    all with FAF's build mode, as many clicks (the first replaces what the engineers do, the rest are queued; with *Plus* all
    are queued), each in its plan's cell also zoomed far out, moving the camera to a plan the screen does not show, or closer
    where a pixel spans more than a cell, and back; the cross, or a tap of two fingers, drops the draft. The selected
    engineers build it, or else the engineers selected when a building was last picked. Up to 30 plans, and a line of up to
    30; while plans wait, an order of the orders panel says "First ✓ or ✕". FAF's build keys and templates keep FAF's ghost,
    which is placed as with the mouse; a template put away does not take the tick's place. The tick (and the undo) waits
    for a finger that pans or draws a box to lift; while their clicks run the camera holds still and a finger does nothing,
    and a gesture they came into gives nothing more, also after them (a line on FAF's ghost is given up).
  - After an order of Griff's gestures a pill *Undo* stands for 3 s in the rail's row, right of *Attack* and as high, with
    a bar of the time it has left; a tap on it takes the gesture's orders back with a PC player's inputs, once the game
    has taken them: each order of the gesture's goes by FAF's Shift+Ctrl right click on its waypoint, which takes it from
    all of its units, so a queue it was appended to stays as it was (a building begun goes on) and a unit that was idle is
    idle again; what an order replaced and the game still held when the order arrived (a network game's delay later)
    comes back by FAF's order modes and clicks, all queued with Shift (a building by its name, one begun by its repair, an
    order on a unit where that unit is now and only while the cursor is on it; a factory's rally route by a first click
    that replaces). An order given just before, which the game had not taken yet as the gesture began (a paused game, a
    network game's delay), counts as the unit's own, and so does what the game makes of it (the attack-move of FAF's
    attack-move button, the rebuild of a Repair on a wreck, the storages FAF rings an extractor with for an engineer's
    assist): it stays where the gesture was queued behind it and comes back where the gesture replaced it. What the game
    made of it in the very moment the gesture replaced it, which nothing showed, comes back only where FAF makes it again
    (the rebuild); else the undo counts it. An assist the undo gives back brings no storages of FAF's: its assist-to-cap is
    held off while the undo's clicks run, as for a player who wants only the assist. A route, a tick of the draft and a
    ground double tap are one undo each. Only the last order counts: one of FAF's build keys, a template, the keyboard's keys
    or the construction panel (an enhancement, an upgrade, a factory's queue) takes the pill away; FAF's own upgrade for an
    assist (its assist-to-upgrade option) does not. While it waits for the game (the fingers free meanwhile) or a finger's
    pan or box and its clicks run the pill says *Undo…*; its clicks never make a double click with the press before them
    (one at that very spot waits until the double-click time has passed, as a player's would). What cannot come back (an
    enhancement the order took, a factory's builds a Stop took, an order on a unit that is gone) or go (the attack-move's
    script left behind an order the unit kept), and what the undo cannot tell (an order the game took in a moment it read
    nothing of, also where a slow frame of the screen brought several of the game's moments at once, what the game made of
    an order in the moment the gesture replaced it, an attack-move of the gesture's at the very spot of one of the unit's
    own), makes the screen say *Undone in part*, also later where FAF's
    upgrade for an assist comes only after the undo (a paused extractor's, in a paused game, also after more undos);
    an undo whose clicks ended before they made anything, or that an order or the game's end came before, says *Not
    undone*, and in the first case the pill offers it again. A tap that comes just after the pill went (up to 0.4 s late)
    lands on nothing. Each order of Griff's gestures leaves pins at its places for 1.5 s,
    in the colour of its kind (move, attack, patrol, build), beside FAF's own blips; FAF's ghost and keys leave none.
  - Control groups 1, 2 and 3 always show under the avatars, an empty one dim, with the group's count: a tap selects the
    group (with *Plus*: adds it to the selection), two taps also move the camera to it as FAF's double click does (its option
    for control groups), and a tap on an empty one says "Empty group: rest to store". A rest stores the selection there,
    replacing the group as Ctrl and the number do, and with *Plus* adds it to the group; the screen says "Group N stored"
    and the phone buzzes once (as the rest began, or with *Box*, whose rest gives no ring, as the group is stored), or
    "Nothing selected". FAF's other groups (4 to 0, which a keyboard's Ctrl makes) stand below them.
    A group is no order: it leaves the undo's pill as it is.
- **FAF's newest version** (card 5, *Get FAF's newest version*; `FafRelease`, `NewestGameFiles`, TestService's
  `ACTION_GAME_FILES`): so that the phone can play a PC on FAF's current version. The number is GitHub's latest release
  of FAForever/fa (`api.github.com/repos/FAForever/fa/releases/latest`, no key: 60 requests an hour from one address,
  which phones on mobile data may share), taken when FAF's updater has its `init_faf_<v>.lua`; a release that is tagged
  but not deployed yet gives way to the next lower numeric tag that is (`/tags`, at most five tried). The size is the
  updater's Content-Length of the packages still to get, less what a cut-off download left (3840: 682 MB). It must fit
  in the phone's free space with 100 MB to spare (750 MB counted when the updater does not say), or the card says how
  much is free and nothing is downloaded. The init file is fetched and checked first, so one the app cannot use fails
  before the packages. Then `GameFiles.ensure` downloads them as a replay's run does, with the Progress card's steps,
  cancel and log. On a metered network (mobile data, a hotspot, a Wi-Fi set so) the card asks first, and while it asks
  the button asks again with the size it knows, without a new check; without that yes the download runs only on a
  network that is not metered and stops when it becomes metered, and the next run goes on from where it stopped. When a
  later check settles on another version, what the button's unfinished download of the old one left goes; a replay
  test's partial downloads of other versions and every ready version stay. A phone with a version above FAF's newest
  downloads nothing, and the card says the game starts the phone's. The card's line shows both versions, `FAF game files: 3835 (newest at FAF: 3840)`; if GitHub is unreachable or at its
  limit, the line says so and nothing is downloaded. Older versions stay for the replay test. The UI package's hooks are
  appended to FAF's files of whatever version, each in a pcall, so a change in FAF's files shows as a `fafre_ui:`
  warning in `engine.sclog` rather than a broken game: a hook says once what it misses of FAF's, and as each UI state is set
  up each hook whose FAF file is gone is named. FAF 3841 moved its chat to `ui/game/chat/` and its patch notes to
  `ui/lobby/changelog/`, which have hooks of their own; the hooks of the old files stay for older versions ([TOUCH_UI.md](TOUCH_UI.md),
  How it is built). The check against the real servers is a JVM test outside the
  normal run: `FAFRE_ONLINE=1 ./gradlew :app:testReleaseUnitTest --tests '*FafReleaseOnlineTest' --rerun`.
- **Network games**: *Multiplayer LAN* in the game's menu is the game's own lobby, with nothing from FAF's servers
  (`native/README.md`, The network). It plays against FAF's game on a PC in lockstep, and both sides need the same
  game: the app's FAF files are those of the newest version on the phone, and the game card's *Get FAF's newest
  version* brings FAF's current one. A PC's FAF folder may
  be on another version. Then the lobby and the first minutes look right, and the game desyncs (a newer exe on 3835's
  Lua did at beat 2350). FAF's client keeps the version of the last replay it played in `replaydata`, program and files.
  The map must be on both sides.
  - On the PC: `<FAF>\replaydata\bin\ForgedAlliance.exe /init <FAF>\replaydata\bin\init.lua /windowed 1280 800
    /hostgame UDP 16010 PC LAN SCMP_009`, with the program allowed through Windows' firewall on the private network
    (it asks the first time).
  - On the phone: *Multiplayer LAN*, the PC's address and 16010 in the IP and port fields (a tap on a field opens the keyboard),
    *Connect*, then the *Ready* box. On the same Wi-Fi the PC's game should also be in the list above them, with a
    *Join* button.
  - On the PC: *AI: Easy* in an open slot, *Ready*, *Launch Game*.
  - Home or the screen going off: the game goes on for everybody, and back in the app it is in sync. While a network
    lobby or game has its socket open, a foreground service runs in the game's process (`NetworkGameService`, type
    `specialUse`, as `dataSync` is limited to 6 hours in 24 on Android 15), with a partial wake lock. Without it Android
    cuts the app's network a few seconds after Home (`APP_BACKGROUND`), the game stands still for all players, whose
    connection dialog opens, and from 170 s each side ejects the others by FAF's rules. Its notification says *A network
    game is running*, and a tap on it brings the game back. In the back the game's window is minimized: it draws nothing,
    and its simulation and network go on, also while a game that the host launched loads.
    - The source is the transport (`Game.networkInUse`): a UDP connector's socket, on SDL's activity the engine's count
      of UDP connectors with their socket open (`NET_OpenUDPConnectors`, `native/README.md`). A skirmish has none (`LOBBY: Game
      port 0[None]`) and starts no service, nor does the LAN screen's search for games, whose list has no use for the
      network in the back. The service goes within a second of the socket's end (leaving a lobby, the program's end), about
      3 s after a game has ended, as the game over panel and the score screen hold the socket with nothing left to play,
      and after 30 minutes in the back, so that a lobby left behind does not keep a phone awake; it comes back with the
      activity if the network is still needed. While it runs Wi-Fi does not save power on the socket either (a Wi-Fi lock
      of high performance), which only a phone can show the worth of. A host in the back does not answer a LAN search on
      a real Wi-Fi, as the multicast lock is held in front only: join it by its address then. A swipe in Recents ends the game's process, as it does without the service; force-stop and a crash leave nothing,
      and nothing restarts it.
    - Android allows the start only from the front. A refusal leaves `network game: no foreground service: <why>` in
      `game.txt`, the game goes on, and the app asks again the next time the activity comes to the front; until then Home
      stops the game for everybody, as above.
    - On SDL's activity (stage 2b of [CLIENT_PLAN.md](CLIENT_PLAN.md)) the service is the same; checked there in a LAN game
      of two emulators with an easy AI, with no desync: Home on one side for a minute, the other side's activity
      destroyed for a minute, the service from the lobby on, its end a few seconds after *End Game* and within a second
      of leaving a lobby. The checks above against a PC's game (the screen off, Doze, five minutes in the back) ran on the
      earlier host only.
    - What remains is what only a phone shows, as all of this ran on the emulator's Android 16: Samsung's and others' own
      limits on a background app (*Put unused apps to sleep*, deep sleep), a CPU that really sleeps and Wi-Fi's power
      saving with the screen off, and whether a phone keeps the game's speed in the back, where all players wait for the
      slowest.
  - A lost network or a host that vanishes ends in the game's dialogs: in the lobby *Connection to host timed out*
    (*Keep Trying*, *Give Up*), in a game the connection dialog. *End Game*, as *End Replay* in a replay, goes to the score screen; the others play
    on. A port another program holds gives *Connecting...* with its *Abort Connect*, and the log says
    `bind(<port>) failed: WSAEADDRINUSE`.
  - A third player: the host passes each player's address on to the others, and they connect to each other. Checked
    in the lobby, not in a game. With the PC's game hosting, the emulator and a second game on the PC joining by the
    PC's LAN address, all three are connected to each other (`"PCJoin" ... has established connections to: 0, 1` in
    the emulator's log). With the emulator hosting, each of two games on the PC reads what it passes on about the
    other, to the address and port; they cannot reach each other there through the emulator's NAT. The emulator's NAT
    also rewrites an address: joining by `10.0.2.2` the host passes on `127.0.0.1`, the other game's packets arrive
    from `10.0.2.2`, its own from the LAN address, and each side rejects the other (*Connecting to ...* stays).
  - The app starts the game with `/notelemetry`: the engine's records of desyncs and of a game's end, with the players'
    names, stay on the phone instead of going to the engine author's server.
- **Share results** zips `summary.json`, the math output, the log, the game's report, tombstone, log and crash report
  (`game.txt`, `game-tombstone.pb`, `game/engine.sclog`, `game/faf_crash_latest.txt`), the last
  game's (`game-previous.txt`, `game-previous-tombstone.pb`) and each replay's `headless.out`,
  `engine.log` and `speed.csv`. `tools/tombstone.py` reads a tombstone, with its stacks symbolized. The FAF client gets the
  game's part of it scrubbed, the tombstones as text, for its problem report (the launcher's contract, The logs).

The default replays (26930624 and 26902315), *Use the core set* and *Use the extended set* (the core
set and its ten longer games) are from [docs/PR_REPLAY_CHECK.md](../docs/PR_REPLAY_CHECK.md), all
recorded on FAF game version 3835. A test runs at full speed only with the app in front: Android
moves a background app, and the simulation process it started, to another CPU group, on a Galaxy
S22 Ultra the little cores.

## The launcher's contract

The FAF client is an app of its own (`com.faforever.rustclient`, signed by another key), which starts the game across
apps and reads its status and logs. This app stays the game: the engine, the SCFA import, FAF's game files and the test cards.

**Who may.** Apps of one signer, whose certificate's SHA-256 is on the allowlist, in one place (`Callers.ALLOWED`): this
PC's debug key, which also signs this app; the client's release certificate comes there once it has one. Android 9 and
later answer with `PackageManager.hasSigningCertificate(package, sha256, CERT_INPUT_SHA256)`, which also knows a rotated
key's past and gives an app of more than one signer none of theirs (a key's rotation is a single signer's); earlier ones
with `GET_SIGNATURES`, by the same rule. A permission with `knownSigner` would say the same from Android 12 on only. The
client declares `<queries><package android:name="io.github.m3rt1n99.fafre" /></queries>`, without which Android 11 and
later hide the status provider from it.

**The start.** Right before it, the client asks the status provider for a token:
`getContentResolver().call(Uri.parse("content://io.github.m3rt1n99.fafre.status"), "play_token", null, null)`, whose
answer has it as the String `token`; an app that is not allowed gets a null answer. Then
`startActivityForResult(new Intent("io.github.m3rt1n99.fafre.action.PLAY").setPackage("io.github.m3rt1n99.fafre").putExtra("io.github.m3rt1n99.fafre.extra.TOKEN", token), ...)`,
or the Activity Result API: a start that waits for a result, as Android names its caller to no other (`CALLER`). That
name is the app the result goes to, which another app that the client started for a result (a file picker, say) could
borrow by forwarding PLAY with extras of its own (`FLAG_ACTIVITY_FORWARD_RESULT`): the provider sees the app that asks
it, so a token says that the start is an allowed app's. One start spends a token, within 60 s of its call; a start
without one, or with one spent or older, gets `CALLER`. `LaunchGameActivity` takes the start, the only way in from
outside, with no UI: it checks the caller and the token, then the extras, starts `GameActivity` (not exported) with
`FLAG_ACTIVITY_FORWARD_RESULT`, so that the game's result goes straight to the client, and ends. Its own refusals end
with the same result.

| Extra `io.github.m3rt1n99.fafre.extra.` | Type | What |
|---|---|---|
| `TOKEN` | String | The status provider's `play_token` for this start, which it spends |
| `GAME_VERSION` | int | The version the game plays, the lobby's or the replay's, whose files must be ready (no other: a lobby would refuse it or desync); without it the newest ready one |
| `LAUNCHER_ARGUMENTS` | String[] | The launcher's switches, by the whitelist below; never split, a value is one element |
| `FEATURED_MOD` | String | `faf`, also when absent; any other gives `NO_GAME_FILES` for now |
| `REJOIN_OFFER` | boolean | The game offers its return after a crash: this side adds `/rejoinoffer /rejoinkeyfile <noBackupFilesDir>/faf_rejoin_seat.key /con net_PeerFeatures=1;net_RejoinAccept=1` |
| `REJOIN_PEERS` | String `^\d{1,10}=\d{1,3}(,\d{1,10}=\d{1,3}){0,15}$` | With `REJOIN_SOURCE` the start takes the seat back: this side adds `/rejoin <peers> <source> 0`. Needs `REJOIN_OFFER` on the same start, and the other of the two |
| `REJOIN_SOURCE` | int, 0 to 255 | The `RejoinOffer`'s first field |
| `MAP` | String `^[a-z0-9_.-]{1,64}$`, not `.` or `..` | FAF's map folder name: unless the SCFA import or the game's vault has it, it comes from `content.faforever.com/maps/<MAP>.zip` into `game/vault/maps` before the program starts |
| `MODS` | String[] of uids, `^[0-9A-Za-z_.-]{8,64}$`, not first a `.`, each once | The mods the game needs in `game/vault/mods`: a folder there whose `mod_info.lua` has the uid, in any case, or else its file |
| `MOD_FILES` | String[], one for each of `MODS` in its order | That mod version's file in `content.faforever.com/mods/`, the last segment of FAF's `downloadUrl`: `<name>.zip`, no folder. It must hold the uid; it takes the place of a folder of its name |
| the intent's data (REPLAY) | `content://` URI, with `FLAG_GRANT_READ_URI_PERMISSION` on the start | A replay in SCFA's format, copied into the app's cache before the program, which plays it (`/replay <copy>`); `GAME_VERSION` names its version, `MAP` its map where the phone may not have it. Android lets a start with the grant through only when its app may read the URI itself, and the game reads nothing for a start without it, with access of its own |

Another extra of the prefix, and an extra of another type, give `ARGUMENTS`. Map and mods come as the replay test's do,
one request a second, a download tried again for about ten minutes when the network drops; the game's activity shows
the step and Back cancels it, also while a zip unpacks. A zip unpacks beside its download, out of the vault, which its
folder joins whole, and an older folder of its name (a mod's older version) leaves whole, to be deleted beside the
download: a process that ends meanwhile leaves no half of a map or mod there. `MODS` names uids, as FAF's lobby does, and
FAF's content server has no file by uid, nor its API without a login: so the client, which has both, names the files.

The whitelist of `LAUNCHER_ARGUMENTS`: each switch spelled so, once, followed by its value. Anything else gives
`ARGUMENTS`: any other switch, among them `/init`, `/log`, `/prefs`, `/fullscreen`, `/nomovie`, `/notelemetry`,
`/nobugreport`, `/con`, `/rejoinoffer`, `/rejoinkeyfile` and `/rejoin`, a switch in another case (the engine would find
it), a value where a switch goes, a file's path, a host off the loopback, a switch twice, and a missing or bad value.

| Switch | Value |
|---|---|
| `/gpgnet` | `127.0.0.1:<port>` or `localhost:<port>`, port 1 to 65535 |
| `/savereplay` | `gpgnet://127.0.0.1:<port>/<game id>/<name>.SCFAreplay`, or the same with `localhost`, as `/gpgnet` takes either: the id 1 to 10 digits, the name `[A-Za-z0-9_.-]{1,64}` |
| `/replay` | The same, a live replay through the client on the loopback |
| `/nativenet`, `/nativenetrelay` | none |
| `/mean`, `/deviation` | A decimal number, `-?\d{1,10}(\.\d{1,20})?` |
| `/numgames`, `/team`, `/players`, `/startspot` | A whole number from 0 to 9999 |
| `/country` | `[A-Z]{2}` |
| `/clan` | `[A-Za-z0-9_-]{1,8}` |
| `/division` | `bronze`, `silver`, `gold`, `diamond`, `master`, `grandmaster` or `unlisted`: the player's league division in a matchmaker game, the league service's name, as FAF's Java client passes it (`unlisted` for none), for FAF's autolobby and its `textures/divisions` |
| `/subdivision` | `I`, `II`, `III`, `IV` or `V`: the division's step; `grandmaster` and `unlisted` have none, and the Java client leaves the switch out |

A start plays one replay, `REPLAY` or `/replay`, and a rejoin none (`ARGUMENTS`). The switches go after the activity's
own (`/init`, `/log`, `/notelemetry`, the HUD's two, `/nomovie` for an import without movies), this side's after the
launcher's, and `/fullscreen <the picture's size>` last.

**The result.** `RESULT_FIRST_USER` when the start ran no program, with `failure`, its code, and `message`, the reason
in English that names none of the test app's cards. `RESULT_CANCELED` when the program ran and ended: its end in front,
which ends its process, or any other end of the process there, which takes the activity with it; `last_game` says how.

| `failure` | Why |
|---|---|
| `CALLER` | The caller is not allowed, did not wait for a result, or gave no token of the game's (none, spent, or older than 60 s) |
| `ARGUMENTS` | An extra or a switch is not what the contract says |
| `NO_GAME_FILES` | No ready FAF game files, none of `GAME_VERSION`, or a featured mod other than `faf` |
| `NO_SCFA`, `NO_FONTS` | No SCFA imported, or the import has no fonts |
| `INIT_FILE`, `ENVIRONMENT`, `STORAGE` | The game's init file, its environment or the app's shared storage failed |
| `RUNNING` | A game runs already, which this start's version and arguments would not reach: the running game goes on, and its report gets the line but no end |
| `PROCESS_ENDED` | Android brought back (Recents) the activity of a launcher's game whose process ended in the back (ended by Android, by a crash or by the program's end): it would start the game anew on a GPGNet session the launcher saw end; the gone game's report, `game-previous.txt`, says how |
| `NO_MAP` | FAF's map vault has no `MAP`, or its file holds no map of that name |
| `NO_MOD` | A mod of `MODS` is not on the phone, and `MOD_FILES` names none of it, FAF has no such file, or it holds another uid |
| `DOWNLOAD` | A download of the map or a mod failed: the network, after its tries, or the phone's storage |
| `REPLAY` | The data is no `content://` URI of another app's, the start does not grant reading it (`FLAG_GRANT_READ_URI_PERMISSION`), or it could not be copied (empty, more than 256 MB) |
| `CANCELLED` | Back ended the start while it fetched, unpacked or copied what the game needs |

The game connects to the `/gpgnet` port; when nothing listens there, or the launcher closes the connection before its
`CreateLobby`, the game says so in a box and ends after its OK (`RESULT_CANCELED`), unless it was started with `/rejoin`
or has joined a game server (`/connectserver`, the launcher's `JoinServer`): those go on without the launcher, with no
box, and their results reach nobody ([native README](native/README.md), The network). A `/rejoin` start whose seat the
device holds no key for is refused in a box of the rejoin's own and goes on to FAF's menu without GPGNet. A GPGNet
argument may be as long as the launcher needs: the game reads an ICE server list or a peer's ICE message of 2 KB or
more whole (checked up to 64 KB), where upstream's engine ended the connection.

**The status.** `content://io.github.m3rt1n99.fafre.status/game_data` and `/last_game` (`StatusProvider`), read-only and
for allowed callers only: a refused one gets the table's columns and no row; another table gives a null cursor. All
columns come, whatever the projection. No change is notified: Android lets any app that sees the provider observe its
URIs, whatever a query answers it, and a notification would tell it when the player plays. The launcher queries when it
needs to know: before a start, after its result, when it comes back to the front, and every 2 s or so while
`job_running` is 1; the program's end reaches it as its result, after which `last_game` has it. No column has a player's
name or a path.

| `game_data` (one row) | |
|---|---|
| `scfa_archives`, `scfa_fonts`, `scfa_sound_banks`, `scfa_movies` | int: the SCFA import's files |
| `game_versions` | text: the ready versions, `"3841,3845"`, `""` for none |
| `newest_known` | int, or null before a check: FAF's newest version as the app's last check found it |
| `job_running`, `job_step` | 1 or 0, and the step's text or null: the app's job (an import, a download, the tests) |
| `game_running` | 1 or 0: the game's process runs (`ProcessExit.running(":game")`) |
| `version_code`, `version_name`, `engine_commit` | The app's version and the commit it was built from |

| `last_game` (one row, none before the first game) | |
|---|---|
| `started_at` | ms since 1970; null for a game of an earlier release |
| `ended` | 1 or 0 |
| `exit_code` | int, or null: the program's own, when it returned or the engine ended its process |
| `crashed` | 1 when the game crashed or stopped on a fatal error |
| `end` | How it ended, in the app's words alone (`the program returned 0 after 61.2 s`, `CRASHED: native crash, status 11`, `the start failed: NO_MAP`), null until it has: the report's END line without the engine's log, a fatal error's message, Android's description or a failed start's message |

**The logs.** For the client's problem report:
`getContentResolver().call(Uri.parse("content://io.github.m3rt1n99.fafre.status"), "game_logs", null, null)`, off the main
thread, as the provider writes the zip before it answers (on the emulator 0.1 s for its games' logs of 30 to 60 KB, 8 s for
an engine log of 10 MB), and for allowed callers only (`Callers`, as `play_token`): a refused one gets a null answer. The
answer has the zip's `content://` URI as the Parcelable `uri`,
`content://io.github.m3rt1n99.fafre.results/game_logs/faf-re-game-logs-<time>.zip` (`application/zip`, its name and size by
`OpenableColumns`), which the provider has granted the caller to read (`grantUriPermission`,
`FLAG_GRANT_READ_URI_PERMISSION`) and the client may hand on with that flag, to an app it shares the zip with; or, when the
zip could not be written, `message`, why, and no `uri`. No other app can open it. The zip lies in this app's cache until the
next call, which deletes it and takes its grants back: the client reads or copies it at once.

| In the zip | |
|---|---|
| `report.txt` | The app's version and version code, the engine's commit, Android's version and API level, the device's maker and model, and the stand-ins below; no account |
| `game.txt`, `game-previous.txt` | The last game's report, with its end once the game's process is gone, and the one before |
| `game-tombstone.txt`, `game-previous-tombstone.txt` | A native crash's tombstone of either game as text (`Tombstone`): the signal and its fault address, the cause, the abort message, each thread's stack (library and offset, function) and the libraries' build ids, for `tools/tombstone.py`'s way of symbolizing a build's frames; not the tombstone's memory, registers, open files, log and command line |
| `game/engine.sclog` | The engine's log of the last game |
| `game/faf_crash_latest.txt` | The engine's newest crash report, which can be an earlier game's (its `time` says when) |

The zip goes into public issues, so its texts are scrubbed (`LogScrubber`) as one zip's: what names a player, their account
or the network becomes a stand-in, the same one throughout the zip.

- **Where these are**, on the emulator's games and by the engine's and FAF's sources: the command line in `game.txt` has the
  app's paths and a launcher's switches, the player's name and game in `/savereplay gpgnet://.../<game id>/<name>.SCFAreplay`
  and the account in `/mean`, `/deviation`, `/numgames`, `/clan`, `/country`, `/division` and `/subdivision`; a replay that
  could not be copied can quote the launcher's `content://` URI. The engine's log has the app's folders (the engine writes
  them with backslashes in lower case too), the profile's folder of the game's replays, each army's nickname as the sim
  begins, the lobby's peers with name, address and uid (the engine's `LOBBY:` lines; FAF's lobby and autolobby:
  `CreateLobby`, `JoinGame`, `ConnectToPeer` and more), FAF's `REPLAY ID`, each message to the launcher (`GpgNetSend`, which
  FAF's Lua logs: players' ids, a team kill's names, chat, the stats' names), the LAN games the screen found (host, title,
  address), a peer's ICE message (ids, credentials, the candidates' addresses) and chat that the lobby could not show. The
  crash report has the app's install folder and Lua's strings, a tombstone's summary its abort message.
- **Players' names** are learned from those lines, in all of the zip's files, and replaced wherever they stand, in any case,
  as a word of their own: `player1`, `player2`, ... in the order the files name them, so that the player is mostly `player1`.
  AIs (`<name> (AI: <type>)`), the civilians' armies (`Civilian`, `civilian`) and names without a letter stay.
- **Players' ids** (the lobby's uid, in a game of FAF's the player's id there) where those lines give them: `id1`, `id2`, ...;
  **a FAF game's id**: `<game>`; **a game's title**: `<title>`; **chat**: `<chat>`; **the account's values**: `<account>`;
  **ICE credentials**: `<secret>`.
- **Addresses**: every IPv4 and IPv6 address of a host, private or public, is `<ip>`; the loopback, `0.0.0.0`, `::`, the
  broadcast and multicast addresses stay, as does what is no address (`1.3.275.0`, a time, a MAC). A version of four
  numbers up to 255 looks like an address and goes too.
- **Paths**: the app's external folder (`/storage/emulated/0/Android/data/io.github.m3rt1n99.fafre` and its other names,
  `/sdcard/Android/data/...`) is `<external>`, its internal one (`/data/user/0/io.github.m3rt1n99.fafre`) `<internal>`, its
  install folder (`/data/app/.../io.github.m3rt1n99.fafre-...`) `<apk>`; any other path of the shared storage, the user's
  own, is `<storage>`, and a `content://` URI keeps its authority alone. Android's own paths stay.

What no line of that list names stays: a name that only a mod's own log line gives, or only a crash report of an earlier
game. The JVM tests (`LogScrubberTest`, `TombstoneTest`, `GameLogsTest`) pin the lines' shapes, with invented names and
documentation addresses.

**The setup.** `startActivity(new Intent("io.github.m3rt1n99.fafre.action.SETUP_GAME_DATA").setPackage("io.github.m3rt1n99.fafre").putExtra("io.github.m3rt1n99.fafre.extra.STEP", "scfa"))`,
or the same for a result: `SetupActivity`, this app's own screen for the game's data, in the look of its cards. Any app
may open it, without the allowlist: it shows only this app's UI and takes nothing of its caller's but the step, no file
and no URI (the intent's data is not read), and no job starts without the player's tap. The player goes back with Back.

| `STEP` (String) | The screen |
|---|---|
| `scfa` | The SCFA import: *Folder…*, the system's folder picker (`ACTION_OPEN_DOCUMENT_TREE`), as the Game data card's, or *ZIP file…*, the file picker (`ACTION_OPEN_DOCUMENT`, `application/zip` and `application/x-zip-compressed`) |
| `game_files` | FAF's newest game files, *Get FAF's newest version* as on the Game card, with its question before a download on a metered network; no other version (a replay's) |
| none | Both |

Another value or type of `STEP`, or another extra of the prefix, ends the screen at once with `RESULT_FIRST_USER`,
`failure` `ARGUMENTS` and its `message`. Else the result is `RESULT_CANCELED`, whatever the player did there: the caller
reads what came of it in `game_data` (`scfa_archives`, `scfa_fonts`, `game_versions`), as it does before a start. An
import or a download goes on after Back in the app's service, one job at a time (`job_running`, `job_step`, which the
caller polls), and the screen's Cancel ends it; an import gives back the grant on its folder or ZIP when it ends. Started
from the caller's activity the screen opens in its task, and Back returns there; with `FLAG_ACTIVITY_NEW_TASK` it opens in
a task of its own, which a later start brings back with the new start's step.

The ZIP's import (`ScfaZip`), which the test app's card 2 offers too (*Import SCFA ZIP…*, for testers without the
client), takes what the folder import takes, by its rules (`ScfaImport`):

- **What it takes**: `gamedata/*.scd`, and `fonts/`, `sounds/`, `movies/` and `maps/` whole, all in any case, of the SCFA
  folder, which is the ZIP's top or one folder there (as Windows' *Send to, Compressed folder* makes it): where its
  `gamedata` folder is. Anything else in the ZIP is left. A ZIP with no `gamedata` folder there, or with one in two
  places, is refused. A file there already with the same size is not written again; a file the ZIP holds twice, also in
  another case, counts once, as its later entry.
- **The ZIP** is read as a stream from its content URI with `ZipInputStream`, twice: first its list, for the place of the
  SCFA folder and the files' sizes, which a stream's local headers need not hold, then the files. ZIP64 (over 4 GB),
  stored or deflated, and names with `\`, as Windows PowerShell 5.1's `Compress-Archive` writes them. Its first bytes make
  it a ZIP, an entry's local header, whatever its name or type say; an empty ZIP, a name not in UTF-8, more than 100,000
  entries, and a damaged or cut-off ZIP are refused, each before anything is written.
- **Zip Slip**: an entry whose name is absolute or has a `..`, with `/` or `\`, refuses the whole ZIP, as Android 14's
  `ZipInputStream` does with `/` alone (`ZipPathValidator`), and every file the import writes is resolved under `scfa/`.
- **Room**: before it writes anything, the files it writes must fit in the free space with 100 MB to spare, as FAF's game
  files' must, or the log says how much is free and how much it needs; as it writes it checks again at each file and
  every 64 MB, and stops when what it still has to write no longer fits.
- **All or nothing**: the files go to `scfa/.zip-import/`, and from there to their places once the whole ZIP has been
  read. A refusal, a Cancel or a failure leaves `scfa/` as it was, and what an import whose process ended left there goes
  as the next import begins, a ZIP's or the folder's, before its room is checked. The progress shows as the folder
  import's: the step (*Checking the ZIP*, *Skipping*, *Importing* a file's name, no path) and the log's lines.

The JVM tests (`ScfaZipTest`) make the ZIPs themselves, a deflated one with a skipped entry of more than 4 GB among them.
The stored one is 4 GB on the disk, and runs where `FAFRE_ZIP64_DIR` names a folder for it:
`FAFRE_ZIP64_DIR=<a folder with 4.1 GB free> ./gradlew :app:testReleaseUnitTest --tests '*ScfaZipTest' --rerun`.

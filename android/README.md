# Android test app

An Android app that runs the engine's simulation on a phone, without graphics: the sim-math tests
and FAF vault replays played to the end on the headless simulation (`src/headless`), with every
checksum the players' games recorded compared against the phone's. It is how a phone's CPU is
checked before the game itself runs there. A renderer test brings up the engine's Vulkan device on
the screen, and the game card starts the whole engine, the game client's first steps
([CLIENT_PLAN.md](CLIENT_PLAN.md)). The engine's
programs and libraries come from the Android native build ([native/README.md](native/README.md)),
which builds the engine for Android without changing its files and the client on top.

## Building

Needs the Android SDK (platform 35, build-tools 35), the NDK release in
`cmake/toolchains/android-pins.cmake` (`sdkmanager "ndk;29.0.14206865"`), JDK 17, CMake 3.25+, Ninja
and Python 3. `ANDROID_HOME` names the SDK.

```sh
# The engine (dependencies once, then the native build's preset):
cmake -DFAF_DEPENDENCIES_ONLY="WildMagic3p8;zlib-1.2.3;boost_1_34_1" -P cmake/FafDependencies.cmake
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
  one place an app may start a program from. It adds the renderer test's `libfafgalsmoke.so` and
  the game's `libfafgame.so`.
  `gradle.properties` names the build folders.
- `stageFafAssets` puts `tests/sim_math/golden.txt` into the assets.
- `versionCode` is `git rev-list --count HEAD`, so a newer commit installs over an older one;
  `versionName` is in `gradle.properties`.
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
| `scfa/` | The user's own *Supreme Commander: Forged Alliance*: `gamedata/*.scd`, `fonts/`, `sounds/`, `movies/` (*Import SCFA folder…*, which copies what an earlier release's import has not), `maps/` if imported |
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
  when the network drops.
- **Renderer test** (card 4, `GalSmokeActivity`): `libfafgalsmoke.so`
  (`native/src/smoke/GalSmoke.cpp`) lists the phone's Vulkan devices, creates the engine's gal
  device (the Vulkan backend, which needs Vulkan 1.3) on a full-screen surface, then clears it in
  changing colours and presents for ten seconds. It needs no game data and draws without shaders or
  textures, which come with C2. The report gives:
  - the screen: the display mode and the app's size, which shows a scaled-down app;
  - each Vulkan device with the features the backend needs;
  - the engine's log lines;
  - the frame rate, or the line that failed.

  The test runs in a process of its own (`:renderer`), and the native
  side writes each report line to `renderer.txt` in the app's files as it comes. If the driver takes
  that process down, the app keeps the lines up to the crash and the reason Android gives for the
  end (`ApplicationExitInfo`). On Android 12 and later it also keeps the tombstone, which goes into
  the zip as `renderer-tombstone.pb`. The emulator's SwiftShader offers only Vulkan 1.2, so the test
  fails there. Started with `-gpu host` on a PC whose GPU has Vulkan 1.3, the emulator gives the
  guest Vulkan 1.3, and the test runs.
- **Game** (card 5, `GameActivity`): `libfafgame.so`, the whole engine with its host
  (`native/src/host`), starts as the PC starts it, with the SCFA import and FAF's game files of the
  newest version on the phone (the highest whole one): `ForgedAlliance.exe /init <its init file> /log
  game/engine.sclog`, the program's path in the game files' `bin` folder. It runs in a process of
  its own (`:game`) on a full-screen surface, which ends with the program, as a Windows program's
  does. It draws the menus and plays a skirmish, with the game's sound (FACT on AAudio) and its
  movies (FFmpeg; `/nomovie` only when the import has none; a tap skips the logos, another the film). A
  mouse is the mouse, keys are the keyboard and Back is Esc. Fingers act as a mouse (`TouchInput`):
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
  game from, with what it typed shown over the picture, as it covers the game's text field; a tap on a text field opens it. In the menus, and over a
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
    right click does: move, attack an enemy, assist a factory with engineers.
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
  - The bar at the left edge has *Box* (a drag is a box at once, a tap on a unit adds it or takes it out, a tap on nothing
    leaves *Box* lit), *Plus* (orders are queued, as with Shift) and *Attack* (taps and routes are FAF's attack-move, where
    FAF's orders panel offers it), beside *Keys*. A tap lights one for the next action; held, it counts until it is let go.
    An order picked in the orders panel ends after the one that a lit *Plus* queues; a held *Plus* keeps it until it is let
    go, as Shift does. With *Plus* or *Attack* and troops selected, a tap on one of your units is the order there too.
  - On FAF's panels a tap is a click and a rest a right click (on an avatar: go to it), with the same help beside small
    controls as with the mouse.
  - A build icon of the construction panel opens a draft instead of FAF's ghost: a tap on the map places a plan of that
    building, on FAF's grid (an extractor on the free mass spot within 5 mm, else "No free resource here"), a tap on a plan
    takes it away, a drag from the newest plan draws a line of them side by side, a drag from another plan moves them all
    (extractors stay on their spots), and a drag elsewhere pans. A finger that rests shows the plan its lift places, with a
    loupe above it of what the finger covers. With an extractor picked, *Box* and a drag give every free mass spot in the
    box a plan. Another icon picks another building for the next plans; plans on taken ground are red. The tick orders them
    all with FAF's build mode, as many clicks (the first replaces what the engineers do, the rest are queued; with *Plus* all
    are queued), moving the camera to a plan the screen does not show and back; the cross, or a tap of two fingers, drops the
    draft. The selected engineers build it, or the ones selected when the draft began. Up to 30 plans; while plans wait,
    an order of the orders panel says "First ✓ or ✕". FAF's build keys and templates keep FAF's ghost, which is placed as
    with the mouse.
  - After an order a pill *Undo* stands for 3 s over the top of FAF's unit view, under the rail, with a bar of the time it
    has left; a tap on it takes the last gesture's orders back with FAF's own inputs: units that were idle stopped by the
    orders panel's Stop, the others' queues before given again by FAF's order modes and clicks, the first replacing, which
    takes an appended order with it (a building by its name, one begun by its repair; an order on a unit where that unit
    is now). A route, a tick of the draft and a ground double tap are one undo each; only the last order counts. While
    its clicks run the pill says *Undo…*; what cannot come back (an enhancement, an order on a unit that is gone) makes
    the screen say *Undone in part*. Each order leaves pins at its places for 1.5 s, in the colour of its kind (move,
    attack, patrol, build), beside FAF's own blips.
  - Not there yet: control groups (Griff has no Ctrl to make one).
- **FAF's newest version** (card 5, *Get FAF's newest version*; `FafRelease`, `NewestGameFiles`, TestService's
  `ACTION_GAME_FILES`): so that the phone can play a PC on FAF's current version. The number is GitHub's latest release
  of FAForever/fa (`api.github.com/repos/FAForever/fa/releases/latest`, no key: 60 requests an hour from one address,
  which phones on mobile data may share), taken when FAF's updater has its `init_faf_<v>.lua`; a release that is tagged
  but not deployed yet gives way to the next lower numeric tag that is (`/tags`, at most five tried). The size is the
  updater's Content-Length of the packages still to get, less what a cut-off download left (3840: 682 MB). Then
  `GameFiles.ensure` downloads them as a replay's run does, with the Progress card's steps, cancel and log. On a metered
  network (mobile data, a hotspot, a Wi-Fi set so) the card asks first; without that yes the download runs only on a
  network that is not metered and stops when it becomes metered, and the next run goes on from where it stopped. The
  card's line shows both versions, `FAF game files: 3835 (newest at FAF: 3840)`; if GitHub is unreachable or at its
  limit, the line says so and nothing is downloaded. Older versions stay for the replay test. The UI package's hooks are
  appended to FAF's files of whatever version, each in a pcall, so a change in FAF's files shows as a `fafre_ui:`
  warning in `engine.sclog` rather than a broken game. The check against the real servers is a JVM test outside the
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
  - On the phone: *Multiplayer LAN*, the PC's address and 16010 in the IP and port fields (*Keys* opens the keyboard),
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
    - The source is the transport (`Game.networkInUse`): a UDP connector's socket. A skirmish has none (`LOBBY: Game
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
- **Share results** zips `summary.json`, the math output, the log, the renderer report
  (`renderer.txt`, `renderer-tombstone.pb`), the game's report, tombstone, log and crash report
  (`game.txt`, `game-tombstone.pb`, `game/engine.sclog`, `game/faf_crash_latest.txt`), the last
  game's (`game-previous.txt`, `game-previous-tombstone.pb`) and each replay's `headless.out`,
  `engine.log` and `speed.csv`. `tools/tombstone.py` reads a tombstone, with its stacks symbolized.

The default replays (26930624 and 26902315), *Use the core set* and *Use the extended set* (the core
set and its ten longer games) are from [docs/PR_REPLAY_CHECK.md](../docs/PR_REPLAY_CHECK.md), all
recorded on FAF game version 3835. A test runs at full speed only with the app in front: Android
moves a background app, and the simulation process it started, to another CPU group, on a Galaxy
S22 Ultra the little cores.

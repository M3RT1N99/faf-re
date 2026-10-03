# Game data the engine needs

Which files the game reads when the FAF client starts it, where each one comes from, and where it
goes on Android. Nothing listed here ships with the app or the repository: the SCFA files come from
the user's own copy of *Supreme Commander: Forged Alliance* (Steam, GOG or retail), the FAF files
from FAF.

The machine-readable version is [`port/data/gamedata.json`](../../port/data/gamedata.json) (the
"manifest"). The PC deploy script, the launcher's in-app import and the native runtime all read it,
so a change there changes all three. This page explains it. The size columns were measured on a
Steam install (German) and FAF game version 3839 with
`python scripts/port/gamedata_report.py`, which prints these tables from the manifest and a local
install. Sizes use binary units (1 MB = 1,048,576 bytes), as the deploy script prints them.

## How the engine finds its data

At startup the engine runs a *data-path script* (`moho::DISK_SetupDataAndSearchPaths`,
`src/sdk/moho/misc/StartupHelpers.cpp`):

1. The script is the value of `/init <file>`, or `SupComDataPath.lua` when there is no `/init`. A
   relative name is looked up in the launch directory, the directory of the executable.
2. Before it runs, the engine sets two globals: `LaunchDir` (the launch directory) and
   `InitFileDir` (the directory holding the script), and registers the bindings the script calls,
   such as `io.dir` and `SHGetFolderPath`.
3. The script fills three tables: `path` (the mount list, `{ dir = ..., mountpoint = ... }` entries),
   `hook` (`{'/schook'}`) and `protocols`.
4. The engine mounts every `path` entry in order into its virtual file system. A lookup walks the
   mounts in that order and the **first mount that holds a file wins**. A `dir` is either a folder
   or a zip archive (`.scd` files are stored zips, `.nx2` files deflated zips).

The FAF client starts `ForgedAlliance.exe` from `C:\ProgramData\FAForever\bin` with
`/init init_faf.lua`. Before that it writes `C:\ProgramData\FAForever\fa_path.lua`, which
`init_faf.lua` loads with `dofile(InitFileDir .. '/../fa_path.lua')`:

```lua
fa_path = "C:/Program Files (x86)/Steam/steamapps/common/supreme commander forged alliance"
custom_vault_path = "C:/ProgramData/FAForever/user/My Games/Gas Powered Games/Supreme Commander Forged Alliance"
GameType = "faf"
GameVersion = "3839"
ClientVersion = "..."
ForceAffinity = false
```

`fa_path` is the SCFA install, `custom_vault_path` the FAF vault (maps and mods downloaded by the
client).

### Mount order of `init_faf.lua` (FAF 3839)

| # | Mounted | From | Mount point |
| --- | --- | --- | --- |
| 1 | each vault map folder, plus its `movies/` and `sounds/` (unless they clash with stock banks) | `custom_vault_path/maps/<map>` | `/maps/<map>`, `/movies`, `/sounds` |
| 2 | each vault mod folder, plus its `sounds/` and `custom-strategic-icons` | `custom_vault_path/mods/<mod>` | `/mods/<mod>`, `/sounds`, `/textures/ui/common/game/strategicicons/<mod>` |
| 3 | FAF archives on the allow list: effects, env, etc, loc, lua, meshes, mods, projectiles, textures, units (`.nx2`) | `InitFileDir/../gamedata/*.nx2` | `/` |
| 4 | SCFA archives on the allow list: effects, env, loc\_\*, meshes, mods, objects, projectiles, props, sc\_music, skins, textures, units (`.scd`) | `fa_path/gamedata/*.scd` | `/` |
| 5 | preferences, shader cache | `SHGetFolderPath('LOCAL_APPDATA')/Gas Powered Games/Supreme Commander Forged Alliance` | `/preferences` |
| 6 | stock movies, sounds, maps, fonts | `fa_path/movies`, `/sounds`, `/maps`, `/fonts` | `/movies`, `/sounds`, `/maps`, `/fonts` |

Archives inside one folder are mounted in the order `io.dir` returns them, which on NTFS is sorted
by upper-cased name; the Android runtime reproduces that order (`faf::port::fs::FindFiles`).
Because FAF's `.nx2` archives come before SCFA's `.scd` archives, FAF's Lua, localisation, shaders
and textures replace the originals; the `.scd` archives only supply what FAF does not override.

Two details the port has to keep: `init_faf.lua` empties the shader cache below `LOCAL_APPDATA` with
`os.remove` on every start, so the data root must be writable there; and the script resolves
`SHGetFolderPath('PERSONAL')` (Documents) only for an old-client fallback that is unused when
`custom_vault_path` is set.

## Android data root

`<root>` is `Context.getExternalFilesDir(null)`, normally
`/sdcard/Android/data/io.github.m3rt1n99.fafre/files`. The native runtime gets it from the activity
(`ANativeActivity::externalDataPath`); nothing hard-codes it.

```
<root>/scfa/...                   SCFA files, original names and case: gamedata/*.scd, fonts/, sounds/, movies/, maps/, bin/splash.png
<root>/faf/bin/init_faf.lua       FAF data-path script, unmodified
<root>/faf/bin/SupComDataPath.lua (optional)
<root>/faf/gamedata/*.nx2         FAF archives, kept as zips
<root>/faf/fa_path.lua            generated before every start (see below)
<root>/faf/version.json           {"featuredMod":"faf","version":3839,"source":"download|pc-deploy|import"}
<root>/vault/maps, vault/mods     custom_vault_path
<root>/localappdata/              SHGetFolderPath('LOCAL_APPDATA'); preferences land in localappdata/Gas Powered Games/Supreme Commander Forged Alliance/
<root>/documents/                 SHGetFolderPath('PERSONAL')
<root>/logs/                      faf_android.log, launcher.log, game.sclog
<root>/launch/status.json         written by the native runtime, read by the launcher
<root>/.deploy/deployed.json      what the deploy script or the importer placed: {"files":[{"dest","size"}...]}
```

`faf/fa_path.lua` keeps `init_faf.lua` working unmodified. The launcher writes it before every
start (the deploy script writes it too), from the manifest's `faPathLua.template`:

```lua
fa_path = "<root>/scfa"
custom_vault_path = "<root>/vault"
GameType = "faf"
GameVersion = "3839"
ClientVersion = "faf-re-android 0.3.0"
ForceAffinity = false
```

Paths use `/`; backslashes and quotes in a value are escaped for the Lua string.

## FAF files (game version 3839)

`init_faf.lua` and the `.nx2` archives of the `faf` featured mod. Source: the FAF client's install
(`C:\ProgramData\FAForever\bin`, `...\gamedata`) or FAF's content server,
`https://content.faforever.com/faf/updaterNew/updates_faf_files/<download name>`. Every copy is
checked against the manifest's size and sha256, so a different FAF version is refused rather than
mixed in. All `.nx2` archives mount at `/` (they contain `lua/`, `loc/`, `textures/`, ...).

| File | Destination | Tier | Size | Download name | Purpose |
| --- | --- | --- | ---: | --- | --- |
| `init_faf.lua` | `faf/bin/init_faf.lua` | required | 26.3 KB | `init_faf_3839.lua` | Data-path script: builds the mount table (path/hook/protocols). |
| `SupComDataPath.lua` | `faf/bin/SupComDataPath.lua` | optional | 215 B | `SupComDataPath_3839.lua` | Default script when no /init is given. Uses a backslash path; the Android launcher always passes /init. |
| `lua.nx2` | `faf/gamedata/lua.nx2` | required | 3.3 MB | `lua.3839.nx2` | All FAF Lua (/lua). |
| `loc.nx2` | `faf/gamedata/loc.nx2` | required | 1.7 MB | `loc.3839.nx2` | Localisation strings (/loc). |
| `etc.nx2` | `faf/gamedata/etc.nx2` | required | 3.7 KB | `etc.3839.nx2` | Mod blacklist (/etc). |
| `effects.nx2` | `faf/gamedata/effects.nx2` | required | 3.0 MB | `effects.3839.nx2` | Shaders (/effects/*.fx); the engine dies without them. |
| `textures.nx2` | `faf/gamedata/textures.nx2` | required | 87.2 MB | `textures.3839.nx2` | FAF UI and texture overrides (/textures). |
| `env.nx2` | `faf/gamedata/env.nx2` | recommended | 478.4 MB | `env.3839.nx2` | Terrain, skies, decals, water (/env). |
| `units.nx2` | `faf/gamedata/units.nx2` | recommended | 104.5 MB | `units.3839.nx2` | Unit blueprints, scripts, textures (/units). |
| `meshes.nx2` | `faf/gamedata/meshes.nx2` | recommended | 3.0 MB | `meshes.3839.nx2` | Mesh overrides (/meshes). |
| `projectiles.nx2` | `faf/gamedata/projectiles.nx2` | recommended | 901.7 KB | `projectiles.3839.nx2` | Projectile blueprints and scripts (/projectiles). |
| `schook.nx2` | `faf/gamedata/schook.nx2` | optional | 2.9 KB | `schook.3839.nx2` | Not mounted by init_faf.lua 3839 (commented out); kept for completeness. |

## SCFA selection

Each entry selects names directly inside one folder of the SCFA install:

- `src` is matched component by component, ignoring case (`sounds/Voice/US` finds `sounds\voice\us`).
- A name is taken when it matches one of the include patterns and none of the exclude patterns.
  Patterns compare case-insensitively and know `*` and `?` only, against a single name.
- `kind = file` takes regular files, `kind = dir` takes folders and copies them recursively.
- The copy goes to `<root>/scfa/<src>/<name>` with the source's spelling. The runtime looks files up
  case-insensitively, so the case on the device does not matter to it.
- Names without a wildcard (`textures.scd`) must exist; a missing one in a required entry stops the
  deploy, in other tiers it is a warning.
- A name containing `..`, `/`, `\` or NUL is skipped with a warning, also inside a copied folder.
  Hidden files count like any other file. The deploy script and the launcher's importer apply the
  same rules, so both place the same files.

| Entry | Source folder | Names | Kind | Tier | Mount | Files | Size | Purpose |
| --- | --- | --- | --- | --- | --- | ---: | ---: | --- |
| `scfa-textures` | `gamedata/` | `textures.scd` | file | required | `/` | 1 | 505.5 MB | UI and world textures FAF does not override (main menu backgrounds live here). |
| `scfa-fonts` | `fonts/` | `*.ttf` | file | required | `/fonts` | 14 | 1.9 MB | All UI fonts. |
| `scfa-gamedata-play` | `gamedata/` | `env.scd`, `units.scd`, `meshes.scd`, `projectiles.scd`, `effects.scd`, `props.scd`, `objects.scd` | file | recommended | `/` | 7 | 2.37 GB | Base assets underneath the FAF overrides. |
| `scfa-sounds-core` | `sounds/` | `*.xgs`, `*.xwb`, `*.xsb` except `Music.*`, `FMV_BG.*`, `Op_Briefing.*` | file | recommended | `/sounds` | 153 | 304.0 MB | XACT global settings and the core sound banks. |
| `scfa-voice-us-xgg` | `sounds/Voice/US/` | `XGG.*` | file | recommended | `/sounds` | 2 | 71.5 MB | Computer/HQ voice lines. |
| `scfa-maps-skirmish` | `maps/` | `SCMP_*`, `X1MP_*` | dir | recommended | `/maps` | 54 dirs, 306 | 1.03 GB | The 54 stock skirmish maps. |
| `scfa-gamedata-extra` | `gamedata/` | `mods.scd`, `skins.scd`, `sc_music.scd` | file | optional | `/` | 2 | 1.2 MB (no sc_music.scd in this install) | Mounted when present; not needed for play. |
| `scfa-music` | `sounds/` | `Music.*` | file | optional | `/sounds` | 2 | 239.2 MB | Streaming music bank. |
| `scfa-sounds-fmv` | `sounds/` | `FMV_BG.*`, `Op_Briefing.*` | file | optional | `/sounds` | 4 | 207.7 MB | Background audio for movies and campaign briefings. |
| `scfa-voice-us-campaign` | `sounds/Voice/US/` | `*` except `XGG.*` | file | optional | `/sounds` | 19 | 588.2 MB | Campaign, tutorial and intro voice-over. |
| `scfa-voice-other` | `sounds/Voice/` | `*` except `US` | dir | optional | `/sounds` | 1 dir, 21 | 645.4 MB | Voice sets for other languages (for example DE). |
| `scfa-maps-campaign` | `maps/` | `X1CA_*` | dir | optional | `/maps` | 7 dirs, 93 | 102.9 MB | Forged Alliance campaign and tutorial. |
| `scfa-movies` | `movies/` | `*.sfd` | file | optional | `/movies` | 1004 | 2.31 GB | Menu, loading, intro, credits and campaign movies. Start with /nomovie without them. |
| `scfa-splash` | `bin/` | `splash.png` | file | optional | `-` | 1 | 1.5 MB | Startup splash image read from the launch directory. |

"Mount" is where `init_faf.lua` makes the files visible: the `.scd` archives mount at `/` (step 4
above), the folders through step 6. `splash.png` is not mounted; the engine reads it from the launch
directory.

## Vault (optional)

The FAF client's vault (`custom_vault_path`, by default
`C:\ProgramData\FAForever\user\My Games\Gas Powered Games\Supreme Commander Forged Alliance`) holds
the maps and mods it downloaded. They are never copied unless asked for (`-IncludeVault` or
`-Include vault-maps`), because a long-time player's vault is easily larger than the game: 8.3 GB of
maps and 6.3 GB of mods on the measured machine.

| Entry | Source folder | Names | Kind | Destination | Mount |
| --- | --- | --- | --- | --- | --- |
| `vault-maps` | `maps/` | `*` | dir | `vault/maps/<name>` | `/maps/<name>` (needs `*_scenario.lua`, `*.scmap`, `*_save.lua`, `*_script.lua`) |
| `vault-mods` | `mods/` | `*` | dir | `vault/mods/<name>` | `/mods/<name>` (needs `mod_info.lua`) |

## Tiers

| Tier | Meaning | FAF | SCFA | This tier | What `-Tier` copies |
| --- | --- | ---: | ---: | ---: | ---: |
| required | Reaches the main menu. | 95.2 MB | 507.4 MB | 602.6 MB | 602.6 MB |
| recommended | Normal skirmish play: units, terrain, sounds, the stock skirmish maps. The default. | 586.8 MB | 3.77 GB | 4.34 GB | 4.93 GB |
| optional | Movies, music, campaign content, other voice languages (`-Tier all`). | 3.1 KB | 4.05 GB | 4.05 GB | 8.98 GB |

Single optional entries can be added to a smaller tier with `-Include <entry>` (deploy script) or
the checkboxes of the in-app import.

## Never needed

- `gamedata/lua.scd`, `mohodata.scd`, `moholua.scd`, `schook.scd`, `editor.scd`, `ambience.scd`:
  disabled by `init_faf.lua` (FAF's `lua.nx2` replaces the first four; the last two are unused or
  empty).
- `gamedata/loc_*.scd`: mounted, but every file in them is shadowed by FAF's `loc.nx2`.
- Any other `.scd` a user dropped into `gamedata/` (for example an icon mod): not on the allow list,
  so `init_faf.lua` does not mount it.
- `bin/*.exe`, `*.dll`, `DirectX/`: Windows binaries, not game data.
- From the FAF client directory: everything except the files above. `gamedata/*.nx5` (FAF develop),
  `*.nmd` (Nomads), `*.cop` and `*_VO.nx2` (coop), `*.gw` (Galactic War) and `faforever.faf` belong to
  other featured mods and their own `init_*.lua`; `repo/`, `cache/`, `replays/`, `logs/` and the
  rest are the client's own state.

## The desktop build uses the same files

The recovered desktop engine (`main.exe`, `src/sdk/main.vcxproj`) runs against the FAF client's
install exactly like `ForgedAlliance.exe` does. The engine resolves its launch directory from its
own path (`DISK_GetLaunchDir` takes `argv[0]`), so the build copies `main.exe` into `$(FafRunDir)`,
by default `C:\ProgramData\FAForever\bin\`, next to the client's `SupComDataPath.lua`. Without
`/init`, that script runs `dofile(InitFileDir .. '\\init_faf.lua')`, and `init_faf.lua` reads
`..\fa_path.lua` - the same chain as on Android, with these locations:

| Desktop (FAF client) | Android data root |
| --- | --- |
| `C:\ProgramData\FAForever\bin\init_faf.lua` | `faf/bin/init_faf.lua` |
| `C:\ProgramData\FAForever\bin\SupComDataPath.lua` | `faf/bin/SupComDataPath.lua` (optional; the launcher passes `/init`) |
| `C:\ProgramData\FAForever\gamedata\*.nx2` | `faf/gamedata/*.nx2` |
| `C:\ProgramData\FAForever\fa_path.lua`, written by the FAF client | `faf/fa_path.lua`, written by the launcher and the deploy script |
| `fa_path` = the SCFA install | `scfa/` |
| `custom_vault_path` | `vault/` |
| `%LOCALAPPDATA%\Gas Powered Games\Supreme Commander Forged Alliance` | `localappdata/Gas Powered Games/Supreme Commander Forged Alliance` |
| Documents | `documents/` |

To run the desktop build against another install, override `FafRunDir`
(`msbuild src\sdk\main.vcxproj /p:FafRunDir=D:\SomeGameDir\bin\`). `scripts/port/deploy_android.ps1
-StageDir <dir>` builds the Android layout on the PC; the host tool `faf_datacheck` runs
`init_faf.lua` against it and reports the same mount table the device will see.

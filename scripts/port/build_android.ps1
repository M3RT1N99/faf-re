<#
.SYNOPSIS
Builds the Android APK: native runtime, headless replay runner, Java launcher,
resources and the game data manifest in one signed file.

.DESCRIPTION
Produces output\android\faf-re-android-<versionName>-<abi>.apk.

Steps:
  1. native   CMake + Ninja + NDK build of port\android (target faf_android) in
              buildstage\android-native; the unstripped library is kept in
              buildstage\android-apk\symbols for symbolization, a stripped copy
              goes into the APK.
  2. runner   the headless replay runner (port\engine\runner, milestone M3c):
              scripts\port\build_runner.py --probe --deviceprobe for the APK's
              ABI in buildstage\android-apk\runner, and --opt O2 in
              buildstage\android-apk\runner-O2, or the prebuilt binaries of
              -RunnerDirectory and -RunnerO2Directory. Stripped copies go into
              the APK as lib\<abi>\libfafengine.so, libfafrunner.so (the
              executable faf_headless_runner; Android only extracts lib*.so
              names), libfafarenaprobe.so, the -O2 pair libfafengine_o2.so and
              libfafrunner_o2.so, and the device probe libfafdeviceprobe.so
              (release 0.4.1); ELF checks; the unstripped files are kept as
              symbols.
  3. link     aapt2 compile + link (manifest, resources, version, R.java).
  4. java     javac (Java 8 bytecode against android.jar) + d8.
  5. package  classes.dex and the assets (gamedata.json, build.json, the
              replay reference table port\android\assets\replay_refs.json,
              license notices when present, and the menu trace's description
              galplay\menu.json) added STORED; the menu trace
              assets\galplay\menu.galtrace (release 0.5.0, -MenuTrace) and the
              lib\<abi>\*.so entries DEFLATED, because the manifest sets
              extractNativeLibs=true
              (the launcher execs the runner from nativeLibraryDir, so it must be
              a file on disk); zipalign with 16 KB pages, apksigner with one
              stable key.
  6. verify   apksigner verify, zipalign -c, aapt2 dump badging and xmltree
              (extractNativeLibs), entry compression, ELF checks.

A Release build also stages the APK (arm64-v8a), the unstripped libraries and
build-<abi>.json (sha256 and build ids) in buildstage\releases\android-v<versionName>\
for symbolizing crash reports from that release (-NoReleaseStage skips it).

versionName comes from port\android\version.properties, versionCode is
`git rev-list --count HEAD`.

The signing key is $env:FAF_ANDROID_KEYSTORE or %USERPROFILE%\.android\debug.keystore
(the standard Android debug key, created when missing). Android only installs
an update signed with the key of the installed app, so the key must not live
in a build directory that -Clean or a fresh clone throws away.

Works in Windows PowerShell 5.1 and PowerShell 7.

.PARAMETER Configuration
Release (default) or Debug. Debug builds the native code without optimization
and marks the APK debuggable.

.PARAMETER Abi
arm64-v8a (default; phones and tablets) or x86_64 (the SDK emulator's x86_64
system images run it without ARM translation). Each ABI builds in its own
directories and gives its own APK.

.PARAMETER SkipNative
Java-only APK for launcher work, written as faf-re-android-<versionName>-nonative.apk.
The game activity cannot start from it. Implies -SkipRunner.

.PARAMETER SkipRunner
Leaves the headless replay runner out of the APK (no build_runner.py run, no
dependencies\WildMagic3p8 needed). The launcher's replay test then reports that
the runner is missing.

.PARAMETER RunnerDirectory
Packages the prebuilt, unstripped faf_headless_runner, libfafengine.so and
libfafarenaprobe.so of this directory (for example build_runner.py's output for
the binaries the reference runs were measured with) instead of building them.
They go through the same strip and checks. Its libfafdeviceprobe.so, when
present, is the device probe (see -DeviceProbe).

.PARAMETER RunnerO2Directory
Packages the prebuilt, unstripped -O2 faf_headless_runner and libfafengine.so of
this directory (build_runner.py --opt O2) as libfafrunner_o2.so and
libfafengine_o2.so, the replay test's "optimised engine" experiment. Without it,
a build that builds its own runner builds this pair too; one that packages
-RunnerDirectory leaves it out, which a Release build refuses unless
-SkipOptimizedRunner is given.

.PARAMETER SkipOptimizedRunner
Leaves the -O2 runner and engine out of the APK (the launcher then disables
the option).

.PARAMETER DeviceProbe
Packages this unstripped libfafdeviceprobe.so (build_runner.py --deviceprobe)
as the device probe instead of the one in -RunnerDirectory or the runner build.

.PARAMETER SkipDeviceProbe
Leaves the device probe out of the APK (the launcher then disables its button).
A Release build refuses to go without it otherwise.

.PARAMETER MenuTrace
The galtrace of FAF's main menu the launcher's "Menu replay" plays (release
0.5.0, port/graphics/trace). It must be a format version 2 trace whose game-file
payloads are references (galtrace-refs): the APK carries no game data. Default:
the one *.galtrace in buildstage\traces (the one whose name starts with "menu"
when there are several). Checked and described by an embedded helper
(galtrace_info.py: header, record scan, sha256) into assets/galplay/menu.json:
refused when it is cut off, version 1, without PayloadRef records, or when an
embedded payload looks like a game file (a DDS/PNG/JPEG image or an effect
source). A Release build also refuses a trace without the PC's Diligent-Vulkan
hash of every readback frame (reference_frames.diligent:vk), with a home
directory in its header metadata, or a libfaf_android.so without the menu
replay mode (the Intent extra names of port/android/src/GalPlay.h); a Debug
build only warns about those.

.PARAMETER SkipMenuTrace
Leaves the menu trace out (the launcher then disables "Menu replay"). A Release
build with native code refuses to go without a trace otherwise.

.PARAMETER NoReleaseStage
Does not copy the APK, the unstripped libraries and build-<abi>.json into
buildstage\releases\android-v<versionName>\ (Release builds do by default).

.PARAMETER AllowUnreferencedRunner
Lets a Release build package a runner whose libfafengine.so or
faf_headless_runner build id (of the -O0 or the -O2 pair) is not in
port\android\assets\replay_refs.json (a warning instead of an error). For development builds only: the app compares
a replay's chain with the reference only for the binaries the reference was
measured with, so a release must ship exactly those (regenerate the table with
run_runner_android.py --host-prefs none --write-refs after a runner change).

.PARAMETER Clean
Deletes the build directories of the steps that run before building.

.PARAMETER NativeLibrary
Packages this unstripped libfaf_android.so (for example one built by CI)
instead of building port\android. It goes through the same strip and checks.

.PARAMETER DryRun
Resolves the tools and prints every command without running anything or
writing files.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts/port/build_android.ps1

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts/port/build_android.ps1 -SkipNative -Configuration Debug

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts/port/build_android.ps1 -Abi x86_64 -RunnerDirectory buildstage/runner/x86_64 -SkipOptimizedRunner -Configuration Debug

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts/port/build_android.ps1 -RunnerDirectory buildstage/runner/r041-pkg/arm64-v8a -RunnerO2Directory buildstage/runner/r041-pkg-O2/arm64-v8a
#>
[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [ValidateSet("arm64-v8a", "x86_64")]
    [string]$Abi = "arm64-v8a",
    [switch]$SkipNative,
    [switch]$SkipRunner,
    [string]$RunnerDirectory,
    [string]$RunnerO2Directory,
    [switch]$SkipOptimizedRunner,
    [string]$DeviceProbe,
    [switch]$SkipDeviceProbe,
    [string]$MenuTrace,
    [switch]$SkipMenuTrace,
    [switch]$NoReleaseStage,
    [switch]$AllowUnreferencedRunner,
    [switch]$Clean,
    [string]$AndroidSdk,
    [string]$Ndk,
    [string]$Jdk,
    [string]$Keystore,
    [string]$CMake,
    [string]$Ninja,
    [string]$Python,
    [string]$NativeLibrary,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

# App identity and platform levels; see docs/port/android.md.
$packageName = "io.github.m3rt1n99.fafre"
$minSdk = 26
$targetSdk = 35
$platformName = "android-35"
$preferredBuildTools = "35.0.1"   # 35+ is needed for zipalign -P (16 KB pages)
$preferredNdk = "29.0.14206865"
$minimumCMake = [version]"3.22"
$minimumJdkMajor = 17

$nativeTarget = "faf_android"
$nativeLibraryName = "libfaf_android.so"
$nativeSourceDirectory = Join-Path $repoRoot "port\android"
# arm64-v8a keeps the original directory names so existing build trees stay valid.
$abiSuffix = if ($abi -eq "arm64-v8a") { "" } else { "-$abi" }
$nativeBuildDirectory = Join-Path $repoRoot "buildstage\android-native$abiSuffix"
$apkBuildDirectory = Join-Path $repoRoot "buildstage\android-apk$abiSuffix"
$outputDirectory = Join-Path $repoRoot "output\android"
$manifestFile = Join-Path $repoRoot "port\android\AndroidManifest.xml"
$resourceDirectory = Join-Path $repoRoot "port\android\res"
$javaSourceDirectory = Join-Path $repoRoot "port\android\src\java"
$gameDataManifest = Join-Path $repoRoot "port\data\gamedata.json"
$versionFile = Join-Path $repoRoot "port\android\version.properties"
$bootstrapScript = Join-Path $PSScriptRoot "bootstrap_android.ps1"
$buildRunnerScript = Join-Path $PSScriptRoot "build_runner.py"
$wildMagicFoundation = Join-Path $repoRoot "dependencies\WildMagic3p8\Foundation"
# Written by the reference runs (scripts/port/run_runner_android.py); optional.
$replayRefsFile = Join-Path $repoRoot "port\android\assets\replay_refs.json"
# License notices of third-party code in the APK's libraries, shipped as assets (the app's Licenses
# screen shows every assets/licenses/* file). The texts are the projects' own license files:
#   zstd                 libfafrunner.so, libfafrunner_o2.so (port/third_party/zstd)
#   DiligentCore         libfaf_android.so (Apache-2.0; dependencies/DiligentCore, local)
#   glslang, SPIRV-Tools libfaf_android.so and libfafdeviceprobe.so (DiligentCore's ThirdParty)
#   SPIRV-Headers        its grammar tables are compiled into SPIRV-Tools and glslang
#   SPIRV-Cross, volk    libfaf_android.so
# A Release build that packages one of those libraries refuses to go without its notices.
$diligentThirdParty = Join-Path $repoRoot "dependencies\DiligentCore\ThirdParty"
$licenseNotices = [ordered]@{
    "assets/licenses/zstd.txt" = (Join-Path $repoRoot "port\third_party\zstd\LICENSE")
    "assets/licenses/DiligentCore.txt" = (Join-Path $repoRoot "dependencies\DiligentCore\License.txt")
    "assets/licenses/glslang.txt" = (Join-Path $diligentThirdParty "glslang\LICENSE.txt")
    "assets/licenses/SPIRV-Tools.txt" = (Join-Path $diligentThirdParty "SPIRV-Tools\LICENSE")
    "assets/licenses/SPIRV-Headers.txt" = (Join-Path $diligentThirdParty "SPIRV-Headers\LICENSE")
    "assets/licenses/SPIRV-Cross.txt" = (Join-Path $diligentThirdParty "SPIRV-Cross\LICENSE")
    "assets/licenses/volk.txt" = (Join-Path $diligentThirdParty "volk\LICENSE.md")
    # Lua 5.0.1 (MIT, its notice must travel with copies): the LuaPlus core in libfaf_android.so
    # and the engine's Lua in libfafengine.so / libfafengine_o2.so.
    "assets/licenses/Lua.txt" = (Join-Path $repoRoot "dependencies\LuaPlus_Build1081\Docs\LuaCopyright")
    # Wild Magic 3.8 Foundation, linked into libfafengine.so; its license is a PDF, this is its text.
    "assets/licenses/WildMagic3.txt" = (Join-Path $repoRoot "port\third_party\licenses\WildMagic3-License.txt")
}

# Release 0.5.0: the menu trace (port/graphics/trace, format version 2 with references) and its description.
$menuTraceEntry = "assets/galplay/menu.galtrace"
$menuTraceInfoEntry = "assets/galplay/menu.json"
$menuTraceDirectory = Join-Path $repoRoot "buildstage\traces"

# The headless replay runner (port/engine/runner): APK entry name -> build_runner.py output name.
# The executables must be called lib*.so: the installer only extracts lib/<abi>/lib*.so entries.
$runnerEngineName = "libfafengine.so"
$runnerExecutableName = "libfafrunner.so"
$runnerProbeName = "libfafarenaprobe.so"
$runnerFiles = [ordered]@{
    $runnerEngineName = "libfafengine.so"
    $runnerExecutableName = "faf_headless_runner"
    $runnerProbeName = "libfafarenaprobe.so"
}
# Release 0.4.1: the -O2 pair (build_runner.py --opt O2; the launcher's "Optimised engine" option)
# and the device probe (build_runner.py --deviceprobe, port/deviceprobe), an executable like the runner.
$runnerEngineO2Name = "libfafengine_o2.so"
$runnerExecutableO2Name = "libfafrunner_o2.so"
$deviceProbeName = "libfafdeviceprobe.so"
$runnerO2Files = [ordered]@{
    $runnerEngineO2Name = "libfafengine.so"
    $runnerExecutableO2Name = "faf_headless_runner"
}
# What each packaged file is, for the ELF checks: runner (the executable with the low arena),
# engine (a library exporting faf_headless_main, also the arena probe), deviceprobe (an executable).
$runnerKinds = @{
    $runnerEngineName = "engine"; $runnerExecutableName = "runner"; $runnerProbeName = "engine"
    $runnerEngineO2Name = "engine"; $runnerExecutableO2Name = "runner"; $deviceProbeName = "deviceprobe"
}
# The engine/runner pairs whose build ids the reference table must name (G10).
$runnerPairs = @(@($runnerEngineName, $runnerExecutableName), @($runnerEngineO2Name, $runnerExecutableO2Name))
# What the runner may need at run time: system libraries every API 26+ device has.
$runnerAllowedNeeded = @("libz.so", "liblog.so", "libm.so", "libdl.so", "libc.so")
# The executable's exports: its low arena replaces bionic's malloc for the whole process.
$runnerRequiredExports = @("malloc", "free", "calloc", "realloc", "memalign", "posix_memalign",
    "aligned_alloc", "malloc_usable_size", "lowarena_enabled")
$elfMachines = @{ "arm64-v8a" = "AArch64"; "x86_64" = "Advanced Micro Devices X86-64" }

# The debug key Android Studio uses; the build only ever signs debug builds.
$keyAlias = "androiddebugkey"
$keyPassword = "android"

# --- helpers -----------------------------------------------------------------

function Write-Step([string]$Message) {
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

# One line of a native command's merged output as text. Windows PowerShell 5.1
# wraps stderr lines in ErrorRecords, and an empty one prints as
# "System.Management.Automation.RemoteException".
function ConvertTo-OutputLine($Item) {
    if ($Item -is [System.Management.Automation.ErrorRecord]) { return $Item.Exception.Message }
    return "$Item"
}

function Format-CommandLine([string]$FilePath, [string[]]$Arguments) {
    $parts = @($FilePath) + @($Arguments) | ForEach-Object {
        if ($_ -match '[\s"]' -or $_ -eq "") { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
    }
    return ($parts -join " ")
}

# Runs a native tool, echoing the command line first. Returns the output lines
# when -Capture is given (stdout and stderr merged), otherwise streams them.
# Under -DryRun only the command line is printed.
function Invoke-Tool {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$Arguments = @(),
        [switch]$Capture,
        [switch]$Quiet,
        [string]$FailureMessage
    )
    if (-not $Quiet) { Write-Host "  > $(Format-CommandLine $FilePath $Arguments)" -ForegroundColor DarkGray }
    if ($DryRun) { return @() }
    # Windows PowerShell 5.1 raises stderr lines of native commands as
    # terminating errors under ErrorActionPreference=Stop; the exit code is
    # what decides success here.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        if ($Capture) {
            $output = @(& $FilePath @Arguments 2>&1 | ForEach-Object { ConvertTo-OutputLine $_ })
        } else {
            & $FilePath @Arguments 2>&1 | ForEach-Object { Write-Host "    $(ConvertTo-OutputLine $_)" }
            $output = @()
        }
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($exitCode -ne 0) {
        if ($Capture -and $output.Count -gt 0) { Write-Host ($output -join "`n") }
        if (-not $FailureMessage) { $FailureMessage = "$([IO.Path]::GetFileName($FilePath)) failed" }
        throw "$FailureMessage (exit code $exitCode)."
    }
    return $output
}

function New-Directory([string]$Path) {
    if (-not $DryRun -and -not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Remove-PathIfPresent([string]$Path) {
    if (Test-Path -LiteralPath $Path) {
        Write-Host "  remove $Path"
        if (-not $DryRun) { Remove-Item -LiteralPath $Path -Recurse -Force }
    }
}

function Write-Utf8File([string]$Path, [string]$Text) {
    # UTF-8 without BOM on both PowerShell editions.
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding $false))
}

function Get-JsonProperty($Object, [string]$Name) {
    if ($null -eq $Object) { return $null }
    $property = $Object.PSObject.Properties[$Name]
    if ($property) { return $property.Value }
    return $null
}

function Get-SubdirectoriesByVersion([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return @() }
    $result = foreach ($directory in Get-ChildItem -LiteralPath $Path -Directory) {
        $parsed = $null
        if ([version]::TryParse(($directory.Name -replace '-.*$', ''), [ref]$parsed)) {
            [pscustomobject]@{ Path = $directory.FullName; Name = $directory.Name; Version = $parsed }
        }
    }
    return @($result | Sort-Object Version -Descending)
}

# --- tool resolution -----------------------------------------------------------

function Resolve-AndroidSdk {
    $candidates = @()
    if ($AndroidSdk) {
        $candidates = @($AndroidSdk)
    } else {
        $candidates = @(@($env:ANDROID_SDK_ROOT, $env:ANDROID_HOME, "C:\Android\sdk",
            (Join-Path ([Environment]::GetFolderPath("LocalApplicationData")) "Android\Sdk")) | Where-Object { $_ })
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate "platforms\$platformName\android.jar")) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Android SDK with platform $platformName not found (tried: $($candidates -join ', ')). " +
        "Install it with: sdkmanager `"platforms;$platformName`" `"build-tools;$preferredBuildTools`" `"ndk;$preferredNdk`" platform-tools, " +
        "then pass -AndroidSdk <dir> or set ANDROID_SDK_ROOT."
}

function Resolve-BuildTools([string]$Sdk) {
    $required = @("aapt2.exe", "zipalign.exe", "d8.bat", "apksigner.bat", "core-lambda-stubs.jar")
    $preferred = Join-Path $Sdk "build-tools\$preferredBuildTools"
    $candidates = @($preferred) + @(Get-SubdirectoriesByVersion (Join-Path $Sdk "build-tools") |
        Where-Object { $_.Version -ge [version]"35.0.0" } | ForEach-Object { $_.Path })
    foreach ($candidate in $candidates) {
        $missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $candidate $_)) })
        if ($missing.Count -eq 0) { return $candidate }
    }
    throw "Android build-tools 35 or newer not found in $Sdk\build-tools. Install with: sdkmanager `"build-tools;$preferredBuildTools`""
}

function Resolve-Ndk([string]$Sdk) {
    $candidates = @()
    if ($Ndk) {
        $candidates = @($Ndk)
    } else {
        $candidates = @(@($env:ANDROID_NDK_ROOT, $env:ANDROID_NDK_HOME, (Join-Path $Sdk "ndk\$preferredNdk")) | Where-Object { $_ })
        $candidates += @(Get-SubdirectoriesByVersion (Join-Path $Sdk "ndk") | ForEach-Object { $_.Path })
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate "build\cmake\android.toolchain.cmake")) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Android NDK not found (tried: $($candidates -join ', ')). Install with: sdkmanager `"ndk;$preferredNdk`" or pass -Ndk <dir>."
}

function Get-JdkMajorVersion([string]$JdkHome) {
    $releaseFile = Join-Path $JdkHome "release"
    if (-not (Test-Path -LiteralPath $releaseFile)) { return 0 }
    $line = Get-Content -LiteralPath $releaseFile | Where-Object { $_ -match '^JAVA_VERSION="([^"]+)"' } | Select-Object -First 1
    if (-not $line -or $line -notmatch '^JAVA_VERSION="(1\.)?(\d+)') { return 0 }
    return [int]$Matches[2]
}

function Test-JdkHome([string]$JdkHome) {
    foreach ($tool in @("javac.exe", "jar.exe", "keytool.exe", "java.exe")) {
        if (-not (Test-Path -LiteralPath (Join-Path $JdkHome "bin\$tool"))) { return $false }
    }
    return $true
}

function Resolve-Jdk {
    if ($Jdk) {
        if (-not (Test-JdkHome $Jdk)) { throw "-Jdk $Jdk is not a JDK (bin\javac.exe, jar.exe, keytool.exe expected)." }
        $major = Get-JdkMajorVersion $Jdk
        if ($major -lt $minimumJdkMajor) { throw "-Jdk $Jdk is Java $major; Java $minimumJdkMajor or newer is required." }
        return (Resolve-Path -LiteralPath $Jdk).Path
    }
    if ($env:JAVA_HOME -and (Test-JdkHome $env:JAVA_HOME) -and (Get-JdkMajorVersion $env:JAVA_HOME) -ge $minimumJdkMajor) {
        return (Resolve-Path -LiteralPath $env:JAVA_HOME).Path
    }
    # Prefer the oldest suitable JDK: javac keeps -source/-target 8 working
    # without the deprecation noise newer releases add.
    $roots = @("Eclipse Adoptium", "Java", "Microsoft", "Zulu", "Amazon Corretto") | ForEach-Object { Join-Path $env:ProgramFiles $_ }
    $found = foreach ($root in $roots) {
        if (Test-Path -LiteralPath $root) {
            foreach ($directory in Get-ChildItem -LiteralPath $root -Directory) {
                if (Test-JdkHome $directory.FullName) {
                    $major = Get-JdkMajorVersion $directory.FullName
                    if ($major -ge $minimumJdkMajor) { [pscustomobject]@{ Path = $directory.FullName; Major = $major } }
                }
            }
        }
    }
    $studioJbr = Join-Path $env:ProgramFiles "Android\Android Studio\jbr"
    if ((Test-JdkHome $studioJbr) -and (Get-JdkMajorVersion $studioJbr) -ge $minimumJdkMajor) {
        $found = @($found) + @([pscustomobject]@{ Path = $studioJbr; Major = (Get-JdkMajorVersion $studioJbr) })
    }
    $best = @($found) | Where-Object { $_ } | Sort-Object Major, Path | Select-Object -First 1
    if ($best) { return $best.Path }
    throw "No JDK $minimumJdkMajor+ found (JAVA_HOME=$env:JAVA_HOME). Install Temurin 17 or pass -Jdk <dir>."
}

function Get-CMakeVersion([string]$Executable) {
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { $text = (@(& $Executable --version 2>&1 | ForEach-Object { "$_" }) -join "`n") } finally { $ErrorActionPreference = $previousPreference }
    if ($text -match 'cmake version (\d+\.\d+(\.\d+)?)') { return [version]$Matches[1] }
    return $null
}

function Resolve-CMake([string]$Sdk) {
    $candidates = @()
    if ($CMake) {
        $candidates = @($CMake)
    } else {
        $onPath = Get-Command cmake.exe -ErrorAction SilentlyContinue
        if ($onPath) { $candidates += $onPath.Source }
        $pythonRoot = Join-Path ([Environment]::GetFolderPath("LocalApplicationData")) "Programs\Python"
        if (Test-Path -LiteralPath $pythonRoot) {
            $candidates += @(Get-ChildItem -LiteralPath $pythonRoot -Directory | Sort-Object Name -Descending |
                ForEach-Object { Join-Path $_.FullName "Scripts\cmake.exe" })
        }
        $candidates += @(Join-Path $env:ProgramFiles "CMake\bin\cmake.exe")
        $candidates += @(Get-SubdirectoriesByVersion (Join-Path $Sdk "cmake") | ForEach-Object { Join-Path $_.Path "bin\cmake.exe" })
    }
    foreach ($candidate in $candidates) {
        if (-not (Test-Path -LiteralPath $candidate)) { continue }
        $version = Get-CMakeVersion $candidate
        if ($version -and $version -ge $minimumCMake) { return [pscustomobject]@{ Path = $candidate; Version = $version } }
    }
    throw "CMake $minimumCMake or newer not found (tried: $($candidates -join ', ')). Install it (python -m pip install cmake) or pass -CMake <cmake.exe>."
}

function Resolve-Ninja([string]$Sdk) {
    $candidates = @()
    if ($Ninja) {
        $candidates = @($Ninja)
    } else {
        $onPath = Get-Command ninja.exe -ErrorAction SilentlyContinue
        if ($onPath) { $candidates += $onPath.Source }
        $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path -LiteralPath $vswhere) {
            $previousPreference = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            try { $installations = @(& $vswhere -sort -products '*' -property installationPath 2>$null) } finally { $ErrorActionPreference = $previousPreference }
            foreach ($installation in $installations) {
                if ($installation) { $candidates += Join-Path $installation "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" }
            }
        }
        $candidates += @(Get-SubdirectoriesByVersion (Join-Path $Sdk "cmake") | ForEach-Object { Join-Path $_.Path "bin\ninja.exe" })
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    throw "Ninja not found (tried: $($candidates -join ', ')). It ships with Visual Studio 2022 (C++ CMake tools) and the SDK's cmake package; or pass -Ninja <ninja.exe>."
}

function Resolve-Python {
    $candidates = @()
    if ($Python) {
        $candidates = @($Python)
    } else {
        # The py launcher knows the real interpreters; python.exe on PATH may
        # be the Microsoft Store redirector.
        $launcher = Get-Command py.exe -ErrorAction SilentlyContinue
        if ($launcher) {
            $previousPreference = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            try { $fromLauncher = @(& $launcher.Source -3 -c "import sys; print(sys.executable)" 2>$null); $exitCode = $LASTEXITCODE } finally { $ErrorActionPreference = $previousPreference }
            if ($exitCode -eq 0 -and $fromLauncher.Count -gt 0 -and $fromLauncher[0]) { $candidates += ([string]$fromLauncher[0]).Trim() }
        }
        foreach ($name in @("python.exe", "python3.exe")) {
            $onPath = Get-Command $name -ErrorAction SilentlyContinue
            if ($onPath) { $candidates += $onPath.Source }
        }
    }
    foreach ($candidate in $candidates) {
        if (-not (Test-Path -LiteralPath $candidate)) { continue }
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try { $version = @(& $candidate --version 2>&1 | ForEach-Object { "$_" }); $exitCode = $LASTEXITCODE } finally { $ErrorActionPreference = $previousPreference }
        if ($exitCode -eq 0 -and $version.Count -gt 0 -and $version[0] -match '^Python 3\.') { return $candidate }
    }
    throw "Python 3 not found (needed to add uncompressed entries to the APK). Install it from python.org or pass -Python <python.exe>."
}

function Read-VersionName {
    if (-not (Test-Path -LiteralPath $versionFile)) { throw "Missing $versionFile." }
    $line = Get-Content -LiteralPath $versionFile | Where-Object { $_ -match '^\s*versionName\s*=' } | Select-Object -First 1
    if (-not $line) { throw "$versionFile does not define versionName." }
    $name = ($line -replace '^\s*versionName\s*=\s*', '').Trim()
    if ($name -notmatch '^\d+\.\d+\.\d+([-.][0-9A-Za-z.]+)?$') { throw "Invalid versionName '$name' in $versionFile." }
    return $name
}

function Get-ExtractNativeLibs {
    [xml]$xml = Get-Content -LiteralPath $manifestFile -Raw
    $value = $xml.manifest.application.GetAttribute("extractNativeLibs", "http://schemas.android.com/apk/res/android")
    if ($value -ne "true" -and $value -ne "false") {
        throw "$manifestFile must set android:extractNativeLibs to true or false (found '$value')."
    }
    return ($value -eq "true")
}

# Output of a git command as text lines; empty when git fails.
function Get-GitOutput([string[]]$Arguments) {
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { $output = @(& git -C $repoRoot @Arguments 2>$null | ForEach-Object { "$_" }); $exitCode = $LASTEXITCODE } finally { $ErrorActionPreference = $previousPreference }
    if ($exitCode -ne 0) { return @() }
    return $output
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-VersionCode {
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    # No Select-Object -First on native output: stopping the pipeline early
    # kills the process and leaves a failing $LASTEXITCODE.
    try { $output = @(& git -C $repoRoot rev-list --count HEAD 2>&1 | ForEach-Object { "$_" }); $exitCode = $LASTEXITCODE } finally { $ErrorActionPreference = $previousPreference }
    $count = if ($output.Count -gt 0) { $output[0].Trim() } else { "" }
    if ($exitCode -ne 0 -or $count -notmatch '^\d+$') {
        throw "versionCode comes from 'git rev-list --count HEAD', which failed: $count. Build from a git checkout with git on PATH."
    }
    $code = [int]$count
    if ($code -lt 1 -or $code -gt 2100000000) { throw "versionCode $code is out of range." }
    return $code
}

# CMake file API: lets the build read where the project puts faf_android
# instead of assuming CMake's default output directory.
$cmakeApiClient = "client-faf-re"

function Request-CMakeCodeModel([string]$BuildDirectory) {
    $queryDirectory = Join-Path $BuildDirectory ".cmake\api\v1\query\$cmakeApiClient"
    New-Directory $queryDirectory
    if (-not $DryRun) { Write-Utf8File (Join-Path $queryDirectory "codemodel-v2") "" }
}

function Get-CMakeTargetArtifact([string]$BuildDirectory, [string]$Target, [string]$FileName) {
    $replyDirectory = Join-Path $BuildDirectory ".cmake\api\v1\reply"
    $index = Get-ChildItem -LiteralPath $replyDirectory -Filter "index-*.json" -ErrorAction SilentlyContinue |
        Sort-Object Name | Select-Object -Last 1
    if ($index) {
        $indexJson = Get-Content -LiteralPath $index.FullName -Raw | ConvertFrom-Json
        $clientReply = Get-JsonProperty (Get-JsonProperty $indexJson "reply") $cmakeApiClient
        $codemodelReply = Get-JsonProperty $clientReply "codemodel-v2"
        $codemodelFile = Get-JsonProperty $codemodelReply "jsonFile"
        if ($codemodelFile) {
            $codemodel = Get-Content -LiteralPath (Join-Path $replyDirectory $codemodelFile) -Raw | ConvertFrom-Json
            foreach ($configuration in @($codemodel.configurations)) {
                $targetRef = @($configuration.targets) | Where-Object { $_.name -eq $Target } | Select-Object -First 1
                if (-not $targetRef) { continue }
                $targetJson = Get-Content -LiteralPath (Join-Path $replyDirectory $targetRef.jsonFile) -Raw | ConvertFrom-Json
                foreach ($artifact in @(Get-JsonProperty $targetJson "artifacts")) {
                    if (-not $artifact) { continue }
                    $path = $artifact.path
                    if ([IO.Path]::GetFileName($path) -ne $FileName) { continue }
                    if (-not [IO.Path]::IsPathRooted($path)) { $path = Join-Path $BuildDirectory $path }
                    return [IO.Path]::GetFullPath($path)
                }
            }
        }
    }
    return (Join-Path $BuildDirectory $FileName)
}

# Small zipfile helper: Windows PowerShell's System.IO.Compression cannot
# write STORED entries (classes.dex, resources.arsc and the assets stay
# uncompressed, and libraries must be stored when extractNativeLibs=false).
$apkEntriesPy = @'
"""Adds files to an APK as STORED or DEFLATED entries, or checks how entries are compressed.

Written by scripts/port/build_android.ps1 on every build; edit it there.

usage: apk_entries.py add <apk> <entry>=<file>...            (STORED)
       apk_entries.py add-deflated <apk> <entry>=<file>...   (DEFLATED, level 9)
       apk_entries.py check-stored <apk> <entry>...
       apk_entries.py check-deflated <apk> <entry>...
"""
import sys
import zipfile

# Fixed timestamp, as aapt2 does, so equal inputs give equal APKs.
ENTRY_TIME = (1981, 1, 1, 1, 1, 2)


def add(apk, pairs, method):
    with zipfile.ZipFile(apk, "a") as archive:
        existing = set(archive.namelist())
        for pair in pairs:
            name, path = pair.split("=", 1)
            if name in existing:
                sys.exit(f"{apk} already contains {name}")
            info = zipfile.ZipInfo(name, date_time=ENTRY_TIME)
            info.compress_type = method
            info.external_attr = 0o100644 << 16
            with open(path, "rb") as source:
                if method == zipfile.ZIP_DEFLATED:
                    archive.writestr(info, source.read(), compress_type=method, compresslevel=9)
                else:
                    archive.writestr(info, source.read())


def check(apk, names, method):
    failed = False
    with zipfile.ZipFile(apk) as archive:
        for name in names:
            try:
                info = archive.getinfo(name)
            except KeyError:
                print(f"missing: {name}")
                failed = True
                continue
            label = {zipfile.ZIP_STORED: "stored", zipfile.ZIP_DEFLATED: "deflated"}.get(info.compress_type,
                                                                                         f"method {info.compress_type}")
            ok = info.compress_type == method
            print(f"{label if ok else label.upper() + ' (unexpected)'}: {name} ({info.file_size} bytes"
                  f"{'' if info.compress_type == zipfile.ZIP_STORED else f', {info.compress_size} compressed'})")
            failed |= not ok
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    modes = {"add": zipfile.ZIP_STORED, "add-deflated": zipfile.ZIP_DEFLATED,
             "check-stored": zipfile.ZIP_STORED, "check-deflated": zipfile.ZIP_DEFLATED}
    if len(sys.argv) < 4 or sys.argv[1] not in modes:
        sys.exit(__doc__)
    if sys.argv[1].startswith("add"):
        add(sys.argv[2], sys.argv[3:], modes[sys.argv[1]])
    else:
        check(sys.argv[2], sys.argv[3:], modes[sys.argv[1]])
'@

# Reads a galtrace (port/graphics/trace/format, GalTraceFormat.h) without the C++ tools: the header, every
# record's op and size, the version 2 payload definitions; writes the description the launcher reads.
$galtraceInfoPy = @'
"""Checks a galtrace for the APK and writes its description (assets/galplay/menu.json).

Written by scripts/port/build_android.ps1 on every build; edit it there.

usage: galtrace_info.py <trace> <menu.json> [--release] [--native <libfaf_android.so>]...

Exit 0: a complete trace (header, records up to the End record) of format version 2 whose game-file
payloads are references (PayloadRef records) and whose embedded payloads do not look like game files
(DDS/PNG/JPEG images, effect sources); 1 otherwise, with the reason. With --release also: the trace
carries the PC's Diligent-Vulkan reference hash of every readback frame, its header names no user's
home directory, and every --native library has the menu replay mode (the Intent extra names of
port/android/src/GalPlay.h), so a tap on Menu replay cannot start the game instead. Without --release
those are warnings.

The description holds the size, sha256, header metadata (without the recorder's command line), and per
payload kind the number and bytes of the definitions, with the archives the references name.
"""
import hashlib
import json
import os
import re
import struct
import sys

OP_BLOB, OP_END, OP_REF, OP_DIGEST, OP_COMPOSE = 0x0001, 0x0002, 0x0008, 0x0009, 0x000A
REFERENCE_KEY = "reference_frames.diligent:vk"
# The native side's contract (GalPlay.h): a libfaf_android.so without these strings ignores the mode.
NATIVE_MARKERS = (b"menu-replay", b"menuReplay.trace", b"onMenuReplayFinished")
HOME = re.compile(r"[A-Za-z]:[\\/]+Users[\\/]+[^\\/\s\"']+|/home/[^/\s\"']+|/Users/[^/\s\"']+")


def read_str(data, at):
    (length,) = struct.unpack_from("<I", data, at)
    return data[at + 4:at + 4 + length].decode("utf-8", "replace"), at + 4 + length


def looks_like_game_file(payload):
    """'dds', 'png', 'jpeg' or 'effect source' when an embedded payload looks like a file of the game."""
    head = bytes(payload[:4])
    if head == b"DDS ":
        return "dds"
    if head == b"\x89PNG":
        return "png"
    if head[:3] == b"\xff\xd8\xff":
        return "jpeg"
    if len(payload) >= 64:
        sample = bytes(payload[:4096])
        text = sum(1 for b in sample if b in (9, 10, 13) or 32 <= b < 127)
        if text >= len(sample) * 0.97:
            whole = bytes(payload)  # a memoryview's "in" looks for one element, not a subsequence
            if b"technique" in whole or b"sampler" in whole:
                return "effect source"
    return None


def main():
    args = sys.argv[1:]
    release = "--release" in args
    args = [a for a in args if a != "--release"]
    natives = []
    while "--native" in args:
        i = args.index("--native")
        if i + 1 >= len(args):
            sys.exit(__doc__)
        natives.append(args[i + 1])
        del args[i:i + 2]
    if len(args) != 2:
        sys.exit(__doc__)
    path, out = args
    data = open(path, "rb").read()
    view = memoryview(data)
    problems = []
    warnings = []
    if data[:8] != b"GALTRACE":
        sys.exit(f"{path}: not a galtrace file")
    version, count = struct.unpack_from("<II", data, 8)
    at = 16
    metadata = {}
    for _ in range(count):
        key, at = read_str(data, at)
        value, at = read_str(data, at)
        metadata[key] = value
    records = 0
    ended = False
    blobs = blob_bytes = refs = ref_bytes = digests = digest_bytes = composes = compose_bytes = 0
    largest_blob = 0
    archives = {}
    suspects = []
    while at + 8 <= len(data):
        op, _flags, length = struct.unpack_from("<HHI", data, at)
        body = at + 8
        if body + length > len(data):
            problems.append(f"record {records} (op 0x{op:04x}) runs past the end of the file")
            break
        records += 1
        if op == OP_BLOB:
            blobs += 1
            payload_id = struct.unpack_from("<I", data, body)[0]
            size = max(0, length - 24)
            blob_bytes += size
            largest_blob = max(largest_blob, size)
            kind = looks_like_game_file(view[body + 24:body + 24 + size])
            if kind:
                suspects.append({"payload": payload_id, "kind": kind, "bytes": size})
        elif op == OP_REF:
            refs += 1
            _payload, _a, _b, size = struct.unpack_from("<IQQI", data, body)
            vfs, pos = read_str(data, body + 24)
            archive, pos = read_str(data, pos)
            ref_bytes += size
            name = os.path.basename(archive.replace("\\", "/").rstrip("/")).lower() or "(none)"
            entry = archives.setdefault(name, {"files": 0, "bytes": 0})
            entry["files"] += 1
            entry["bytes"] += size
        elif op == OP_DIGEST:
            digests += 1
            digest_bytes += struct.unpack_from("<IQQI", data, body)[3]
        elif op == OP_COMPOSE:
            composes += 1
            compose_bytes += struct.unpack_from("<IQQI", data, body)[3]
        elif op == OP_END:
            ended = True
            at = body + length
            break
        at = body + length
    if not ended:
        problems.append("no End record: the trace is cut off")
    elif at != len(data):
        problems.append(f"{len(data) - at} bytes after the End record")
    if version < 2:
        problems.append(f"format version {version}: every payload is embedded, game files included "
                        "(make a version 2 trace with galtrace-refs)")
    elif refs == 0:
        problems.append("version 2 but no PayloadRef record: the game files would be embedded")
    if suspects:
        kinds = {}
        for s in suspects:
            kinds[s["kind"]] = kinds.get(s["kind"], 0) + 1
        problems.append(f"{len(suspects)} embedded payloads look like game files ("
                        + ", ".join(f"{n} {k}" for k, n in sorted(kinds.items()))
                        + "; first: payload " + ", ".join(str(s["payload"]) for s in suspects[:8])
                        + "): they must be references (galtrace-refs)")

    # The PC's Diligent-Vulkan frame hashes the phone compares with, one per readback frame.
    references = {k: v for k, v in metadata.items() if k.startswith("reference_frames.")}
    frames = [int(f) for f in re.findall(r"\d+", metadata.get("harness_frames", ""))]
    vk = dict((int(f), h.lower()) for f, h in re.findall(r"(\d+)\s*[:=]\s*([0-9a-fA-F]{16})", metadata.get(REFERENCE_KEY, "")))
    without = [f for f in frames if f not in vk]
    if not frames:
        (problems if release else warnings).append("the header has no harness_frames (the readback frames)")
    if not vk:
        (problems if release else warnings).append(f"the header has no {REFERENCE_KEY}: the app can only say NO REFERENCE")
    elif without:
        (problems if release else warnings).append(f"{REFERENCE_KEY} has no hash for frames {without}")
    homes = sorted({m for v in metadata.values() for m in HOME.findall(v)})
    if homes:
        (problems if release else warnings).append(
            "the header metadata names a home directory (" + ", ".join(homes) + "; keys "
            + ", ".join(k for k, v in metadata.items() if HOME.search(v)) + "): drop or rewrite it before a release")

    native = {}
    for lib in natives:
        blob = open(lib, "rb").read()
        missing = [m.decode() for m in NATIVE_MARKERS if m not in blob]
        native[os.path.basename(lib)] = {"menu_replay_mode": not missing, "missing": missing}
        if missing:
            (problems if release else warnings).append(
                f"{lib} has no menu replay mode (no {', '.join(missing)}): GameActivity would start the game instead")

    info = {
        "schema": 1,
        "asset": "galplay/menu.galtrace",
        "name": os.path.basename(path),
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "version": version,
        "metadata": {k: v for k, v in metadata.items() if k != "command_line"},
        "records": records,
        "payloads": {
            "embedded": {"count": blobs, "bytes": blob_bytes, "largest": largest_blob},
            "references": {"count": refs, "bytes": ref_bytes, "archives": archives},
            "digests": {"count": digests, "bytes": digest_bytes},
            "compositions": {"count": composes, "bytes": compose_bytes},
        },
        "suspect_game_files": suspects[:64],
        "archives": sorted(k for k in archives if k != "(none)"),
        "readback_frames": frames,
        "reference_frames": references,
        "native": native,
        "warnings": warnings,
        "problems": problems,
    }
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(info, f, indent=1)
        f.write("\n")
    print(f"trace {os.path.basename(path)}: v{version}, {len(data):,} bytes, sha256 {info['sha256']}, {records:,} records")
    print(f"  embedded {blobs} payloads ({blob_bytes:,} bytes, largest {largest_blob:,}), references {refs} ({ref_bytes:,} bytes "
          f"of game files), digests {digests}, compositions {composes} ({compose_bytes:,} bytes built)")
    print("  archives " + (", ".join(f"{k} ({v['files']} files)" for k, v in sorted(archives.items())) or "none"))
    print("  reference frames " + (", ".join(f"{k}: {len(re.findall(r'[0-9a-fA-F]{16}', v))}" for k, v in references.items()) or "NONE"))
    print("  harness_frames " + metadata.get("harness_frames", "(none)") + ", frame_rate " + metadata.get("frame_rate", "(none)")
          + ", presents " + metadata.get("presents", "(none)"))
    for lib, state in native.items():
        print(f"  native {lib}: " + ("menu replay mode present" if state["menu_replay_mode"] else "NO menu replay mode"))
    for warning in warnings:
        print("WARNING: " + warning)
    if problems:
        for problem in problems:
            print("PROBLEM: " + problem)
        sys.exit(1)


if __name__ == "__main__":
    main()
'@

# Reads an ELF file with llvm-readelf and returns what the checks need.
function Get-ElfFacts([string]$Path) {
    $header = Invoke-Tool $llvmReadelf @("--file-header", $Path) -Capture -Quiet
    $dynamic = Invoke-Tool $llvmReadelf @("--dynamic-table", $Path) -Capture -Quiet
    $symbols = Invoke-Tool $llvmReadelf @("--dyn-symbols", "--wide", $Path) -Capture -Quiet
    $segments = Invoke-Tool $llvmReadelf @("--program-headers", "--wide", $Path) -Capture -Quiet
    $notes = Invoke-Tool $llvmReadelf @("--notes", $Path) -Capture -Quiet
    $machine = @($header | ForEach-Object { if ($_ -match '^\s*Machine:\s*(.+?)\s*$') { $Matches[1] } }) | Select-Object -First 1
    $type = @($header | ForEach-Object { if ($_ -match '^\s*Type:\s*(\S+)') { $Matches[1] } }) | Select-Object -First 1
    $interpreter = @($segments | ForEach-Object { if ($_ -match 'Requesting program interpreter:\s*([^\]]+)\]') { $Matches[1] } }) | Select-Object -First 1
    $exports = @($symbols | Where-Object { $_ -notmatch '\sUND\s' -and $_ -match '\s(GLOBAL|WEAK)\s' } |
        ForEach-Object { (($_ -split '\s+') | Where-Object { $_ })[-1] -replace '@.*$', '' })
    return [pscustomobject]@{
        Machine = $machine
        Type = $type
        Interpreter = $interpreter
        Needed = @($dynamic | ForEach-Object { if ($_ -match '\(NEEDED\)\s+Shared library: \[([^\]]+)\]') { $Matches[1] } })
        Exports = $exports
        LoadAlignments = @($segments | Where-Object { $_ -match '^\s*LOAD\s' } | ForEach-Object { [Convert]::ToInt64((($_ -split '\s+') | Where-Object { $_ })[-1], 16) })
        BuildId = @($notes | ForEach-Object { if ($_ -match 'Build ID:\s*([0-9a-f]+)') { $Matches[1] } }) | Select-Object -First 1
    }
}

# The checks for the runner's three files: right ABI, 16 KB segments, only system
# libraries, the entry points the launcher and the executable rely on, and a
# build id the strip kept. Returns the build id.
function Test-RunnerElf([string]$Name, [string]$Stripped, [string]$Unstripped) {
    $facts = Get-ElfFacts $Stripped
    $problems = New-Object System.Collections.Generic.List[string]
    if ($facts.Machine -ne $elfMachines[$abi]) { $problems.Add("machine is '$($facts.Machine)', expected '$($elfMachines[$abi])' for $abi") }
    if ($facts.Type -ne "DYN") { $problems.Add("ELF type is $($facts.Type), expected DYN (PIE executable or shared library)") }
    $foreign = @($facts.Needed | Where-Object { $runnerAllowedNeeded -notcontains $_ })
    if ($foreign.Count -gt 0) { $problems.Add("needs $($foreign -join ', '); only $($runnerAllowedNeeded -join ', ') are allowed") }
    $misaligned = @($facts.LoadAlignments | Where-Object { $_ -lt 0x4000 })
    if ($facts.LoadAlignments.Count -eq 0 -or $misaligned.Count -gt 0) {
        $problems.Add("LOAD segments aligned below 16 KB ($(($facts.LoadAlignments | ForEach-Object { '0x{0:x}' -f $_ }) -join ', '))")
    }
    $kind = $runnerKinds[$Name]
    if ($kind -eq "runner") {
        if ($facts.Interpreter -ne "/system/bin/linker64") { $problems.Add("interpreter is '$($facts.Interpreter)', expected /system/bin/linker64") }
        $missing = @($runnerRequiredExports | Where-Object { $facts.Exports -notcontains $_ })
        if ($missing.Count -gt 0) { $problems.Add("does not export $($missing -join ', ') (the low arena's malloc family)") }
    } elseif ($kind -eq "deviceprobe") {
        # An executable; Vulkan, EGL and GLES are opened with dlopen, so NEEDED is checked like the runner's.
        if ($facts.Interpreter -ne "/system/bin/linker64") { $problems.Add("interpreter is '$($facts.Interpreter)', expected /system/bin/linker64") }
    } else {
        if ($facts.Interpreter) { $problems.Add("has an interpreter ($($facts.Interpreter)); expected a shared library") }
        if ($facts.Exports -notcontains "faf_headless_main") { $problems.Add("does not export faf_headless_main") }
    }
    if (-not $facts.BuildId) { $problems.Add("has no build id after stripping") }
    $unstrippedId = (Get-ElfFacts $Unstripped).BuildId
    if ($facts.BuildId -and $unstrippedId -ne $facts.BuildId) { $problems.Add("build id $($facts.BuildId) differs from the unstripped file's $unstrippedId") }
    if ($problems.Count -gt 0) { throw "$Name ($Stripped): $($problems -join '; ')." }
    Write-Host ("  {0}: NEEDED {1}; LOAD align {2}; build id {3}" -f $Name, ($facts.Needed -join ', '),
        (($facts.LoadAlignments | Sort-Object -Unique | ForEach-Object { '0x{0:x}' -f $_ }) -join '/'), $facts.BuildId)
    return $facts.BuildId
}

# Every 40-digit hex string value in the reference table (build ids, whatever
# the table's exact layout; a commit hash there never equals a build id).
function Get-ReferenceBuildIds([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return @() }
    $text = [IO.File]::ReadAllText($Path)
    return @([regex]::Matches($text, '"([0-9a-fA-F]{40})"') | ForEach-Object { $_.Groups[1].Value.ToLowerInvariant() } | Sort-Object -Unique)
}

# --- resolve everything before touching the disk -------------------------------

$isDebug = $Configuration -eq "Debug"
$versionName = Read-VersionName
$versionCode = Get-VersionCode

$sdkPath = Resolve-AndroidSdk
$buildToolsPath = Resolve-BuildTools $sdkPath
$androidJar = Join-Path $sdkPath "platforms\$platformName\android.jar"
$aapt2 = Join-Path $buildToolsPath "aapt2.exe"
$zipalign = Join-Path $buildToolsPath "zipalign.exe"
$d8 = Join-Path $buildToolsPath "d8.bat"
$apksigner = Join-Path $buildToolsPath "apksigner.bat"
$lambdaStubs = Join-Path $buildToolsPath "core-lambda-stubs.jar"

$jdkPath = Resolve-Jdk
$javac = Join-Path $jdkPath "bin\javac.exe"
$jar = Join-Path $jdkPath "bin\jar.exe"
$keytool = Join-Path $jdkPath "bin\keytool.exe"
$pythonPath = Resolve-Python

if ($Keystore) {
    $keystorePath = $Keystore
} elseif ($env:FAF_ANDROID_KEYSTORE) {
    $keystorePath = $env:FAF_ANDROID_KEYSTORE
} else {
    $keystorePath = Join-Path $env:USERPROFILE ".android\debug.keystore"
}
$keystorePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($keystorePath)
foreach ($buildRoot in @((Join-Path $repoRoot "buildstage"), (Join-Path $repoRoot "output"))) {
    if ($keystorePath.StartsWith($buildRoot + "\", [StringComparison]::OrdinalIgnoreCase)) {
        throw "The signing key must live outside the build directories ($keystorePath). Updates only install over an app signed with the same key."
    }
}

foreach ($required in @($manifestFile, $gameDataManifest, $javaSourceDirectory)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing $required." }
}

$extractNativeLibs = Get-ExtractNativeLibs
$includeRunner = -not $SkipNative -and -not $SkipRunner
if ($includeRunner -and -not $extractNativeLibs) {
    throw "The replay runner is started as a process from nativeLibraryDir, which needs android:extractNativeLibs=`"true`" in $manifestFile (or build with -SkipRunner)."
}
if (($RunnerDirectory -or $RunnerO2Directory -or $DeviceProbe) -and -not $includeRunner) {
    throw "-RunnerDirectory, -RunnerO2Directory and -DeviceProbe cannot be combined with -SkipRunner or -SkipNative."
}
if ($RunnerO2Directory -and $SkipOptimizedRunner) { throw "-RunnerO2Directory and -SkipOptimizedRunner contradict each other." }
if ($DeviceProbe -and $SkipDeviceProbe) { throw "-DeviceProbe and -SkipDeviceProbe contradict each other." }
$runnerBuildDirectory = Join-Path $apkBuildDirectory "runner"
$runnerO2BuildDirectory = Join-Path $apkBuildDirectory "runner-O2"
# What this build packages: APK entry name -> unstripped source file.
$runnerSources = [ordered]@{}
$buildRunnerO0 = $false
$buildRunnerO2 = $false
$buildDeviceProbe = $false
$includeOptimized = $includeRunner -and -not $SkipOptimizedRunner
$includeDeviceProbe = $includeRunner -and -not $SkipDeviceProbe
if ($includeRunner) {
    if ($RunnerDirectory) {
        $RunnerDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($RunnerDirectory)
        foreach ($source in $runnerFiles.Values) {
            if (-not (Test-Path -LiteralPath (Join-Path $RunnerDirectory $source))) {
                throw "-RunnerDirectory $RunnerDirectory has no $source (build it with: python scripts/port/build_runner.py --abi $abi --probe)."
            }
        }
        $runnerSourceDirectory = $RunnerDirectory
    } else {
        if (-not (Test-Path -LiteralPath $buildRunnerScript)) { throw "Missing $buildRunnerScript." }
        if (-not (Test-Path -LiteralPath $wildMagicFoundation)) {
            throw "dependencies\WildMagic3p8\Foundation is missing; build_runner.py compiles the engine's Wild Magic TUs from it (local, gitignored). " +
                "Package prebuilt runner binaries with -RunnerDirectory, or leave the runner out with -SkipRunner."
        }
        $runnerSourceDirectory = $runnerBuildDirectory
        $buildRunnerO0 = $true
    }
    foreach ($entryName in $runnerFiles.Keys) { $runnerSources[$entryName] = Join-Path $runnerSourceDirectory $runnerFiles[$entryName] }

    if ($includeOptimized) {
        if ($RunnerO2Directory) {
            $RunnerO2Directory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($RunnerO2Directory)
            foreach ($source in $runnerO2Files.Values) {
                if (-not (Test-Path -LiteralPath (Join-Path $RunnerO2Directory $source))) {
                    throw "-RunnerO2Directory $RunnerO2Directory has no $source (build it with: python scripts/port/build_runner.py --abi $abi --opt O2)."
                }
            }
            foreach ($entryName in $runnerO2Files.Keys) { $runnerSources[$entryName] = Join-Path $RunnerO2Directory $runnerO2Files[$entryName] }
        } elseif ($RunnerDirectory) {
            if (-not $isDebug) {
                throw "A Release build packages the -O2 runner and engine as well: pass -RunnerO2Directory (build_runner.py --abi $abi --opt O2), or -SkipOptimizedRunner."
            }
            Write-Host "  no -RunnerO2Directory: the -O2 runner and engine are left out" -ForegroundColor Yellow
            $includeOptimized = $false
        } else {
            $buildRunnerO2 = $true
            foreach ($entryName in $runnerO2Files.Keys) { $runnerSources[$entryName] = Join-Path $runnerO2BuildDirectory $runnerO2Files[$entryName] }
        }
    }

    if ($includeDeviceProbe) {
        if ($DeviceProbe) {
            $DeviceProbe = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DeviceProbe)
            if (-not (Test-Path -LiteralPath $DeviceProbe -PathType Leaf)) { throw "-DeviceProbe $DeviceProbe does not exist." }
            $runnerSources[$deviceProbeName] = $DeviceProbe
        } elseif ($RunnerDirectory -and (Test-Path -LiteralPath (Join-Path $RunnerDirectory $deviceProbeName))) {
            $runnerSources[$deviceProbeName] = Join-Path $RunnerDirectory $deviceProbeName
        } elseif ($RunnerDirectory) {
            if (-not $isDebug) {
                throw "A Release build packages the device probe as well: -RunnerDirectory has no $deviceProbeName; pass -DeviceProbe <file> (build_runner.py --abi $abi --deviceprobe), or -SkipDeviceProbe."
            }
            Write-Host "  -RunnerDirectory has no $($deviceProbeName): the device probe is left out" -ForegroundColor Yellow
            $includeDeviceProbe = $false
        } else {
            $buildDeviceProbe = $true
            $runnerSources[$deviceProbeName] = Join-Path $runnerBuildDirectory $deviceProbeName
        }
    }
}

# The menu trace (release 0.5.0): -MenuTrace, else the one in buildstage\traces (menu*.galtrace when there
# are several). Debug builds go without one when there is none; a Release build refuses.
if ($MenuTrace -and $SkipMenuTrace) { throw "-MenuTrace and -SkipMenuTrace contradict each other." }
$menuTraceFile = $null
if (-not $SkipMenuTrace) {
    if ($MenuTrace) {
        $menuTraceFile = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($MenuTrace)
        if (-not (Test-Path -LiteralPath $menuTraceFile -PathType Leaf)) { throw "-MenuTrace $menuTraceFile does not exist." }
    } else {
        $traceCandidates = @(Get-ChildItem -LiteralPath $menuTraceDirectory -Filter "*.galtrace" -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Extension -eq ".galtrace" })
        if ($traceCandidates.Count -gt 1) { $traceCandidates = @($traceCandidates | Where-Object { $_.Name -like "menu*" }) }
        if ($traceCandidates.Count -eq 1) {
            $menuTraceFile = $traceCandidates[0].FullName
        } elseif ($traceCandidates.Count -gt 1) {
            throw "$menuTraceDirectory has several menu*.galtrace files ($(($traceCandidates | ForEach-Object { $_.Name }) -join ', ')); choose one with -MenuTrace."
        } elseif (-not $isDebug -and -not $SkipNative) {
            throw "A Release build packages the menu trace (the launcher's Menu replay): there is none in $menuTraceDirectory. Pass -MenuTrace <file> (a format version 2 trace with game-file references, port/graphics/trace), or -SkipMenuTrace."
        } else {
            Write-Host "  no menu trace in $($menuTraceDirectory): the menu replay is left out" -ForegroundColor Yellow
        }
    }
}

$releaseDirectory = Join-Path $repoRoot "buildstage\releases\android-v$versionName"
$stageRelease = -not $SkipNative -and -not $isDebug -and -not $NoReleaseStage

if (-not $SkipNative) {
    $ndkPath = Resolve-Ndk $sdkPath
    $llvmBin = Join-Path $ndkPath "toolchains\llvm\prebuilt\windows-x86_64\bin"
    $llvmStrip = Join-Path $llvmBin "llvm-strip.exe"
    $llvmReadelf = Join-Path $llvmBin "llvm-readelf.exe"
    foreach ($tool in @($llvmStrip, $llvmReadelf)) {
        if (-not (Test-Path -LiteralPath $tool)) { throw "NDK tool missing: $tool" }
    }
    # The libraries an app may link against at minSdk: the NDK's stub
    # libraries for that API level. Everything else would have to ship in the
    # APK, and libfaf_android.so brings everything it needs with it.
    $systemLibraryDirectory = Join-Path $ndkPath "toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\aarch64-linux-android\$minSdk"
    $systemLibraries = @(Get-ChildItem -LiteralPath $systemLibraryDirectory -Filter "*.so" -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ne "libc++.so" } | ForEach-Object { $_.Name })
    if ($systemLibraries.Count -eq 0) { throw "NDK sysroot libraries for API $minSdk not found in $systemLibraryDirectory." }

    if ($NativeLibrary) {
        $NativeLibrary = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($NativeLibrary)
        if (-not (Test-Path -LiteralPath $NativeLibrary)) { throw "-NativeLibrary $NativeLibrary does not exist." }
    } else {
        $cmakeTool = Resolve-CMake $sdkPath
        $ninjaPath = Resolve-Ninja $sdkPath
        try {
            $dependencies = & $bootstrapScript -Check
        } catch {
            throw "Native dependencies are not ready: $($_.Exception.Message) Run: powershell -ExecutionPolicy Bypass -File scripts/port/bootstrap_android.ps1"
        }
        $luaPlusCore = Join-Path $repoRoot "dependencies\LuaPlus_Build1081\Src\LuaPlus\src\lapi.c"
        if (-not (Test-Path -LiteralPath $luaPlusCore)) {
            throw "dependencies\LuaPlus_Build1081 is missing (the vendored LuaPlus_Build1081.7z the desktop build uses, see dependencies/patches/luaplus_build1081_faf_required.md)."
        }
    }
}

$apkSuffix = if ($SkipNative) { "nonative" } else { $abi }
$apkName = "faf-re-android-$versionName-$apkSuffix.apk"
$finalApk = Join-Path $outputDirectory $apkName

Write-Host "faf-re Android build: $packageName $versionName (versionCode $versionCode), $Configuration, $(if ($SkipNative) { 'Java only' } else { $abi })"
Write-Host "  Android SDK  $sdkPath"
Write-Host "  build-tools  $buildToolsPath"
Write-Host "  JDK          $jdkPath (Java $(Get-JdkMajorVersion $jdkPath))"
Write-Host "  Python       $pythonPath"
if ($SkipNative) {
    Write-Host "  native       skipped"
} elseif ($NativeLibrary) {
    Write-Host "  NDK          $ndkPath"
    Write-Host "  native       prebuilt $NativeLibrary"
} else {
    Write-Host "  NDK          $ndkPath"
    Write-Host "  CMake        $($cmakeTool.Path) ($($cmakeTool.Version))"
    Write-Host "  Ninja        $ninjaPath"
    Write-Host "  DiligentCore $($dependencies.DiligentCore) @ $($dependencies.Revision)"
    Write-Host "  abseil-cpp   $(if ($dependencies.AbseilSource) { $dependencies.AbseilSource } else { 'not pre-fetched: configure downloads it' })"
}
if (-not $includeRunner) {
    Write-Host "  runner       skipped"
} elseif ($RunnerDirectory) {
    Write-Host "  runner       prebuilt $RunnerDirectory"
} else {
    Write-Host "  runner       build_runner.py --abi $abi --probe$(if ($buildDeviceProbe) { ' --deviceprobe' }) into $runnerBuildDirectory"
}
if ($includeRunner) {
    Write-Host "  runner -O2   $(if (-not $includeOptimized) { 'left out' } elseif ($RunnerO2Directory) { "prebuilt $RunnerO2Directory" } else { "build_runner.py --abi $abi --opt O2 into $runnerO2BuildDirectory" })"
    Write-Host "  deviceprobe  $(if (-not $includeDeviceProbe) { 'left out' } elseif ($buildDeviceProbe) { 'built with the runner (--deviceprobe)' } else { "prebuilt $($runnerSources[$deviceProbeName])" })"
}
Write-Host "  libraries    $(if ($extractNativeLibs) { 'deflated, extracted at install (extractNativeLibs=true)' } else { 'stored, mapped from the APK (extractNativeLibs=false)' })"
Write-Host "  refs         $(if (Test-Path -LiteralPath $replayRefsFile) { $replayRefsFile } else { 'none (port\android\assets\replay_refs.json not written yet)' })"
Write-Host "  menu trace   $(if ($menuTraceFile) { "$menuTraceFile ($('{0:N1}' -f ((Get-Item -LiteralPath $menuTraceFile).Length / 1MB)) MB)" } else { 'left out' })"
Write-Host "  keystore     $keystorePath$(if (-not (Test-Path -LiteralPath $keystorePath)) { ' (will be created)' })"
if ($DryRun) { Write-Host "Dry run: commands are printed, nothing is executed or written." -ForegroundColor Yellow }

# d8.bat and apksigner.bat start the JVM from JAVA_HOME; point it at the
# selected JDK for this run only.
$savedJavaHome = $env:JAVA_HOME
$env:JAVA_HOME = $jdkPath

try {
    if ($Clean) {
        Write-Step "Clean"
        Remove-PathIfPresent $apkBuildDirectory
        if (-not $SkipNative -and -not $NativeLibrary) { Remove-PathIfPresent $nativeBuildDirectory }
    }

    $stageDirectory = Join-Path $apkBuildDirectory "stage"
    Remove-PathIfPresent $stageDirectory
    New-Directory $apkBuildDirectory
    New-Directory $stageDirectory

    # --- 1. native ----------------------------------------------------------------

    $stagedLibrary = Join-Path $stageDirectory "lib\$abi\$nativeLibraryName"
    if ($SkipNative) {
        Write-Step "[1/6] Native library: skipped (-SkipNative)"
    } elseif ($NativeLibrary) {
        Write-Step "[1/6] Native library: prebuilt"
        $builtLibrary = $NativeLibrary
    } else {
        Write-Step "[1/6] Native library ($Configuration, $abi)"
        $toolchainFile = Join-Path $ndkPath "build\cmake\android.toolchain.cmake"
        $configureArguments = @(
            "-S", $nativeSourceDirectory,
            "-B", $nativeBuildDirectory,
            "-G", "Ninja",
            "-DCMAKE_MAKE_PROGRAM=$ninjaPath",
            "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile",
            "-DANDROID_ABI=$abi",
            "-DANDROID_PLATFORM=android-$minSdk",
            "-DANDROID_STL=c++_static",
            "-DCMAKE_BUILD_TYPE=$Configuration",
            "-DFAF_ANDROID_VERSION_NAME=$versionName"
        )
        if ($dependencies.AbseilSource) {
            # Diligent's only FetchContent dependency; with a local copy the
            # configure step needs no network at all.
            $configureArguments += @(
                "-DFETCHCONTENT_SOURCE_DIR_ABSEIL-CPP=$($dependencies.AbseilSource)",
                "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
            )
        }

        # Reconfigure only when the arguments changed; Ninja re-runs CMake by
        # itself when a CMakeLists.txt changes.
        $configureStamp = Join-Path $nativeBuildDirectory "faf-configure-arguments.txt"
        $stampText = ($configureArguments -join "`n") + "`n"
        $needsConfigure = -not (Test-Path -LiteralPath (Join-Path $nativeBuildDirectory "build.ninja")) -or
            -not (Test-Path -LiteralPath $configureStamp) -or
            ([IO.File]::ReadAllText($configureStamp) -ne $stampText)
        if ($needsConfigure -or $DryRun) {
            Request-CMakeCodeModel $nativeBuildDirectory
            Invoke-Tool $cmakeTool.Path $configureArguments -FailureMessage "Configuring port/android failed" | Out-Null
            if (-not $DryRun) { Write-Utf8File $configureStamp $stampText }
        } else {
            Write-Host "  configuration unchanged ($configureStamp)"
        }
        Invoke-Tool $cmakeTool.Path @("--build", $nativeBuildDirectory, "--target", $nativeTarget) -FailureMessage "Building $nativeTarget failed" | Out-Null

        $builtLibrary = if ($DryRun) { Join-Path $nativeBuildDirectory $nativeLibraryName } else {
            Get-CMakeTargetArtifact $nativeBuildDirectory $nativeTarget $nativeLibraryName
        }
        if (-not $DryRun -and -not (Test-Path -LiteralPath $builtLibrary)) {
            throw "The build did not produce $builtLibrary."
        }
    }

    if (-not $SkipNative) {
        # Keep the unstripped library next to the APK it belongs to; tombstones
        # and logcat backtraces are symbolized against it (ndk-stack -sym).
        $symbolsDirectory = Join-Path $apkBuildDirectory "symbols\$abi"
        $symbolsLibrary = Join-Path $symbolsDirectory $nativeLibraryName
        New-Directory $symbolsDirectory
        New-Directory (Split-Path -Parent $stagedLibrary)
        if (-not $DryRun) { Copy-Item -LiteralPath $builtLibrary -Destination $symbolsLibrary -Force }
        Invoke-Tool $llvmStrip @("--strip-unneeded", "-o", $stagedLibrary, $builtLibrary) -FailureMessage "Stripping $nativeLibraryName failed" | Out-Null

        # The checks below catch the link mistakes that only show on a device:
        # a missing entry point (NativeActivity cannot start), a dependency on
        # a library that is neither part of Android nor in the APK (the shared
        # C++ runtime, a shared Diligent build) and 4 KB segment alignment
        # (refused on 16 KB page devices).
        $dynamic = Invoke-Tool $llvmReadelf @("--dynamic-table", $stagedLibrary) -Capture
        $symbols = Invoke-Tool $llvmReadelf @("--dyn-symbols", "--wide", $stagedLibrary) -Capture
        $segments = Invoke-Tool $llvmReadelf @("--program-headers", "--wide", $stagedLibrary) -Capture
        $notes = Invoke-Tool $llvmReadelf @("--notes", $builtLibrary) -Capture
        if (-not $DryRun) {
            $needed = @($dynamic | ForEach-Object { if ($_ -match '\(NEEDED\)\s+Shared library: \[([^\]]+)\]') { $Matches[1] } })
            Write-Host "  NEEDED: $($needed -join ', ')"
            if ($needed -contains "libc++_shared.so") { throw "$nativeLibraryName depends on libc++_shared.so; it must link the C++ runtime statically (ANDROID_STL=c++_static)." }
            $foreign = @($needed | Where-Object { $systemLibraries -notcontains $_ })
            if ($foreign.Count -gt 0) {
                throw "$nativeLibraryName depends on $($foreign -join ', '), which Android does not provide at API $minSdk and the APK does not ship (link Diligent and every other dependency statically)."
            }
            $entryPoint = @($symbols | Where-Object { $_ -match '\sANativeActivity_onCreate$' -and $_ -notmatch '\sUND\s' -and $_ -match '\sGLOBAL\s' })
            if ($entryPoint.Count -eq 0) { throw "$nativeLibraryName does not export ANativeActivity_onCreate; NativeActivity cannot start it." }
            $loadAlignments = @($segments | Where-Object { $_ -match '^\s*LOAD\s' } | ForEach-Object { [Convert]::ToInt64((($_ -split '\s+') | Where-Object { $_ })[-1], 16) })
            $misaligned = @($loadAlignments | Where-Object { $_ -lt 0x4000 })
            if ($loadAlignments.Count -eq 0 -or $misaligned.Count -gt 0) {
                throw "$nativeLibraryName has LOAD segments aligned below 16 KB ($(($loadAlignments | ForEach-Object { '0x{0:x}' -f $_ }) -join ', ')); link with -Wl,-z,max-page-size=16384."
            }
            $machine = (Get-ElfFacts $stagedLibrary).Machine
            if ($machine -ne $elfMachines[$abi]) { throw "$nativeLibraryName is built for '$machine', not $abi." }
            $nativeBuildId = @($notes | ForEach-Object { if ($_ -match 'Build ID:\s*([0-9a-f]+)') { $Matches[1] } }) | Select-Object -First 1
            Write-Host ("  {0}: {1:N1} MB unstripped, {2:N1} MB stripped, build id {3}" -f $nativeLibraryName,
                ((Get-Item -LiteralPath $builtLibrary).Length / 1MB), ((Get-Item -LiteralPath $stagedLibrary).Length / 1MB), $nativeBuildId)
        }
    }

    # --- 2. headless replay runner ---------------------------------------------------------

    # APK entry name -> facts about the packaged (stripped) file, for build.json and the release stage.
    $runnerPackaged = [ordered]@{}
    if (-not $includeRunner) {
        Write-Step "[2/6] Replay runner: skipped ($(if ($SkipNative) { '-SkipNative' } else { '-SkipRunner' }))"
    } else {
        if ($RunnerDirectory) {
            Write-Step "[2/6] Replay runner: prebuilt ($RunnerDirectory)"
        } else {
            Write-Step "[2/6] Replay runner ($abi, build_runner.py --probe)"
        }
        if ($buildRunnerO0) {
            # build_runner.py seeds a new --out directory from buildstage\runner\<abi>,
            # so only what changed since the last runner build compiles here.
            $runnerArguments = @($buildRunnerScript, "--abi", $abi, "--out", $runnerBuildDirectory, "--probe", "--ndk", $ndkPath)
            if ($buildDeviceProbe) { $runnerArguments += "--deviceprobe" }
            Invoke-Tool $pythonPath $runnerArguments -FailureMessage "build_runner.py failed (report: $runnerBuildDirectory\report.md)" | Out-Null
        } elseif ($buildDeviceProbe) {
            Invoke-Tool $pythonPath @($buildRunnerScript, "--abi", $abi, "--out", $runnerBuildDirectory, "--only-deviceprobe", "--ndk", $ndkPath) `
                -FailureMessage "build_runner.py --only-deviceprobe failed" | Out-Null
        }
        if ($buildRunnerO2) {
            # Seeds from buildstage\runner\<abi>-O2 when that exists; every engine unit differs from -O0 by its flags.
            Invoke-Tool $pythonPath @($buildRunnerScript, "--abi", $abi, "--opt", "O2", "--out", $runnerO2BuildDirectory, "--ndk", $ndkPath) `
                -FailureMessage "build_runner.py --opt O2 failed (report: $runnerO2BuildDirectory\report.md)" | Out-Null
        }
        foreach ($entryName in $runnerSources.Keys) {
            $built = $runnerSources[$entryName]
            $staged = Join-Path $stageDirectory "lib\$abi\$entryName"
            if (-not $DryRun -and -not (Test-Path -LiteralPath $built)) { throw "The runner build did not produce $built." }
            New-Directory (Split-Path -Parent $staged)
            # The unstripped files are the symbols for a crash line from this APK
            # ("[runner] CRASH ... build id ..."); kept under the APK's name.
            if (-not $DryRun) { Copy-Item -LiteralPath $built -Destination (Join-Path $symbolsDirectory $entryName) -Force }
            Invoke-Tool $llvmStrip @("--strip-unneeded", "-o", $staged, $built) -FailureMessage "Stripping $entryName failed" | Out-Null
            if (-not $DryRun) {
                $runnerBuildId = Test-RunnerElf $entryName $staged $built
                $runnerPackaged[$entryName] = [ordered]@{
                    source = [IO.Path]::GetFileName($built)
                    buildId = $runnerBuildId
                    sha256 = (Get-Sha256 $staged)
                    size = (Get-Item -LiteralPath $staged).Length
                    unstrippedSha256 = (Get-Sha256 $built)
                }
            }
        }
        if (-not $DryRun) {
            Write-Host ("  stripped: {0}" -f (($runnerPackaged.Keys | ForEach-Object { "{0} {1:N1} MB" -f $_, ($runnerPackaged[$_].size / 1MB) }) -join ', '))
            # G10: a chain is only comparable between identical binaries, so a Release
            # ships the engine and runner the reference table was measured with.
            $referenceIds = @(Get-ReferenceBuildIds $replayRefsFile)
            $unreferenced = New-Object System.Collections.Generic.List[string]
            foreach ($pair in $runnerPairs) {
                foreach ($entryName in $pair) {
                    if (-not $runnerPackaged.Contains($entryName)) { continue }
                    $known = $referenceIds -contains $runnerPackaged[$entryName].buildId
                    if (-not $known) { $unreferenced.Add("$entryName $($runnerPackaged[$entryName].buildId)") }
                    Write-Host ("  {0} build id {1} the reference table" -f $entryName, $(if ($known) { "is in" } else { "is NOT in" })) `
                        -ForegroundColor $(if ($known) { "Gray" } else { "Yellow" })
                }
            }
            if ($unreferenced.Count -gt 0 -and -not $isDebug) {
                $refsState = if (Test-Path -LiteralPath $replayRefsFile) { $replayRefsFile } else { "$replayRefsFile (missing)" }
                $why = "the build ids of $($unreferenced -join ', ') are not in $refsState"
                if ($AllowUnreferencedRunner) {
                    Write-Host "  -AllowUnreferencedRunner: packaging anyway; $why" -ForegroundColor Yellow
                } else {
                    throw ("Release build refused: $why. Package the runner set the references were measured with " +
                        "(-RunnerDirectory), or regenerate the table (run_runner_android.py --host-prefs none --write-refs); " +
                        "-AllowUnreferencedRunner overrides this for development builds.")
                }
            }
        }
    }

    # --- 3. resources and manifest -----------------------------------------------------

    Write-Step "[3/6] Resources and manifest"
    $compiledResources = Join-Path $apkBuildDirectory "resources.zip"
    $generatedSources = Join-Path $apkBuildDirectory "gen"
    $linkedApk = Join-Path $apkBuildDirectory "linked.apk"
    Remove-PathIfPresent $generatedSources
    Remove-PathIfPresent $compiledResources
    New-Directory $generatedSources
    $hasResources = (Test-Path -LiteralPath $resourceDirectory) -and
        @(Get-ChildItem -LiteralPath $resourceDirectory -Recurse -File).Count -gt 0
    if ($hasResources) {
        Invoke-Tool $aapt2 @("compile", "--dir", $resourceDirectory, "-o", $compiledResources) -FailureMessage "aapt2 compile failed" | Out-Null
    }
    $linkArguments = @(
        "link",
        "-o", $linkedApk,
        "--manifest", $manifestFile,
        "-I", $androidJar,
        "--java", $generatedSources,
        "--auto-add-overlay",
        "--min-sdk-version", "$minSdk",
        "--target-sdk-version", "$targetSdk",
        "--version-code", "$versionCode",
        "--version-name", $versionName
    )
    if ($hasResources) { $linkArguments += @("-R", $compiledResources) }
    if ($isDebug) { $linkArguments += "--debug-mode" }
    Invoke-Tool $aapt2 $linkArguments -FailureMessage "aapt2 link failed" | Out-Null

    # --- 4. Java ----------------------------------------------------------------------

    Write-Step "[4/6] Java launcher"
    $classesDirectory = Join-Path $apkBuildDirectory "classes"
    $dexDirectory = Join-Path $apkBuildDirectory "dex"
    $classesJar = Join-Path $apkBuildDirectory "classes.jar"
    Remove-PathIfPresent $classesDirectory
    Remove-PathIfPresent $dexDirectory
    New-Directory $classesDirectory
    New-Directory $dexDirectory

    $javaSources = @(Get-ChildItem -LiteralPath $javaSourceDirectory -Recurse -Filter "*.java" | ForEach-Object { $_.FullName })
    if ($javaSources.Count -eq 0) { throw "No Java sources in $javaSourceDirectory." }
    $sourceList = Join-Path $apkBuildDirectory "java-sources.txt"
    if (-not $DryRun) {
        $javaSources += @(Get-ChildItem -LiteralPath $generatedSources -Recurse -Filter "*.java" | ForEach-Object { $_.FullName })
        # javac argument file: quoted, forward slashes (a backslash escapes in quotes).
        Write-Utf8File $sourceList ((($javaSources | ForEach-Object { '"' + ($_ -replace '\\', '/') + '"' }) -join "`n") + "`n")
    }
    Write-Host "  $($javaSources.Count) source files"
    $javacArguments = @(
        "-encoding", "UTF-8",
        "-source", "8", "-target", "8",
        # Java 8 bytecode compiled against the Android API, not the JDK's
        # class library; lambda stubs for invokedynamic, which d8 desugars.
        "-bootclasspath", "$androidJar;$lambdaStubs",
        "-Xlint:-options",
        "-d", $classesDirectory
    )
    $debugInfo = if ($isDebug) { "-g" } else { "-g:source,lines" }
    $javacArguments += @($debugInfo, "@$sourceList")
    Invoke-Tool $javac $javacArguments -FailureMessage "javac failed" | Out-Null
    Invoke-Tool $jar @("--create", "--file", $classesJar, "--no-manifest", "-C", $classesDirectory, ".") -FailureMessage "jar failed" | Out-Null
    $d8Mode = if ($isDebug) { "--debug" } else { "--release" }
    Invoke-Tool $d8 @($d8Mode, "--min-api", "$minSdk", "--lib", $androidJar, "--output", $dexDirectory, $classesJar) -FailureMessage "d8 failed" | Out-Null
    if (-not $DryRun -and -not (Test-Path -LiteralPath (Join-Path $dexDirectory "classes.dex"))) {
        throw "d8 did not produce classes.dex."
    }
    if (-not $DryRun -and (Test-Path -LiteralPath (Join-Path $dexDirectory "classes2.dex"))) {
        throw "d8 produced more than one dex file; the packaging step only adds classes.dex."
    }

    # --- 5. package, align, sign ----------------------------------------------------------

    Write-Step "[5/6] Package, align, sign"
    $helperScript = Join-Path $apkBuildDirectory "apk_entries.py"
    $unsignedApk = Join-Path $apkBuildDirectory "unsigned.apk"
    $alignedApk = Join-Path $apkBuildDirectory "aligned.apk"
    $signedApk = Join-Path $apkBuildDirectory $apkName
    foreach ($stale in @($unsignedApk, $alignedApk, $signedApk)) { Remove-PathIfPresent $stale }
    if (-not $DryRun) {
        Write-Utf8File $helperScript $apkEntriesPy
        Copy-Item -LiteralPath $linkedApk -Destination $unsignedApk
    }

    # build.json: what the launcher records in every replay test's meta.json
    # (version, commit, the packaged binaries' sha256 and build ids).
    $commit = @(Get-GitOutput @("rev-parse", "HEAD")) | Select-Object -First 1
    $dirty = @(Get-GitOutput @("status", "--porcelain", "--untracked-files=no")).Count -gt 0
    $buildInfo = [ordered]@{
        schema = 1
        package = $packageName
        versionName = $versionName
        versionCode = $versionCode
        commit = $(if ($commit) { $commit.Trim() } else { "" })
        dirty = $dirty
        abi = $(if ($SkipNative) { "" } else { $abi })
        configuration = $Configuration
        extractNativeLibs = $extractNativeLibs
        libraries = [ordered]@{}
    }
    if (-not $SkipNative -and -not $DryRun) {
        $buildInfo.libraries[$nativeLibraryName] = [ordered]@{ buildId = $nativeBuildId; sha256 = (Get-Sha256 $stagedLibrary); size = (Get-Item -LiteralPath $stagedLibrary).Length }
    }
    foreach ($entryName in $runnerPackaged.Keys) {
        $buildInfo.libraries[$entryName] = [ordered]@{ buildId = $runnerPackaged[$entryName].buildId; sha256 = $runnerPackaged[$entryName].sha256; size = $runnerPackaged[$entryName].size }
    }
    $buildInfoFile = Join-Path $apkBuildDirectory "build.json"
    if (-not $DryRun) { Write-Utf8File $buildInfoFile (($buildInfo | ConvertTo-Json -Depth 6) + "`n") }

    $storedEntries = [ordered]@{ "classes.dex" = (Join-Path $dexDirectory "classes.dex") }
    $storedEntries["assets/gamedata.json"] = $gameDataManifest
    $storedEntries["assets/build.json"] = $buildInfoFile
    if (Test-Path -LiteralPath $replayRefsFile) {
        $storedEntries["assets/replay_refs.json"] = $replayRefsFile
    } elseif ($includeRunner) {
        Write-Host "  no replay reference table yet ($replayRefsFile): the replay test shows no reference chain" -ForegroundColor Yellow
    }
    # The menu trace: checked (a complete version 2 trace with game-file references, so no game data goes into
    # the APK) and described in menu.json (stored); the trace itself deflated.
    $deflatedAssets = [ordered]@{}
    $menuTraceInfo = $null
    $menuTraceCompressed = $null
    if ($menuTraceFile) {
        $galtraceInfoScript = Join-Path $apkBuildDirectory "galtrace_info.py"
        $menuTraceInfoFile = Join-Path $apkBuildDirectory "menu.json"
        if (-not $DryRun) {
            Write-Utf8File $galtraceInfoScript $galtraceInfoPy
            Remove-PathIfPresent $menuTraceInfoFile
        }
        # A Release build also needs the PC's Vulkan reference hashes, a header without home directories, and a
        # native library that has the menu replay mode (else GameActivity would start the game instead).
        $galtraceInfoArguments = @($galtraceInfoScript, $menuTraceFile, $menuTraceInfoFile)
        if (-not $isDebug) { $galtraceInfoArguments += "--release" }
        if (-not $SkipNative) { $galtraceInfoArguments += @("--native", $stagedLibrary) }
        Invoke-Tool $pythonPath $galtraceInfoArguments `
            -FailureMessage "The menu trace $menuTraceFile cannot go into the APK" | Out-Null
        $storedEntries[$menuTraceInfoEntry] = $menuTraceInfoFile
        $deflatedAssets[$menuTraceEntry] = $menuTraceFile
        if (-not $DryRun) {
            $menuTraceInfo = Get-Content -LiteralPath $menuTraceInfoFile -Raw | ConvertFrom-Json
            $buildInfo["menuTrace"] = [ordered]@{
                name = $menuTraceInfo.name; size = $menuTraceInfo.size; sha256 = $menuTraceInfo.sha256
                version = $menuTraceInfo.version; archives = @($menuTraceInfo.archives)
                readbackFrames = @($menuTraceInfo.readback_frames)
            }
            Write-Utf8File $buildInfoFile (($buildInfo | ConvertTo-Json -Depth 6) + "`n")
        }
    }
    $missingNotices = @()
    foreach ($notice in $licenseNotices.Keys) {
        if (Test-Path -LiteralPath $licenseNotices[$notice]) {
            $storedEntries[$notice] = $licenseNotices[$notice]
        } elseif (-not $SkipNative) {
            $missingNotices += $licenseNotices[$notice]
            Write-Host "  license notice missing: $($licenseNotices[$notice])" -ForegroundColor Yellow
        }
    }
    if ($missingNotices.Count -gt 0 -and -not $isDebug -and -not $DryRun) {
        throw "A Release build ships the license texts of the third-party code in its libraries; missing: $($missingNotices -join ', ')."
    }
    # Libraries: deflated when the installer extracts them (smaller download),
    # stored and page-aligned when Android maps them straight out of the APK.
    $libraryEntries = [ordered]@{}
    if (-not $SkipNative) { $libraryEntries["lib/$abi/$nativeLibraryName"] = $stagedLibrary }
    foreach ($entryName in $runnerSources.Keys) {
        $libraryEntries["lib/$abi/$entryName"] = Join-Path $stageDirectory "lib\$abi\$entryName"
    }
    if (-not $extractNativeLibs) {
        foreach ($entry in $libraryEntries.Keys) { $storedEntries[$entry] = $libraryEntries[$entry] }
    }
    $addArguments = @($helperScript, "add", $unsignedApk) + @($storedEntries.Keys | ForEach-Object { "$_=$($storedEntries[$_])" })
    Invoke-Tool $pythonPath $addArguments -FailureMessage "Adding entries to the APK failed" | Out-Null
    if ($extractNativeLibs -and $libraryEntries.Count -gt 0) {
        $deflateArguments = @($helperScript, "add-deflated", $unsignedApk) + @($libraryEntries.Keys | ForEach-Object { "$_=$($libraryEntries[$_])" })
        Invoke-Tool $pythonPath $deflateArguments -FailureMessage "Adding the libraries to the APK failed" | Out-Null
    }
    if ($deflatedAssets.Count -gt 0) {
        $deflateArguments = @($helperScript, "add-deflated", $unsignedApk) + @($deflatedAssets.Keys | ForEach-Object { "$_=$($deflatedAssets[$_])" })
        Invoke-Tool $pythonPath $deflateArguments -FailureMessage "Adding the menu trace to the APK failed" | Out-Null
    }

    # -P 16: page-align stored libraries for 16 KB page devices (Android 15+);
    # 4: every other uncompressed entry on 32-bit boundaries.
    Invoke-Tool $zipalign @("-f", "-P", "16", "4", $unsignedApk, $alignedApk) -FailureMessage "zipalign failed" | Out-Null

    if (-not (Test-Path -LiteralPath $keystorePath)) {
        Write-Host "  creating signing key $keystorePath (keep it: updates must be signed with the same key)" -ForegroundColor Yellow
        New-Directory (Split-Path -Parent $keystorePath)
        Invoke-Tool $keytool @(
            "-genkeypair", "-keystore", $keystorePath, "-storetype", "PKCS12",
            "-storepass", $keyPassword, "-keypass", $keyPassword, "-alias", $keyAlias,
            "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
            "-dname", "CN=Android Debug,O=Android,C=US"
        ) -Capture -FailureMessage "Creating the signing key failed" | Out-Null
    }
    # v4 signing would add an .idsig file next to the APK; one file is the contract.
    Invoke-Tool $apksigner @(
        "sign", "--ks", $keystorePath, "--ks-key-alias", $keyAlias,
        "--ks-pass", "pass:$keyPassword", "--key-pass", "pass:$keyPassword",
        "--v4-signing-enabled", "false",
        "--out", $signedApk, $alignedApk
    ) -FailureMessage "apksigner sign failed" | Out-Null

    # --- 6. verify --------------------------------------------------------------------------

    Write-Step "[6/6] Verify"
    $verifyOutput = Invoke-Tool $apksigner @("verify", "-v", "--print-certs", $signedApk) -Capture -FailureMessage "apksigner verify failed"
    $alignOutput = Invoke-Tool $zipalign @("-c", "-P", "16", "-v", "4", $signedApk) -Capture -FailureMessage "zipalign -c failed"
    $badging = Invoke-Tool $aapt2 @("dump", "badging", $signedApk) -Capture -FailureMessage "aapt2 dump badging failed"
    $storedCheck = Invoke-Tool $pythonPath (@($helperScript, "check-stored", $signedApk, "resources.arsc") + @($storedEntries.Keys)) -Capture -FailureMessage "Uncompressed entry check failed"
    $deflatedCheck = @()
    $deflatedNames = @($deflatedAssets.Keys)
    if ($extractNativeLibs) { $deflatedNames += @($libraryEntries.Keys) }
    if ($deflatedNames.Count -gt 0) {
        $deflatedCheck = Invoke-Tool $pythonPath (@($helperScript, "check-deflated", $signedApk) + $deflatedNames) -Capture -FailureMessage "Compressed entry check failed"
    }
    $manifestTree = Invoke-Tool $aapt2 @("dump", "xmltree", "--file", "AndroidManifest.xml", $signedApk) -Capture -FailureMessage "aapt2 dump xmltree failed"

    $certificateDigest = $null
    if (-not $DryRun) {
        if (-not ($verifyOutput -match '^Verifies$')) { throw "apksigner did not verify the APK:`n$($verifyOutput -join "`n")" }
        $certificateDigest = @($verifyOutput | ForEach-Object { if ($_ -match 'certificate SHA-256 digest:\s*([0-9a-f]+)') { $Matches[1] } }) | Select-Object -First 1
        if (-not ($alignOutput -match 'Verification succes')) { throw "zipalign -c did not confirm the alignment:`n$($alignOutput -join "`n")" }
        $storedCheck | ForEach-Object { Write-Host "  $_" }
        $deflatedCheck | ForEach-Object { Write-Host "  $_" }
        $menuTraceCompressed = @($deflatedCheck | ForEach-Object {
            if ($_ -match "^deflated: $([regex]::Escape($menuTraceEntry)) \(\d+ bytes, (\d+) compressed\)") { [int64]$Matches[1] } }) |
            Select-Object -First 1
        # The packaging above follows the source manifest; the compiled one must agree.
        $extractLine = @($manifestTree | Where-Object { $_ -match 'extractNativeLibs\(0x[0-9a-f]+\)=' }) | Select-Object -First 1
        $compiledExtract = if ($extractLine -match '=(true|false|0xffffffff|0x0)\b') { $Matches[1] -eq "true" -or $Matches[1] -eq "0xffffffff" } else { $null }
        if ($compiledExtract -ne $extractNativeLibs) {
            throw "The APK's compiled manifest has extractNativeLibs '$extractLine', but the libraries were packaged for extractNativeLibs=$extractNativeLibs."
        }
        Write-Host "  extractNativeLibs=$($compiledExtract.ToString().ToLowerInvariant()) in the compiled manifest"

        $badgingProblems = New-Object System.Collections.Generic.List[string]
        $packageLine = @($badging | Where-Object { $_ -like "package:*" }) | Select-Object -First 1
        if ($packageLine -notmatch "name='$([regex]::Escape($packageName))'") { $badgingProblems.Add("package is not $packageName") }
        if ($packageLine -notmatch "versionCode='$versionCode'") { $badgingProblems.Add("versionCode is not $versionCode") }
        if ($packageLine -notmatch "versionName='$([regex]::Escape($versionName))'") { $badgingProblems.Add("versionName is not $versionName") }
        if (-not ($badging -match "^(min)?[sS]dkVersion:'$minSdk'$")) { $badgingProblems.Add("minSdkVersion is not $minSdk") }
        if (-not ($badging -match "^targetSdkVersion:'$targetSdk'$")) { $badgingProblems.Add("targetSdkVersion is not $targetSdk") }
        $launchable = @($badging | ForEach-Object { if ($_ -match "^launchable-activity: name='([^']+)'") { $Matches[1] } }) | Select-Object -First 1
        if (-not $launchable) { $badgingProblems.Add("no launchable activity") }
        $nativeCode = @($badging | Where-Object { $_ -like "native-code:*" }) | Select-Object -First 1
        if ($SkipNative) {
            if ($nativeCode) { $badgingProblems.Add("unexpected native code: $nativeCode") }
        } elseif ($nativeCode -ne "native-code: '$abi'") {
            $badgingProblems.Add("native code is '$nativeCode', expected native-code: '$abi'")
        }
        $debuggable = [bool]($badging -match '^application-debuggable$')
        if ($debuggable -ne $isDebug) { $badgingProblems.Add("debuggable is $debuggable for a $Configuration build") }
        if ($badgingProblems.Count -gt 0) {
            Write-Host ($badging -join "`n")
            throw "aapt2 dump badging: $($badgingProblems -join '; ')."
        }
        Write-Host "  $packageLine"
        Write-Host "  launchable-activity: $launchable$(if ($nativeCode) { "; $nativeCode" })"
    }

    New-Directory $outputDirectory
    Remove-PathIfPresent $finalApk
    if (-not $DryRun) { Copy-Item -LiteralPath $signedApk -Destination $finalApk }

    # --- release stage ------------------------------------------------------------------
    # Crash lines and tombstones from a released APK are symbolized against the
    # unstripped libraries of exactly that build; keep them with the version.
    if ($stageRelease) {
        Write-Step "Release stage: $releaseDirectory"
        New-Directory $releaseDirectory
        $staged = New-Object System.Collections.Generic.List[string]
        if (-not $DryRun) {
            if ($abi -eq "arm64-v8a") {
                Copy-Item -LiteralPath $finalApk -Destination (Join-Path $releaseDirectory $apkName) -Force
                $staged.Add($apkName)
            }
            foreach ($library in @($nativeLibraryName) + @($runnerPackaged.Keys)) {
                $source = Join-Path $symbolsDirectory $library
                $target = "{0}-{1}.so" -f [IO.Path]::GetFileNameWithoutExtension($library), $abi
                Copy-Item -LiteralPath $source -Destination (Join-Path $releaseDirectory $target) -Force
                $staged.Add($target)
            }
            $releaseInfo = [ordered]@{}
            foreach ($key in $buildInfo.Keys) { $releaseInfo[$key] = $buildInfo[$key] }
            $releaseInfo["apk"] = [ordered]@{ name = $apkName; sha256 = (Get-Sha256 $finalApk); size = (Get-Item -LiteralPath $finalApk).Length; certificateSha256 = $certificateDigest }
            $releaseInfo["symbols"] = [ordered]@{}
            foreach ($library in @($nativeLibraryName) + @($runnerPackaged.Keys)) {
                $releaseInfo.symbols[$library] = "{0}-{1}.so" -f [IO.Path]::GetFileNameWithoutExtension($library), $abi
            }
            $releaseInfoName = "build-$abi.json"
            Write-Utf8File (Join-Path $releaseDirectory $releaseInfoName) (($releaseInfo | ConvertTo-Json -Depth 6) + "`n")
            $staged.Add($releaseInfoName)
            Write-Host "  $($staged -join ', ')"
        }
    }
} finally {
    $env:JAVA_HOME = $savedJavaHome
}

if ($DryRun) {
    Write-Host ""
    Write-Host "Dry run finished; the APK would be $finalApk"
    return
}

Write-Host ""
Write-Host "APK:      $finalApk" -ForegroundColor Green
Write-Host ("Size:     {0:N1} MB ({1:N0} bytes)" -f ((Get-Item -LiteralPath $finalApk).Length / 1MB), (Get-Item -LiteralPath $finalApk).Length)
Write-Host "Version:  $versionName ($versionCode), $Configuration"
Write-Host "Cert:     SHA-256 $certificateDigest"
Write-Host "APK hash: SHA-256 $(Get-Sha256 $finalApk)"
Write-Host "Key:      $keystorePath"
if (-not $SkipNative) { Write-Host "Symbols:  $symbolsDirectory" }
if ($menuTraceInfo) {
    Write-Host ("Trace:    {0} v{1}, {2:N1} MB ({3} in the APK), sha256 {4}; references into {5}" -f $menuTraceInfo.name, $menuTraceInfo.version,
        ($menuTraceInfo.size / 1MB), $(if ($menuTraceCompressed) { "{0:N1} MB" -f ($menuTraceCompressed / 1MB) } else { "?" }),
        $menuTraceInfo.sha256, $(if (@($menuTraceInfo.archives).Count -gt 0) { @($menuTraceInfo.archives) -join ', ' } else { 'no archive' }))
    foreach ($warning in @($menuTraceInfo.warnings)) { if ($warning) { Write-Host "          warning: $warning" -ForegroundColor Yellow } }
} else {
    Write-Host "Trace:    none (Menu replay disabled)"
}
foreach ($library in $buildInfo.libraries.Keys) {
    Write-Host ("Build id: {0,-22} {1}" -f $library, $buildInfo.libraries[$library].buildId)
}
if ($stageRelease) { Write-Host "Release:  $releaseDirectory" }
Write-Host "Install and copy game data: powershell -ExecutionPolicy Bypass -File scripts/port/deploy_android.ps1"

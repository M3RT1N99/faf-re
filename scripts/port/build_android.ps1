<#
.SYNOPSIS
Builds the Android APK: native runtime, Java launcher, resources and the game
data manifest in one signed file.

.DESCRIPTION
Produces output\android\faf-re-android-<versionName>-arm64-v8a.apk.

Steps:
  1. native   CMake + Ninja + NDK build of port\android (target faf_android) in
              buildstage\android-native; the unstripped library is kept in
              buildstage\android-apk\symbols for symbolization, a stripped copy
              goes into the APK.
  2. link     aapt2 compile + link (manifest, resources, version, R.java).
  3. java     javac (Java 8 bytecode against android.jar) + d8.
  4. package  classes.dex, lib\arm64-v8a\libfaf_android.so and
              assets\gamedata.json added STORED (uncompressed: the manifest sets
              extractNativeLibs=false, so the library is mapped straight out of
              the APK), zipalign with 16 KB pages, apksigner with one stable key.
  5. verify   apksigner verify, zipalign -c, aapt2 dump badging, ELF checks.

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
The game activity cannot start from it.

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
#>
[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [ValidateSet("arm64-v8a", "x86_64")]
    [string]$Abi = "arm64-v8a",
    [switch]$SkipNative,
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
# write STORED entries, and the library must be stored to be mapped in place.
$apkEntriesPy = @'
"""Adds files to an APK as STORED entries, or checks that entries are STORED.

Written by scripts/port/build_android.ps1 on every build; edit it there.

usage: apk_entries.py add <apk> <entry>=<file>...
       apk_entries.py check-stored <apk> <entry>...
"""
import sys
import zipfile

# Fixed timestamp, as aapt2 does, so equal inputs give equal APKs.
ENTRY_TIME = (1981, 1, 1, 1, 1, 2)


def add(apk, pairs):
    with zipfile.ZipFile(apk, "a") as archive:
        existing = set(archive.namelist())
        for pair in pairs:
            name, path = pair.split("=", 1)
            if name in existing:
                sys.exit(f"{apk} already contains {name}")
            info = zipfile.ZipInfo(name, date_time=ENTRY_TIME)
            info.compress_type = zipfile.ZIP_STORED
            info.external_attr = 0o100644 << 16
            with open(path, "rb") as source:
                archive.writestr(info, source.read())


def check_stored(apk, names):
    failed = False
    with zipfile.ZipFile(apk) as archive:
        for name in names:
            try:
                info = archive.getinfo(name)
            except KeyError:
                print(f"missing: {name}")
                failed = True
                continue
            stored = info.compress_type == zipfile.ZIP_STORED
            print(f"{'stored' if stored else 'COMPRESSED'}: {name} ({info.file_size} bytes)")
            failed |= not stored
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    if len(sys.argv) < 4 or sys.argv[1] not in ("add", "check-stored"):
        sys.exit(__doc__)
    if sys.argv[1] == "add":
        add(sys.argv[2], sys.argv[3:])
    else:
        check_stored(sys.argv[2], sys.argv[3:])
'@

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
    # APK, and the APK ships exactly one library.
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
        Write-Step "[1/5] Native library: skipped (-SkipNative)"
    } elseif ($NativeLibrary) {
        Write-Step "[1/5] Native library: prebuilt"
        $builtLibrary = $NativeLibrary
    } else {
        Write-Step "[1/5] Native library ($Configuration, $abi)"
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
            $buildId = @($notes | ForEach-Object { if ($_ -match 'Build ID:\s*([0-9a-f]+)') { $Matches[1] } }) | Select-Object -First 1
            Write-Host ("  {0}: {1:N1} MB unstripped, {2:N1} MB stripped, build id {3}" -f $nativeLibraryName,
                ((Get-Item -LiteralPath $builtLibrary).Length / 1MB), ((Get-Item -LiteralPath $stagedLibrary).Length / 1MB), $buildId)
        }
    }

    # --- 2. resources and manifest -----------------------------------------------------

    Write-Step "[2/5] Resources and manifest"
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

    # --- 3. Java ----------------------------------------------------------------------

    Write-Step "[3/5] Java launcher"
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

    # --- 4. package, align, sign ----------------------------------------------------------

    Write-Step "[4/5] Package, align, sign"
    $helperScript = Join-Path $apkBuildDirectory "apk_entries.py"
    $unsignedApk = Join-Path $apkBuildDirectory "unsigned.apk"
    $alignedApk = Join-Path $apkBuildDirectory "aligned.apk"
    $signedApk = Join-Path $apkBuildDirectory $apkName
    foreach ($stale in @($unsignedApk, $alignedApk, $signedApk)) { Remove-PathIfPresent $stale }
    if (-not $DryRun) {
        Write-Utf8File $helperScript $apkEntriesPy
        Copy-Item -LiteralPath $linkedApk -Destination $unsignedApk
    }

    $storedEntries = [ordered]@{ "classes.dex" = (Join-Path $dexDirectory "classes.dex") }
    if (-not $SkipNative) { $storedEntries["lib/$abi/$nativeLibraryName"] = $stagedLibrary }
    $storedEntries["assets/gamedata.json"] = $gameDataManifest
    $addArguments = @($helperScript, "add", $unsignedApk) + @($storedEntries.Keys | ForEach-Object { "$_=$($storedEntries[$_])" })
    Invoke-Tool $pythonPath $addArguments -FailureMessage "Adding entries to the APK failed" | Out-Null

    # -P 16: page-align the stored library for 16 KB page devices (Android 15+);
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

    # --- 5. verify --------------------------------------------------------------------------

    Write-Step "[5/5] Verify"
    $verifyOutput = Invoke-Tool $apksigner @("verify", "-v", "--print-certs", $signedApk) -Capture -FailureMessage "apksigner verify failed"
    $alignOutput = Invoke-Tool $zipalign @("-c", "-P", "16", "-v", "4", $signedApk) -Capture -FailureMessage "zipalign -c failed"
    $badging = Invoke-Tool $aapt2 @("dump", "badging", $signedApk) -Capture -FailureMessage "aapt2 dump badging failed"
    $storedCheck = Invoke-Tool $pythonPath (@($helperScript, "check-stored", $signedApk, "resources.arsc") + @($storedEntries.Keys)) -Capture -FailureMessage "Uncompressed entry check failed"

    $certificateDigest = $null
    if (-not $DryRun) {
        if (-not ($verifyOutput -match '^Verifies$')) { throw "apksigner did not verify the APK:`n$($verifyOutput -join "`n")" }
        $certificateDigest = @($verifyOutput | ForEach-Object { if ($_ -match 'certificate SHA-256 digest:\s*([0-9a-f]+)') { $Matches[1] } }) | Select-Object -First 1
        if (-not ($alignOutput -match 'Verification succes')) { throw "zipalign -c did not confirm the alignment:`n$($alignOutput -join "`n")" }
        $storedCheck | ForEach-Object { Write-Host "  $_" }

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
Write-Host "Key:      $keystorePath"
if (-not $SkipNative) { Write-Host "Symbols:  $symbolsLibrary" }
Write-Host "Install and copy game data: powershell -ExecutionPolicy Bypass -File scripts/port/deploy_android.ps1"

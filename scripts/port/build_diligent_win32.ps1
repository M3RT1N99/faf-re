<#
.SYNOPSIS
Builds the pinned DiligentCore for the opt-in graphics build of main.exe
(FafPortGraphics, port\graphics\port_graphics.props): Win32, Debug and Release,
static libraries plus the engine DLLs, installed to one prefix.

.DESCRIPTION
The recipe of the Win32 Diligent build that main.exe's graphics option links.
It is the same CMake configuration as the build already present in
dependencies\DiligentCore\install\Win32 (made 2026-09-23 by
scripts/port/bootstrap_diligent.py on the feature/diligent-renderer branch,
which is not in this repository), with two changes so that it runs with no
network access:

  - abseil-cpp. Diligent's ThirdParty\abseil-cpp FetchContent-declares the
    Chromium mirror at 07d2ef8b. dependencies\abseil-cpp holds that commit
    (port\android\CMakeLists.txt:102-115 uses it the same way), so it is passed
    as FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP. Only dawn (WebGPU, off here) links it.
  - NVAPI. Graphics\GraphicsEngineD3DBase\CMakeLists.txt:16-31 clones
    github.com/NVIDIA/nvapi whenever DILIGENT_NVAPI_PATH is unset on x86/x64,
    and :103-113 then compiles the D3D11/D3D12 engines with
    DILIGENT_ENABLE_D3D_NVAPI. The existing install was built that way: its
    DiligentCore.lib references NvAPI_Initialize, NvAPI_Unload and eight
    NvAPI_D3D11/D3D12_* functions, but nvapi.lib is not installed, so a static
    consumer cannot resolve them. Here FETCHCONTENT_SOURCE_DIR_NVAPI points at
    an empty directory, nvapi.h is absent and NVAPI stays off (Diligent then
    simply never reports the NVIDIA-only multi-draw-indirect and tiled-resource
    extensions; NVApiLoader.hpp is a no-op). -NvapiPath gives a local NVAPI
    checkout instead (it must contain nvapi.h and x86\nvapi.lib).

FETCHCONTENT_FULLY_DISCONNECTED=ON guarantees no other download is attempted.

Checked against the existing install (M6a; library by library with dumpbin: members, linker
directives, external symbols, section sizes). Same 46 files in lib\ and bin\. Debug DiligentCore.lib:
the same 245 members, identical /FAILIFMISMATCH and /DEFAULTLIB directives in all of them
(_ITERATOR_DEBUG_LEVEL=0, RuntimeLibrary=MDd_DynamicDebug, _MSC_VER=1900), identical external
symbols in 234 (the 11 others: the NVAPI imports and the log-message instantiations next to them in
the D3D11/D3D12 engines, and three names that hash the source path), identical .text sizes in 235. The
third-party Debug libraries match the same way. Release libraries are /GL (LTCG) objects, so only
member lists and directives compare (identical); the D3D11 and D3D12 DLLs are smaller by the NVAPI
code. The remaining byte differences are timestamps, PDB records and the absolute source paths in
__FILE__ strings (the old build ran in another checkout).

Everything else matches bootstrap_diligent.py: Visual Studio 2022 generator,
-A Win32, DILIGENT_BUILD_TESTS/ARCHIVER/SUPER_RESOLUTION/METAL/WEBGPU off,
DILIGENT_INSTALL_PDB on, and CMake's MSVC flags plus /D_ITERATOR_DEBUG_LEVEL=0
in every configuration. Debug therefore compiles /MDd with
_ITERATOR_DEBUG_LEVEL=0, which is what main.vcxproj's Debug|Win32 ClCompile
uses (the IDL comment there: a mismatch fails the link with LNK2038). Release
is /MD with the default level 0.

The CMake build tree is buildstage\diligent\build\<Platform>, the default
install prefix buildstage\diligent\install\<Platform>, which is what
port_graphics.props links. Both are under the gitignored buildstage.

The script lowers its own priority to BelowNormal before it starts CMake, so
the compilers it spawns inherit it.

Works in Windows PowerShell 5.1 and PowerShell 7.

.PARAMETER Configuration
Debug, Release, or both (default: Debug, Release).

.PARAMETER InstallPrefix
Where to install. Default: buildstage\diligent\install\Win32.

.PARAMETER BuildDirectory
The CMake build tree. Default: buildstage\diligent\build\Win32.

.PARAMETER NvapiPath
A local NVAPI tree (nvapi.h, x86\nvapi.lib). Without it NVAPI is off.

.PARAMETER Jobs
Parallel MSBuild/CL processes (default: half the logical processors).

.PARAMETER Clean
Deletes the build tree before configuring.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\port\build_diligent_win32.ps1

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\port\build_diligent_win32.ps1 -Configuration Debug -InstallPrefix C:\tmp\dg
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string[]] $Configuration = @('Debug', 'Release'),
    [string] $InstallPrefix,
    [string] $BuildDirectory,
    [string] $NvapiPath,
    [int] $Jobs = 0,
    [switch] $Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Platform = 'Win32'
$DiligentDir = Join-Path $RepoRoot 'dependencies\DiligentCore'
$AbseilDir = Join-Path $RepoRoot 'dependencies\abseil-cpp'
# 1436d1fe, master of 2026-09-22: the pin of dependencies\DiligentCore and of the
# Android build (port\android\CMakeLists.txt). Submodules as pinned by it.
$DiligentCommit = '1436d1fea00763178ae835fd36b065efb31d96e5'
$AbseilCommit = '07d2ef8bd61ab88f0b81b0d8c7fb2c7e19b1d01e'

if (-not $BuildDirectory) { $BuildDirectory = Join-Path $RepoRoot "buildstage\diligent\build\$Platform" }
if (-not $InstallPrefix) { $InstallPrefix = Join-Path $RepoRoot "buildstage\diligent\install\$Platform" }
if ($Jobs -le 0) { $Jobs = [Math]::Max(1, [int]([Environment]::ProcessorCount / 2)) }

# Builds share the PC with interactive use; children inherit the priority class.
try { (Get-Process -Id $PID).PriorityClass = 'BelowNormal' } catch { Write-Warning "could not lower priority: $_" }

function Find-CMake {
    # The CMake bundled with Visual Studio 2022 (3.31.6-msvc6 at the time of
    # writing) is the one bootstrap_diligent.py preferred.
    $bundled = Get-ChildItem -Path 'C:\Program Files\Microsoft Visual Studio\2022\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($bundled) { return $bundled.FullName }
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    throw 'cmake not found (install the Visual Studio "C++ CMake tools for Windows" component)'
}

function Invoke-Checked([string] $exe, [string[]] $arguments) {
    Write-Host ('> "{0}" {1}' -f $exe, ($arguments -join ' '))
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe failed with exit code $LASTEXITCODE" }
}

# --- sources: present and at the pin (no fetching here) --------------------
if (-not (Test-Path (Join-Path $DiligentDir 'CMakeLists.txt'))) {
    throw "DiligentCore sources missing at $DiligentDir"
}
$head = (& git -C $DiligentDir rev-parse HEAD).Trim()
if ($head -ne $DiligentCommit) { throw "dependencies\DiligentCore is at $head, expected $DiligentCommit" }
foreach ($sub in 'glslang', 'SPIRV-Cross', 'SPIRV-Tools', 'SPIRV-Headers', 'Vulkan-Headers', 'volk', 'xxHash') {
    if (-not (Get-ChildItem (Join-Path $DiligentDir "ThirdParty\$sub") -ErrorAction SilentlyContinue)) {
        throw "submodule ThirdParty\$sub is empty; run git submodule update --init in dependencies\DiligentCore"
    }
}
$abseilHead = (& git -C $AbseilDir rev-parse HEAD 2>$null)
if (-not $abseilHead -or $abseilHead.Trim() -ne $AbseilCommit) {
    throw "dependencies\abseil-cpp must be the Chromium abseil-cpp at $AbseilCommit (Diligent's pin)"
}

if ($Clean -and (Test-Path $BuildDirectory)) { Remove-Item -Recurse -Force $BuildDirectory }
New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null

$nvapiSource = if ($NvapiPath) {
    if (-not (Test-Path (Join-Path $NvapiPath 'nvapi.h'))) { throw "$NvapiPath has no nvapi.h" }
    (Resolve-Path $NvapiPath).Path
} else {
    $empty = Join-Path $BuildDirectory 'nvapi-disabled'
    New-Item -ItemType Directory -Force -Path $empty | Out-Null
    $empty
}

$cmake = Find-CMake
$configure = @(
    '-S', $DiligentDir,
    '-B', $BuildDirectory,
    '-G', 'Visual Studio 17 2022',
    '-A', $Platform,
    "-DCMAKE_INSTALL_PREFIX=$InstallPrefix",
    '-DDILIGENT_BUILD_TESTS=OFF',
    '-DDILIGENT_NO_FORMAT_VALIDATION=ON',
    '-DDILIGENT_NO_ARCHIVER=ON',
    '-DDILIGENT_NO_SUPER_RESOLUTION=ON',
    '-DDILIGENT_NO_METAL=ON',
    '-DDILIGENT_NO_WEBGPU=ON',
    '-DDILIGENT_INSTALL_PDB=ON',
    # CMake's MSVC defaults plus the engine's iterator debug level.
    '-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /GR /EHsc /D_ITERATOR_DEBUG_LEVEL=0',
    # No network: see the description.
    '-DFETCHCONTENT_FULLY_DISCONNECTED=ON',
    "-DFETCHCONTENT_SOURCE_DIR_ABSEIL-CPP=$AbseilDir",
    "-DFETCHCONTENT_SOURCE_DIR_NVAPI=$nvapiSource"
)
Invoke-Checked $cmake $configure

foreach ($config in $Configuration) {
    Invoke-Checked $cmake @('--build', $BuildDirectory, '--config', $config, '--target', 'install', '--parallel', "$Jobs")
}

# --- check the result -------------------------------------------------------
foreach ($config in $Configuration) {
    $lib = Join-Path $InstallPrefix "lib\$config\DiligentCore.lib"
    if (-not (Test-Path $lib)) { throw "missing $lib" }
}
Write-Host "installed to $InstallPrefix"

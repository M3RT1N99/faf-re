<#
.SYNOPSIS
Fetches the pinned third-party sources the Android build needs.

.DESCRIPTION
DiligentCore is checked out at a pinned revision into dependencies\DiligentCore
(gitignored), with only the submodules the Android renderer build uses.

DiligentCore's configure step downloads abseil-cpp with FetchContent. This
script pre-fetches the exact commit Diligent pins into dependencies\abseil-cpp,
so build_android.ps1 can configure offline (FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP).
If an earlier Android build already downloaded that commit into
buildstage\android\_deps\abseil-cpp-src, it is copied from there instead of the
network.

Running the script again is cheap: every step is skipped when its result is
already in place.

-Check verifies the same state without touching the network or the disk and
returns the resolved paths; build_android.ps1 calls it before configuring.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts/port/bootstrap_android.ps1
#>
[CmdletBinding()]
param(
    [string]$DependencyDirectory = "dependencies\DiligentCore",
    # Full 40-character commit: a fetch by an abbreviated id is refused by the
    # server, and a prefix can become ambiguous.
    [string]$Revision = "1436d1fea00763178ae835fd36b065efb31d96e5",
    [string]$AbseilDirectory = "dependencies\abseil-cpp",
    [switch]$Check
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

# Submodules the Android build compiles (shader compilers, Vulkan headers and
# loader, hashing). googletest, DXC, dawn and the rest are not needed.
$requiredSubmodules = @(
    "ThirdParty/SPIRV-Cross",
    "ThirdParty/SPIRV-Headers",
    "ThirdParty/SPIRV-Tools",
    "ThirdParty/Vulkan-Headers",
    "ThirdParty/glslang",
    "ThirdParty/volk",
    "ThirdParty/xxHash"
)

# FetchContent download directory of the earlier Android build; reused as an
# offline source for abseil-cpp when it holds the pinned commit.
$previousAbseilCheckout = Join-Path $repoRoot "buildstage\android\_deps\abseil-cpp-src"

function Resolve-RepoPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return $Path }
    return (Join-Path $repoRoot $Path)
}

function Invoke-Git {
    param([Parameter(Mandatory)][string[]]$Arguments, [switch]$AllowFailure)
    # Windows PowerShell 5.1 turns stderr lines of a native command into
    # terminating errors under ErrorActionPreference=Stop; git reports progress
    # on stderr, so relax it for the call and judge by the exit code instead.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = @(& git @Arguments 2>&1 | ForEach-Object { "$_" })
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($exitCode -ne 0 -and -not $AllowFailure) {
        throw "git $($Arguments -join ' ') failed (exit $exitCode):`n$($output -join "`n")"
    }
    return [pscustomobject]@{ ExitCode = $exitCode; Output = $output }
}

function Get-GitHead([string]$Directory) {
    if (-not (Test-Path -LiteralPath (Join-Path $Directory ".git"))) { return $null }
    $result = Invoke-Git -Arguments @("-C", $Directory, "rev-parse", "HEAD") -AllowFailure
    if ($result.ExitCode -ne 0 -or $result.Output.Count -eq 0) { return $null }
    return $result.Output[0].Trim().ToLowerInvariant()
}

# Submodules from $requiredSubmodules that are not checked out at the commit
# the superproject records ('-' = not initialized, '+' = other commit,
# 'U' = conflict).
function Get-StaleSubmodules([string]$Directory) {
    $result = Invoke-Git -Arguments (@("-C", $Directory, "submodule", "status", "--") + $requiredSubmodules)
    $stale = @()
    foreach ($line in $result.Output) {
        if ($line -match '^([ +\-U])[0-9a-f]+ (\S+)') {
            if ($Matches[1] -ne ' ') { $stale += $Matches[2] }
        }
    }
    return , $stale
}

# Diligent pins abseil-cpp in its own CMake file (FetchContent_DeclareShallowGit);
# read it from there so a Diligent update cannot leave a stale copy behind.
function Get-AbseilPin([string]$DiligentDirectory) {
    $cmakeFile = Join-Path $DiligentDirectory "ThirdParty\abseil-cpp\CMakeLists.txt"
    if (-not (Test-Path -LiteralPath $cmakeFile)) {
        throw "DiligentCore has no ThirdParty/abseil-cpp/CMakeLists.txt; cannot determine the abseil-cpp pin."
    }
    $text = [IO.File]::ReadAllText($cmakeFile)
    $repository = [regex]::Match($text, 'GIT_REPOSITORY\s+(\S+)')
    $tag = [regex]::Match($text, 'GIT_TAG\s+([0-9A-Za-z._\-/]+)')
    if (-not $repository.Success -or -not $tag.Success) {
        throw "Could not read GIT_REPOSITORY/GIT_TAG from $cmakeFile."
    }
    return [pscustomobject]@{ Repository = $repository.Groups[1].Value; Tag = $tag.Groups[1].Value.ToLowerInvariant() }
}

if ($Revision -notmatch '^[0-9a-fA-F]{40}$') {
    throw "Revision must be a full 40-character commit id, got '$Revision'."
}
$Revision = $Revision.ToLowerInvariant()
$diligentPath = Resolve-RepoPath $DependencyDirectory
$abseilPath = Resolve-RepoPath $AbseilDirectory

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git was not found on PATH."
}

if ($Check) {
    $problems = New-Object System.Collections.Generic.List[string]
    $head = Get-GitHead $diligentPath
    if (-not $head) {
        $problems.Add("DiligentCore is not checked out at $diligentPath.")
    } elseif ($head -ne $Revision) {
        $problems.Add("DiligentCore is at $head, expected $Revision.")
    } else {
        $stale = Get-StaleSubmodules $diligentPath
        if ($stale.Count -gt 0) { $problems.Add("DiligentCore submodules not checked out: $($stale -join ', ').") }
    }
    if ($problems.Count -gt 0) { throw ($problems -join " ") }

    # abseil-cpp is optional for -Check: without a local copy the configure
    # step downloads it, which works whenever the network does.
    $pin = Get-AbseilPin $diligentPath
    $abseilSource = $null
    foreach ($candidate in @($abseilPath, $previousAbseilCheckout)) {
        if ((Get-GitHead $candidate) -eq $pin.Tag -and (Test-Path -LiteralPath (Join-Path $candidate "CMakeLists.txt"))) {
            $abseilSource = $candidate
            break
        }
    }
    return [pscustomobject]@{
        DiligentCore = $diligentPath
        Revision = $Revision
        AbseilCommit = $pin.Tag
        AbseilSource = $abseilSource
    }
}

# --- DiligentCore ---------------------------------------------------------

if (-not (Test-Path -LiteralPath (Join-Path $diligentPath ".git"))) {
    if (Test-Path -LiteralPath $diligentPath) {
        throw "Dependency directory exists but is not a Git checkout: $diligentPath"
    }
    Write-Host "Cloning DiligentCore into $diligentPath"
    Invoke-Git -Arguments @("clone", "--filter=blob:none", "--no-checkout", "https://github.com/DiligentGraphics/DiligentCore.git", $diligentPath) | Out-Null
}

if ((Get-GitHead $diligentPath) -ne $Revision) {
    Write-Host "Fetching DiligentCore $Revision"
    Invoke-Git -Arguments @("-C", $diligentPath, "fetch", "--depth", "1", "origin", $Revision) | Out-Null
    Invoke-Git -Arguments @("-C", $diligentPath, "checkout", "--detach", $Revision) | Out-Null
} else {
    Write-Host "DiligentCore already at $Revision"
}

$staleSubmodules = Get-StaleSubmodules $diligentPath
if ($staleSubmodules.Count -gt 0) {
    Write-Host "Updating DiligentCore submodules: $($staleSubmodules -join ', ')"
    Invoke-Git -Arguments (@("-C", $diligentPath, "submodule", "update", "--init", "--depth", "1", "--") + $staleSubmodules) | Out-Null
} else {
    Write-Host "DiligentCore submodules up to date"
}

# --- abseil-cpp -------------------------------------------------------------

$abseilPin = Get-AbseilPin $diligentPath
if ((Get-GitHead $abseilPath) -eq $abseilPin.Tag) {
    Write-Host "abseil-cpp already at $($abseilPin.Tag)"
} else {
    if ((Test-Path -LiteralPath $abseilPath) -and -not (Test-Path -LiteralPath (Join-Path $abseilPath ".git"))) {
        throw "Dependency directory exists but is not a Git checkout: $abseilPath"
    }
    if (-not (Test-Path -LiteralPath $abseilPath)) {
        Invoke-Git -Arguments @("init", "--quiet", $abseilPath) | Out-Null
    }
    # Same shallow fetch-by-commit FetchContent_DeclareShallowGit performs.
    $source = $abseilPin.Repository
    if ((Get-GitHead $previousAbseilCheckout) -eq $abseilPin.Tag) { $source = $previousAbseilCheckout }
    Write-Host "Fetching abseil-cpp $($abseilPin.Tag) from $source"
    Invoke-Git -Arguments @("-C", $abseilPath, "fetch", "--depth", "1", $source, $abseilPin.Tag) | Out-Null
    Invoke-Git -Arguments @("-C", $abseilPath, "checkout", "--detach", "--force", "FETCH_HEAD") | Out-Null
    if ((Get-GitHead $abseilPath) -ne $abseilPin.Tag) {
        throw "abseil-cpp checkout at $abseilPath is not at $($abseilPin.Tag) after fetching."
    }
}

Write-Host "DiligentCore $Revision and abseil-cpp $($abseilPin.Tag) are ready."

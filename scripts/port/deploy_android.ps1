<#
.SYNOPSIS
Installs the Android APK and copies the game data it needs from this PC to the
device over adb.

.DESCRIPTION
What to copy is defined by port/data/gamedata.json (see docs/port/gamedata.md):

  - FAF files (init_faf.lua, *.nx2) from the FAF client install
    (C:\ProgramData\FAForever) or downloaded from FAF's content server, always
    checked against the manifest's size and sha256,
  - a selection of the user's Supreme Commander: Forged Alliance install
    (found through the FAF client's fa_path.lua or the Steam libraries),
  - optionally the FAF vault (downloaded maps and mods).

Everything lands in the app's data root, /sdcard/Android/data/<package>/files,
in the layout the launcher and the native runtime expect; the script then
writes faf/fa_path.lua, faf/version.json and .deploy/deployed.json there.
Files that are already on the device with the same size are not copied again,
and nothing on the device is ever deleted.

-DryRun prints the plan with sizes and touches nothing. -StageDir builds the
same layout in a local folder instead of a device, for host checks such as
faf_datacheck.

Works in Windows PowerShell 5.1 and PowerShell 7.

.PARAMETER Apk
APK to install. Default: the newest output\android\faf-re-android-*-arm64-v8a.apk.

.PARAMETER NoInstall
Do not install; the app must already be on the device.

.PARAMETER ScfaPath
Supreme Commander: Forged Alliance install. Default: fa_path from the FAF
client's fa_path.lua, else the Steam libraries.

.PARAMETER FafPath
FAF client data directory (default C:\ProgramData\FAForever).

.PARAMETER FafSource
Local (default): FAF files from -FafPath. Download: from the manifest's
baseUrl into buildstage\faf-cache\<version> (resumable, verified).

.PARAMETER Tier
required (main menu), recommended (default: normal play) or all (adds the
optional entries).

.PARAMETER Include
Additional entries by id (scfa-movies, vault-maps, ...) or FAF file name
(SupComDataPath.lua), regardless of -Tier. The plan lists the ids.

.PARAMETER IncludeVault
Also copy the FAF vault (maps and mods downloaded by the FAF client).

.PARAMETER DryRun
Print the plan and sizes only: no adb, no downloads, no copies.

.PARAMETER StageDir
Build the device layout in this folder instead of pushing to a device.

.PARAMETER Hardlink
With -StageDir: hard-link files instead of copying them when source and stage
are on the same volume. The stage then shares the files with the install;
the script only ever replaces a staged file by deleting the link first, so
the originals are never written.

.PARAMETER Launch
Bring the launcher to the front when done.

.PARAMETER Package
Application id (default io.github.m3rt1n99.fafre).

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts/port/deploy_android.ps1

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts/port/deploy_android.ps1 -Tier required -DryRun

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts/port/deploy_android.ps1 -StageDir D:\faf-stage -Tier required -Hardlink
#>
[CmdletBinding()]
param(
    [string]$Apk,
    [switch]$NoInstall,
    [string]$ScfaPath,
    [string]$FafPath = "C:\ProgramData\FAForever",
    [ValidateSet("Local", "Download")]
    [string]$FafSource = "Local",
    [ValidateSet("required", "recommended", "all")]
    [string]$Tier = "recommended",
    [string[]]$Include = @(),
    [switch]$IncludeVault,
    [string]$VaultPath,
    [switch]$DryRun,
    [string]$StageDir,
    [switch]$Hardlink,
    [switch]$Launch,
    [string]$Package = "io.github.m3rt1n99.fafre",
    [string]$AndroidSdk,
    [string]$FafCacheDir
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$manifestPath = Join-Path $repoRoot "port\data\gamedata.json"
$versionFile = Join-Path $repoRoot "port\android\version.properties"
$launcherActivity = ".LauncherActivity"
$minimumSdk = 26
$requiredAbi = "arm64-v8a"
# Room left on the device after the copy, for the app's own files (logs,
# preferences, shader cache) and Android itself.
$freeSpaceReserve = 512MB


# --- small helpers -------------------------------------------------------------

function Get-JsonProperty($Object, [string]$Name) {
    if ($null -eq $Object) { return $null }
    $property = $Object.PSObject.Properties[$Name]
    if ($property) { return $property.Value }
    return $null
}

function Format-Size([double]$Bytes) {
    if ($Bytes -ge 1GB) { return "{0:N2} GB" -f ($Bytes / 1GB) }
    if ($Bytes -ge 1MB) { return "{0:N1} MB" -f ($Bytes / 1MB) }
    if ($Bytes -ge 1KB) { return "{0:N1} KB" -f ($Bytes / 1KB) }
    return "{0:N0} B" -f $Bytes
}

function Resolve-FullPath([string]$Path) {
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

function Write-Utf8File([string]$Path, [string]$Text) {
    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    # UTF-8 without BOM, LF line ends: Lua 5.0 and the JSON readers on the
    # device must not see a BOM.
    [IO.File]::WriteAllText($Path, ($Text -replace "`r`n", "`n"), (New-Object Text.UTF8Encoding $false))
}

function ConvertTo-JsonString([string]$Value) {
    $builder = New-Object Text.StringBuilder
    [void]$builder.Append('"')
    foreach ($character in $Value.ToCharArray()) {
        switch ($character) {
            '"' { [void]$builder.Append('\"') }
            '\' { [void]$builder.Append('\\') }
            "`n" { [void]$builder.Append('\n') }
            "`r" { [void]$builder.Append('\r') }
            "`t" { [void]$builder.Append('\t') }
            default {
                if ([int]$character -lt 0x20) { [void]$builder.AppendFormat('\u{0:x4}', [int]$character) }
                else { [void]$builder.Append($character) }
            }
        }
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

# Inside a Lua "..." literal; the same escaping as the launcher (FaPathWriter).
function ConvertTo-LuaStringContent([string]$Value) {
    return $Value.Replace('\', '\\').Replace('"', '\"').Replace("`n", '\n').Replace("`r", '\r').Replace([string][char]0, '\0')
}

# Shell-quotes one argument for `adb shell` (POSIX sh on the device).
function ConvertTo-ShellArgument([string]$Value) {
    return "'" + $Value.Replace("'", "'\''") + "'"
}

# Wildcards as the manifest defines them: '*' and '?', case-insensitive,
# matched against a single name. Built by hand rather than with -like so that
# '[' in a name or pattern means itself, as in the launcher and the runtime.
$wildcardCache = @{}
function Test-Wildcard([string]$Pattern, [string]$Name) {
    $regex = $wildcardCache[$Pattern]
    if (-not $regex) {
        $expression = "^" + ([regex]::Escape($Pattern) -replace '\\\*', '.*' -replace '\\\?', '.') + "$"
        $regex = New-Object Text.RegularExpressions.Regex $expression, ([Text.RegularExpressions.RegexOptions]::IgnoreCase -bor [Text.RegularExpressions.RegexOptions]::CultureInvariant)
        $wildcardCache[$Pattern] = $regex
    }
    return $regex.IsMatch($Name)
}

function Test-EntryExcludes($Entry, [string]$Name) {
    foreach ($pattern in @(Get-JsonProperty $Entry "exclude")) {
        if ($pattern -and (Test-Wildcard $pattern $Name)) { return $true }
    }
    return $false
}

function Test-EntryMatch($Entry, [string]$Name) {
    foreach ($pattern in @(Get-JsonProperty $Entry "include")) {
        if ($pattern -and (Test-Wildcard $pattern $Name)) { return -not (Test-EntryExcludes $Entry $Name) }
    }
    return $false
}

# A single path component the device may receive: the launcher's rule for
# names coming from SAF or the manifest (Names.isSafeName), which also
# refuses any name containing "..".
function Test-SafeName([string]$Name) {
    return $Name -and $Name -ne "." -and -not $Name.Contains("..") -and $Name.IndexOfAny([char[]]@('/', '\', [char]0)) -lt 0
}

function Test-SafeRelativePath([string]$Path) {
    if (-not $Path -or $Path.StartsWith("/") -or $Path -match '^[A-Za-z]:') { return $false }
    foreach ($component in $Path.Split('/')) {
        if (-not (Test-SafeName $component)) { return $false }
    }
    return $true
}

# Resolves `relative` ('/'-separated) below `base` one component at a time,
# case-insensitively. Returns the directory and the components' real names,
# which become the destination path (the source's case is kept), or $null.
function Resolve-SourceDirectory([string]$Base, [string]$Relative) {
    $current = $Base
    $names = @()
    foreach ($component in ($Relative -split '/' | Where-Object { $_ })) {
        if (-not (Test-Path -LiteralPath $current -PathType Container)) { return $null }
        $match = @(Get-ChildItem -LiteralPath $current -Directory -Force -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -eq $component })
        if ($match.Count -eq 0) { return $null }
        $exact = @($match | Where-Object { $_.Name -ceq $component })
        $chosen = if ($exact.Count -gt 0) { $exact[0] } else { $match[0] }
        $current = $chosen.FullName
        $names += $chosen.Name
    }
    if (-not (Test-Path -LiteralPath $current -PathType Container)) { return $null }
    return [pscustomobject]@{ Path = $current; Names = $names }
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# Reads `name = "value"` from a Lua file such as the FAF client's fa_path.lua.
function Read-LuaStringAssignment([string]$Path, [string]$Name) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    $text = [IO.File]::ReadAllText($Path)
    $match = [regex]::Match($text, "(?m)^\s*$([regex]::Escape($Name))\s*=\s*""((?:[^""\\]|\\.)*)""")
    if (-not $match.Success) { return $null }
    return [regex]::Replace($match.Groups[1].Value, '\\(.)', '$1')
}

function Read-VersionName {
    $line = Get-Content -LiteralPath $versionFile | Where-Object { $_ -match '^\s*versionName\s*=' } | Select-Object -First 1
    if (-not $line) { throw "$versionFile does not define versionName." }
    return ($line -replace '^\s*versionName\s*=\s*', '').Trim()
}

# --- sources ---------------------------------------------------------------------

# The SCFA install: the FAF client's own fa_path.lua knows it; otherwise look
# through the Steam libraries. The marker file (gamedata/textures.scd) proves
# a candidate.
function Find-ScfaInstall($Detect) {
    $tried = New-Object System.Collections.Generic.List[string]
    $marker = [string](Get-JsonProperty $Detect "marker")
    $markerDirectory = (Split-Path -Parent $marker).Replace('\', '/')
    $markerName = Split-Path -Leaf $marker
    $test = {
        param([string]$Path)
        $tried.Add($Path)
        $directory = Resolve-SourceDirectory $Path $markerDirectory
        return [bool]($directory -and (Test-Path -LiteralPath ([IO.Path]::Combine($directory.Path, $markerName)) -PathType Leaf))
    }

    $faPathLua = [IO.Path]::Combine($FafPath, "fa_path.lua")
    $fromClient = Read-LuaStringAssignment $faPathLua "fa_path"
    if ($fromClient -and (& $test $fromClient)) {
        return [pscustomobject]@{ Path = [IO.Path]::GetFullPath($fromClient); Origin = "fa_path in $faPathLua" }
    }

    $steamRoots = New-Object System.Collections.Generic.List[string]
    foreach ($key in @(@("HKCU:\Software\Valve\Steam", "SteamPath"), @("HKLM:\SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath"), @("HKLM:\SOFTWARE\Valve\Steam", "InstallPath"))) {
        $value = Get-ItemProperty -Path $key[0] -Name $key[1] -ErrorAction SilentlyContinue
        if ($value) { $steamRoots.Add([string](Get-JsonProperty $value $key[1])) }
    }
    if (${env:ProgramFiles(x86)}) { $steamRoots.Add([IO.Path]::Combine(${env:ProgramFiles(x86)}, "Steam")) }
    $libraries = New-Object System.Collections.Generic.List[string]
    foreach ($steamRoot in ($steamRoots | Where-Object { $_ } | Select-Object -Unique)) {
        $libraries.Add($steamRoot)
        $vdf = [IO.Path]::Combine($steamRoot, "steamapps\libraryfolders.vdf")
        if (Test-Path -LiteralPath $vdf) {
            foreach ($match in [regex]::Matches([IO.File]::ReadAllText($vdf), '"path"\s+"((?:[^"\\]|\\.)*)"')) {
                $libraries.Add(($match.Groups[1].Value -replace '\\\\', '\'))
            }
        }
    }
    $steamFolder = ([string](Get-JsonProperty $Detect "steamFolder")).Replace("/", "\")
    foreach ($library in ($libraries | Select-Object -Unique)) {
        $candidate = [IO.Path]::Combine($library, $steamFolder)
        if (& $test $candidate) {
            return [pscustomobject]@{ Path = [IO.Path]::GetFullPath($candidate); Origin = "Steam library $library" }
        }
    }
    throw "Supreme Commander: Forged Alliance not found (looked for $marker in: $($tried -join '; ')). Pass -ScfaPath <install folder>."
}

function Get-VaultDefault {
    $fromClient = Read-LuaStringAssignment ([IO.Path]::Combine($FafPath, "fa_path.lua")) "custom_vault_path"
    if ($fromClient) { return [pscustomobject]@{ Path = [IO.Path]::GetFullPath($fromClient); Origin = "custom_vault_path in $([IO.Path]::Combine($FafPath, 'fa_path.lua'))" } }
    $documents = [Environment]::GetFolderPath("MyDocuments")
    return [pscustomobject]@{ Path = [IO.Path]::Combine($documents, "My Games\Gas Powered Games\Supreme Commander Forged Alliance"); Origin = "Documents" }
}

# --- HTTP ---------------------------------------------------------------------------

$script:httpClient = $null
function Get-HttpClient([string]$UserAgent) {
    if (-not $script:httpClient) {
        Add-Type -AssemblyName System.Net.Http
        # Windows PowerShell 5.1 (.NET Framework) may still default to TLS 1.0.
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        $script:httpClient = New-Object System.Net.Http.HttpClient
        $script:httpClient.Timeout = [TimeSpan]::FromSeconds(60)
        $script:httpClient.DefaultRequestHeaders.UserAgent.ParseAdd($UserAgent)
    }
    return $script:httpClient
}

function Get-RemoteLength([string]$Url) {
    $client = Get-HttpClient $userAgent   # first: loads System.Net.Http on Windows PowerShell
    $request = New-Object System.Net.Http.HttpRequestMessage -ArgumentList ([System.Net.Http.HttpMethod]::Head), $Url
    $response = $client.SendAsync($request).GetAwaiter().GetResult()
    try {
        if (-not $response.IsSuccessStatusCode) { throw "HTTP $([int]$response.StatusCode) $($response.ReasonPhrase) for $Url" }
        return $response.Content.Headers.ContentLength
    } finally {
        $response.Dispose()
    }
}

# Downloads into <Destination>.part, resuming an earlier partial download with
# an HTTP range request, verifies size and sha256, then renames. A corrupt
# result is deleted and fetched once more from scratch.
function Receive-VerifiedFile([string]$Url, [string]$Destination, [long]$ExpectedSize, [string]$ExpectedSha256) {
    $partial = "$Destination.part"
    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $client = Get-HttpClient $userAgent
    $name = Split-Path -Leaf $Destination
    for ($attempt = 1; $attempt -le 2; $attempt++) {
        $offset = 0
        if (Test-Path -LiteralPath $partial) { $offset = (Get-Item -LiteralPath $partial).Length }
        if ($offset -gt $ExpectedSize) { Remove-Item -LiteralPath $partial -Force; $offset = 0 }
        if ($offset -lt $ExpectedSize) {
            $request = New-Object System.Net.Http.HttpRequestMessage -ArgumentList ([System.Net.Http.HttpMethod]::Get), $Url
            if ($offset -gt 0) { $request.Headers.Range = New-Object System.Net.Http.Headers.RangeHeaderValue -ArgumentList ([long]$offset), $null }
            $response = $client.SendAsync($request, [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
            try {
                if (-not $response.IsSuccessStatusCode) { throw "HTTP $([int]$response.StatusCode) $($response.ReasonPhrase) for $Url" }
                # A server that ignores the range answers 200 with the whole file.
                $append = $offset -gt 0 -and [int]$response.StatusCode -eq 206
                if (-not $append) { $offset = 0 }
                $mode = if ($append) { [IO.FileMode]::Append } else { [IO.FileMode]::Create }
                $source = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
                $target = New-Object IO.FileStream -ArgumentList $partial, $mode, ([IO.FileAccess]::Write), ([IO.FileShare]::None), 1048576
                try {
                    $buffer = New-Object byte[] 1048576
                    $written = $offset
                    $clock = [Diagnostics.Stopwatch]::StartNew()
                    $lastReport = -1000
                    while (($read = $source.Read($buffer, 0, $buffer.Length)) -gt 0) {
                        $target.Write($buffer, 0, $read)
                        $written += $read
                        if ($clock.ElapsedMilliseconds - $lastReport -ge 500) {
                            $lastReport = $clock.ElapsedMilliseconds
                            # Both arguments of the same type: Windows PowerShell may
                            # otherwise pick Max(int, int) and fail above 2 GB.
                            $percent = [int][Math]::Min([double]100, 100 * $written / [Math]::Max([long]1, [long]$ExpectedSize))
                            Write-Progress -Activity "Downloading $name" -Status "$(Format-Size $written) of $(Format-Size $ExpectedSize)" -PercentComplete $percent
                        }
                    }
                } finally {
                    $target.Dispose()
                    $source.Dispose()
                    Write-Progress -Activity "Downloading $name" -Completed
                }
            } finally {
                $response.Dispose()
            }
        }
        $size = (Get-Item -LiteralPath $partial).Length
        if ($size -ne $ExpectedSize) {
            if ($attempt -lt 2) { Write-Host "    incomplete ($(Format-Size $size)), resuming"; continue }
            throw "$name has $size bytes after downloading, expected $ExpectedSize."
        }
        $hash = Get-Sha256 $partial
        if ($hash -ne $ExpectedSha256) {
            Remove-Item -LiteralPath $partial -Force
            if ($attempt -lt 2) { Write-Host "    sha256 mismatch, downloading again"; continue }
            throw "$name from $Url does not match the manifest's sha256 ($hash)."
        }
        Move-Item -LiteralPath $partial -Destination $Destination -Force
        return
    }
}

# --- adb ------------------------------------------------------------------------------

function Resolve-Adb {
    $candidates = New-Object System.Collections.Generic.List[string]
    $roots = if ($AndroidSdk) { @($AndroidSdk) } else {
        @($env:ANDROID_SDK_ROOT, $env:ANDROID_HOME, "C:\Android\sdk", [IO.Path]::Combine([Environment]::GetFolderPath("LocalApplicationData"), "Android\Sdk")) | Where-Object { $_ }
    }
    foreach ($root in $roots) { $candidates.Add([IO.Path]::Combine($root, "platform-tools\adb.exe")) }
    if (-not $AndroidSdk) {
        $onPath = Get-Command adb.exe -ErrorAction SilentlyContinue
        if ($onPath) { $candidates.Add($onPath.Source) }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    throw "adb not found (tried: $($candidates -join ', ')). Install the SDK platform-tools, set ANDROID_SDK_ROOT or pass -AndroidSdk."
}

$script:adbPath = $null
$script:adbSerial = $null

# Runs adb against the selected device; returns exit code and output lines.
function Invoke-Adb([string[]]$Arguments) {
    $fullArguments = @()
    if ($script:adbSerial) { $fullArguments += @("-s", $script:adbSerial) }
    $fullArguments += $Arguments
    # adb writes progress and some errors to stderr; Windows PowerShell 5.1
    # would turn those into terminating errors under Stop.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        # An ErrorRecord (stderr line) as its message: an empty one would
        # otherwise read "System.Management.Automation.RemoteException".
        $output = @(& $script:adbPath @fullArguments 2>&1 | ForEach-Object {
            $line = if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.Exception.Message } else { "$_" }
            $line.TrimEnd("`r")
        })
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    return [pscustomobject]@{ ExitCode = $exitCode; Output = $output; Text = ($output -join "`n") }
}

function Invoke-AdbShell([string]$Command) {
    return Invoke-Adb @("shell", $Command)
}

function Select-Device {
    $result = Invoke-Adb @("devices")
    if ($result.ExitCode -ne 0) { throw "adb devices failed:`n$($result.Text)" }
    $devices = @($result.Output | Where-Object { $_ -match '^(\S+)\s+(device|unauthorized|offline|authorizing|no permissions.*)$' } |
        ForEach-Object { $null = $_ -match '^(\S+)\s+(.+)$'; [pscustomobject]@{ Serial = $Matches[1]; State = $Matches[2].Trim() } })
    if ($env:ANDROID_SERIAL) {
        $devices = @($devices | Where-Object { $_.Serial -eq $env:ANDROID_SERIAL })
        if ($devices.Count -eq 0) { throw "ANDROID_SERIAL=$env:ANDROID_SERIAL is not connected." }
    }
    $ready = @($devices | Where-Object { $_.State -eq "device" })
    if ($ready.Count -eq 0) {
        $unauthorized = @($devices | Where-Object { $_.State -eq "unauthorized" })
        if ($unauthorized.Count -gt 0) { throw "Device $($unauthorized[0].Serial) has not authorized this computer: unlock it and accept the 'Allow USB debugging?' prompt, then run again." }
        throw "No Android device connected. Enable USB debugging (Settings > Developer options), connect the device and check 'adb devices'."
    }
    if ($ready.Count -gt 1) {
        throw "More than one device connected ($(($ready | ForEach-Object { $_.Serial }) -join ', ')). Disconnect all but one, or set ANDROID_SERIAL."
    }
    return $ready[0].Serial
}

function Get-DeviceProperty([string]$Name) {
    $result = Invoke-AdbShell "getprop $Name"
    if ($result.ExitCode -ne 0) { throw "adb shell getprop $Name failed:`n$($result.Text)" }
    return ($result.Output -join "").Trim()
}

# size of every file below the data root: dest (relative, '/') -> bytes.
function Get-RemoteInventory([string]$RemoteRoot) {
    $inventory = @{}
    $result = Invoke-AdbShell "find $(ConvertTo-ShellArgument $RemoteRoot) -type f -exec stat -c '%s %n' {} + 2>/dev/null"
    foreach ($line in $result.Output) {
        if ($line -match '^(\d+) (.+)$' -and $Matches[2].StartsWith("$RemoteRoot/")) {
            $inventory[$Matches[2].Substring($RemoteRoot.Length + 1)] = [long]$Matches[1]
        }
    }
    return $inventory
}

function Get-RemoteFreeBytes([string]$RemotePath) {
    $result = Invoke-AdbShell "df -k $(ConvertTo-ShellArgument $RemotePath)"
    $line = @($result.Output | Where-Object { $_ -match '^\S+\s+\d+\s+\d+\s+\d+\s+\d+%' }) | Select-Object -Last 1
    if (-not $line) { return $null }
    $fields = $line -split '\s+'
    return [long]$fields[3] * 1024
}

# --- deploy record ------------------------------------------------------------------------

# deployed.json as the launcher reads it: an object with a "files" list of
# {dest, size} (older or foreign shapes, a bare list or "entries", are read
# too). Records from earlier runs and from the in-app importer are kept, so
# the file describes everything that was placed, not only this run.
function Read-DeployRecord([string]$Text) {
    $records = New-Object System.Collections.Specialized.OrderedDictionary
    if (-not $Text -or -not $Text.Trim()) { return $records }
    try {
        $json = $Text | ConvertFrom-Json
    } catch {
        Write-Warning "Existing deployed.json is damaged; starting a new one."
        return $records
    }
    $list = $null
    if ($json -is [array]) { $list = $json }
    elseif ($null -ne (Get-JsonProperty $json "files")) { $list = Get-JsonProperty $json "files" }
    elseif ($null -ne (Get-JsonProperty $json "entries")) { $list = Get-JsonProperty $json "entries" }
    elseif ($null -ne (Get-JsonProperty $json "dest")) { $list = @($json) }   # a one-item list, unrolled by ConvertFrom-Json
    foreach ($item in @($list)) {
        $dest = [string](Get-JsonProperty $item "dest")
        if (-not $dest) { continue }
        $dest = $dest.Replace('\', '/')
        if (-not (Test-SafeRelativePath $dest)) { continue }
        $size = Get-JsonProperty $item "size"
        $records[$dest.ToLowerInvariant()] = [pscustomobject]@{ Dest = $dest; Size = $(if ($null -ne $size) { [long]$size } else { [long]-1 }) }
    }
    return $records
}

function Format-DeployRecord($Records, [string]$VersionName) {
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("{")
    $lines.Add(' "schema": 1,')
    $lines.Add(' "updated": ' + (ConvertTo-JsonString ([DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ", [Globalization.CultureInfo]::InvariantCulture))) + ',')
    $lines.Add(' "tool": ' + (ConvertTo-JsonString "deploy_android.ps1 $VersionName") + ',')
    $lines.Add(' "files": [')
    $items = @($Records.Values)
    for ($i = 0; $i -lt $items.Count; $i++) {
        $separator = if ($i -lt $items.Count - 1) { "," } else { "" }
        $lines.Add('  {"dest": ' + (ConvertTo-JsonString $items[$i].Dest) + ', "size": ' + $items[$i].Size + '}' + $separator)
    }
    $lines.Add(' ]')
    $lines.Add("}")
    return (($lines -join "`n") + "`n")
}

# --- hard links (-StageDir -Hardlink) -----------------------------------------------------

function Initialize-HardLinkSupport {
    if (-not ("FafRe.NativeMethods" -as [type])) {
        Add-Type -Namespace FafRe -Name NativeMethods -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
[return: MarshalAs(UnmanagedType.Bool)]
public static extern bool CreateHardLink(string lpFileName, string lpExistingFileName, IntPtr lpSecurityAttributes);
'@
    }
}

# =====================================================================================
# Plan
# =====================================================================================

if (-not (Test-Path -LiteralPath $manifestPath)) { throw "Missing $manifestPath." }
$manifest = [IO.File]::ReadAllText($manifestPath) | ConvertFrom-Json
if ((Get-JsonProperty $manifest "schema") -ne 1) { throw "Unsupported gamedata.json schema $(Get-JsonProperty $manifest 'schema')." }
$layout = $manifest.layout
$faf = $manifest.faf
$fafVersion = [int]$faf.version

if ($Hardlink -and -not $StageDir) { throw "-Hardlink only applies to -StageDir." }
$deviceMode = -not $StageDir
if ($StageDir) {
    $StageDir = Resolve-FullPath $StageDir
    foreach ($flag in @("Launch", "NoInstall", "Apk")) {
        if ($PSBoundParameters.ContainsKey($flag)) { Write-Warning "-$flag has no effect with -StageDir." }
    }
}

$tierRank = @{ "required" = 0; "recommended" = 1; "optional" = 2 }
$selectedRank = @{ "required" = 0; "recommended" = 1; "all" = 2 }[$Tier]

# -Include accepts entry ids and FAF file names.
$knownIds = @{}
foreach ($entry in @($manifest.scfa.entries) + @($manifest.user.entries)) { $knownIds[$entry.id.ToLowerInvariant()] = $entry.id }
foreach ($file in @($faf.files)) { $knownIds[$file.name.ToLowerInvariant()] = $file.name }
$includeSet = @{}
foreach ($id in @($Include | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
    if (-not $knownIds.ContainsKey($id.ToLowerInvariant())) {
        throw "Unknown -Include '$id'. Known: $(($knownIds.Values | Sort-Object) -join ', ')."
    }
    $includeSet[$id.ToLowerInvariant()] = $true
}
if ($IncludeVault) { foreach ($entry in @($manifest.user.entries)) { $includeSet[$entry.id.ToLowerInvariant()] = $true } }

function Test-Selected([string]$Id, [string]$EntryTier) {
    if ($includeSet.ContainsKey($Id.ToLowerInvariant())) { return $true }
    return $tierRank[$EntryTier] -le $selectedRank
}

$versionName = Read-VersionName
$apkPath = $null
if ($deviceMode -and -not $NoInstall) {
    if ($Apk) {
        $apkPath = Resolve-FullPath $Apk
        if (-not $DryRun -and -not (Test-Path -LiteralPath $apkPath)) { throw "APK not found: $apkPath" }
    } else {
        $newest = Get-ChildItem -LiteralPath (Join-Path $repoRoot "output\android") -Filter "faf-re-android-*-$requiredAbi.apk" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
        if ($newest) { $apkPath = $newest.FullName }
        elseif (-not $DryRun) { throw "No APK in output\android. Build one with scripts/port/build_android.ps1, or pass -Apk / -NoInstall." }
    }
    if ($apkPath -and ((Split-Path -Leaf $apkPath) -match '^faf-re-android-(.+)-arm64-v8a\.apk$')) { $versionName = $Matches[1] }
}
$userAgent = "faf-re-android/$versionName (deploy_android.ps1)"

$problems = New-Object System.Collections.Generic.List[string]
$warnings = New-Object System.Collections.Generic.List[string]
$plan = New-Object System.Collections.Generic.List[object]
$seenDestinations = @{}

function New-PlanEntry([string]$Group, [string]$Id, [string]$Label, [string]$EntryTier, [bool]$Selected, [string]$DestDir) {
    $entry = [pscustomobject]@{
        Group = $Group; Id = $Id; Label = $Label; Tier = $EntryTier; Selected = $Selected; DestDir = $DestDir
        Units = New-Object System.Collections.Generic.List[object]
        Bytes = [long]0; FileCount = 0; Note = ""
    }
    $plan.Add($entry)
    return $entry
}

# A unit is what one adb push moves: a file, or a directory with its files.
# Entries that are not selected are expanded too, so the plan can show what
# they would add on this install.
function Add-PlanUnit($Entry, [string]$Source, [string]$Dest, [bool]$IsDirectory, [bool]$Selected) {
    if ($Selected) {
        $key = $Dest.ToLowerInvariant()
        if ($seenDestinations.ContainsKey($key)) {
            $warnings.Add("$($Entry.Id): $Dest is already selected by $($seenDestinations[$key]); skipped.")
            return
        }
        $seenDestinations[$key] = $Entry.Id
    }
    $files = New-Object System.Collections.Generic.List[object]
    if ($IsDirectory) {
        # -Force: hidden files too, like the launcher's importer copies them.
        foreach ($file in @(Get-ChildItem -LiteralPath $Source -Recurse -File -Force -ErrorAction SilentlyContinue)) {
            $relative = $file.FullName.Substring($Source.Length).TrimStart('\').Replace('\', '/')
            if (-not (Test-SafeRelativePath $relative)) { $warnings.Add("$($Entry.Id): skipped unsafe name $relative"); continue }
            $files.Add([pscustomobject]@{ Source = $file.FullName; Dest = "$Dest/$relative"; Size = [long]$file.Length })
        }
    } else {
        $files.Add([pscustomobject]@{ Source = $Source; Dest = $Dest; Size = [long](Get-Item -LiteralPath $Source -Force).Length })
    }
    $bytes = [long]0
    foreach ($file in $files) { $bytes += $file.Size }
    $Entry.Units.Add([pscustomobject]@{ Source = $Source; Dest = $Dest; IsDirectory = $IsDirectory; Files = $files; Bytes = $bytes })
    $Entry.Bytes += $bytes
    $Entry.FileCount += $files.Count
}

# Expands one scfa/user entry below `base` (the selection rule shared with the
# launcher's importer and the native runtime: names directly inside src that
# match include and not exclude, files or directories by kind).
function Add-SelectionEntry([string]$Group, $Entry, [string]$Base, [string]$DestPrefix, [bool]$Selected) {
    $plannedEntry = New-PlanEntry $Group $Entry.id $Entry.label $Entry.tier $Selected "$DestPrefix/$($Entry.src)"
    if (-not $Base) { $plannedEntry.Note = "source not found"; return }
    $sourceDirectory = Resolve-SourceDirectory $Base $Entry.src
    $isDirectoryKind = $Entry.kind -eq "dir"
    # Names without wildcards must exist; a pattern may legitimately match nothing.
    $literals = @(@(Get-JsonProperty $Entry "include") | Where-Object { $_ -notmatch '[*?]' -and -not (Test-EntryExcludes $Entry $_) })
    if (-not $sourceDirectory) {
        $plannedEntry.Note = "folder $($Entry.src) not found"
        if ($Selected) {
            $message = "$($Entry.id): folder '$($Entry.src)' not found in $Base."
            if ($Entry.tier -eq "required") { $problems.Add($message) } else { $warnings.Add($message) }
        }
        return
    }
    $plannedEntry.DestDir = "$DestPrefix/$($sourceDirectory.Names -join '/')"
    $found = @{}
    foreach ($child in @(Get-ChildItem -LiteralPath $sourceDirectory.Path -Force -ErrorAction SilentlyContinue | Sort-Object Name)) {
        if ($child.PSIsContainer -ne $isDirectoryKind) { continue }
        if (-not (Test-EntryMatch $Entry $child.Name)) { continue }
        if (-not (Test-SafeName $child.Name)) { $warnings.Add("$($Entry.id): skipped unsafe name '$($child.Name)'"); continue }
        $found[$child.Name.ToLowerInvariant()] = $true
        Add-PlanUnit $plannedEntry $child.FullName "$($plannedEntry.DestDir)/$($child.Name)" $isDirectoryKind $Selected
    }
    $missing = @($literals | Where-Object { -not $found.ContainsKey($_.ToLowerInvariant()) })
    if ($plannedEntry.Units.Count -eq 0 -or $missing.Count -gt 0) {
        $what = if ($missing.Count -gt 0) { "missing $($missing -join ', ')" } else { "nothing matches" }
        $plannedEntry.Note = $what
        if ($Selected) {
            $message = "$($Entry.id): $what in $($sourceDirectory.Path)."
            if ($Entry.tier -eq "required") { $problems.Add($message) } else { $warnings.Add($message) }
        }
    }
}

# --- FAF files ---

$fafCacheRoot = $null
if ($FafSource -eq "Download") {
    $cacheBase = if ($FafCacheDir) { Resolve-FullPath $FafCacheDir } else { Join-Path $repoRoot "buildstage\faf-cache" }
    $fafCacheRoot = [IO.Path]::Combine($cacheBase, "$fafVersion")
    $fafOrigin = "download from $($faf.baseUrl) via $fafCacheRoot"
} else {
    $FafPath = Resolve-FullPath $FafPath
    $fafOrigin = $FafPath
}

$fafDownloads = New-Object System.Collections.Generic.List[object]
$fafDeployed = $false
foreach ($file in @($faf.files)) {
    $selected = Test-Selected $file.name $file.tier
    if (-not (Test-SafeRelativePath $file.dest) -or -not (Test-SafeRelativePath $file.local)) { throw "Manifest entry $($file.name) has an unsafe path." }
    $plannedEntry = New-PlanEntry "faf" $file.name $file.purpose $file.tier $selected $file.dest
    if (-not $selected) {
        $plannedEntry.Bytes = [long]$file.size
        continue
    }
    $expectedSize = [long]$file.size
    $expectedSha256 = ([string]$file.sha256).ToLowerInvariant()
    if ($FafSource -eq "Local") {
        $source = [IO.Path]::Combine($FafPath, $file.local.Replace('/', '\'))
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            $plannedEntry.Note = "missing"
            $problems.Add("$($file.name): $source not found. The FAF client downloads it when it starts a FAF game; or use -FafSource Download.")
            continue
        }
        $actualSize = (Get-Item -LiteralPath $source).Length
        if ($actualSize -ne $expectedSize) {
            $plannedEntry.Note = "size differs"
            $problems.Add("$($file.name): $source has $actualSize bytes, the manifest (FAF $fafVersion) expects $expectedSize. Probably another FAF version; use -FafSource Download.")
            continue
        }
        Write-Host "  checking $($file.name) ($(Format-Size $actualSize))"
        $hash = Get-Sha256 $source
        if ($hash -ne $expectedSha256) {
            $plannedEntry.Note = "sha256 differs"
            $problems.Add("$($file.name): sha256 of $source is $hash, the manifest (FAF $fafVersion) expects $expectedSha256; use -FafSource Download.")
            continue
        }
        $plannedEntry.Note = "sha256 ok"
    } else {
        $source = [IO.Path]::Combine($fafCacheRoot, $file.local.Replace('/', '\'))
        $url = "$($faf.baseUrl.TrimEnd('/'))/$($file.remote.Replace('{v}', "$fafVersion"))"
        $cached = $false
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            if ((Get-Item -LiteralPath $source).Length -eq $expectedSize -and (Get-Sha256 $source) -eq $expectedSha256) {
                $cached = $true
                $plannedEntry.Note = "cached, sha256 ok"
            } elseif (-not $DryRun) {
                Remove-Item -LiteralPath $source -Force
            }
        }
        if (-not $cached) {
            $partialBytes = [long]0
            if (Test-Path -LiteralPath "$source.part") { $partialBytes = (Get-Item -LiteralPath "$source.part").Length }
            try {
                $remoteLength = Get-RemoteLength $url
                if ($null -ne $remoteLength -and $remoteLength -ne $expectedSize) {
                    $problems.Add("$($file.name): $url has $remoteLength bytes, the manifest expects $expectedSize.")
                }
                $plannedEntry.Note = if ($partialBytes -gt 0) { "download, resume at $(Format-Size $partialBytes)" } else { "download" }
            } catch {
                $plannedEntry.Note = "unreachable"
                $problems.Add("$($file.name): $url is not reachable: $($_.Exception.Message)")
            }
            $fafDownloads.Add([pscustomobject]@{ Name = $file.name; Url = $url; Destination = $source; Size = $expectedSize; Sha256 = $expectedSha256 })
        }
    }
    # A file still to be downloaded is planned at its cache path; the
    # downloads run before anything is copied.
    $unitFiles = New-Object System.Collections.Generic.List[object]
    $unitFiles.Add([pscustomobject]@{ Source = $source; Dest = $file.dest; Size = $expectedSize })
    $plannedEntry.Units.Add([pscustomobject]@{ Source = $source; Dest = $file.dest; IsDirectory = $false; Files = $unitFiles; Bytes = $expectedSize })
    $plannedEntry.Bytes = $expectedSize
    $plannedEntry.FileCount = 1
    $seenDestinations[$file.dest.ToLowerInvariant()] = $file.name
    $fafDeployed = $true
}

# --- SCFA ---

$scfa = $null
if ($ScfaPath) {
    $ScfaPath = Resolve-FullPath $ScfaPath
    if (-not (Test-Path -LiteralPath $ScfaPath -PathType Container)) { throw "-ScfaPath $ScfaPath does not exist." }
    $scfa = [pscustomobject]@{ Path = $ScfaPath; Origin = "-ScfaPath" }
} else {
    $scfa = Find-ScfaInstall $manifest.scfa.detect
}
foreach ($entry in @($manifest.scfa.entries)) {
    Add-SelectionEntry "scfa" $entry $scfa.Path $layout.scfa (Test-Selected $entry.id $entry.tier)
}

# --- vault (only when asked for: it is the user's own download collection) ---

$vault = $null
$vaultSelected = @($manifest.user.entries | Where-Object { $includeSet.ContainsKey($_.id.ToLowerInvariant()) }).Count -gt 0
if ($vaultSelected) {
    $vault = if ($VaultPath) { [pscustomobject]@{ Path = (Resolve-FullPath $VaultPath); Origin = "-VaultPath" } } else { Get-VaultDefault }
    if (-not (Test-Path -LiteralPath $vault.Path -PathType Container)) {
        $warnings.Add("Vault $($vault.Path) does not exist; nothing to copy from it.")
        $vault.Path = $null
    }
}
foreach ($entry in @($manifest.user.entries)) {
    $selected = $includeSet.ContainsKey($entry.id.ToLowerInvariant())
    if ($selected) {
        Add-SelectionEntry "vault" $entry $vault.Path $layout.vault $true
    } else {
        $skipped = New-PlanEntry "vault" $entry.id $entry.label $entry.tier $false "$($layout.vault)/$($entry.src)"
        $skipped.Note = "-IncludeVault"
    }
}

# --- print the plan ---

$selectedEntries = @($plan | Where-Object { $_.Selected })
$totalBytes = [long]0
$totalFiles = 0
foreach ($entry in $selectedEntries) { $totalBytes += $entry.Bytes; $totalFiles += $entry.FileCount }

$target = if ($StageDir) { "stage folder $StageDir" } else { "device /sdcard/Android/data/$Package/files" }
Write-Host ""
Write-Host "Deploy plan: tier $Tier$(if ($includeSet.Count -gt 0) { ' + ' + (($includeSet.Keys | ForEach-Object { $knownIds[$_] } | Sort-Object) -join ', ') }) -> $target"
Write-Host "  FAF $fafVersion  $fafOrigin"
Write-Host "  SCFA      $($scfa.Path) ($($scfa.Origin))"
if ($vault) { Write-Host "  vault     $(if ($vault.Path) { $vault.Path } else { '-' }) ($($vault.Origin))" }
if ($deviceMode) {
    if ($NoInstall) { Write-Host "  APK       not installed (-NoInstall)" }
    else { Write-Host "  APK       $(if ($apkPath) { $apkPath } else { 'none built yet' })" }
}
Write-Host ""
$format = "  {0,-3} {1,-12} {2,-26} {3,11} {4,11}  {5}"
Write-Host ($format -f "", "tier", "entry", "files", "size", "destination")
foreach ($entry in $plan) {
    $mark = if ($entry.Selected) { "[x]" } else { "[ ]" }
    $directories = @($entry.Units | Where-Object { $_.IsDirectory }).Count
    $files = if ($directories -eq 1) { "1 dir" } elseif ($directories -gt 1) { "$directories dirs" } elseif ($entry.FileCount -gt 0) { "$($entry.FileCount)" } else { "" }
    $size = if ($entry.Bytes -gt 0) { Format-Size $entry.Bytes } else { "" }
    $note = if ($entry.Note) { "  ($($entry.Note))" } else { "" }
    Write-Host ($format -f $mark, $entry.Tier, $entry.Id, $files, $size, "$($entry.DestDir)$note")
}
Write-Host ""
foreach ($tierName in @("required", "recommended", "optional")) {
    $tierEntries = @($selectedEntries | Where-Object { $_.Tier -eq $tierName })
    if ($tierEntries.Count -eq 0) { continue }
    $tierBytes = [long]0
    foreach ($entry in $tierEntries) { $tierBytes += $entry.Bytes }
    Write-Host ("  {0,-12} {1,10}" -f $tierName, (Format-Size $tierBytes))
}
Write-Host ("  {0,-12} {1,10}  in {2} files" -f "total", (Format-Size $totalBytes), $totalFiles)
if ($fafDownloads.Count -gt 0) {
    $downloadBytes = [long]0
    foreach ($download in $fafDownloads) { $downloadBytes += $download.Size }
    Write-Host ("  {0,-12} {1,10}  ({2} FAF files)" -f "to download", (Format-Size $downloadBytes), $fafDownloads.Count)
}
Write-Host "  Not selected entries are marked [ ]; add them with -Include <entry> (or -Tier all, -IncludeVault)."
foreach ($warning in $warnings) { Write-Warning $warning }
if ($problems.Count -gt 0) {
    Write-Host ""
    foreach ($problem in $problems) { Write-Host "  ERROR $problem" -ForegroundColor Red }
    throw "The deploy plan has $($problems.Count) problem(s); nothing was copied."
}
if ($DryRun) {
    Write-Host ""
    Write-Host "Dry run: nothing was downloaded, installed or copied."
    return
}

# =====================================================================================
# Execute
# =====================================================================================

function Get-DeployRoot {
    if ($StageDir) { return $StageDir.Replace('\', '/').TrimEnd('/') }
    return "/sdcard/Android/data/$Package/files"
}
$deployRoot = Get-DeployRoot

function Format-FaPathLua {
    $template = [string]$manifest.faPathLua.template
    $values = @{
        "root" = $deployRoot
        "version" = "$fafVersion"
        "client" = "faf-re-android $versionName"
    }
    # One pass, so a value containing "{...}" is never substituted again.
    return [regex]::Replace($template, '\{(root|version|client)\}', {
        param($match)
        ConvertTo-LuaStringContent $values[$match.Groups[1].Value]
    })
}

function Format-FafVersionJson {
    return '{"featuredMod":' + (ConvertTo-JsonString ([string]$faf.featuredMod)) + ',"version":' + $fafVersion + ',"source":"pc-deploy"}' + "`n"
}

$deployUnits = New-Object System.Collections.Generic.List[object]
foreach ($entry in $selectedEntries) { foreach ($unit in $entry.Units) { $deployUnits.Add($unit) } }

if ($deviceMode) {
    $script:adbPath = Resolve-Adb
    $script:adbSerial = Select-Device
    $sdk = Get-DeviceProperty "ro.build.version.sdk"
    $abis = Get-DeviceProperty "ro.product.cpu.abilist"
    $model = Get-DeviceProperty "ro.product.model"
    Write-Host ""
    Write-Host "Device $script:adbSerial ($model), Android API $sdk, ABIs $abis"
    if (-not ($sdk -match '^\d+$') -or [int]$sdk -lt $minimumSdk) { throw "The app needs Android 8.0 (API $minimumSdk) or newer; this device reports API '$sdk'." }
    if (($abis -split ',') -notcontains $requiredAbi) { throw "The app is built for $requiredAbi only; this device supports $abis." }
}

# Downloads come after the device checks so a missing device fails fast.
foreach ($download in $fafDownloads) {
    Write-Host "Downloading $($download.Name) ($(Format-Size $download.Size)) from $($download.Url)"
    Receive-VerifiedFile $download.Url $download.Destination $download.Size $download.Sha256
}

$generatedDirectory = $null
$placed = New-Object System.Collections.Generic.List[object]
try {
    if ($deviceMode) {
        # --- install ---
        if ($NoInstall) {
            $installed = Invoke-AdbShell "pm path $Package"
            if ($installed.Text -notmatch 'package:') { throw "$Package is not installed on the device; run without -NoInstall." }
        } else {
            Write-Host ""
            Write-Host "Installing $(Split-Path -Leaf $apkPath)"
            $install = Invoke-Adb @("install", "-r", $apkPath)
            if ($install.ExitCode -ne 0 -or $install.Text -notmatch '(?m)^Success') {
                Write-Host $install.Text
                if ($install.Text -match 'INSTALL_FAILED_UPDATE_INCOMPATIBLE') {
                    throw ("The installed $Package is signed with a different key than this APK, so Android refuses the update. " +
                        "Uninstalling it once fixes that, but also deletes everything in /sdcard/Android/data/$Package (the copied game data, settings and logs): " +
                        "adb uninstall $Package  -- then run this script again. The script never uninstalls on its own.")
                }
                if ($install.Text -match 'INSTALL_FAILED_VERSION_DOWNGRADE') {
                    throw "The device has a newer build (higher versionCode) installed. Build from a newer commit, or downgrade by hand with: adb install -r -d `"$apkPath`""
                }
                throw "adb install failed."
            }
        }

        # --- data root: the app creates its external files directory itself,
        # which gives it the right owner; start the launcher once for that ---
        $existsCommand = "test -d $(ConvertTo-ShellArgument $deployRoot) && echo present"
        if ((Invoke-AdbShell $existsCommand).Text -notmatch 'present') {
            Write-Host "Starting the launcher once so the app creates $deployRoot"
            $start = Invoke-AdbShell "am start -W -n $Package/$launcherActivity"
            if ($start.ExitCode -ne 0 -or $start.Text -match 'Error') { Write-Host $start.Text; throw "Could not start $Package/$launcherActivity." }
            $deadline = [DateTime]::UtcNow.AddSeconds(30)
            while ((Invoke-AdbShell $existsCommand).Text -notmatch 'present') {
                if ([DateTime]::UtcNow -gt $deadline) { throw "The app did not create $deployRoot within 30 s. Open 'FAF (faf-re)' on the device once, then run again with -NoInstall." }
                Start-Sleep -Milliseconds 500
            }
        }

        # --- what is there already, and does the rest fit? ---
        $inventory = Get-RemoteInventory $deployRoot
        $transferBytes = [long]0
        foreach ($unit in $deployUnits) {
            foreach ($file in $unit.Files) {
                if (-not ($inventory.ContainsKey($file.Dest) -and $inventory[$file.Dest] -eq $file.Size)) { $transferBytes += $file.Size }
            }
        }
        $freeBytes = Get-RemoteFreeBytes $deployRoot
        Write-Host ("To copy: {0} of {1}; free on the device: {2}" -f (Format-Size $transferBytes), (Format-Size $totalBytes), $(if ($null -ne $freeBytes) { Format-Size $freeBytes } else { "unknown" }))
        if ($null -ne $freeBytes -and $transferBytes + $freeSpaceReserve -gt $freeBytes) {
            throw "Not enough space on the device: $(Format-Size $transferBytes) to copy plus $(Format-Size $freeSpaceReserve) reserve, $(Format-Size $freeBytes) free. Choose a smaller -Tier or free up space."
        }
        # Parent folders first: a directory push lands inside an existing parent.
        $mkdirTargets = @($deployUnits | ForEach-Object { "$deployRoot/" + ($_.Dest -replace '/[^/]+$', '') } | Select-Object -Unique)
        for ($first = 0; $first -lt $mkdirTargets.Count; $first += 40) {
            $part = @($mkdirTargets[$first..([Math]::Min($first + 39, $mkdirTargets.Count - 1))])
            $mkdir = Invoke-AdbShell ("mkdir -p " + (($part | ForEach-Object { ConvertTo-ShellArgument $_ }) -join " "))
            if ($mkdir.ExitCode -ne 0) { throw "Creating folders on the device failed:`n$($mkdir.Text)" }
        }
    } else {
        if (-not (Test-Path -LiteralPath $StageDir)) { New-Item -ItemType Directory -Path $StageDir -Force | Out-Null }
        if ($Hardlink) { Initialize-HardLinkSupport }
        # The folders the app would create on first start.
        foreach ($directory in @($layout.scfa, $layout.fafBin, $layout.fafGamedata, "$($layout.vault)/maps", "$($layout.vault)/mods",
                $layout.localAppData, $layout.documents, $layout.logs, $layout.launch)) {
            $path = Join-Path $StageDir $directory.Replace('/', '\')
            if (-not (Test-Path -LiteralPath $path)) { New-Item -ItemType Directory -Path $path -Force | Out-Null }
        }
    }

    # --- copy ---
    Write-Host ""
    $script:stagingFallback = $false
    $hardlinkVolumeWarned = $false
    $counts = @{ copied = 0; linked = 0; current = 0 }
    $doneBytes = [long]0
    $index = 0
    foreach ($unit in $deployUnits) {
        $index++
        $label = "[{0,3}/{1}] {2}" -f $index, $deployUnits.Count, $unit.Dest
        $clock = [Diagnostics.Stopwatch]::StartNew()
        # [long] on both sides: with an int literal Windows PowerShell picks
        # Math.Max(int, int), which throws once the total passes 2 GB.
        Write-Progress -Activity "Deploying game data" -Status $unit.Dest -PercentComplete ([int](100 * $doneBytes / [Math]::Max([long]1, [long]$totalBytes)))

        if ($deviceMode) {
            $upToDate = $true
            foreach ($file in $unit.Files) {
                if (-not ($inventory.ContainsKey($file.Dest) -and $inventory[$file.Dest] -eq $file.Size)) { $upToDate = $false; break }
            }
            if ($upToDate) {
                Write-Host "$label  $(Format-Size $unit.Bytes)  up to date"
                $counts.current++
            } else {
                $remote = "$deployRoot/$($unit.Dest)"
                $pushed = $false
                if (-not $script:stagingFallback) {
                    # A directory goes into its parent, which exists (mkdir -p above),
                    # so adb places it at <parent>/<name>.
                    $pushTarget = if ($unit.IsDirectory) { ($remote -replace '/[^/]+$', '') + "/" } else { $remote }
                    $push = Invoke-Adb @("push", "--sync", $unit.Source, $pushTarget)
                    if ($push.ExitCode -eq 0) {
                        $pushed = $true
                    } elseif ($push.Text -match 'fchown') {
                        # Some devices refuse adb's ownership change on another app's
                        # external directory. Pushing to /data/local/tmp and moving the
                        # file with the shell works there; keep doing that from now on.
                        Write-Host "    adb push cannot set the owner here (fchown); continuing through /data/local/tmp"
                        $script:stagingFallback = $true
                    } else {
                        throw "adb push $($unit.Source) failed:`n$($push.Text)"
                    }
                }
                if (-not $pushed) {
                    $temporary = "/data/local/tmp/faf-re-deploy.tmp"
                    foreach ($file in $unit.Files) {
                        $push = Invoke-Adb @("push", $file.Source, $temporary)
                        if ($push.ExitCode -ne 0) { throw "adb push $($file.Source) to $temporary failed:`n$($push.Text)" }
                        $destination = "$deployRoot/$($file.Dest)"
                        $parent = $destination -replace '/[^/]+$', ''
                        $move = Invoke-AdbShell "mkdir -p $(ConvertTo-ShellArgument $parent) && mv -f $(ConvertTo-ShellArgument $temporary) $(ConvertTo-ShellArgument $destination)"
                        if ($move.ExitCode -ne 0) { throw "Moving $($file.Dest) into place failed:`n$($move.Text)" }
                    }
                }
                $seconds = [Math]::Max(0.001, $clock.Elapsed.TotalSeconds)
                Write-Host ("{0}  {1}  copied in {2:N1} s ({3}/s)" -f $label, (Format-Size $unit.Bytes), $seconds, (Format-Size ($unit.Bytes / $seconds)))
                $counts.copied++
            }
        } else {
            $linkedHere = 0
            $copiedHere = 0
            foreach ($file in $unit.Files) {
                $destination = Join-Path $StageDir $file.Dest.Replace('/', '\')
                $sourceItem = Get-Item -LiteralPath $file.Source -Force
                if (Test-Path -LiteralPath $destination -PathType Leaf) {
                    $existing = Get-Item -LiteralPath $destination -Force
                    if ($existing.Length -eq $sourceItem.Length -and $existing.LastWriteTimeUtc -eq $sourceItem.LastWriteTimeUtc) { continue }
                    # Delete first: writing into a hard link would write into the
                    # original file in the install.
                    Remove-Item -LiteralPath $destination -Force
                }
                $parent = Split-Path -Parent $destination
                if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
                $linked = $false
                if ($Hardlink) {
                    if ([IO.Path]::GetPathRoot($file.Source) -eq [IO.Path]::GetPathRoot($destination)) {
                        $linked = [FafRe.NativeMethods]::CreateHardLink($destination, $file.Source, [IntPtr]::Zero)
                        if (-not $linked -and -not $hardlinkVolumeWarned) {
                            Write-Warning "Hard link to $($file.Source) failed (error $([Runtime.InteropServices.Marshal]::GetLastWin32Error())); copying instead."
                            $hardlinkVolumeWarned = $true
                        }
                    } elseif (-not $hardlinkVolumeWarned) {
                        Write-Warning "$($file.Source) is on another volume than $StageDir; copying instead of linking."
                        $hardlinkVolumeWarned = $true
                    }
                }
                if ($linked) { $linkedHere++ } else { [IO.File]::Copy($file.Source, $destination, $false); $copiedHere++ }
            }
            $verb = if ($linkedHere + $copiedHere -eq 0) { "up to date" } elseif ($copiedHere -eq 0) { "linked" } elseif ($linkedHere -eq 0) { "copied" } else { "linked/copied" }
            Write-Host "$label  $(Format-Size $unit.Bytes)  $verb"
            if ($linkedHere + $copiedHere -eq 0) { $counts.current++ } elseif ($copiedHere -eq 0) { $counts.linked++ } else { $counts.copied++ }
        }
        $doneBytes += $unit.Bytes
        foreach ($file in $unit.Files) { $placed.Add($file) }
    }
    Write-Progress -Activity "Deploying game data" -Completed

    # --- generated files: fa_path.lua, version.json, deployed.json ---
    $faPathText = Format-FaPathLua
    $versionText = Format-FafVersionJson
    if ($deviceMode) {
        $generatedDirectory = Join-Path ([IO.Path]::GetTempPath()) ("faf-re-deploy-" + [Guid]::NewGuid().ToString("N").Substring(0, 8))
        New-Item -ItemType Directory -Path $generatedDirectory -Force | Out-Null
        $previous = Invoke-AdbShell "cat $(ConvertTo-ShellArgument "$deployRoot/$($layout.deployRecord)") 2>/dev/null"
        $records = Read-DeployRecord $(if ($previous.ExitCode -eq 0) { $previous.Text } else { "" })
    } else {
        $recordPath = Join-Path $StageDir $layout.deployRecord.Replace('/', '\')
        $records = Read-DeployRecord $(if (Test-Path -LiteralPath $recordPath) { [IO.File]::ReadAllText($recordPath) } else { "" })
    }
    foreach ($file in $placed) { $records[$file.Dest.ToLowerInvariant()] = [pscustomobject]@{ Dest = $file.Dest; Size = $file.Size } }

    $generated = [ordered]@{}
    $generated[$layout.faPathLua] = $faPathText
    if ($fafDeployed) { $generated[$layout.fafVersion] = $versionText }
    $generated[$layout.deployRecord] = Format-DeployRecord $records $versionName
    foreach ($relative in $generated.Keys) {
        if ($deviceMode) {
            $local = Join-Path $generatedDirectory (Split-Path -Leaf $relative)
            Write-Utf8File $local $generated[$relative]
            $remote = "$deployRoot/$relative"
            Invoke-AdbShell "mkdir -p $(ConvertTo-ShellArgument ($remote -replace '/[^/]+$', ''))" | Out-Null
            $push = if ($script:stagingFallback) { $null } else { Invoke-Adb @("push", $local, $remote) }
            if ($null -eq $push -or $push.ExitCode -ne 0) {
                $push = Invoke-Adb @("push", $local, "/data/local/tmp/faf-re-deploy.tmp")
                $move = Invoke-AdbShell "mv -f /data/local/tmp/faf-re-deploy.tmp $(ConvertTo-ShellArgument $remote)"
                if ($push.ExitCode -ne 0 -or $move.ExitCode -ne 0) { throw "Could not write $remote`n$($push.Text)`n$($move.Text)" }
            }
        } else {
            Write-Utf8File (Join-Path $StageDir $relative.Replace('/', '\')) $generated[$relative]
        }
        Write-Host "wrote $relative"
    }

    if ($deviceMode) {
        # Folders that adb creates belong to the shell user (drwxrws---, group
        # ext_data_rw), which the app is neither nor a member of: it could not
        # even list faf/ or scfa/ and reported every pushed file as missing.
        # Seen on the API 36 emulator. Android/data/<package> is reachable only
        # by the app and adb, so opening the tree to "other" exposes nothing,
        # and the app also needs write access there (fa_path.lua, FAF updates).
        $topLevel = @($placed | ForEach-Object { ($_.Dest -split '/')[0] }) + @(".deploy", ($layout.faPathLua -split '/')[0]) | Select-Object -Unique
        $chmod = Invoke-AdbShell ("chmod -R a+rwX " + (($topLevel | ForEach-Object { ConvertTo-ShellArgument "$deployRoot/$_" }) -join " "))
        if ($chmod.ExitCode -ne 0) { Write-Warning "chmod on the device failed (the app may not be able to read these files):`n$($chmod.Text)" }
        if ($Launch) {
            $start = Invoke-AdbShell "am start -n $Package/$launcherActivity"
            if ($start.ExitCode -ne 0) { Write-Warning "Could not start the launcher:`n$($start.Text)" }
        }
    }
} finally {
    if ($generatedDirectory -and (Test-Path -LiteralPath $generatedDirectory)) { Remove-Item -LiteralPath $generatedDirectory -Recurse -Force }
}

Write-Host ""
$summary = "Done: $($counts.copied) copied, $($counts.current) up to date"
if ($StageDir) { $summary += ", $($counts.linked) linked" }
Write-Host "$summary; $(Format-Size $totalBytes) of game data in $deployRoot" -ForegroundColor Green
if ($StageDir) {
    Write-Host "Host check: faf_datacheck --init `"$StageDir\$($layout.fafBin.Replace('/', '\'))\init_faf.lua`" --localappdata `"$StageDir\$($layout.localAppData)`" --documents `"$StageDir\$($layout.documents)`""
} elseif (-not $Launch) {
    Write-Host "Open 'FAF (faf-re)' on the device and press Start."
}

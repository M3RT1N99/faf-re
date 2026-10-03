[CmdletBinding()]
param(
    [string]$AndroidSdk = $(if ($env:ANDROID_SDK_ROOT) { $env:ANDROID_SDK_ROOT } else { "C:\Android\sdk" })
)

$ErrorActionPreference = "Stop"
$apk = Get-ChildItem -LiteralPath $PSScriptRoot -Filter "faf-android-*-arm64-v8a.apk" | Select-Object -First 1
if (-not $apk) { throw "Versioned arm64 APK not found beside this installer." }
$adb = Join-Path $AndroidSdk "platform-tools\adb.exe"
if (-not (Test-Path -LiteralPath $adb)) { throw "adb not found at $adb. Set ANDROID_SDK_ROOT." }

& $adb install -r $apk.FullName
if ($LASTEXITCODE -ne 0) { throw "APK installation failed. Confirm USB debugging and authorize this computer on the device." }
Write-Host "Installed $($apk.Name)."

[CmdletBinding()]
param(
    [string]$AndroidSdk = $(if ($env:ANDROID_SDK_ROOT) { $env:ANDROID_SDK_ROOT } else { "C:\Android\sdk" }),
    [int]$ApiLevel = 26,
    [string]$BuildDirectory = "buildstage\android-launcher"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$buildToolsDirectory = Join-Path $AndroidSdk "build-tools\35.0.1"
$platformDirectory = Join-Path $AndroidSdk "platforms\android-35"
$buildPath = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $repoRoot $BuildDirectory }

$versionProperties = Get-Content -LiteralPath (Join-Path $repoRoot "port\android\version.properties")
$versionCodeLine = $versionProperties | Where-Object { $_ -match '^versionCode=' } | Select-Object -First 1
$versionNameLine = $versionProperties | Where-Object { $_ -match '^versionName=' } | Select-Object -First 1
if (-not $versionCodeLine -or -not $versionNameLine) { throw "Android version.properties must define versionCode and versionName." }
$versionCode = [int]($versionCodeLine -replace '^versionCode=', '')
$versionName = $versionNameLine -replace '^versionName=', ''
if ($versionCode -lt 1 -or $versionName -notmatch '^\d+\.\d+\.\d+[-A-Za-z0-9.]*$') { throw "Invalid Android version: $versionCode / $versionName" }
if ($ApiLevel -lt 26 -or $ApiLevel -gt 35) { throw "ApiLevel must be between 26 and 35." }

$requiredTools = @(
    (Join-Path $buildToolsDirectory "aapt2.exe"),
    (Join-Path $buildToolsDirectory "d8.bat"),
    (Join-Path $buildToolsDirectory "zipalign.exe"),
    (Join-Path $buildToolsDirectory "apksigner.bat"),
    (Join-Path $platformDirectory "android.jar")
)
foreach ($tool in $requiredTools) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Android build tool missing: $tool" }
}

$javac = Get-Command javac.exe -ErrorAction Stop
$javaHome = Split-Path -Parent (Split-Path -Parent $javac.Source)
$env:JAVA_HOME = $javaHome
$env:PATH = "$(Join-Path $javaHome 'bin');$env:PATH"
$keytool = Join-Path $javaHome "bin\keytool.exe"
if (-not (Test-Path -LiteralPath $keytool)) { throw "keytool not found in selected JDK: $keytool" }

New-Item -ItemType Directory -Path $buildPath -Force | Out-Null
$classesDirectory = Join-Path $buildPath "java-classes"
$dexDirectory = Join-Path $buildPath "dex"
New-Item -ItemType Directory -Path $classesDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $dexDirectory -Force | Out-Null

$androidJar = Join-Path $platformDirectory "android.jar"
$javaSources = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot "port\android\src\java") -Recurse -Filter "*.java" | ForEach-Object { $_.FullName })
if ($javaSources.Count -eq 0) { throw "No Android Java sources found." }
& $javac.Source -encoding UTF-8 -source 8 -target 8 -classpath $androidJar -d $classesDirectory $javaSources
if ($LASTEXITCODE -ne 0) { throw "Android launcher Java compilation failed." }

$classFiles = @(Get-ChildItem -LiteralPath $classesDirectory -Recurse -Filter "*.class" | ForEach-Object { $_.FullName })
& (Join-Path $buildToolsDirectory "d8.bat") --min-api $ApiLevel --lib $androidJar --output $dexDirectory $classFiles
if ($LASTEXITCODE -ne 0) { throw "Android DEX compilation failed." }

$unsignedApk = Join-Path $buildPath "faf-unsigned.apk"
$alignedApk = Join-Path $buildPath "faf-aligned.apk"
$debugApk = Join-Path $buildPath "faf-android-debug.apk"
$compiledResources = Join-Path $buildPath "compiled-resources.zip"
$keyStore = Join-Path $buildPath "debug.keystore"
$aapt2 = Join-Path $buildToolsDirectory "aapt2.exe"
& $aapt2 compile --dir (Join-Path $repoRoot "port\android\res") -o $compiledResources
if ($LASTEXITCODE -ne 0) { throw "Android resources compilation failed." }

& $aapt2 link `
    --manifest (Join-Path $repoRoot "port\android\AndroidManifest.xml") `
    -I $androidJar `
    -R $compiledResources `
    --auto-add-overlay `
    --min-sdk-version $ApiLevel --target-sdk-version 35 `
    --version-code $versionCode --version-name $versionName `
    -o $unsignedApk
if ($LASTEXITCODE -ne 0) { throw "Android manifest packaging failed." }

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::Open($unsignedApk, [System.IO.Compression.ZipArchiveMode]::Update)
try {
    $dexFile = Join-Path $dexDirectory "classes.dex"
    if (-not (Test-Path -LiteralPath $dexFile)) { throw "DEX compiler did not produce classes.dex." }
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $archive, $dexFile, "classes.dex", [System.IO.Compression.CompressionLevel]::Optimal
    ) | Out-Null
} finally {
    $archive.Dispose()
}

& (Join-Path $buildToolsDirectory "zipalign.exe") -f -p 4 $unsignedApk $alignedApk
if ($LASTEXITCODE -ne 0) { throw "APK alignment failed." }

if (-not (Test-Path -LiteralPath $keyStore)) {
    & $keytool -genkeypair -keystore $keyStore -storepass android -keypass android `
        -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 `
        -dname "CN=Android Debug,O=Android,C=US"
    if ($LASTEXITCODE -ne 0) { throw "Could not create a local debug signing key." }
}

& (Join-Path $buildToolsDirectory "apksigner.bat") sign `
    --ks $keyStore --ks-pass pass:android --key-pass pass:android `
    --out $debugApk $alignedApk
if ($LASTEXITCODE -ne 0) { throw "APK signing failed." }

$releaseDirectory = Join-Path $repoRoot "output\android"
$bundleDirectory = Join-Path $buildPath "distribution"
$versionedApkName = "faf-android-$versionName-arm64-v8a.apk"
$versionedApk = Join-Path $releaseDirectory $versionedApkName
$bundleZip = Join-Path $releaseDirectory "faf-android-$versionName-arm64-v8a.zip"
New-Item -ItemType Directory -Path $releaseDirectory -Force | Out-Null
if (Test-Path -LiteralPath $bundleDirectory) { Remove-Item -LiteralPath $bundleDirectory -Recurse -Force }
New-Item -ItemType Directory -Path $bundleDirectory -Force | Out-Null
Copy-Item -LiteralPath $debugApk -Destination $versionedApk -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "port\android\INSTALL.txt") -Destination $bundleDirectory
Copy-Item -LiteralPath (Join-Path $repoRoot "port\android\install.ps1") -Destination $bundleDirectory
Copy-Item -LiteralPath $versionedApk -Destination $bundleDirectory
@("versionCode=$versionCode", "versionName=$versionName", "abi=arm64-v8a") |
    Set-Content -LiteralPath (Join-Path $bundleDirectory "VERSION.txt") -Encoding Ascii
if (Test-Path -LiteralPath $bundleZip) { Remove-Item -LiteralPath $bundleZip -Force }
[System.IO.Compression.ZipFile]::CreateFromDirectory($bundleDirectory, $bundleZip)

Write-Host "Built Android launcher APK: $versionedApk"
Write-Host "Install bundle: $bundleZip"

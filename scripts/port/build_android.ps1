[CmdletBinding()]
param(
    [string]$AndroidSdk = $(if ($env:ANDROID_SDK_ROOT) { $env:ANDROID_SDK_ROOT } else { "C:\Android\sdk" }),
    [string]$Abi = "arm64-v8a",
    [int]$ApiLevel = 26,
    [string]$BuildDirectory = "buildstage\android"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$ndkPath = Join-Path $AndroidSdk "ndk\29.0.14206865"
$cmakePath = (Get-Command cmake.exe -ErrorAction Stop).Source
$ninjaDirectory = Join-Path $AndroidSdk "cmake\3.10.2.4988404\bin"
$buildToolsDirectory = Join-Path $AndroidSdk "build-tools\35.0.1"
$platformDirectory = Join-Path $AndroidSdk "platforms\android-35"
$buildPath = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $repoRoot $BuildDirectory }

if (-not (Test-Path (Join-Path $ndkPath "build\cmake\android.toolchain.cmake"))) {
    throw "Android NDK 29.0.14206865 not found under $AndroidSdk. Set ANDROID_SDK_ROOT or run bootstrap_android.ps1 for DiligentCore."
}
if (-not (Test-Path $cmakePath)) { throw "Android SDK CMake not found: $cmakePath" }
$cmakeVersionText = (& $cmakePath --version | Select-Object -First 1)
if ($cmakeVersionText -notmatch "cmake version ([0-9]+\.[0-9]+)") { throw "Could not read CMake version from $cmakePath" }
if ([version]$Matches[1] -lt [version]"3.22") { throw "CMake 3.22 or newer is required; found $($Matches[1]) at $cmakePath" }
if (-not (Test-Path (Join-Path $ninjaDirectory "ninja.exe"))) { throw "Android SDK Ninja not found under $ninjaDirectory" }
if (-not (Test-Path (Join-Path $buildToolsDirectory "aapt2.exe"))) { throw "Android build-tools 35.0.1 not found under $AndroidSdk" }
if (-not (Test-Path (Join-Path $buildToolsDirectory "zipalign.exe"))) { throw "zipalign not found under $buildToolsDirectory" }
if (-not (Test-Path (Join-Path $buildToolsDirectory "apksigner.bat"))) { throw "apksigner not found under $buildToolsDirectory" }
if (-not (Test-Path (Join-Path $platformDirectory "android.jar"))) { throw "Android platform 35 not found under $AndroidSdk" }
if (-not (Test-Path (Join-Path $repoRoot "dependencies\DiligentCore\CMakeLists.txt"))) {
    throw "DiligentCore is missing. Run scripts/port/bootstrap_android.ps1 first."
}

$env:PATH = "$ninjaDirectory;$env:PATH"
New-Item -ItemType Directory -Path $buildPath -Force | Out-Null
& $cmakePath -S (Join-Path $repoRoot "port\android") -B $buildPath -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $ndkPath 'build\cmake\android.toolchain.cmake')" `
    "-DANDROID_ABI=$Abi" "-DANDROID_PLATFORM=android-$ApiLevel" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "Android CMake configure failed." }

& $cmakePath --build $buildPath --target faf_android --parallel
if ($LASTEXITCODE -ne 0) { throw "Android native build failed." }

$unsignedApk = Join-Path $buildPath "faf-unsigned.apk"
$alignedApk = Join-Path $buildPath "faf-aligned.apk"
$debugApk = Join-Path $buildPath "faf-android-debug.apk"
$keyStore = Join-Path $buildPath "debug.keystore"
$packageRoot = Join-Path $buildPath "apk"
$libraryDirectory = Join-Path $packageRoot "lib\$Abi"
New-Item -ItemType Directory -Path $libraryDirectory -Force | Out-Null

& (Join-Path $buildToolsDirectory "aapt2.exe") link `
    --manifest (Join-Path $repoRoot "port\android\AndroidManifest.xml") `
    -I (Join-Path $platformDirectory "android.jar") `
    --min-sdk-version $ApiLevel --target-sdk-version 35 `
    -o $unsignedApk
if ($LASTEXITCODE -ne 0) { throw "Android manifest packaging failed." }

$vulkanLibrary = Join-Path $buildPath "DiligentCore\Graphics\GraphicsEngineVulkan\libGraphicsEngineVk.so"
if (-not (Test-Path $vulkanLibrary)) { throw "Diligent Vulkan library missing: $vulkanLibrary" }
Copy-Item -LiteralPath (Join-Path $buildPath "libfaf_android.so") -Destination (Join-Path $libraryDirectory "libfaf_android.so") -Force
Copy-Item -LiteralPath $vulkanLibrary -Destination (Join-Path $libraryDirectory "libGraphicsEngineVk.so") -Force

Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$archive = [System.IO.Compression.ZipFile]::Open($unsignedApk, [System.IO.Compression.ZipArchiveMode]::Update)
try {
    foreach ($library in @("libfaf_android.so", "libGraphicsEngineVk.so")) {
        $source = Join-Path $libraryDirectory $library
        $entry = "lib/$Abi/$library"
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $source, $entry, [System.IO.Compression.CompressionLevel]::Optimal
        ) | Out-Null
    }
} finally {
    $archive.Dispose()
}

& (Join-Path $buildToolsDirectory "zipalign.exe") -f -p 4 $unsignedApk $alignedApk
if ($LASTEXITCODE -ne 0) { throw "APK alignment failed." }

if (-not (Test-Path $keyStore)) {
    & keytool.exe -genkeypair -keystore $keyStore -storepass android -keypass android `
        -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 `
        -dname "CN=Android Debug,O=Android,C=US"
    if ($LASTEXITCODE -ne 0) { throw "Could not create a local debug signing key." }
}

$keyToolPath = (Get-Command keytool.exe -ErrorAction Stop).Source
$env:JAVA_HOME = Split-Path -Parent (Split-Path -Parent $keyToolPath)
$env:PATH = "$(Join-Path $env:JAVA_HOME 'bin');$env:PATH"
& (Join-Path $buildToolsDirectory "apksigner.bat") sign `
    --ks $keyStore --ks-pass pass:android --key-pass pass:android `
    --out $debugApk $alignedApk
if ($LASTEXITCODE -ne 0) { throw "APK signing failed." }

Write-Host "Built $Abi native library: $(Join-Path $buildPath 'libfaf_android.so')"
Write-Host "Packaged debug APK: $debugApk"

[CmdletBinding()]
param(
    [string]$DependencyDirectory = "dependencies\DiligentCore",
    [string]$Revision = "1436d1fea00763178ae835fd36b065efb31d96e"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$dependencyPath = if ([IO.Path]::IsPathRooted($DependencyDirectory)) {
    $DependencyDirectory
} else {
    Join-Path $repoRoot $DependencyDirectory
}

if (-not (Test-Path (Join-Path $dependencyPath ".git"))) {
    if (Test-Path $dependencyPath) {
        throw "Dependency directory exists but is not a Git checkout: $dependencyPath"
    }
    git clone --filter=blob:none --no-checkout https://github.com/DiligentGraphics/DiligentCore.git $dependencyPath
    if ($LASTEXITCODE -ne 0) { throw "Could not clone DiligentCore." }
}

git -C $dependencyPath fetch --depth 1 origin $Revision
if ($LASTEXITCODE -ne 0) { throw "Could not fetch DiligentCore revision $Revision." }
git -C $dependencyPath checkout --detach FETCH_HEAD
if ($LASTEXITCODE -ne 0) { throw "Could not check out DiligentCore revision $Revision." }
git -C $dependencyPath submodule update --init --depth 1 ThirdParty/SPIRV-Cross ThirdParty/SPIRV-Headers ThirdParty/SPIRV-Tools ThirdParty/Vulkan-Headers ThirdParty/glslang ThirdParty/volk ThirdParty/xxHash
if ($LASTEXITCODE -ne 0) { throw "Could not initialize DiligentCore Android build dependencies." }

Write-Host "DiligentCore is ready at $Revision."

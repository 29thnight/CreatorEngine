[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [string]$Dependencies = '',
    [string]$FSharpToolsRoot = '',
    [string]$OutputPath = '',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$previousResourceProbe = $env:CE_PHYSICS_RESOURCE_PROBE
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $OutputPath) { $OutputPath = Join-Path $repo ('Build/Verification/Headless/Run-' + [guid]::NewGuid().ToString('N')) }
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $OutputPath) { throw 'Use a new verification directory' }
New-Item -ItemType Directory -Path $OutputPath | Out-Null
if (-not $Dependencies) { $Dependencies = Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows' }
if (-not $FSharpToolsRoot) { $FSharpToolsRoot = Join-Path $repo '../FSharp_Tool_Pakage_for_CreatorEngine' }
Push-Location $repo
try {
    & python "$PSScriptRoot/headless_scene.py" --root $repo --source-only --output "$OutputPath/source.json"
    if ($LASTEXITCODE) { throw 'Headless dependency boundary failed' }
    $vs = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'MSVC toolchain unavailable' }
    $msbuild = Join-Path $vs 'MSBuild/Current/Bin/amd64/MSBuild.exe'
    if (-not $SkipBuild) {
        foreach ($project in 'Tools/AssetCooker/AssetCooker.vcxproj','HeadlessHost/HeadlessHost.vcxproj') {
            $name = [IO.Path]::GetFileNameWithoutExtension($project)
            & $msbuild $project /m:4 "/p:Configuration=$Configuration" /p:Platform=x64 "/p:SolutionDir=$repo\" /p:VcpkgManifestInstall=false /v:minimal /nologo *> "$OutputPath/$name-build.log"
            if ($LASTEXITCODE) { throw "Native build failed: $OutputPath/$name-build.log" }
        }
    }
    $toolVersion = Get-ChildItem -LiteralPath "$vs/VC/Tools/MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1
    $dumpbin = Join-Path $toolVersion.FullName 'bin/Hostx64/x64/dumpbin.exe'
    & $dumpbin /nologo /dependents "$repo/Bin/x64-$Configuration/HeadlessHost/HeadlessHost.exe" | Out-File -LiteralPath "$OutputPath/imports.txt" -Encoding utf8
    if ($LASTEXITCODE) { throw 'Native import inventory failed' }
    Copy-Item -LiteralPath "$repo/Bin/x64-$Configuration/HeadlessHost/HeadlessHost.map" -Destination "$OutputPath/link.map"
    $fixtureKey = 'Headless-' + [guid]::NewGuid().ToString('N')
    & dotnet build "$repo/Engine/Scripting/GameScripts/ScriptNative.GameScripts.fsproj" -c $Configuration "-p:CreatorScriptBuildKey=$fixtureKey" "-p:CreatorScriptSourceRoot=$PSScriptRoot/fixtures/headless" "-p:CreatorFSharpToolsRoot=$FSharpToolsRoot" "-p:EngineManagedOutDir=$OutputPath/Managed/" -v:q *> "$OutputPath/managed-build.log"
    if ($LASTEXITCODE) { throw "F# build failed: $OutputPath/managed-build.log" }
    $assets = Join-Path $OutputPath 'Source/Assets'
    New-Item -ItemType Directory -Path "$assets/Scenes" -Force | Out-Null
    Copy-Item -LiteralPath "$PSScriptRoot/fixtures/headless/Scene.creator","$PSScriptRoot/fixtures/headless/Scene.creator.meta" -Destination "$assets/Scenes"
    $cooker = Join-Path $repo "Bin/x64-$Configuration/Tools/AssetCooker/AssetCooker.exe"
    & $cooker --asset-root $assets --scene "$assets/Scenes/Scene.creator" --output "$OutputPath/Cooked" *> "$OutputPath/cook.log"
    if ($LASTEXITCODE) { throw "Actual scene cook failed: $OutputPath/cook.log" }
    $cooked = @(Get-ChildItem "$OutputPath/Cooked/Derived/Scenes" -Recurse -File -Filter *.creator)
    if ($cooked.Count -ne 1) { throw 'Expected exactly one cooked scene' }
    & "$repo/Tools/build/stage-headless.ps1" -Configuration $Configuration -CookedScene $cooked[0].FullName -ManagedDirectory "$OutputPath/Managed" -OutputPath "$OutputPath/Package" -Dependencies $Dependencies
    if ($LASTEXITCODE) { throw 'Headless staging failed' }
    $env:CE_PHYSICS_RESOURCE_PROBE = '1'
    & python "$PSScriptRoot/headless_scene.py" --root $repo --package "$OutputPath/Package" --cooker $cooker --output "$OutputPath/result.json" --imports "$OutputPath/imports.txt" --map "$OutputPath/link.map"
    if ($LASTEXITCODE) { throw "Headless product acceptance failed: $OutputPath/result.json" }
    Write-Output "HEADLESS_SCENE_ACCEPTANCE_OK evidence=$OutputPath"
} finally {
    if ($null -eq $previousResourceProbe) { Remove-Item Env:CE_PHYSICS_RESOURCE_PROBE -ErrorAction SilentlyContinue }
    else { $env:CE_PHYSICS_RESOURCE_PROBE = $previousResourceProbe }
    Pop-Location
}

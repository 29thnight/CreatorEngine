param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path $output){throw 'Use a fresh verification directory'}
New-Item -ItemType Directory $output|Out-Null
$vs=& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
$msbuild=Join-Path $vs 'MSBuild/Current/Bin/amd64/MSBuild.exe'
$runs=@()
foreach($configuration in @('Debug','Release')){
    & $msbuild "$PSScriptRoot/OrderedMapMigrationProbe.vcxproj" /t:Build "/p:Configuration=$configuration" /p:Platform=x64 `
        /p:UseDynamicDebugging=false /p:LinkIncremental=false "/p:SolutionDir=$repo\" /m:2 /v:minimal /nologo *> "$output/build-$configuration.log"
    if($LASTEXITCODE){throw "Ordered map build failed: $configuration"}
    $exe="$repo/Build/Validation/UtilityModules/OrderedMapMigrationProbe/x64-$configuration/OrderedMapMigrationProbe.exe"
    & $exe *> "$output/run-$configuration.log"
    $code=$LASTEXITCODE; $text=Get-Content "$output/run-$configuration.log" -Raw
    $allocation=if($configuration -eq 'Debug'){1}else{0}
    if($code -ne 0 -or $text -notmatch "ORDERED_MAP_MIGRATION_OK checks=\d+ modelOperations=8192 stringReads=8192 nodeTransfer=1 allocatorDomains=1 allocationChecks=$allocation"){
        throw "Ordered map execution failed: $configuration"
    }
    $runs+=[ordered]@{configuration=$configuration;exitCode=$code;binary=(Get-FileHash $exe);verdict=$text.Trim()}
    $text.Trim()
}
$paths=@('Engine/Containers/ordered-map/ordered_map.h',
    'Engine/Utility_Framework/Foundation/EngineOrderedMap.h',
    'Engine/Contracts/Core/Creator.Core.Arena.ixx','Tools/regression/ordered_map_migration_probe.cpp',
    'Tools/regression/OrderedMapMigrationProbe.vcxproj','Tools/regression/verify-ordered-map-migration.ps1')
[ordered]@{pass=$true;runs=$runs;sources=@($paths|ForEach-Object {[ordered]@{path=$_;sha256=(Get-FileHash "$repo/$_").Hash}})}|
    ConvertTo-Json -Depth 8|Set-Content "$output/result.json" -Encoding utf8

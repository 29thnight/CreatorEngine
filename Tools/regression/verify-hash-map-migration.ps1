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
    & $msbuild "$PSScriptRoot/HashMapMigrationProbe.vcxproj" /t:Build "/p:Configuration=$configuration" /p:Platform=x64 `
        /p:UseDynamicDebugging=false /p:LinkIncremental=false "/p:SolutionDir=$repo\" /m:2 /v:minimal /nologo *> "$output/build-$configuration.log"
    if($LASTEXITCODE){throw "Map probe build failed: $configuration"}
    $exe="$repo/Build/Validation/UtilityModules/HashMapMigrationProbe/x64-$configuration/HashMapMigrationProbe.exe"
    & $exe *> "$output/run-$configuration.log"
    $code=$LASTEXITCODE; $text=Get-Content "$output/run-$configuration.log" -Raw
    $suffix=if($configuration -eq 'Debug'){'allocationChecks=1 nodeFreeChecks=2'}else{'allocationChecks=0 nodeFreeChecks=0'}
    if($code -ne 0 -or $text -notmatch ('HASH_MAP_MIGRATION_OK checks=\d+ modelOperations=8192 stringReads=8192 ownership=1 stableNodes=1 '+$suffix)){
        throw "Map probe execution failed: $configuration"
    }
    $runs+=[ordered]@{configuration=$configuration;exitCode=$code;binarySHA256=(Get-FileHash $exe).Hash;verdict=$text.Trim()}
    Write-Output $text.Trim()
}
$paths=@('Engine/Utility_Framework/Foundation/EngineNodeMap.h','Tools/regression/hash_map_migration_probe.cpp',
    'Tools/regression/HashMapMigrationProbe.vcxproj','Tools/regression/verify-hash-map-migration.ps1')
[ordered]@{pass=$true;runs=$runs;sources=@($paths|ForEach-Object {[ordered]@{path=$_;sha256=(Get-FileHash "$repo/$_").Hash}})}|
    ConvertTo-Json -Depth 8|Set-Content "$output/result.json" -Encoding utf8

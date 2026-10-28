param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path $output){throw 'Use a fresh verification directory'}
New-Item -ItemType Directory $output|Out-Null
$vs=& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
$runs=@()
foreach($configuration in @('Debug','Release'))
{
    & "$vs/MSBuild/Current/Bin/amd64/MSBuild.exe" "$PSScriptRoot/StorageMigrationProbe.vcxproj" /t:Build "/p:Configuration=$configuration" /p:Platform=x64 /p:UseDynamicDebugging=false /p:LinkIncremental=false "/p:SolutionDir=$repo\" /m:2 /v:minimal /nologo *> "$output/build-$configuration.log"
    if($LASTEXITCODE){throw "Storage build failed: $configuration"}
    $exe="$repo/Build/Validation/UtilityModules/StorageMigrationProbe/x64-$configuration/StorageMigrationProbe.exe"
    & $exe *> "$output/run-$configuration.log"
    if($LASTEXITCODE -or (Get-Content "$output/run-$configuration.log" -Raw) -notmatch 'STORAGE_MIGRATION_OK checks=48244'){throw "Storage execution failed: $configuration"}
    $runs+=@{configuration=$configuration;pass=$true;checks=48244;binary=(Get-FileHash $exe)}
}
@{pass=$true;runs=$runs;sources=@(@('Engine/Containers/storage/storage.h','Engine/Containers/segmented_deque/segmented_deque.h','Engine/Containers/storage_queue/storage_queue.h','Engine/Containers/storage_stack/storage_stack.h','Engine/Containers/storage_priority_queue/storage_priority_queue.h','Engine/Containers/extractable_hash_map/extractable_hash_map.h','Engine/Utility_Framework/Foundation/EngineStorage.h','Tools/regression/storage_migration_probe.cpp','Tools/regression/StorageMigrationProbe.vcxproj','Tools/regression/verify-storage-migration.ps1')|ForEach-Object {Get-FileHash "$repo/$_"})}|ConvertTo-Json -Depth 7|Set-Content "$output/result.json"
'STORAGE_MODULE_CONTRACTS_PASS Debug/Release checks=48244'

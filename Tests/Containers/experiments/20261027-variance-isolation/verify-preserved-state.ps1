$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$engine=Join-Path (Split-Path $root) 'CreatorEngine'
$work=Join-Path $engine 'Build/Verification/ContainerMigration/VarianceReacceptance20261027'
if(Test-Path "$work/preserved-state.json"){throw 'Do not overwrite verification'}
[void](New-Item -ItemType Directory -Force $work)
$snapshot=Get-Content "$root/results/20261027-variance-canonical-before.json" -Raw|ConvertFrom-Json
foreach($file in $snapshot){if((Get-FileHash $file.Path).Hash -ne $file.Hash){throw "Canonical source changed: $($file.Path)"}}
$previous=Get-Content "$engine/Build/Verification/ContainerMigration/ValueTypes20261027/migration-batch.json" -Raw|ConvertFrom-Json
foreach($file in $previous.sources){if((Get-FileHash (Join-Path $engine $file.path)).Hash -ne $file.sha256){throw "Prior source changed: $($file.path)"}}
$product=Get-Content "$engine/Build/Verification/ContainerMigration/ValueTypes20261027/editor-final/acceptance.json" -Raw|ConvertFrom-Json
foreach($file in $product.binaries){if((Get-FileHash $file.Path).Hash -ne $file.Hash){throw "Prior engine binary changed: $($file.Path)"}}
$runs=@()
foreach($configuration in @('Debug','Release')){
    $binary="$engine/Build/Validation/UtilityModules/ValueTypesProbe/x64-$configuration/ValueTypesProbe.exe"
    & $binary *> "$work/values-$configuration.log"
    if($LASTEXITCODE -ne 0 -or (Get-Content "$work/values-$configuration.log" -Raw) -notmatch 'VALUE_TYPES_OK'){throw "Value correctness failed: $configuration"}
    $runs+=[ordered]@{configuration=$configuration;binary=(Get-FileHash $binary);verdict=(Get-Content "$work/values-$configuration.log" -Raw).Trim()}
}
$asan="$engine/Build/Verification/ContainerMigration/ValueTypes20261027/native-accepted/asan/value_types.exe"
@"
@echo off
call "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
if errorlevel 1 exit /b 1
"$asan"
exit /b %errorlevel%
"@|Set-Content "$work/run-existing-asan.cmd" -Encoding ascii
& cmd.exe /d /c "$work/run-existing-asan.cmd" *> "$work/values-asan.log"
if($LASTEXITCODE -ne 0 -or (Get-Content "$work/values-asan.log" -Raw) -notmatch 'VALUE_TYPES_OK'){throw 'Existing ASAN binary validation failed'}
& "$root/build-modules/modules/Release/module_container_tests.exe" --test *> "$work/module-correctness.log"
if($LASTEXITCODE -ne 0 -or (Get-Content "$work/module-correctness.log" -Raw) -notmatch 'CORRECTNESS_PASS'){throw 'Module correctness failed'}
Copy-Item "$engine/Engine/Containers/integration-source.json" "$work/integration-source-before.json"
[ordered]@{pass=$true;canonicalFilesUnchanged=$snapshot.Count;priorSourceEntriesUnchanged=$previous.sources.Count;priorEngineBinariesUnchanged=$product.binaries.Count;
    nativeValues=$runs;asanBinary=(Get-FileHash $asan);asanVerdict=(Get-Content "$work/values-asan.log" -Raw).Trim();moduleCorrectness=$true;
    note='No implementation edits or engine rebuild. Prior live Editor/DX12 evidence retained with matching source and engine binary hashes.'}|ConvertTo-Json -Depth 7|Set-Content "$work/preserved-state.json" -Encoding utf8
'PRESERVED_STATE_OK canonical=66 priorSourceEntries=193 nativeValues=Debug,Release ASAN=PASS moduleCorrectness=PASS'

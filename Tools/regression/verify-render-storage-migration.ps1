#Requires -Version 7.0
param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path $out){throw 'Use a fresh evidence directory'}
New-Item -ItemType Directory $out | Out-Null
. (Join-Path $PSScriptRoot 'ContractProbeConfiguration.ps1')
$vcvars='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$source=Join-Path $PSScriptRoot 'render_storage_migration_probe.cpp'
function Rebase-Includes([string]$text,[string]$headerPath) {
    return [regex]::Replace($text,'#include "([^"]+)"',{
        param($m)
        '#include "'+[IO.Path]::GetFullPath((Join-Path (Split-Path $headerPath) $m.Groups[1].Value)).Replace('\','/')+'"'
    })
}
function Compile-Probe([string]$config,[string]$dir,[string]$probeSource) {
    New-Item -ItemType Directory $dir | Out-Null
    $contract=New-ContractProbeConfiguration -Configuration $config -OutputDirectory $dir
    $flags=if($config -eq 'Debug'){'/MDd /Od /RTC1 /D_DEBUG'}else{'/MD /O2 /DNDEBUG'}
    $exe=Join-Path $dir 'render_storage_migration_probe.exe'
    $cmd='call "'+$vcvars+'" >nul && cl /nologo /EHsc /std:c++latest /utf-8 /W4 /WX /wd4189 '+$flags+
        ' /I"'+$repo+'/Engine/RenderEngine" /I"'+$repo+'/Engine/Utility_Framework" /I"'+$repo+'/ThirdParty/mimalloc/include" /I"'+$repo+'/ThirdParty/Mathematics/include"'+
        ' /Fo"'+$dir+'/" /Fd"'+$dir+'/probe.pdb" /Fe"'+$exe+'" "'+$probeSource+'"'+$contract.CommandLine
    & $env:ComSpec /d /s /c $cmd *> "$dir/build.log"
    if($LASTEXITCODE -ne 0){throw "Compile failed: $dir/build.log"}
    return $exe
}
$runs=@()
foreach($config in @('Debug','Release')) {
    $dir=Join-Path $out $config
    $exe=Compile-Probe $config $dir $source
    & $exe *> "$dir/run.log"
    if($LASTEXITCODE -ne 0){throw "Run failed: $dir/run.log"}
    $text=Get-Content "$dir/run.log" -Raw
    if($text -notmatch 'RENDER_STORAGE_MIGRATION_PASS checks=(\d+) allocation_checks=([01])'){throw 'Missing success marker'}
    $runs+=@{configuration=$config;checks=[int]$Matches[1];allocationChecks=[int]$Matches[2];binaryHash=(Get-FileHash $exe).Hash}
    Write-Output "$config $($text.Trim())"
}
$baselineDir=Join-Path $out 'stl-baseline'
New-Item -ItemType Directory $baselineDir | Out-Null
$historyPath=Join-Path $repo 'Engine/RenderEngine/Render/Graph/EnhancedGpuMeasurementHistory.h'
$ledgerPath=Join-Path $repo 'Engine/RenderEngine/Render/Graph/EnhancedDrawSealLedger.h'
$history=Get-Content $historyPath -Raw
$history=[regex]::Replace($history,'ce::node_hash_map<uint64_t, Entry, ce::hash<uint64_t>,\s*ce::equal_to<uint64_t>, ce::heap_allocator, false>',
    'std::unordered_map<uint64_t, Entry>')
$ledger=Get-Content $ledgerPath -Raw
$ledger=[regex]::Replace($ledger,'ce::node_hash_map<std::uint64_t, Binding, ce::hash<std::uint64_t>,\s*ce::equal_to<std::uint64_t>, ce::heap_allocator, false>',
    'std::unordered_map<std::uint64_t, Binding>')
if($history -notmatch 'using DomainSamples = std::unordered_map' -or $ledger -notmatch 'std::unordered_map<std::uint64_t, Binding> bindings') {
    throw 'STL baseline replacement failed'
}
$historyCopy=Join-Path $baselineDir 'history.h'
$ledgerCopy=Join-Path $baselineDir 'ledger.h'
[IO.File]::WriteAllText($historyCopy,(Rebase-Includes $history $historyPath),[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($ledgerCopy,(Rebase-Includes $ledger $ledgerPath),[Text.UTF8Encoding]::new($false))
$baselineProbe=(Get-Content $source -Raw).Replace('../../Engine/RenderEngine/Render/Graph/EnhancedGpuMeasurementHistory.h',$historyCopy.Replace('\','/')).
    Replace('../../Engine/RenderEngine/Render/Graph/EnhancedDrawSealLedger.h',$ledgerCopy.Replace('\','/'))
$baselineSource=Join-Path $baselineDir 'probe.cpp'
[IO.File]::WriteAllText($baselineSource,$baselineProbe,[Text.UTF8Encoding]::new($false))
$exe=Compile-Probe 'Debug' (Join-Path $baselineDir 'build') $baselineSource
& $exe *> "$baselineDir/run.log"
if($LASTEXITCODE -ne 0 -or (Get-Content "$baselineDir/run.log" -Raw) -notmatch 'RENDER_STORAGE_MIGRATION_PASS checks=(\d+) allocation_checks=1') {
    throw 'The same lifetime and allocation checks failed for the previous STL storage'
}
$baselineChecks=[int]$Matches[1]
Write-Output "STL_BASELINE_PASS checks=$baselineChecks allocation_checks=1"
$mutationDir=Join-Path $out 'pool-retention-mutation'
New-Item -ItemType Directory $mutationDir | Out-Null
$ledger=Get-Content $ledgerPath -Raw
$old='ce::heap_allocator, false> bindings'
if(!$ledger.Contains($old)){throw 'Mutation anchor missing'}
$ledger=$ledger.Replace($old,'ce::heap_allocator, true> bindings')
# Rebase only quoted includes so the temporary header uses the real dependencies.
$ledger=Rebase-Includes $ledger $ledgerPath
$mutatedHeader=Join-Path $mutationDir 'ledger.h'
[IO.File]::WriteAllText($mutatedHeader,$ledger,[Text.UTF8Encoding]::new($false))
$probe=Get-Content $source -Raw
$probe=$probe.Replace('../../Engine/RenderEngine/Render/Graph/EnhancedGpuMeasurementHistory.h',
    (Join-Path $repo 'Engine/RenderEngine/Render/Graph/EnhancedGpuMeasurementHistory.h').Replace('\','/'))
$probe=$probe.Replace('../../Engine/RenderEngine/Render/Graph/EnhancedDrawSealLedger.h',$mutatedHeader.Replace('\','/'))
$mutatedSource=Join-Path $mutationDir 'probe.cpp'
[IO.File]::WriteAllText($mutatedSource,$probe,[Text.UTF8Encoding]::new($false))
$exe=Compile-Probe 'Debug' (Join-Path $mutationDir 'build') $mutatedSource
& $exe *> "$mutationDir/run.log"
$mutationExit=$LASTEXITCODE
if($mutationExit -eq 0 -or (Get-Content "$mutationDir/run.log" -Raw) -notmatch 'erase immediately frees one node without pool retention') {
    throw 'Allocation-policy mutation was not detected by the regression'
}
Write-Output 'POOL_RETENTION_MUTATION_CAUGHT'
$paths=@($source,$PSCommandPath,(Join-Path $repo 'Engine/RenderEngine/Render/Graph/EnhancedGpuMeasurementHistory.h'),
    (Join-Path $repo 'Engine/RenderEngine/Render/Graph/EnhancedDrawSealLedger.h'))
[ordered]@{pass=$true;scope='Actual engine headers using synchronized canonical container headers; Debug CRT hooks check warmed lookup allocation and immediate node free. The same checks run against temporary copies restored to STL storage. Product named-module Editor is validated separately.';runs=$runs;stlBaselineChecks=$baselineChecks;poolRetentionMutationCaught=$true;mutationHeaderHash=(Get-FileHash $mutatedHeader).Hash;sourceHashes=@($paths|Get-FileHash|Select-Object Path,Hash)} |
    ConvertTo-Json -Depth 6 | Set-Content "$out/result.json" -Encoding utf8

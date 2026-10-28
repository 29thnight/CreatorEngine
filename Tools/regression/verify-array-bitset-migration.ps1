param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path $output){throw 'Use a fresh evidence directory'}
New-Item -ItemType Directory $output|Out-Null
$vs=& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
$msbuild=Join-Path $vs 'MSBuild/Current/Bin/amd64/MSBuild.exe'
$runs=@()
foreach($configuration in @('Debug','Release')){
    foreach($test in @('EffectStateTests','DynamicBitsetModuleProbe')){
        & $msbuild "$PSScriptRoot/$test.vcxproj" /t:Build "/p:Configuration=$configuration" /p:Platform=x64 /p:EngineEffectPackage=true `
            /p:UseDynamicDebugging=false /p:LinkIncremental=false "/p:SolutionDir=$repo\" /m:2 /v:minimal /nologo *> "$output/$test-$configuration-build.log"
        if($LASTEXITCODE){throw "Build failed: $test $configuration"}
        $exe=if($test -eq 'EffectStateTests'){"$repo/Build/Tests/EffectState/x64-$configuration/$test.exe"}else{"$repo/Build/Validation/UtilityModules/$test/x64-$configuration/$test.exe"}
        & $exe *> "$output/$test-$configuration-run.log"
        $code=$LASTEXITCODE; $text=Get-Content "$output/$test-$configuration-run.log" -Raw
        $marker=if($test -eq 'EffectStateTests'){'EFFECT_STATE_OK checks=\d+'}else{'DYNAMIC_BITSET_OK boundaries=8 values=2'}
        if($code -ne 0 -or $text -notmatch $marker){throw "Execution failed: $test $configuration"}
        if($test -eq 'EffectStateTests'){
            $checks=if($configuration -eq 'Debug'){'allocationChecks=1 stringReleaseChecks=2'}else{'allocationChecks=0 stringReleaseChecks=0'}
            if($text -notmatch ('EFFECT_ARRAY_STORAGE_OK reads=4096 snapshot=1 '+$checks)){throw 'The array allocation/lifetime regressions did not run'}
        }
        $runs+=[ordered]@{test=$test;configuration=$configuration;exitCode=$code;binarySHA256=(Get-FileHash $exe).Hash;verdict=$text.Trim()}
        Write-Output "ARRAY_BITSET_CPU_OK $test $configuration"
    }
}
$paths=@('Engine/Effect/EffectState.h','Engine/Effect/EffectState.cpp','Engine/RenderEngine/Effects/EffectFrame.h',
    'Engine/Contracts/Core/Creator.Core.DynamicBitset.ixx','Tools/regression/EffectStateTests.cpp',
    'Tools/regression/dynamic_bitset_probe.cpp','Tools/regression/verify-array-bitset-migration.ps1')
[ordered]@{pass=$true;runs=$runs;sources=@($paths|ForEach-Object {[ordered]@{path=$_;sha256=(Get-FileHash "$repo/$_").Hash}})}|
    ConvertTo-Json -Depth 8|Set-Content "$output/result.json" -Encoding utf8

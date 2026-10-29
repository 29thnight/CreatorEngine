[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release', [switch]$SkipNative, [switch]$Product, [string]$EditorExe='')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Push-Location $repo
try {
    $out=Join-Path $repo ('Build/Verification/FSharpFixedUpdate/Contracts-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force $out|Out-Null
    & "$PSScriptRoot/verify-script-native-table.ps1" -Configuration $Configuration *> "$out/fsharp-runtime.log"
    if($LASTEXITCODE){throw "F# runtime regression failed: $out/fsharp-runtime.log"}
    Get-Content "$out/fsharp-runtime.log" | Where-Object {$_ -match '^(FIXED_UPDATE_MANAGED_OK|CONTACT_STREAM_OK|PHYSICS_BODY_IDENTITY_OK)'}
    if(!$SkipNative) {
        $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if(!$vs){throw 'MSVC toolchain unavailable'}
        $batch=Join-Path $out 'clock.cmd'
        @"
@echo off
call "$vs\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc Tools\regression\FixedSimulationClockProbe.cpp /Fo"$out\clock.obj" /Fe"$out\clock.exe"
if errorlevel 1 exit /b 1
"$out\clock.exe"
"@ | Set-Content $batch -Encoding ascii
        & $env:ComSpec /d /c $batch *> "$out/clock.log"
        if($LASTEXITCODE){throw "Native clock regression failed: $out/clock.log"}
        Get-Content "$out/clock.log" | Where-Object {$_ -like 'FIXED_SIMULATION_CLOCK_OK*'}
    }
    $hashes=@{}
    foreach($path in @('Engine/SceneRuntime/SimulationTick.h','Engine/SceneRuntime/SimulationExchange.h','Engine/SceneRuntime/SceneManager.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.cpp','Engine/SceneRuntime/ClrHost.cpp')) {
        $hashes[$path]=(Get-FileHash "$repo/$path").Hash
    }
    foreach($file in Get-ChildItem "$repo/Engine/Scripting/Bindings" -Recurse -File -Filter *.fs | Where-Object {$_.FullName -notmatch '\\(bin|obj)\\'}) {
        $hashes[[IO.Path]::GetRelativePath($repo,$file.FullName).Replace('\','/')]=(Get-FileHash $file.FullName).Hash
    }
    @{result='FSHARP_FIXED_UPDATE_CONTRACTS_OK';configuration=$Configuration;nativeClockTested=(!$SkipNative);sourceHashes=$hashes;scope='Native clock and real F# dispatch with a fake native API table; actual Editor/Player acceptance is separate'}|ConvertTo-Json -Depth 10|Set-Content "$out/result.json" -Encoding utf8
    "FSHARP_FIXED_UPDATE_CONTRACTS_OK evidence=$out"
    if($Product){& "$PSScriptRoot/verify-fsharp-fixed-update-http.ps1" -Configuration $Configuration -EditorExe $EditorExe}
} finally {Pop-Location}

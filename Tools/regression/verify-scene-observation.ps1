[CmdletBinding()]
param([ValidateSet('Debug','Release','All')][string]$Configuration='All')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo ('Build/Verification/W4NDB/Native-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out|Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC toolchain unavailable'}
Push-Location $repo
try {
foreach($config in $(if($Configuration -eq 'All'){@('Debug','Release')}else{@($Configuration)})) {
$flags=if($config -eq 'Debug'){'/Od /Zi /MDd'}else{'/O2 /MD'}
$batch=Join-Path $out "$config.cmd"
@"
@echo off
call "$vs\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 $flags Tools\regression\SceneObservationProbe.cpp /Fo"$out\$config.obj" /Fe"$out\$config.exe" /Fd"$out\$config.pdb"
if errorlevel 1 exit /b 1
"$out\$config.exe"
"@ | Set-Content $batch -Encoding ascii
& $env:ComSpec /d /c $batch *> "$out/$config.log"
if($LASTEXITCODE){Get-Content "$out/$config.log";throw "Observation regression failed: $config"}
Get-Content "$out/$config.log" | Where-Object {$_ -like 'SCENE_OBSERVATION_OK*'}
}
@{result='SCENE_OBSERVATION_CONTRACTS_OK';configuration=$Configuration;headerHash=(Get-FileHash Engine/SceneRuntime/SceneObservation.h).Hash;scope='Production observation header; engine DDOL/authoring integration is verified by verify-scene-gc.ps1'}|ConvertTo-Json|Set-Content "$out/result.json" -Encoding utf8
"SCENE_OBSERVATION_CONTRACTS_OK evidence=$out"
} finally {Pop-Location}

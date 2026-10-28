#Requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Use a fresh artifact directory.' }
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/amd64/MSBuild.exe'
New-Item -ItemType Directory -Path $output | Out-Null
$runs = @()
foreach ($config in @('Debug','Release')) {
    & $msbuild "$repo/Tools/regression/EffectModuleIRTests.vcxproj" /t:Build "/p:Configuration=$config" /p:Platform=x64 /p:EngineEffectPackage=true /m:2 /v:minimal /nologo *> "$output/$config-build.log"
    if ($LASTEXITCODE -ne 0) { throw "Effect module IR $config build failed: $output/$config-build.log" }
    $exe = "$repo/Build/Tests/EffectModuleIR/x64-$config/EffectModuleIRTests.exe"
    $text = (& $exe 2>&1 | Out-String)
    $code = $LASTEXITCODE
    $text | Set-Content -LiteralPath "$output/$config-run.log" -Encoding utf8
    if ($code -ne 0 -or $text -notmatch 'EFFECT_MODULE_IR_OK checks=(\d+)') { throw "Effect module IR $config failed: $text" }
    $runs += @{ configuration=$config; exitCode=$code; checks=[int]$Matches[1]; executableSha256=(Get-FileHash -LiteralPath $exe).Hash }
    Write-Output "EFFECT_MODULE_IR_ACCEPTANCE_OK $config"
}
$paths = @('Engine/Effect/EffectDefinition.h','Engine/Effect/EffectDefinition.cpp',
    'Engine/Effect/EffectModuleIR.h','Engine/Effect/EffectModuleIR.cpp','Engine/Effect/Effect.vcxproj',
    'Tools/regression/EffectModuleIRTests.cpp','Tools/regression/EffectModuleIRTests.vcxproj',
    'Tools/regression/verify-effect-module-ir.ps1')
$sources = @($paths | ForEach-Object { @{ path=$_; sha256=(Get-FileHash -LiteralPath (Join-Path $repo $_)).Hash } })
@{ passed=$true; scope='CPU typed module IR, graph compilation and reference evaluation; not GPU/render/editor/serialization acceptance'; runs=$runs; sources=$sources } |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$output/result.json" -Encoding utf8
"EFFECT_MODULE_IR_MATRIX_OK $output"

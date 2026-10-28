param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path $output) { throw 'Use a fresh verification directory' }
[void](New-Item -ItemType Directory $output)
$installation = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
$msbuild = Join-Path $installation 'MSBuild/Current/Bin/amd64/MSBuild.exe'
$solution = Join-Path $repo 'Tests/Containers/value-types/ValueTypes.proj'
$runs = @()
foreach ($configuration in @('Debug', 'Release')) {
    & $msbuild $solution /t:Build "/p:Configuration=$configuration" /p:Platform=x64 /p:UseDynamicDebugging=false /p:LinkIncremental=false /m:2 /v:minimal /nologo *> "$output/build-$configuration.log"
    if ($LASTEXITCODE) { throw "Value type module build failed: $configuration" }
    $exe = "$repo/Build/Validation/UtilityModules/ValueTypesProbe/x64-$configuration/ValueTypesProbe.exe"
    & $exe *> "$output/run-$configuration.log"
    $verdict = Get-Content "$output/run-$configuration.log" -Raw
    $allocationVerdict = if ($configuration -eq 'Debug') { 'allocations=0' } else { 'allocationChecks=0' }
    if ($LASTEXITCODE -ne 0 -or $verdict -notmatch ('VALUE_TYPES_OK checks=\d+ valueIterations=8192 ownership=1 references=1 memberInvoke=1 order=1 ' + $allocationVerdict)) {
        throw "Value type module execution failed: $configuration"
    }
    $runs += [ordered]@{ configuration = $configuration; binarySHA256 = (Get-FileHash $exe).Hash; verdict = $verdict.Trim() }
    $verdict.Trim()
}
$asan = Join-Path $output 'asan'
[void](New-Item -ItemType Directory $asan)
$asanExe = Join-Path $asan 'value_types.exe'
$command = @"
@echo off
call "$installation/Common7/Tools/VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++23preview /utf-8 /EHsc /MD /O1 /Zi /fsanitize=address /DCE_CONTRACT_STANDALONE=1 /Fo"$asan/value_types.obj" /Fd"$asan/value_types.pdb" /Fe"$asanExe" "$repo/Tools/regression/value_types_probe.cpp"
if errorlevel 1 exit /b 1
"$asanExe"
exit /b %errorlevel%
"@
$command | Set-Content "$asan/run.cmd" -Encoding ascii
& cmd.exe /d /c "$asan/run.cmd" *> "$asan/build-run.log"
if ($LASTEXITCODE -ne 0 -or (Get-Content "$asan/build-run.log" -Raw) -notmatch 'VALUE_TYPES_OK') { throw 'AddressSanitizer validation failed' }
$paths = @('Engine/Containers/values/value_types.h',
    'Tools/regression/value_types_probe.cpp',
    'Tools/regression/ValueTypesProbe.vcxproj', 'Tools/regression/verify-value-types.ps1')
[ordered]@{ pass = $true; runs = $runs; addressSanitizer = $true
    sources = @($paths | ForEach-Object { [ordered]@{ path = $_; sha256 = (Get-FileHash "$repo/$_").Hash } }) } |
    ConvertTo-Json -Depth 7 | Set-Content "$output/result.json" -Encoding utf8
'VALUE_TYPES_VALIDATION_OK namedModules=Debug,Release standaloneAddressSanitizer=PASS'

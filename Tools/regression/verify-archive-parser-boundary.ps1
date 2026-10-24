[CmdletBinding()]
param([ValidateSet('Debug','Release','All')][string]$Configuration='All')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo ('Build/Verification/N1/Contracts-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out|Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$deps=if($env:CREATOR_N1_DEPENDENCIES){[IO.Path]::GetFullPath($env:CREATOR_N1_DEPENDENCIES)}else{Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows'}
foreach($required in @('include/reflgen/core/schema.h','include/ryml/ryml.hpp','lib/ryml.lib','debug/lib/ryml.lib')){if(!(Test-Path (Join-Path $deps $required))){throw "N1 native dependency missing: $deps/$required"}}
Push-Location $repo
try {
foreach($config in $(if($Configuration -eq 'All'){@('Debug','Release')}else{@($Configuration)})) {
$flags=if($config -eq 'Debug'){'/Od /Zi /MDd'}else{'/O2 /MD'}
$libroot=if($config -eq 'Debug'){Join-Path $deps 'debug'}else{$deps}
$dir=Join-Path $out $config
New-Item -ItemType Directory -Force $dir|Out-Null
$batch=Join-Path $dir 'probe.cmd'
@"
@echo off
call "$vs\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
set "PATH=$libroot\bin;%PATH%"
cl /nologo /std:c++latest /EHsc /utf-8 /DCE_CONTRACT_STANDALONE=1 /DNOMINMAX /W4 $flags /I"$repo\Engine\Utility_Framework" /external:I"$deps\include" /external:W0 Tools\regression\ArchiveParserProbe.cpp Engine\Utility_Framework\DiagnosticDocument.cpp Engine\Utility_Framework\AuthoringParsedDocument.cpp Engine\Utility_Framework\AuthoringCookedDocument.cpp Engine\Utility_Framework\CookedDocumentTree.cpp Engine\Utility_Framework\AuthoringRymlErrorPolicy.cpp Engine\Utility_Framework\AuthoringScalarConvert.cpp /Fo"$dir\\" /Fe"$dir\probe.exe" /Fd"$dir\probe.pdb" /link /LIBPATH:"$libroot\lib" ryml.lib c4core.lib
if errorlevel 1 exit /b 1
"$dir\probe.exe" "$dir"
"@ | Set-Content $batch -Encoding ascii
& $env:ComSpec /d /c $batch *> "$dir/probe.log"
if($LASTEXITCODE){Get-Content "$dir/probe.log" -Tail 18;throw "Archive parser native failed: $config"}
Get-Content "$dir/probe.log" | Where-Object {$_ -like 'ARCHIVE_PARSER_OK*'}
& python "$PSScriptRoot/archive_parser_boundary.py" --diagnostic "$dir/diagnostic.json" *> "$dir/json-check.log"
if($LASTEXITCODE){Get-Content "$dir/json-check.log";throw 'Diagnostic JSON contract failed'}
}
& python "$PSScriptRoot/archive_parser_boundary.py" --root $repo --output "$out/boundary.json"
if($LASTEXITCODE){throw 'Archive parser source boundary failed'}
& python "$PSScriptRoot/archive_parser_boundary_tests.py" --root $repo --output "$out/canaries.json"
if($LASTEXITCODE){throw 'Archive parser mutation regressions failed'}
@{result='ARCHIVE_PARSER_BOUNDARY_OK';configuration=$Configuration;scope='Runtime direct backend/API gates, neutral/Network transitive include closure, real authoring/cooked codec and diagnostic producer; packaged Player is a separate product gate'}|ConvertTo-Json|Set-Content "$out/result.json" -Encoding utf8
"ARCHIVE_PARSER_BOUNDARY_OK evidence=$out"
} finally {Pop-Location}

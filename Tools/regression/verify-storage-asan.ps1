param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path $output){throw 'Use a fresh verification directory'}
New-Item -ItemType Directory $output|Out-Null
$vs=& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
$command=@"
@echo off
call "$vs/Common7/Tools/VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /EHsc /DCE_CONTRACT_STANDALONE /utf-8 /MD /O2 /Zi /fsanitize=address /W4 /WX /wd4324 "$repo/Tools/regression/storage_migration_probe.cpp" /Fe:storage_asan.exe /Fd:storage_asan.pdb
if errorlevel 1 exit /b 1
storage_asan.exe
exit /b %errorlevel%
"@
[IO.File]::WriteAllText("$output/run.cmd",$command,[Text.ASCIIEncoding]::new())
Push-Location $output
try{& cmd /c run.cmd *> run.log;$code=$LASTEXITCODE}finally{Pop-Location}
if($code -or (Get-Content "$output/run.log" -Raw) -notmatch 'STORAGE_MIGRATION_OK checks=48244'){throw 'Storage ASAN verification failed'}
@{pass=$true;checks=48244;scope='Standalone headers; storage allocations and value lifetimes. This is not an ASAN-instrumented Editor.';binary=(Get-FileHash "$output/storage_asan.exe");source=(Get-FileHash "$repo/Tools/regression/storage_migration_probe.cpp")}|ConvertTo-Json -Depth 5|Set-Content "$output/result.json"
'STORAGE_ASAN_PASS checks=48244'

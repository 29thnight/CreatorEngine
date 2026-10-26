#Requires -Version 7.0
param()
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $repo 'Build/Obj/GeometryPreparationIndex-Debug'
$null = New-Item $out -ItemType Directory -Force
$vcvars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$command = 'call "' + $vcvars + '" >nul && cl /nologo /EHsc /std:c++latest /utf-8 /W4 /WX /MDd /Od /RTC1 /D_DEBUG /DCE_CONTRACT_STANDALONE ' +
    '/Fo"' + $out + '/" /Fe"' + $out + '/probe.exe" "' + $PSScriptRoot + '/geometry_preparation_index_probe.cpp"'
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE)
{
    throw 'Geometry preparation index compilation failed.'
}
& "$out/probe.exe"
if ($LASTEXITCODE)
{
    throw 'Geometry preparation index regression failed.'
}

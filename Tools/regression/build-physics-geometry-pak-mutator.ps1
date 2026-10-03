[CmdletBinding()]
param([string]$Out = '')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$Out) { $Out = Join-Path $repo 'Build/Obj/PhysicsGeometryFailure/Tools' }
$Out = [IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force $Out | Out-Null
$sources = @('Tools/regression/physics_geometry_pak_mutator.cpp', 'Engine/RenderEngine/Experiment/Cooked/CookedAssetManifest.cpp', 'Engine/RenderEngine/Assets/AssetIdentityProfile.cpp')
$sourceArgs = ($sources | ForEach-Object { '"' + (Join-Path $repo $_) + '"' }) -join ' '
$cmd = 'call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" >nul && cl /nologo /MP3 /EHsc /Gy /std:c++latest /utf-8 /MD /O2 /DNOMINMAX /I"' + $repo + '/Engine/Utility_Framework" /I"' + $repo + '/ThirdParty/Mathematics/include" /Fo"' + $Out + '/" /Fe"' + $Out + '/pak-mutator.exe" ' + $sourceArgs + ' /link /OPT:REF'
& $env:ComSpec /d /s /c $cmd *> "$Out/build.log"
if ($LASTEXITCODE) { throw "Geometry Pak mutator build failed: $Out/build.log" }
Write-Output (Join-Path $Out 'pak-mutator.exe')

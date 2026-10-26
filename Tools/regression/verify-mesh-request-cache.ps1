#Requires -Version 7.0
param()
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $repo 'Build/Obj/MeshRequestCache-Debug'
$null = New-Item $out -ItemType Directory -Force
$path = Join-Path $repo 'Engine/RenderEngine/MaterialGraphMeshSurface.cpp'
$source = [IO.File]::ReadAllText($path)
$begin = $source.IndexOf('    struct MeshSurfaceRequestCache')
$end = $source.IndexOf('    struct MeshSurfaceStaticBuffers', $begin)
if ($begin -lt 0 -or $end -le $begin) { throw 'Production cache boundary missing' }
[IO.File]::WriteAllText((Join-Path $out 'MeshSurfaceRequestCache.generated.inl'), $source.Substring($begin, $end-$begin))
$cache = $source.IndexOf('    struct MeshSurfaceStaticCache')
$fields = $source.IndexOf('        struct RecordingOwners', $cache)
$fieldsEnd = $source.IndexOf('        struct ProvisionalEntry', $fields)
$methods = $source.IndexOf('        bool HasLease(', $cache)
$methodsEnd = $source.IndexOf('        void TrimIdle()', $methods)
if ($fields -lt 0 -or $fieldsEnd -le $fields -or $methods -lt 0 -or $methodsEnd -le $methods)
{
    throw 'Production lease ledger boundary missing'
}
[IO.File]::WriteAllText((Join-Path $out 'StaticCacheLease.generated.inl'),
    $source.Substring($fields, $fieldsEnd-$fields)+$source.Substring($methods, $methodsEnd-$methods))
$vcvars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$command = 'call "' + $vcvars + '" >nul && cl /nologo /EHsc /std:c++latest /utf-8 /W4 /WX /MDd /Od /RTC1 /D_DEBUG /DCE_CONTRACT_STANDALONE ' +
    '/I"' + $out + '" /Fo"' + $out + '/" /Fe"' + $out + '/probe.exe" "' + $PSScriptRoot + '/mesh_request_cache_probe.cpp"'
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE) { throw 'Production request cache compilation failed' }
& "$out/probe.exe"
if ($LASTEXITCODE) { throw 'Production request cache regression failed' }

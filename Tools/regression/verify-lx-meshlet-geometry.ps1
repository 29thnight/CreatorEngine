#Requires -Version 7.0
param(
    [ValidateSet('Debug','Release')][string[]]$Configurations = @('Debug','Release'),
    [string]$OutputDirectory = '',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repo ('Build/lx-geometry-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $out) { throw 'Use a fresh evidence directory.' }
New-Item -ItemType Directory -Path $out | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)[0]
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
$sourcePaths = @(
    'Engine/RenderEngine/MaterialGraphSceneLod.h', 'Engine/RenderEngine/MaterialGraphSceneInput.cpp',
    'Engine/RenderEngine/MaterialGraphSceneInput.h', 'Engine/RenderEngine/MaterialGraphSceneHost.cpp',
    'Engine/RenderEngine/MaterialGraphSceneHost.h', 'Engine/RenderEngine/MaterialGraphSceneCompiler.cpp',
    'Engine/RenderEngine/MaterialGraphSceneCompiler.h', 'Engine/RenderEngine/MaterialGraphProduct.h',
    'Engine/RenderEngine/MaterialGraphMeshSurface.cpp', 'Engine/RenderEngine/MaterialGraphMeshletTopology.h',
    'Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialGraphSceneHost.slang',
    'Tools/regression/material_raster_surface_probe.cpp'
)
$snapshot = @($sourcePaths | ForEach-Object { @{path=$_; sha256=(Get-FileHash -LiteralPath (Join-Path $repo $_)).Hash} })
@{head=(git -C $repo rev-parse HEAD); sources=$snapshot; performanceValidated=$false} |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$out/source.json"
foreach ($configuration in $Configurations)
{
    if (!$SkipBuild)
    {
        & $msbuild "$repo/Tools/regression/MaterialRasterSurfaceProbe.vcxproj" /t:Build /m:2 /nologo `
            "/p:Configuration=$configuration" /p:Platform=x64 /p:VcpkgManifestInstall=false `
            /p:UseDynamicDebugging=false /p:LinkIncremental=false /v:minimal *> "$out/build-$configuration.log"
        if ($LASTEXITCODE -ne 0) { throw "Build failed: $configuration" }
    }
    $exe = "$repo/Bin/x64-$configuration/Tools/MaterialRasterSurfaceProbe/MaterialRasterSurfaceProbe.exe"
    $binary = (Get-FileHash -LiteralPath $exe).Hash
    foreach ($mesh in @(0,1))
    {
        foreach ($hzb in @(0,1))
        {
            $log = "$out/$configuration-mesh$mesh-hzb$hzb.log"
            $stderr = "$out/$configuration-mesh$mesh-hzb$hzb.stderr.log"
            $oldPath = $env:PATH
            $oldMesh = $env:CREATOR_LX_MESHLETS
            $oldHzb = $env:CREATOR_LX_HZB
            $oldValidation = $env:CREATOR_DX12_VALIDATION
            try
            {
                $dep = if ($configuration -eq 'Debug') {'debug/bin'} else {'bin'}
                $env:PATH = "$repo/vcpkg_installed/x64-windows/x64-windows/$dep;$repo/vcpkg_installed/x64-windows/$dep;" + $env:PATH
                $env:CREATOR_LX_MESHLETS = "$mesh"
                $env:CREATOR_LX_HZB = "$hzb"
                $env:CREATOR_DX12_VALIDATION = 'gpu'
                $process = Start-Process -FilePath $exe -ArgumentList @(('"'+$repo+'"'), '--lx-meshlet-only') `
                    -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError $stderr
                $deadline = [DateTime]::UtcNow.AddMinutes(15)
                while (!$process.WaitForExit(1000))
                {
                    if ([DateTime]::UtcNow -gt $deadline -or
                        (Test-Path $stderr) -and (Select-String -LiteralPath $stderr -Pattern 'PROBE_CHECK_FAIL|LX_MATERIAL_RASTER_SURFACE_FAIL' -Quiet))
                    {
                        Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
                        throw "GPU regression failed or timed out: $configuration mesh=$mesh hzb=$hzb; see $stderr"
                    }
                }
                if ($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -Pattern '^LX_MESHLET_CULL_LOD_HZB_OK .*validation=0$' -Quiet))
                {
                    throw "GPU regression failed: $configuration mesh=$mesh hzb=$hzb"
                }
                if ($mesh -eq 1 -and !(Select-String -LiteralPath $log -Pattern '\[lx.meshlets\] surface dispatch' -Quiet))
                {
                    throw 'GPU run did not execute the mesh shader surface route.'
                }
                if ((Get-FileHash -LiteralPath $exe).Hash -ne $binary) { throw 'Binary changed during validation.' }
            }
            finally
            {
                $env:PATH = $oldPath
                $env:CREATOR_LX_MESHLETS = $oldMesh
                $env:CREATOR_LX_HZB = $oldHzb
                $env:CREATOR_DX12_VALIDATION = $oldValidation
            }
        }
    }
}
foreach ($source in $snapshot)
{
    if ((Get-FileHash -LiteralPath (Join-Path $repo $source.path)).Hash -ne $source.sha256) { throw 'Source changed during validation.' }
}
'LX_GEOMETRY_REGRESSION_COMPLETE'

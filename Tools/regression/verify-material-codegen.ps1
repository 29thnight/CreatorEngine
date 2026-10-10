param([string]$VisualStudioInstallation = '', [switch]$WriteGolden)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioInstallation = @(& $vswhere -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)[0]
}
$vcvars = Join-Path $VisualStudioInstallation 'VC\Auxiliary\Build\vcvars64.bat'
$dependencies = Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows'
if (!(Test-Path -LiteralPath (Join-Path $dependencies 'include/ryml/ryml.hpp'))) {
    $dependencies = Join-Path $repo 'vcpkg_installed/x64-windows'
}
$output = Join-Path $repo 'Build\Obj\MaterialCodegenProbe'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$exe = Join-Path $output 'material_codegen_probe.exe'
$sources = @('Tools/regression/material_codegen_probe.cpp', 'Lattice/Core/LXGraph.cpp',
    'Lattice/Core/LXNodeDefinition.cpp', 'Lattice/Material/LXMaterialGraph.cpp',
    'Lattice/Material/LXMaterialIR.cpp', 'Lattice/Material/LXMaterialNodes.cpp',
    'Lattice/Material/LXMaterialOperators.cpp', 'Lattice/Material/LXMaterialCompiler.cpp')
$quoted = ($sources | ForEach-Object { '"' + (Join-Path $repo $_) + '"' }) -join ' '
$compile = 'call "' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++latest ' +
    '/permissive- /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /W4 /WX /MD ' +
    '/I"' + (Join-Path $repo 'ThirdParty/Slang/include') + '" /I"' + (Join-Path $repo 'Engine/RenderEngine/RHI') +
    '" /I"' + (Join-Path $dependencies 'include') + '" /Fo:"' + $output + '/" /Fe:"' + $exe + '" ' +
    $quoted + ' /link /LIBPATH:"' + (Join-Path $dependencies 'lib') + '" ryml.lib c4core.lib d3d12.lib dxgi.lib'
& $env:ComSpec /d /s /c $compile
if ($LASTEXITCODE -ne 0) { throw "Material codegen probe build failed: exit $LASTEXITCODE" }
$arguments = @($repo)
if ($WriteGolden) { $arguments += '--write-golden' }
$previousPath = $env:PATH
try {
    $env:PATH = (Join-Path $dependencies 'bin') + ';' + $previousPath
    $result = @(& $exe @arguments 2>&1)
    $probeExit = $LASTEXITCODE
}
finally {
    $env:PATH = $previousPath
}
$result | Set-Content -LiteralPath (Join-Path $output 'codegen.log') -Encoding utf8
if ($probeExit -ne 0 -or @($result | Where-Object { $_ -match '^LX_MATERIAL_CODEGEN_OK ' }).Count -ne 1) {
    throw "Material codegen gate failed: $($result -join "`n")"
}
$result
$manifest = @(Import-Csv -LiteralPath (Join-Path $output 'manifest.csv'))
foreach ($row in $manifest) {
    $metadata = Get-Content -LiteralPath (Join-Path $output ($row.case + '.materialprogram.json')) -Raw | ConvertFrom-Json
    if ($metadata.kind -ne 'LatticeMaterialProgram' -or $metadata.schemaVersion -ne 1 -or
        $metadata.features -ne [int]$row.features -or $metadata.textureSamples -ne [int]$row.samples -or
        @($metadata.parameters).Count -ne [int]$row.parameters -or
        @($metadata.resources | Where-Object { $_.kind -eq 'Texture' }).Count -ne [int]$row.textures -or
        @($metadata.resources | Where-Object { $_.kind -eq 'Sampler' }).Count -ne [int]$row.samplers) {
        throw "Generated metadata differs: $($row.case)"
    }
    foreach ($resource in $metadata.resources) {
        if ($resource.space -ne 1 -or $resource.shaderName -ne
            ('lx_' + $resource.kind.ToLowerInvariant() + '_' + $resource.register)) {
            throw "Generated resource symbol differs: $($row.case)"
        }
    }
}
Write-Output "LX_MATERIAL_METADATA_JSON_OK cases=$($manifest.Count)"

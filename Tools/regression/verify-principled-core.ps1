param(
    [string]$VisualStudioInstallation = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))

# Check the nine core defaults against the MAT-0/LX-0 pinned Blender export.
# The GPU probe separately reads DefaultMaterialInputs, so drift on either side
# fails the gate rather than changing an expected result with the implementation.
$schemaPath = Join-Path $repo 'Tools\LatticeExample\fixtures\blender-5.1.1-shader-nodes.json'
$schema = Get-Content -LiteralPath $schemaPath -Raw | ConvertFrom-Json
$principled = @($schema.nodes | Where-Object { $_.type -eq 'ShaderNodeBsdfPrincipled' })
if ($schema.blender_version -ne '5.1.1' -or $principled.Count -ne 1) {
    throw 'Pinned Blender Principled schema was not found'
}
$defaults = @{
    'Base Color' = @(0.8, 0.8, 0.8, 1.0)
    'Metallic' = @(0.0)
    'Roughness' = @(0.5)
    'IOR' = @(1.5)
    'Alpha' = @(1.0)
    'Specular IOR Level' = @(0.5)
    'Specular Tint' = @(1.0, 1.0, 1.0, 1.0)
    'Emission Color' = @(1.0, 1.0, 1.0, 1.0)
    'Emission Strength' = @(0.0)
}
foreach ($name in $defaults.Keys) {
    $socket = @($principled[0].inputs | Where-Object { $_.identifier -eq $name })
    if ($socket.Count -ne 1) {
        throw "Missing Principled socket: $name"
    }
    $actual = @($socket[0].default)
    $expected = $defaults[$name]
    if ($actual.Count -ne $expected.Count) {
        throw "Principled default shape differs: $name"
    }
    for ($index = 0; $index -lt $actual.Count; ++$index) {
        if ([Math]::Abs($actual[$index] - $expected[$index]) -gt 0.0000001) {
            throw "Principled default differs: $name component $index"
        }
    }
}
Write-Output 'PRINCIPLED_DEFAULT_SCHEMA_OK sockets=9'

if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $installations = @(& $vswhere -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if ($LASTEXITCODE -ne 0 -or $installations.Count -eq 0) {
        throw 'MSVC x64 installation was not found'
    }
    $VisualStudioInstallation = $installations[0]
}
$vcvars = Join-Path $VisualStudioInstallation 'VC\Auxiliary\Build\vcvars64.bat'
$output = Join-Path $repo 'Build\Obj\PrincipledCoreProbe'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$exe = Join-Path $output 'principled_core_probe.exe'
$object = Join-Path $output 'principled_core_probe.obj'
$source = Join-Path $PSScriptRoot 'principled_core_probe.cpp'
$slangInclude = Join-Path $repo 'ThirdParty\Slang\include'
$renderInclude = Join-Path $repo 'Engine\RenderEngine\RHI'
$compile = 'call "' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++latest ' +
    '/permissive- /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /W4 /WX ' +
    '/I"' + $slangInclude + '" /I"' + $renderInclude + '" ' +
    '/Fo:"' + $object + '" /Fe:"' + $exe + '" "' + $source + '" ' +
    '/link d3d12.lib dxgi.lib'
& $env:ComSpec /d /s /c $compile
if ($LASTEXITCODE -ne 0) {
    throw "Principled core probe build failed: exit $LASTEXITCODE"
}
$result = @(& $exe $repo 2>&1)
$result | Set-Content -LiteralPath (Join-Path $output 'gpu.log') -Encoding utf8
if ($LASTEXITCODE -ne 0 -or
    @($result | Where-Object { $_ -match '^PRINCIPLED_CORE_GPU_OK cases=23 angles=9 checks=8280 ' }).Count -ne 1) {
    throw "Principled core GPU gate failed: $($result -join "`n")"
}
$result

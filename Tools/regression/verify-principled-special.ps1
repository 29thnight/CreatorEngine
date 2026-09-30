param(
    [string]$VisualStudioInstallation = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))

function NormalizedHash([string]$path) {
    $text = [IO.File]::ReadAllText($path).Replace("`r`n", "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text))).Replace('-', '')
    } finally {
        $sha.Dispose()
    }
}

$schema = Get-Content -LiteralPath (Join-Path $repo 'Tools\LatticeExample\fixtures\blender-5.1.1-shader-nodes.json') -Raw | ConvertFrom-Json
$principled = @($schema.nodes | Where-Object { $_.type -eq 'ShaderNodeBsdfPrincipled' })
$fixtureDirectory = Join-Path $repo 'Tools\blender\fixtures\principled-special-5.1.1'
$volumePath = Join-Path $fixtureDirectory 'volume-defaults.json'
$volume = Get-Content -LiteralPath $volumePath -Raw | ConvertFrom-Json
if ($schema.blender_version -ne '5.1.1' -or $principled.Count -ne 1 -or
    $volume.blender_version -ne '5.1.1' -or $volume.blender_build_hash -ne 'b70da489d7f4' -or
    (NormalizedHash $volumePath) -ne '9F93BBAA0CBA09D71E3D65979D91F719378740C1E74A3623B4721103B24C6D1B') {
    throw 'Pinned Blender Special input schemas differ'
}
$defaultSets = @(
    @{ inputs = $principled[0].inputs; expected = @{
        'Transmission Weight' = @(0.0)
        'Subsurface Weight' = @(0.0)
        'Subsurface Radius' = @(1.0, 0.2, 0.1)
        'Subsurface Scale' = @(0.05)
        'Subsurface IOR' = @(1.4)
        'Subsurface Anisotropy' = @(0.0)
    } },
    @{ inputs = $volume.inputs; expected = @{
        'Color' = @(0.5, 0.5, 0.5, 1.0)
        'Density' = @(1.0)
        'Anisotropy' = @(0.0)
        'Absorption Color' = @(0.0, 0.0, 0.0, 1.0)
        'Emission Strength' = @(0.0)
        'Emission Color' = @(1.0, 1.0, 1.0, 1.0)
    } }
)
foreach ($set in $defaultSets) {
    foreach ($name in $set.expected.Keys) {
        $socket = @($set.inputs | Where-Object { $_.identifier -eq $name })
        if ($socket.Count -ne 1) { throw "Missing Special socket: $name" }
        $actual = @($socket[0].default)
        $expected = $set.expected[$name]
        if ($actual.Count -ne $expected.Count) { throw "Special default shape differs: $name" }
        for ($index = 0; $index -lt $actual.Count; ++$index) {
            if ([Math]::Abs($actual[$index] - $expected[$index]) -gt 0.0000001) {
                throw "Special default differs: $name component $index"
            }
        }
    }
}
Write-Output 'PRINCIPLED_SPECIAL_DEFAULT_SCHEMA_OK sockets=12'
if ((NormalizedHash (Join-Path $repo 'Tools\blender\fixtures\principled-layered-5.1.1\sheen-ltc.csv')) -ne
    'AEB8870B010370633FF1EBF802F87A8045244CCD7E7DCF5D99BCCB2F5A511A14') {
    throw 'Special layered reference requires the pinned Sheen table'
}

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
$output = Join-Path $repo 'Build\Obj\PrincipledSpecialProbe'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$exe = Join-Path $output 'principled_special_probe.exe'
$object = Join-Path $output 'principled_special_probe.obj'
$source = Join-Path $PSScriptRoot 'principled_special_probe.cpp'
$slangInclude = Join-Path $repo 'ThirdParty\Slang\include'
$renderInclude = Join-Path $repo 'Engine\RenderEngine\RHI'
$compile = 'call "' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++latest ' +
    '/permissive- /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /W4 /WX ' +
    '/I"' + $slangInclude + '" /I"' + $renderInclude + '" ' +
    '/Fo:"' + $object + '" /Fe:"' + $exe + '" "' + $source + '" ' +
    '/link d3d12.lib dxgi.lib'
& $env:ComSpec /d /s /c $compile
if ($LASTEXITCODE -ne 0) {
    throw "Principled special probe build failed: exit $LASTEXITCODE"
}
$result = @(& $exe $repo 2>&1)
$result | Set-Content -LiteralPath (Join-Path $output 'gpu.log') -Encoding utf8
if ($LASTEXITCODE -ne 0 -or
    @($result | Where-Object { $_ -match '^PRINCIPLED_SPECIAL_GPU_OK cases=35 views=6 variants=7 checks=202049 furnaceChecks=4410 ' }).Count -ne 1) {
    throw "Principled special GPU gate failed: $($result -join "`n")"
}
$result

if (@($result | Where-Object { $_ -eq 'PRINCIPLED_SPECIAL_COMPILE_OK compiled=28 rejected=22' }).Count -ne 1) {
    throw 'Special compile/route gate did not finish'
}
$goldenPath = Join-Path $fixtureDirectory 'numeric-golden.csv'
$goldenHash = NormalizedHash $goldenPath
$manifest = Get-Content -LiteralPath (Join-Path $fixtureDirectory 'manifest.json') -Raw | ConvertFrom-Json
if ($goldenHash -ne '247190C6DA8200B1197E544A68B87F866DC22BF416CB4B3DE912684146261A47' -or
    $manifest.numeric_golden_sha256 -ne $goldenHash -or $manifest.numeric_golden_rows -ne 4410 -or
    $manifest.volume_defaults_normalized_sha256 -ne (NormalizedHash $volumePath) -or
    $manifest.blender_version -ne '5.1.1' -or $manifest.mask -ne 16383) {
    throw 'Pinned Special numeric golden/manifest differs'
}
$fresh = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
foreach ($row in (Import-Csv -LiteralPath (Join-Path $output 'cpu-reference.csv'))) {
    if ($row.mask -eq '16383') {
        $key = $row.case + '/' + $row.view + '/' + $row.field
        if ($fresh.ContainsKey($key)) { throw "Duplicate numeric reference row: $key" }
        $fresh.Add($key, $row)
    }
}
$golden = @(Import-Csv -LiteralPath $goldenPath)
if ($golden.Count -ne 4410) { throw 'Expected 4410 numeric golden rows' }
foreach ($row in $golden) {
    $key = $row.case + '/' + $row.view + '/' + $row.field
    if (-not $fresh.ContainsKey($key)) { throw "Missing numeric golden row: $key" }
    foreach ($channel in @('x', 'y', 'z', 'w')) {
        $actual = [double]::Parse($fresh[$key].$channel, [Globalization.CultureInfo]::InvariantCulture)
        $expected = [double]::Parse($row.$channel, [Globalization.CultureInfo]::InvariantCulture)
        if ([double]::IsNaN($actual) -or [double]::IsInfinity($actual) -or
            [Math]::Abs($actual - $expected) / [Math]::Max(1.0, [Math]::Abs($expected)) -gt 0.000001) {
            throw "Pinned numeric golden differs: $key/$channel"
        }
    }
}
Write-Output 'PRINCIPLED_SPECIAL_NUMERIC_GOLDEN_OK rows=4410 checks=17640'

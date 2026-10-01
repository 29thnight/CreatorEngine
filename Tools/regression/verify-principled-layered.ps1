param(
    [string]$VisualStudioInstallation = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$schema = Get-Content -LiteralPath (Join-Path $repo 'Tools\LatticeExample\fixtures\blender-5.1.1-shader-nodes.json') -Raw | ConvertFrom-Json
$principled = @($schema.nodes | Where-Object { $_.type -eq 'ShaderNodeBsdfPrincipled' })
if ($schema.blender_version -ne '5.1.1' -or $principled.Count -ne 1) {
    throw 'Pinned Blender Principled schema was not found'
}
$defaults = @{
    'Coat Weight' = @(0.0)
    'Coat Roughness' = @(0.03)
    'Coat IOR' = @(1.5)
    'Coat Tint' = @(1.0, 1.0, 1.0, 1.0)
    'Coat Normal' = @(0.0, 0.0, 0.0)
    'Sheen Weight' = @(0.0)
    'Sheen Roughness' = @(0.5)
    'Sheen Tint' = @(1.0, 1.0, 1.0, 1.0)
    'Anisotropic' = @(0.0)
    'Anisotropic Rotation' = @(0.0)
    'Tangent' = @(0.0, 0.0, 0.0)
    'Thin Film Thickness' = @(0.0)
    'Thin Film IOR' = @(1.33)
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
Write-Output 'PRINCIPLED_LAYERED_DEFAULT_SCHEMA_OK sockets=13'
$tablePath = Join-Path $repo 'Tools\blender\fixtures\principled-layered-5.1.1\sheen-ltc.csv'
$tableText = [IO.File]::ReadAllText($tablePath).Replace("`r`n", "`n")
$sha = [Security.Cryptography.SHA256]::Create()
try {
    $hash = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($tableText))).Replace('-', '')
} finally {
    $sha.Dispose()
}
if ($hash -ne 'AEB8870B010370633FF1EBF802F87A8045244CCD7E7DCF5D99BCCB2F5A511A14') {
    throw 'Sheen fixture differs from Blender 5.1.1'
}
Write-Output 'PRINCIPLED_SHEEN_FIXTURE_OK entries=1024'

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
$output = Join-Path $repo 'Build\Obj\PrincipledLayeredProbe'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$exe = Join-Path $output 'principled_layered_probe.exe'
$object = Join-Path $output 'principled_layered_probe.obj'
$source = Join-Path $PSScriptRoot 'principled_layered_probe.cpp'
$slangInclude = Join-Path $repo 'ThirdParty\Slang\include'
$renderInclude = Join-Path $repo 'Engine\RenderEngine\RHI'
$compile = 'call "' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++latest ' +
    '/permissive- /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /W4 /WX ' +
    '/I"' + $slangInclude + '" /I"' + $renderInclude + '" ' +
    '/Fo:"' + $object + '" /Fe:"' + $exe + '" "' + $source + '" ' +
    '/link d3d12.lib dxgi.lib'
& $env:ComSpec /d /s /c $compile
if ($LASTEXITCODE -ne 0) {
    throw "Principled layered probe build failed: exit $LASTEXITCODE"
}
$result = @(& $exe $repo 2>&1)
$result | Set-Content -LiteralPath (Join-Path $output 'gpu.log') -Encoding utf8
if ($LASTEXITCODE -ne 0 -or
    @($result | Where-Object { $_ -match '^PRINCIPLED_LAYERED_GPU_OK cases=35 angles=8 variants=7 checks=216776 furnaceChecks=5712 ' }).Count -ne 1) {
    throw "Principled layered GPU gate failed: $($result -join "`n")"
}
$result

if (@($result | Where-Object { $_ -eq 'PRINCIPLED_GGX_CLOSURE_INVARIANTS_OK checks=135' }).Count -ne 1) {
    throw 'Separate GGX closure and energy limit invariants did not finish'
}
if (@($result | Where-Object { $_ -match '^PRINCIPLED_GGX_BUDGET_GPU_OK cases=4 rawExcessViews=[1-9]\d* checks=\d+$' }).Count -ne 1) {
    throw 'Raw compensated energy and direct/IBL budget gate did not finish'
}

if (@($result | Where-Object { $_ -eq 'PRINCIPLED_LAYERED_COMPILE_OK compiled=14 rejected=8' }).Count -ne 1) {
    throw 'Layered permutation/dependency gate did not finish'
}
$fixtureDirectory = Join-Path $repo 'Tools\blender\fixtures\principled-layered-5.1.1'
$goldenPath = Join-Path $fixtureDirectory 'numeric-golden.csv'
$goldenText = [IO.File]::ReadAllText($goldenPath).Replace("`r`n", "`n")
$sha = [Security.Cryptography.SHA256]::Create()
try {
    $goldenHash = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($goldenText))).Replace('-', '')
} finally {
    $sha.Dispose()
}
$manifest = Get-Content -LiteralPath (Join-Path $fixtureDirectory 'manifest.json') -Raw | ConvertFrom-Json
if ($goldenHash -ne '26AEF415D59000EB9876B5BF1B9951042A18B1A06A2DBB15660831A6D8047F2B' -or
    $manifest.numeric_golden_sha256 -ne $goldenHash -or $manifest.numeric_golden_rows -ne 1960 -or
    $manifest.blender_version -ne '5.1.1' -or $manifest.mask -ne 2047) {
    throw 'Pinned Layered numeric golden/manifest differs'
}
$fresh = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
foreach ($row in (Import-Csv -LiteralPath (Join-Path $output 'cpu-reference.csv'))) {
    if ($row.mask -eq '2047') {
        $key = $row.case + '/' + $row.angle + '/' + $row.field
        if ($fresh.ContainsKey($key)) {
            throw "Duplicate numeric reference row: $key"
        }
        $fresh.Add($key, $row)
    }
}
$golden = @(Import-Csv -LiteralPath $goldenPath)
if ($golden.Count -ne 1960) {
    throw 'Expected 1960 numeric golden rows'
}
. (Join-Path $PSScriptRoot 'verify-thin-film-numeric-baseline.ps1')
Assert-ThinFilmNumericBaseline -Domain layered -Directory $fixtureDirectory -Fresh $fresh -Historical $golden -View angle

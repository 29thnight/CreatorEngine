param(
    [string]$Source = '',
    [string]$Output = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$fixture = Join-Path $PSScriptRoot 'fixtures\principled-layered-5.1.1'
if ([string]::IsNullOrWhiteSpace($Source)) {
    $Source = Join-Path $repo 'Build\Obj\blender-shader-5.1.1.tables'
}
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $repo 'Dynamic_CPP\Assets\Shaders\DefaultPassShader\Includes\SheenLtc.slang'
}
$text = [IO.File]::ReadAllText($Source)
$match = [regex]::Match($text, 'static const float table_sheen_ltc\[3072\] = \{(?<data>[\s\S]*?)\};')
if (-not $match.Success) {
    throw 'Blender 5.1.1 sheen table was not found'
}
$values = @([regex]::Matches($match.Groups['data'].Value, '[-+]?\d+\.\d+(?:e[-+]?\d+)?f') |
    ForEach-Object { $_.Value.TrimEnd('f') })
if ($values.Count -ne 3072) {
    throw 'Sheen table must contain three 32x32 planes'
}
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
$csv = [Collections.Generic.List[string]]::new()
$csv.Add('a,b,albedo')
$shader = [Collections.Generic.List[string]]::new()
$shader.Add('// SPDX-FileCopyrightText: 2011-2022 Blender Foundation')
$shader.Add('// SPDX-License-Identifier: Apache-2.0')
$shader.Add('// Generated from Blender v5.1.1 intern/cycles/scene/shader.tables.')
$shader.Add('// Reproduce with Tools/blender/export-lx-sheen-table.ps1.')
$shader.Add('#pragma once')
$shader.Add('')
$shader.Add('static const float3 kPrincipledSheenLtc[1024] =')
$shader.Add('{')
for ($index = 0; $index -lt 1024; ++$index) {
    $a = $values[$index]
    $b = $values[$index + 1024]
    $albedo = $values[$index + 2048]
    $csv.Add("$a,$b,$albedo")
    $shader.Add("    float3($a, $b, $albedo),")
}
$shader.Add('};')
$csvText = ($csv -join "`n") + "`n"
$sha = [Security.Cryptography.SHA256]::Create()
try {
    $hash = [Convert]::ToHexString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($csvText)))
} finally {
    $sha.Dispose()
}
if ($hash -ne 'AEB8870B010370633FF1EBF802F87A8045244CCD7E7DCF5D99BCCB2F5A511A14') {
    throw 'Sheen coefficients do not match the pinned Blender 5.1.1 table'
}
[IO.File]::WriteAllText((Join-Path $fixture 'sheen-ltc.csv'), $csvText, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($Output, ($shader -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
Write-Output 'SHEEN_LTC_EXPORT_OK size=32x32 channels=3'

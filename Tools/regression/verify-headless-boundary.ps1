[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $repo ('Build/Verification/Headless/Boundary-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out -Force | Out-Null
& python "$PSScriptRoot/headless_scene.py" --root $repo --source-only --output "$out/source.json"
if ($LASTEXITCODE) { throw 'Headless source boundary failed' }
& python "$PSScriptRoot/headless_boundary_tests.py" --root $repo --output "$out/canaries.json"
if ($LASTEXITCODE) { throw 'Headless dependency canaries failed' }
Write-Output "HEADLESS_BOUNDARY_OK evidence=$out"

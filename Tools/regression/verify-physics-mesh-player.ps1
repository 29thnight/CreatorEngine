[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Stage,
    [switch]$Shipping,
    [switch]$GeometryDdol,
    [int]$TimeoutSeconds = 240
)

$ErrorActionPreference = 'Stop'
$destination = if ($GeometryDdol) { 'PhysicsCharacterDestination.creator' } else { '' }
$output = & "$PSScriptRoot/verify-physics-character-player.ps1" -Stage $Stage -Mesh -GeometryDdol:$GeometryDdol -DestinationScene $destination -Shipping:$Shipping -TimeoutSeconds $TimeoutSeconds
$line = $output | Where-Object { $_ -like 'PHYSICS_CHARACTER_PLAYER_OK evidence=*' } | Select-Object -Last 1
if (!$line) { throw 'Mesh Player motion gate produced no evidence' }

$evidence = $line.Substring($line.IndexOf('evidence=') + 9)
$runtime = Join-Path $evidence 'Runtime'
$scene = if ($GeometryDdol) { 'PhysicsCharacterGeometry' } else { 'PhysicsCharacterMesh' }
$meta = Get-Content "$PSScriptRoot/fixtures/$scene.creator.meta" -Raw
$guid = [regex]::Match($meta, '(?m)^guid: (.+)$').Groups[1].Value.Trim()
$logs = Get-ChildItem $runtime -Recurse -File -Filter '*.html' | ForEach-Object { Get-Content $_.FullName -Raw }
if ((($logs -join "`n") + (Get-Content "$evidence/player.out" -Raw)) -notmatch ('\[scene.document\] source=cooked guid=' + [regex]::Escape($guid))) {
    throw 'Expected cooked mesh fixture Scene was not loaded'
}

$result = Get-Content "$evidence/result.json" -Raw | ConvertFrom-Json
$result.mesh | Add-Member -NotePropertyName sceneGuid -NotePropertyValue $guid
$result | ConvertTo-Json -Depth 20 | Set-Content "$evidence/result.json" -Encoding utf8
Write-Output "PHYSICS_MESH_PLAYER_OK evidence=$evidence"

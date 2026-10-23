[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$AssetsRoot,
    [string]$ScenePath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $ScenePath) { $ScenePath = Join-Path $PSScriptRoot 'fixtures/AnimationPlayer.creator' }

# This is a preflight for this authored fixture, not a general scene parser.
# The cooker remains responsible for sealing the complete dependency closure.
$models = @{}
foreach ($sidecar in Get-ChildItem -LiteralPath $AssetsRoot -Recurse -File -Filter '*.meta') {
    if ($sidecar.Name -notmatch '\.(glb|gltf|fbx)\.meta$') { continue }

    $text = [IO.File]::ReadAllText($sidecar.FullName)
    $root = [regex]::Match($text, '(?m)^assetId: ([0-9a-f-]{36})\s*$')
    # Unrelated legacy sidecars are not part of this fixture's identity index.
    # A referenced model without a current identity is rejected below.
    if (-not $root.Success) { continue }
    $modelId = $root.Groups[1].Value
    if ($models.ContainsKey($modelId)) { throw "Duplicate model identity: $modelId" }

    $meshes = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($mesh in [regex]::Matches($text,
        '(?m)^  - kind: mesh\r?\n    stableKey: [^\r\n]+\r?\n    assetId: ([0-9a-f-]{36})\s*$')) {
        [void]$meshes.Add($mesh.Groups[1].Value)
    }
    $walk = [regex]::IsMatch($text,
        '(?m)^  - kind: animation\r?\n    stableKey: name:Walk\r?\n    assetId: [0-9a-f-]{36}\r?\n    binding: animation/1\s*$')
    $materials = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $textures = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($asset in [regex]::Matches($text,
        '(?m)^  - kind: (material|texture)\r?\n    stableKey: [^\r\n]+\r?\n    assetId: ([0-9a-f-]{36})\s*$')) {
        if ($asset.Groups[1].Value -eq 'material') { [void]$materials.Add($asset.Groups[2].Value) }
        else { [void]$textures.Add($asset.Groups[2].Value) }
    }
    $models[$modelId] = @{ meshes = $meshes; materials = $materials; textures = $textures; walkAtIndexOne = $walk }
}

$scene = [IO.File]::ReadAllText([IO.Path]::GetFullPath($ScenePath))
if ($scene -match '(?m)^    (m_layer|m_collisionType):') { throw 'Fixture has obsolete entity layer fields' }
$entityCount = [regex]::Matches($scene, '(?m)^  - Entity:').Count
if ($entityCount -ne 69 -or [regex]::Matches($scene, '(?m)^    m_layerId: 1\s*$').Count -ne $entityCount) {
    throw 'Expected 69 entities in the registered Default layer'
}
$pairs = [regex]::Matches($scene,
    '(?m)^        m_modelGuid: ([0-9a-f-]{36})\r?\n        m_meshAssetId: ([0-9a-f-]{36})\s*$')
if ($pairs.Count -ne 12) { throw "Expected 12 authored mesh bindings, got $($pairs.Count)" }

$referencedModels = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($pair in $pairs) {
    $modelId = $pair.Groups[1].Value
    $meshId = $pair.Groups[2].Value
    if (-not $models.ContainsKey($modelId)) { throw "Unresolved fixture model: $modelId" }
    if (-not $models[$modelId].meshes.Contains($meshId)) {
        throw "Fixture mesh $meshId does not belong to model $modelId"
    }
    [void]$referencedModels.Add($modelId)
}

$motions = [regex]::Matches($scene,
    '(?m)^        m_AnimIndexChosen: 1\r?\n        m_AnimIndex: 0\r?\n        m_Motion: ([0-9a-f-]{36})\s*$')
if ($motions.Count -ne 1) { throw 'Expected one Animator selecting the Walk clip' }
$motionId = $motions[0].Groups[1].Value
if (-not $models.ContainsKey($motionId) -or -not $models[$motionId].walkAtIndexOne) {
    throw "Fixture motion model does not provide Walk at index 1: $motionId"
}

if ($scene -match '(?m)^          schema: 1\s*$') { throw 'Fixture still contains obsolete inline Code materials' }
$instances = [regex]::Matches($scene,
    '(?ms)^        m_Material:\r?\n          lattice_material: 1\r?\n(?<body>.*?)(?=^        m_LightMapping:)')
if ($instances.Count -ne 12) { throw 'Expected all twelve mesh material graph instances' }
foreach ($instance in $instances) {
    $body = $instance.Groups['body'].Value
    $materialId = [regex]::Match($body, '(?m)^          assetId: ([0-9a-f-]{36})\s*$').Groups[1].Value
    $owners = @($models.Keys | Where-Object { $models[$_].materials.Contains($materialId) })
    if ($owners.Count -ne 1) { throw "Unresolved material owner: $materialId" }
    $owner = $owners[0]
    $graphMeta = Join-Path $AssetsRoot "Materials/Models/$owner/$materialId.shadergraph.meta"
    $graphId = [regex]::Match($body, '(?m)^          graphAssetId: ([0-9a-f-]{36})\s*$').Groups[1].Value
    if (-not (Test-Path -LiteralPath $graphMeta) -or
        [IO.File]::ReadAllText($graphMeta).Trim() -cne "guid: $graphId") {
        throw "Material graph identity does not match its model material: $materialId"
    }
    $bindings = [regex]::Matches($body, '(?m)^              guid: ([0-9a-f-]{36})\s*$')
    $graphPath = $graphMeta.Substring(0, $graphMeta.Length - 5)
    $graph = Get-Content -LiteralPath $graphPath -Raw | ConvertFrom-Json
    $expectedBindings = @($graph.blackboard | Where-Object type -EQ 'Texture').Count
    if ($bindings.Count -ne $expectedBindings) { throw "Texture binding count differs from its graph: $materialId" }
    foreach ($binding in $bindings) {
        if (-not $models[$owner].textures.Contains($binding.Groups[1].Value)) {
            throw "Material texture does not belong to its model: $($binding.Groups[1].Value)"
        }
    }
}

Write-Output "ANIMATION_FIXTURE_OK entities=$entityCount models=$($referencedModels.Count) meshReferences=$($pairs.Count) animationModels=$($motions.Count) materialGraphs=$($instances.Count)"

param(
    [string]$AssetCooker = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Tools\AssetCooker\AssetCooker.exe'),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sourceAssets = (Resolve-Path (Join-Path $root 'Dynamic_CPP\Assets')).Path
$sourceCube = (Resolve-Path (Join-Path $sourceAssets 'Models\Prim_Cube.glb')).Path
$sourceCubeMeta = $sourceCube + '.meta'
$run = Join-Path $Work ('creator-mbc3-authoring-' + [guid]::NewGuid().ToString('N'))
$project = Join-Path $run 'Project'
$assets = Join-Path $project 'Assets'
$modelDir = Join-Path $assets 'Models'
$model = Join-Path $modelDir 'Prim_Cube.glb'
$meta = $model + '.meta'
$generations = Join-Path $project 'Library\ModelAssetGenerations'
$header = Join-Path $project 'ProjectSetting\AssetIdentity.asset'
$uuidV8 = '[0-9a-f]{8}-[0-9a-f]{4}-8[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}'
$failures = [System.Collections.Generic.List[string]]::new()

function Add-Failure([string]$Message) { $failures.Add($Message) }

function Invoke-Authoring([string]$Label, [string[]]$Extra = @()) {
    $stdout = Join-Path $run ($Label + '.stdout.txt')
    $stderr = Join-Path $run ($Label + '.stderr.txt')
    $arguments = @('--author-model-asset', '--asset-root', $assets,
        '--output', $generations, '--model', $model) + $Extra
    $process = Start-Process -FilePath $AssetCooker -ArgumentList $arguments `
        -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Stdout = if (Test-Path $stdout) { [IO.File]::ReadAllText($stdout) } else { '' }
        Stderr = if (Test-Path $stderr) { [IO.File]::ReadAllText($stderr) } else { '' }
    }
}

function Get-SidecarSnapshot {
    $text = [IO.File]::ReadAllText($meta)
    $ids = @([regex]::Matches($text, "(?m)^\s*assetId:\s*($uuidV8)\s*$") |
        ForEach-Object { $_.Groups[1].Value })
    return [pscustomobject]@{
        Text = $text
        Hash = (Get-FileHash -LiteralPath $meta -Algorithm SHA256).Hash
        Ids = $ids
        ModelId = if ($ids.Count -gt 0) { $ids[0] } else { '' }
        Generation = [int]([regex]::Match($text,
            '(?m)^generation:\s*([0-9]+)\s*$').Groups[1].Value)
    }
}

try {
    if (-not (Test-Path -LiteralPath $AssetCooker -PathType Leaf)) {
        throw "AssetCooker가 없다: $AssetCooker"
    }
    New-Item -ItemType Directory -Path $modelDir,
        (Split-Path -Parent $header),
        (Join-Path $assets 'Shaders\DefaultPassShader') -Force | Out-Null
    Copy-Item -LiteralPath $sourceCube -Destination $model
    # 실제 corpus는 MBC4 이후 schema v2다. 이 회귀는 legacy v1/sidecar 없음에서
    # 최초 authoring으로 진입하는 경계를 검증하므로 fixture identity를 자체 생성한다.
    # 실제 모델 GUID나 현재 epoch를 복사하면 fixture epoch와 결합돼 테스트가 데이터
    # 상태에 종속된다.
    $legacyMeta = @'
guid: 11111111-1111-4111-8111-111111111111
importSettings:
  extension: .glb
  timestamp: 1
ModelImporter:
  OptimizeMeshes: true
  ImproveCacheLocality: true
  CreateMeshCollider: false
'@
    [IO.File]::WriteAllText($meta, $legacyMeta.Replace("`r`n", "`n") + "`n",
        [Text.UTF8Encoding]::new($false))
    foreach ($shader in @('GBuffer', 'Forward')) {
        $sourceMeta = Join-Path $sourceAssets (
            'Shaders\DefaultPassShader\' + $shader + '.shadermeta.meta')
        $destinationMeta = Join-Path $assets (
            'Shaders\DefaultPassShader\' + $shader + '.shadermeta.meta')
        Copy-Item -LiteralPath $sourceMeta -Destination $destinationMeta
    }
    $headerText = @"
schemaVersion: 1
identityProfile: ce.uuidv8.sha256.v1
identityEpoch: mbc3-regression-epoch
identityEpochSeed: 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
createdAt: 2026-09-02T00:00:00Z
"@
    [IO.File]::WriteAllText($header, $headerText.Replace("`r`n", "`n") + "`n",
        [Text.UTF8Encoding]::new($false))

    $sourceHash = (Get-FileHash -LiteralPath $model -Algorithm SHA256).Hash
    $legacyHash = (Get-FileHash -LiteralPath $meta -Algorithm SHA256).Hash
    $first = Invoke-Authoring 'first'
    if ($first.ExitCode -ne 0 -or -not [string]::IsNullOrWhiteSpace($first.Stderr)) {
        Add-Failure "첫 authoring 실패: exit=$($first.ExitCode) $($first.Stderr)"
    }
    if ((Get-FileHash -LiteralPath $meta -Algorithm SHA256).Hash -eq $legacyHash) {
        Add-Failure 'legacy sidecar가 schema v2로 교체되지 않았다.'
    }
    $one = Get-SidecarSnapshot
    if ($one.Generation -ne 1 -or $one.Ids.Count -lt 2 -or
        $one.Text -notmatch '(?m)^schemaVersion:\s*2\s*$' -or
        $one.Text -match '(?m)^guid:' -or
        $one.Text -notmatch '(?m)^identityProfile:\s*ce\.uuidv8\.sha256\.v1\s*$') {
        Add-Failure '첫 schema v2/UUIDv8 sidecar 계약이 맞지 않는다.'
    }
    $generationOne = Join-Path $generations ($one.ModelId + '\1')
    foreach ($name in @('sidecar.meta', 'model.cemc', 'generation.asset')) {
        if (-not (Test-Path -LiteralPath (Join-Path $generationOne $name) -PathType Leaf)) {
            Add-Failure "generation 1 산출물이 없다: $name"
        }
    }

    $second = Invoke-Authoring 'second'
    if ($second.ExitCode -ne 0 -or -not [string]::IsNullOrWhiteSpace($second.Stderr)) {
        Add-Failure "두 번째 authoring 실패: exit=$($second.ExitCode) $($second.Stderr)"
    }
    $two = Get-SidecarSnapshot
    if ($two.Generation -ne 2 -or $two.ModelId -ne $one.ModelId -or
        ($two.Ids -join "`n") -ne ($one.Ids -join "`n")) {
        Add-Failure 'reimport가 UUIDv8 closure를 보존하며 generation만 증가시키지 않았다.'
    }
    $generationTwo = Join-Path $generations ($two.ModelId + '\2')
    if (-not (Test-Path -LiteralPath $generationTwo -PathType Container)) {
        Add-Failure 'generation 2가 게시되지 않았다.'
    }

    foreach ($point in @('after-decode', 'after-identity', 'after-stage-write',
        'after-stage-validation', 'after-generation-publish')) {
        $before = Get-SidecarSnapshot
        $beforeGenerations = @(Get-ChildItem -LiteralPath (
            Join-Path $generations $two.ModelId) -Directory).Count
        $failed = Invoke-Authoring ('fail-' + $point) @('--model-authoring-fail', $point)
        $after = Get-SidecarSnapshot
        $afterGenerations = @(Get-ChildItem -LiteralPath (
            Join-Path $generations $two.ModelId) -Directory).Count
        if ($failed.ExitCode -eq 0 -or $after.Hash -ne $before.Hash -or
            $afterGenerations -ne $beforeGenerations) {
            Add-Failure "실패 주입 원자성 위반: $point"
        }
    }

    $duplicateDir = Join-Path $assets 'Duplicate'
    New-Item -ItemType Directory -Path $duplicateDir -Force | Out-Null
    $duplicateModel = Join-Path $duplicateDir 'Prim_Cube.glb'
    Copy-Item -LiteralPath $model -Destination $duplicateModel
    Copy-Item -LiteralPath $meta -Destination ($duplicateModel + '.meta')
    $beforeCollision = Get-SidecarSnapshot
    $collision = Invoke-Authoring 'collision'
    $afterCollision = Get-SidecarSnapshot
    if ($collision.ExitCode -eq 0 -or
        $collision.Stderr -notmatch 'corpus collision' -or
        $beforeCollision.Hash -ne $afterCollision.Hash) {
        Add-Failure 'corpus UUIDv8 collision이 게시 전에 거부되지 않았다.'
    }

    if ((Get-FileHash -LiteralPath $model -Algorithm SHA256).Hash -ne $sourceHash) {
        Add-Failure 'authoring transaction이 model source를 수정했다.'
    }
    $temporaryLeaks = @(Get-ChildItem -LiteralPath $project -Recurse -Force |
        Where-Object { $_.Name -match 'model-authoring|\.staging-' })
    if ($temporaryLeaks.Count -gt 0) {
        Add-Failure "transaction temporary가 남았다: $($temporaryLeaks.Count)"
    }

    $productRoots = @('Engine', 'Editor', 'Tools\AssetCooker')
    $writerCallFiles = @()
    foreach ($productRoot in $productRoots) {
        $writerCallFiles += @(Get-ChildItem -LiteralPath (Join-Path $root $productRoot) `
            -Recurse -File -Include '*.cpp','*.h' | Where-Object {
                $_.FullName -notmatch '[\\/]RenderTests[\\/]' -and
                [IO.File]::ReadAllText($_.FullName) -match 'WriteModelSidecarV2\s*\('
            } | ForEach-Object { [IO.Path]::GetRelativePath($root, $_.FullName) })
    }
    $allowedWriterFiles = @(
        'Engine\RenderEngine\Assets\ModelSidecarV2.cpp',
        'Engine\RenderEngine\Assets\ModelSidecarV2.h',
        'Engine\RenderEngine\Assets\ModelAssetAuthoringTransaction.cpp')
    if (@($writerCallFiles | Where-Object { $_ -notin $allowedWriterFiles }).Count -gt 0 -or
        -not ($writerCallFiles -contains
            'Engine\RenderEngine\Assets\ModelAssetAuthoringTransaction.cpp')) {
        Add-Failure ('model sidecar writer 소유권 위반: ' + ($writerCallFiles -join ','))
    }
    $allProductText = ($productRoots | ForEach-Object {
        Get-ChildItem -LiteralPath (Join-Path $root $_) -Recurse -File `
            -Include '*.cpp','*.h' | Where-Object {
                $_.FullName -notmatch '[\\/]RenderTests[\\/]'
            } | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }
    }) -join "`n"
    if ($allProductText -match 'DeterministicSubAssetId|--refresh-model-identities') {
        Add-Failure 'pseudo-v5/legacy model identity writer 표면이 남았다.'
    }
    $editorPath = Join-Path $root 'Editor\EngineEntry\EditorAssetDatabase.cpp'
    $cookerPath = Join-Path $root 'Tools\AssetCooker\AssetCooker.cpp'
    $editorText = [IO.File]::ReadAllText($editorPath)
    $cookerText = [IO.File]::ReadAllText($cookerPath)
    if (([regex]::Matches($editorText, 'IsModelAuthoringSource')).Count -lt 3 -or
        $editorText -notmatch 'AuthorModelAsset\s*\(' -or
        $cookerText -notmatch '--author-model-asset' -or
        $cookerText -notmatch 'AuthorModelAsset\s*\(') {
        Add-Failure 'Editor create/modify/move 또는 AssetCooker가 단일 transaction에 연결되지 않았다.'
    }
    $repairPath = Join-Path $root 'Tools\migration\Repair-AssetSidecarIdentities.ps1'
    $applyMigrationPath = Join-Path $root 'Tools\migration\Invoke-AssetGuidMigration.ps1'
    $repairText = [IO.File]::ReadAllText($repairPath)
    $applyMigrationText = [IO.File]::ReadAllText($applyMigrationPath)
    if ($repairText -notmatch 'Test-IsModelMeta' -or
        $applyMigrationText -notmatch 'legacy UUIDv4 migration cannot write model identity') {
        Add-Failure 'legacy migration 도구의 model sidecar write 차단이 없다.'
    }
    $gltfImporterPath = Join-Path $root `
        'Engine\RenderEngine\Experiment\Import\GltfImporter.cpp'
    $gltfImporter = [IO.File]::ReadAllText($gltfImporterPath)
    if ($gltfImporter -notmatch 'setExtrasParseCallback' -or
        $gltfImporter -notmatch 'creatorEngineId') {
        Add-Failure 'glTF exporter persistent ID 수집 배선이 없다.'
    }

    "model-authoring-transaction model=$($two.ModelId) ids=$($two.Ids.Count) generations=2 failurePoints=5 collisions=1 failures=$($failures.Count)"
    if ($failures.Count -gt 0) {
        $failures | ForEach-Object { "  $_" }
        exit 1
    }
    '통과 — UUIDv8 model sidecar와 cooked generation이 단일 transaction으로 게시되고 모든 주입 실패가 기존 상태를 보존했다'
    exit 0
}
finally {
    if (Test-Path -LiteralPath $run) {
        Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
    }
}

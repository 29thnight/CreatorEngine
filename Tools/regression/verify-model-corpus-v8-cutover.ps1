param(
    [string]$AssetCooker = (Join-Path $PSScriptRoot '..\..\Bin\x64-Release\Tools\AssetCooker\AssetCooker.exe'),
    [string]$Work = $env:TEMP
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sourceBaseline = Join-Path $PSScriptRoot 'mbc0_corpus_baseline.json'
$migrator = Join-Path $root 'Tools\migration\Invoke-ModelAssetV8Cutover.ps1'
$verifier = Join-Path $root 'Tools\regression\verify-model-corpus-v8.ps1'
$run = Join-Path $Work ('creator-mbc4-regression-' + [guid]::NewGuid().ToString('N'))
$fixture = Join-Path $run 'Fixture'
$fixtureAssets = Join-Path $fixture 'Dynamic_CPP\Assets'
$fixtureBaseline = Join-Path $run 'baseline.json'
$failures = [Collections.Generic.List[string]]::new()

function Add-Failure([string]$Message) { $failures.Add($Message) }

function Invoke-Process([string]$FilePath, [string[]]$Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $FilePath
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { [void]$start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw "process 시작 실패: $FilePath" }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(300000)) {
        $process.Kill($true)
        throw "process timeout: $FilePath"
    }
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Stdout = $stdout.GetAwaiter().GetResult()
        Stderr = $stderr.GetAwaiter().GetResult()
    }
}

function Copy-Relative([string]$Relative) {
    $source = Join-Path $root $Relative
    $destination = Join-Path $fixture $Relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
}

function Get-FileSnapshot([string]$Path) {
    $snapshot = [ordered]@{}
    foreach ($file in @(Get-ChildItem -LiteralPath $Path -Recurse -File | Sort-Object FullName)) {
        $relative = [IO.Path]::GetRelativePath($Path, $file.FullName)
        $snapshot[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
    return $snapshot
}

function Test-SameSnapshot($Left, $Right) {
    if ($Left.Count -ne $Right.Count) { return $false }
    foreach ($key in $Left.Keys) {
        if (-not $Right.Contains($key) -or $Left[$key] -ne $Right[$key]) { return $false }
    }
    return $true
}

try {
    if (-not (Test-Path -LiteralPath $AssetCooker -PathType Leaf)) {
        throw "AssetCooker가 없다: $AssetCooker"
    }
    New-Item -ItemType Directory -Path $fixtureAssets -Force | Out-Null
    $baseline = Get-Content -LiteralPath $sourceBaseline -Raw | ConvertFrom-Json
    foreach ($model in @($baseline.models)) {
        Copy-Relative ([string]$model.source)
        # 실제 workspace는 MBC4 뒤 schema v2다. 회귀 입력은 baseline이 기록한
        # legacy GUID만 사용해 자체 sidecar를 만들고 현재 epoch/authoring key를
        # 복사하지 않는다.
        $fixtureMeta = (Join-Path $fixture ([string]$model.source)) + '.meta'
        $extension = [IO.Path]::GetExtension([string]$model.source).ToLowerInvariant()
        $legacyMeta = @"
guid: $($model.guid)
importSettings:
  extension: $extension
  timestamp: 1
ModelImporter:
  OptimizeMeshes: true
  ImproveCacheLocality: true
  CreateMeshCollider: false
"@
        [IO.File]::WriteAllText($fixtureMeta,
            $legacyMeta.Replace("`r`n", "`n") + "`n", [Text.UTF8Encoding]::new($false))
    }
    foreach ($relative in @(
        'Dynamic_CPP\Assets\Shaders\DefaultPassShader\GBuffer.shadermeta.meta',
        'Dynamic_CPP\Assets\Shaders\DefaultPassShader\Forward.shadermeta.meta')) {
        Copy-Relative $relative
    }
    foreach ($file in @(Get-ChildItem -LiteralPath (Join-Path $root 'Dynamic_CPP\Assets') `
        -Recurse -File | Where-Object {
            $_.Extension.ToLowerInvariant() -in @('.creator', '.prefab', '.asset')
        })) {
        $relative = [IO.Path]::GetRelativePath($root, $file.FullName)
        Copy-Relative $relative
    }

    # 실제 FT_Primitives는 이미 UUIDv8이다. baseline이 기록한 legacy model
    # reference 8건을 별도 fixture로 재구성해 회귀가 현재 workspace identity에
    # 기대지 않게 한다.
    $legacyReferenceLines = [Collections.Generic.List[string]]::new()
    foreach ($reference in @($baseline.references)) {
        foreach ($entry in @($reference.modelGuids)) {
            $separator = ([string]$entry).IndexOf('=')
            if ($separator -ge 0) {
                $legacyReferenceLines.Add('m_fileGuid: ' + ([string]$entry).Substring($separator + 1))
            }
        }
    }
    $modelReferenceFixture = Join-Path $fixtureAssets 'Scenes\Mbc4ModelReferences.creator'
    New-Item -ItemType Directory -Path (Split-Path -Parent $modelReferenceFixture) -Force | Out-Null
    [IO.File]::WriteAllText($modelReferenceFixture,
        ($legacyReferenceLines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))

    $firstMaterial = @($baseline.models | ForEach-Object { @($_.materials) } |
        Where-Object { $null -ne $_ } | Select-Object -First 1)[0]
    $synthetic = Join-Path $fixtureAssets 'Prefabs\Mbc4SubassetReference.prefab'
    New-Item -ItemType Directory -Path (Split-Path -Parent $synthetic) -Force | Out-Null
    [IO.File]::WriteAllText($synthetic,
        "m_materialGuid: $($firstMaterial.guid)`n", [Text.UTF8Encoding]::new($false))
    $baseline.summary.subassetReferences = [int]$baseline.summary.subassetReferences + 1
    [IO.File]::WriteAllText($fixtureBaseline,
        ($baseline | ConvertTo-Json -Depth 8) + "`n", [Text.UTF8Encoding]::new($false))

    $before = Get-FileSnapshot $fixture
    $common = @('-NoProfile', '-File', $migrator,
        '-Root', $fixture, '-AssetCooker', $AssetCooker,
        '-Baseline', $fixtureBaseline, '-Verifier', $verifier,
        '-ExpectedSubAssets', '310', '-Apply')
    $injected = Invoke-Process (Join-Path $PSHOME 'pwsh.exe') `
        ($common + @('-InjectFailure', 'AfterSidecars'))
    $afterFailure = Get-FileSnapshot $fixture
    if ($injected.ExitCode -eq 0) {
        Add-Failure '실패 주입이 성공으로 반환됐다.'
    }
    if (-not (Test-SameSnapshot $before $afterFailure)) {
        Add-Failure 'sidecar 게시 실패 뒤 fixture 파일 snapshot이 복구되지 않았다.'
    }
    if (Test-Path -LiteralPath (Join-Path $fixture 'Dynamic_CPP\ProjectSetting\AssetIdentity.asset')) {
        Add-Failure 'rollback 뒤 identity epoch header가 남았다.'
    }
    $generationRoot = Join-Path $fixture 'Dynamic_CPP\Library\ModelAssetGenerations'
    if ((Test-Path -LiteralPath $generationRoot) -and
        @(Get-ChildItem -LiteralPath $generationRoot -Directory).Count -ne 0) {
        Add-Failure 'rollback 뒤 generation이 남았다.'
    }

    $applied = Invoke-Process (Join-Path $PSHOME 'pwsh.exe') $common
    if ($applied.ExitCode -ne 0) {
        throw "정상 fixture cutover 실패: $($applied.Stderr) $($applied.Stdout)"
    }
    $syntheticText = Get-Content -LiteralPath $synthetic -Raw
    if ($syntheticText -match [regex]::Escape([string]$firstMaterial.guid) -or
        $syntheticText -notmatch '[0-9a-f]{8}-[0-9a-f]{4}-8[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}') {
        Add-Failure '합성 subasset 참조가 UUIDv8로 치환되지 않았다.'
    }
    $header = Join-Path $fixture 'Dynamic_CPP\ProjectSetting\AssetIdentity.asset'
    $headerHash = (Get-FileHash -LiteralPath $header -Algorithm SHA256).Hash
    $secondIssue = Invoke-Process $AssetCooker @(
        '--issue-model-identity-epoch', '--asset-root', $fixtureAssets,
        '--identity-epoch', 'must-not-overwrite')
    if ($secondIssue.ExitCode -eq 0 -or
        (Get-FileHash -LiteralPath $header -Algorithm SHA256).Hash -ne $headerHash) {
        Add-Failure '기존 identity epoch header overwrite가 거부되지 않았다.'
    }
    $verified = Invoke-Process (Join-Path $PSHOME 'pwsh.exe') @(
        '-NoProfile', '-File', $verifier, '-Root', $fixture,
        '-Baseline', $fixtureBaseline, '-GenerationRoot', $generationRoot,
        '-ExpectedSubAssets', '310', '-RequireFirstGeneration')
    if ($verified.ExitCode -ne 0) {
        Add-Failure "적용 후 verifier 실패: $($verified.Stderr) $($verified.Stdout)"
    }

    "model-corpus-v8-cutover models=$($baseline.models.Count) identities=68 modelRefs=8 syntheticSubassetRefs=1 rollback=1 epochOverwriteRejected=1 failures=$($failures.Count)"
    if ($failures.Count -gt 0) {
        $failures | ForEach-Object { "  $_" }
        exit 1
    }
    '통과 — corpus 실패 rollback과 model/material/texture UUIDv8 저장 참조 rewrite가 닫혔다'
    exit 0
}
finally {
    $workFull = [IO.Path]::GetFullPath($Work).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $runFull = [IO.Path]::GetFullPath($run)
    if ($runFull.StartsWith($workFull, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $run -PathType Container)) {
        Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
    }
}

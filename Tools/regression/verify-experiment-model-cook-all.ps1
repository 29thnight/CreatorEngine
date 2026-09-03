param(
    [string]$AssetCooker = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Tools\AssetCooker\AssetCooker.exe'),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$assets = (Resolve-Path (Join-Path $root 'Dynamic_CPP\Assets')).Path
$models = @(Get-ChildItem -LiteralPath $assets -File -Recurse | Where-Object {
    $_.Extension.ToLowerInvariant() -in @('.fbx', '.glb', '.gltf')
} | Sort-Object FullName)
$identityHeader = Join-Path $root 'Dynamic_CPP\ProjectSetting\AssetIdentity.asset'
$modelV2 = @($models | Where-Object {
    (Get-Content -LiteralPath ($_.FullName + '.meta') -Raw) -match
        '(?m)^schemaVersion:\s*2\s*$'
})
if ((Test-Path -LiteralPath $identityHeader -PathType Leaf) -or $modelV2.Count -gt 0) {
    if (-not (Test-Path -LiteralPath $identityHeader -PathType Leaf) -or
        $modelV2.Count -ne $models.Count) {
        "MBC4 partial corpus를 거부한다: header=$([int](Test-Path -LiteralPath $identityHeader)) modelV2=$($modelV2.Count)/$($models.Count)"
        exit 1
    }
    if (-not (Test-Path -LiteralPath $AssetCooker -PathType Leaf)) {
        "AssetCooker가 없다: $AssetCooker"
        exit 1
    }
    $v8Gate = Join-Path $PSScriptRoot 'verify-model-corpus-v8.ps1'
    $generationRoot = Join-Path $root 'Dynamic_CPP\Library\ModelAssetGenerations'
    $v8Output = @(& pwsh -NoProfile -File $v8Gate -Root $root `
        -GenerationRoot $generationRoot -ExpectedSubAssets 310 2>&1)
    $v8Valid = $LASTEXITCODE -eq 0
    $authoringGate = Join-Path $PSScriptRoot 'verify-model-authoring-transaction.ps1'
    $authoringOutput = @(& pwsh -NoProfile -File $authoringGate `
        -AssetCooker $AssetCooker -Work $Work 2>&1)
    $authoringValid = $LASTEXITCODE -eq 0
    $v8Output
    $authoringOutput
    "model-cook-all cutover=uuidv8 models=$($models.Count) generations=$($models.Count) v8Corpus=$v8Valid authoringTransaction=$authoringValid"
    if (-not $v8Valid -or -not $authoringValid) { exit 1 }
    '전체 통과 — legacy v1 cook 대신 MBC4 schema v2/cooked generation 전수 폐포와 MBC3 원자 authoring을 검증했다'
    exit 0
}
$trackedPatterns = @(
    'Dynamic_CPP/Assets/Animation/*.fbx',
    'Dynamic_CPP/Assets/Models/*.fbx',
    'Dynamic_CPP/Assets/Models/*.glb',
    'Dynamic_CPP/Assets/Models/*.gltf')
$trackedRelative = @(& git -C $root ls-files -- @trackedPatterns)
if ($LASTEXITCODE -ne 0) {
    'tracked model corpus를 읽지 못했다.'
    exit 1
}
$trackedModels = @($trackedRelative | ForEach-Object {
    [IO.Path]::GetFullPath((Join-Path $root $_))
})

if (-not (Test-Path -LiteralPath $AssetCooker -PathType Leaf)) {
    "AssetCooker가 없다: $AssetCooker"
    exit 1
}

# D5-b2c-3 이후 재질 manifest entry가 shaderAssetId 의존을 갖는다. 실측
# (2026-08-30) 결과 14개 모델 재질 전부 GBuffer만 참조하므로 GBuffer.shadermeta
# 하나가 전수 cook 폐포를 닫는다. 새 shader를 참조하는 모델이 늘면 여기 실패한다.
$gbufferShaderMeta = Join-Path $assets 'Shaders\DefaultPassShader\GBuffer.shadermeta'
foreach ($path in @($gbufferShaderMeta, ($gbufferShaderMeta + '.meta'))) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        "shadermeta producer 입력이 없다: $path"
        exit 1
    }
}
if ($trackedModels.Count -eq 0) {
    'tracked model corpus가 비어 있다.'
    exit 1
}
$discoveredModels = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($model in $models) { [void]$discoveredModels.Add($model.FullName) }
$missingTrackedModels = @($trackedModels | Where-Object {
    -not $discoveredModels.Contains($_)
})
if ($missingTrackedModels.Count -gt 0) {
    'tracked model이 현재 corpus에서 누락됐다:'
    $missingTrackedModels
    exit 1
}

$uuidV4Pattern = '[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}'
$allIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$sourceHashes = @{}
foreach ($path in @($gbufferShaderMeta, ($gbufferShaderMeta + '.meta'))) {
    $sourceHashes[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}
$materialCount = 0
$embeddedTextureCount = 0
foreach ($model in $models) {
    $meta = $model.FullName + '.meta'
    foreach ($path in @($model.FullName, $meta)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            "model producer 입력이 없다: $path"
            exit 1
        }
        $sourceHashes[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    }

    $text = Get-Content -LiteralPath $meta -Raw
    $top = [regex]::Match($text, "(?m)^guid:\s*($uuidV4Pattern)\s*$")
    if (-not $top.Success -or -not $allIds.Add($top.Groups[1].Value)) {
        "model 최상위 UUIDv4가 없거나 중복됐다: $meta"
        exit 1
    }
    if ($text -notmatch '(?m)^subAssets:\s*$' -or
        $text -notmatch '(?m)^\s{2}schemaVersion:\s*1\s*$' -or
        $text -notmatch '(?m)^\s{2}materials:\s*(?:\[\])?\s*$' -or
        $text -notmatch '(?m)^\s{2}embeddedTextures:\s*(?:\[\])?\s*$') {
        "model subAssets schema가 불완전하다: $meta"
        exit 1
    }
    $materialBlock = [regex]::Match($text,
        '(?ms)^\s{2}materials:\s*(?:\[\]\s*$|\r?\n(?<body>.*?))^\s{2}embeddedTextures:')
    if (-not $materialBlock.Success) {
        "material subasset block을 읽지 못했다: $meta"
        exit 1
    }
    $textureBlock = [regex]::Match($text,
        '(?ms)^\s{2}embeddedTextures:\s*(?:\[\]\s*$|\r?\n(?<body>.*))\z')
    if (-not $textureBlock.Success) {
        "embedded texture subasset block을 읽지 못했다: $meta"
        exit 1
    }
    $materialCount += [regex]::Matches(
        $materialBlock.Groups['body'].Value, '(?m)^\s{4}- key:\s*').Count
    $embeddedTextureCount += [regex]::Matches(
        $textureBlock.Groups['body'].Value, '(?m)^\s{4}- key:\s*').Count

    foreach ($match in [regex]::Matches($text,
        "(?m)^\s{6}guid:\s*($uuidV4Pattern)\s*$")) {
        if (-not $allIds.Add($match.Groups[1].Value)) {
            "model/subasset UUIDv4가 전수 corpus에서 중복됐다: $($match.Groups[1].Value)"
            exit 1
        }
    }
}

if ($allIds.Count -ne ($models.Count + $materialCount + $embeddedTextureCount)) {
    "model subasset UUIDv4 수가 entry 수와 다르다: ids=$($allIds.Count) models=$($models.Count) materials=$materialCount embeddedTextures=$embeddedTextureCount"
    exit 1
}

$run = Join-Path $Work ('CE_D5ModelCookAll_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null

function Invoke-Cooker {
    param([string[]]$Arguments, [string]$Label)

    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $AssetCooker
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { [void]$start.ArgumentList.Add($argument) }

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw "AssetCooker process를 시작하지 못했다: $Label" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill($true)
        throw "AssetCooker TIMEOUT: $Label"
    }
    [pscustomobject]@{
        ExitCode = $process.ExitCode
        Stdout = $stdoutTask.GetAwaiter().GetResult()
        Stderr = $stderrTask.GetAwaiter().GetResult()
    }
}

function Get-TreeSnapshot {
    param([string]$Path)

    $snapshot = [ordered]@{}
    foreach ($file in @(Get-ChildItem -LiteralPath $Path -File -Recurse | Sort-Object FullName)) {
        $relative = [IO.Path]::GetRelativePath($Path, $file.FullName).Replace('\', '/')
        $snapshot[$relative] = [pscustomobject]@{
            Bytes = $file.Length
            Sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        }
    }
    return $snapshot
}

function Test-SameSnapshot {
    param($Left, $Right)

    if ($Left.Count -ne $Right.Count) { return $false }
    foreach ($key in $Left.Keys) {
        if (-not $Right.Contains($key) -or
            $Left[$key].Bytes -ne $Right[$key].Bytes -or
            $Left[$key].Sha256 -ne $Right[$key].Sha256) { return $false }
    }
    return $true
}

$cookArguments = [Collections.Generic.List[string]]::new()
$cookArguments.Add('--asset-root')
$cookArguments.Add($assets)
foreach ($model in $models) {
    $cookArguments.Add('--model')
    $cookArguments.Add($model.FullName)
}
$cookArguments.Add('--shadermeta')
$cookArguments.Add($gbufferShaderMeta)
$outputA = Join-Path $run 'output-a'
$outputB = Join-Path $run 'output-b'
$firstArguments = [Collections.Generic.List[string]]::new()
$firstArguments.AddRange($cookArguments)
$firstArguments.Insert(2, '--output')
$firstArguments.Insert(3, $outputA)
$secondArguments = [Collections.Generic.List[string]]::new()
$secondArguments.AddRange($cookArguments)
$secondArguments.Insert(2, '--output')
$secondArguments.Insert(3, $outputB)
$first = Invoke-Cooker -Arguments $firstArguments.ToArray() -Label 'all-model-a'
$second = Invoke-Cooker -Arguments $secondArguments.ToArray() -Label 'all-model-b'

$snapshotA = Get-TreeSnapshot -Path $outputA
$snapshotB = Get-TreeSnapshot -Path $outputB
$deterministic = Test-SameSnapshot -Left $snapshotA -Right $snapshotB
$artifacts = @(Get-ChildItem -LiteralPath (Join-Path $outputA 'Derived\Models') `
    -File -Filter '*.cemc' -Recurse)
$manifest = Join-Path $outputA 'Derived\asset-manifest.cemf'
# manifest entry = model + material + embedded texture + shadermeta(1).
# 게시 파일 = model CEMC + embedded texture + cooked shadermeta(1) + manifest.
$manifestEntries = $models.Count + $materialCount + $embeddedTextureCount + 1
$expectedFiles = $models.Count + $embeddedTextureCount + 1 + 1
$sourceIdentityCount = @(Get-ChildItem -LiteralPath $assets -Recurse -File -Filter '*.meta').Count
$summaryPattern = "asset-cooker models=$($models.Count) materials=$materialCount embeddedTextures=$embeddedTextureCount textureReferences=(\d+) externalTextureRefs=0 embeddedTextureBytes=(\d+) textures=0 shaderMetas=1 standaloneMaterials=0 standaloneMaterialBytes=0 scenes=0 prefabs=0 sceneBytes=0 legacyTextureNameRefs=0 unproducedGuidRefs=0 artifactPaths=$($expectedFiles - 1) files=$expectedFiles manifestEntries=$manifestEntries sourceIdentities=$sourceIdentityCount artifactBytes=(\d+) textureBytes=0 shaderMetaBytes=(\d+) manifest=Derived/asset-manifest\.cemf"
$successRuns = @($first, $second | Where-Object {
    $_.ExitCode -eq 0 -and [regex]::IsMatch($_.Stdout, $summaryPattern)
}).Count
$unexpectedStderr = @($first, $second | Where-Object {
    -not [string]::IsNullOrWhiteSpace($_.Stderr)
}).Count

# MBC3부터 model identity authoring은 별도 UUIDv4 refresher가 아니라 schema v2,
# UUIDv8, cooked generation을 함께 게시하는 단일 transaction이다. 이 전수 cook
# 게이트는 제품 corpus의 legacy cook 결정성을 유지하고, 새 transaction의 원자성은
# 전용 게이트 결과를 함께 요구한다.
$authoringGate = Join-Path $PSScriptRoot 'verify-model-authoring-transaction.ps1'
$authoringGateOutput = @(& pwsh -NoProfile -File $authoringGate `
    -AssetCooker $AssetCooker -Work $Work 2>&1)
$authoringGateValid = $LASTEXITCODE -eq 0

$sourceMutations = @($sourceHashes.Keys | Where-Object {
    -not (Test-Path -LiteralPath $_ -PathType Leaf) -or
    (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash -ne $sourceHashes[$_]
})
$artifactBytes = ($artifacts | Measure-Object Length -Sum).Sum
$manifestBytes = if (Test-Path -LiteralPath $manifest -PathType Leaf) {
    (Get-Item -LiteralPath $manifest).Length
} else { 0 }

"experiment-model-cook-all output=$run"
"models=$($models.Count) materials=$materialCount embeddedTextures=$embeddedTextureCount globalIds=$($allIds.Count) successRuns=$successRuns deterministic=$deterministic files=$($snapshotA.Count) artifacts=$($artifacts.Count) artifactBytes=$artifactBytes manifestBytes=$manifestBytes authoringTransaction=$authoringGateValid sourceMutations=$($sourceMutations.Count) unexpectedStderr=$unexpectedStderr"

$passed = $successRuns -eq 2 -and $deterministic -and
    $snapshotA.Count -eq $expectedFiles -and
    $artifacts.Count -eq $models.Count -and
    $artifactBytes -gt 0 -and $manifestBytes -gt 0 -and
    $authoringGateValid -and
    $sourceMutations.Count -eq 0 -and
    $unexpectedStderr -eq 0
if (-not $passed) {
    if ($first.ExitCode -ne 0 -or -not [string]::IsNullOrWhiteSpace($first.Stderr)) {
        'first stderr:'; $first.Stderr
    }
    if ($second.ExitCode -ne 0 -or -not [string]::IsNullOrWhiteSpace($second.Stderr)) {
        'second stderr:'; $second.Stderr
    }
    if (-not $authoringGateValid) { 'model authoring transaction:'; $authoringGateOutput }
    if ($sourceMutations.Count -gt 0) { '원본 변경:'; $sourceMutations }
    exit 1
}

"전체 통과 — tracked $($trackedModels.Count) + local $($models.Count - $trackedModels.Count) legacy corpus cook이 결정적이고 MBC3 UUIDv8 authoring transaction이 sidecar/cooked generation을 원자 게시했다"
exit 0

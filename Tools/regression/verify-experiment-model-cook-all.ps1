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

# 명시적 migration 모드는 복제 sidecar에서만 실행한다. 최상위 identity는
# 유지되고 하위 identity는 모두 새 UUIDv4가 되어야 한다.
$fixtureAssets = Join-Path $run 'refresh-fixture\Assets'
$fixtureModelDir = Join-Path $fixtureAssets 'Models'
New-Item -ItemType Directory -Path $fixtureModelDir -Force | Out-Null
$cube = (Resolve-Path (Join-Path $assets 'Models\Prim_Cube.glb')).Path
$fixtureCube = Join-Path $fixtureModelDir 'Prim_Cube.glb'
Copy-Item -LiteralPath $cube -Destination $fixtureCube
Copy-Item -LiteralPath ($cube + '.meta') -Destination ($fixtureCube + '.meta')
$beforeRefresh = Get-Content -LiteralPath ($fixtureCube + '.meta') -Raw
$beforeTop = [regex]::Match($beforeRefresh, "(?m)^guid:\s*($uuidV4Pattern)\s*$").Groups[1].Value
$beforeNested = @([regex]::Matches($beforeRefresh,
    "(?m)^\s{6}guid:\s*($uuidV4Pattern)\s*$") | ForEach-Object { $_.Groups[1].Value })
$refresh = Invoke-Cooker -Label 'identity-refresh' -Arguments @(
    '--refresh-model-identities', '--asset-root', $fixtureAssets,
    '--model', $fixtureCube)
$afterRefresh = Get-Content -LiteralPath ($fixtureCube + '.meta') -Raw
$afterTop = [regex]::Match($afterRefresh, "(?m)^guid:\s*($uuidV4Pattern)\s*$").Groups[1].Value
$afterNested = @([regex]::Matches($afterRefresh,
    "(?m)^\s{6}guid:\s*($uuidV4Pattern)\s*$") | ForEach-Object { $_.Groups[1].Value })
$refreshValid = $refresh.ExitCode -eq 0 -and
    [string]::IsNullOrWhiteSpace($refresh.Stderr) -and
    $refresh.Stdout -match 'identity-refresh models=1 materials=1 embeddedTextures=1' -and
    $beforeTop -eq $afterTop -and $afterNested.Count -eq 2 -and
    @($afterNested | Where-Object { $_ -in $beforeNested }).Count -eq 0

# batch 전체의 기존 상위 ID를 먼저 예약해야 한다. 같은 model GUID를 가진 두
# sidecar를 주면 import/temporary write 전에 거부하고 첫 sidecar도 그대로 둔다.
$duplicateAssets = Join-Path $run 'duplicate-fixture\Assets'
$duplicateA = Join-Path $duplicateAssets 'A\Cube.glb'
$duplicateB = Join-Path $duplicateAssets 'B\Cube.glb'
New-Item -ItemType Directory -Path (Split-Path -Parent $duplicateA), `
    (Split-Path -Parent $duplicateB) -Force | Out-Null
foreach ($destination in @($duplicateA, $duplicateB)) {
    Copy-Item -LiteralPath $cube -Destination $destination
    Copy-Item -LiteralPath ($cube + '.meta') -Destination ($destination + '.meta')
}
$duplicateBeforeA = (Get-FileHash -LiteralPath ($duplicateA + '.meta') -Algorithm SHA256).Hash
$duplicateBeforeB = (Get-FileHash -LiteralPath ($duplicateB + '.meta') -Algorithm SHA256).Hash
$duplicateReject = Invoke-Cooker -Label 'duplicate-top-id' -Arguments @(
    '--refresh-model-identities', '--asset-root', $duplicateAssets,
    '--model', $duplicateA, '--model', $duplicateB)
$duplicateTransactionRejected = $duplicateReject.ExitCode -ne 0 -and
    $duplicateReject.Stderr -match 'asset root UUIDv4가 중복' -and
    (Get-FileHash -LiteralPath ($duplicateA + '.meta') -Algorithm SHA256).Hash -eq $duplicateBeforeA -and
    (Get-FileHash -LiteralPath ($duplicateB + '.meta') -Algorithm SHA256).Hash -eq $duplicateBeforeB -and
    @(Get-ChildItem -LiteralPath $duplicateAssets -File -Recurse | Where-Object {
        $_.Name -match '\.(?:identity-refresh|rollback)-'
    }).Count -eq 0

$sourceMutations = @($sourceHashes.Keys | Where-Object {
    -not (Test-Path -LiteralPath $_ -PathType Leaf) -or
    (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash -ne $sourceHashes[$_]
})
$artifactBytes = ($artifacts | Measure-Object Length -Sum).Sum
$manifestBytes = if (Test-Path -LiteralPath $manifest -PathType Leaf) {
    (Get-Item -LiteralPath $manifest).Length
} else { 0 }

"experiment-model-cook-all output=$run"
"models=$($models.Count) materials=$materialCount embeddedTextures=$embeddedTextureCount globalIds=$($allIds.Count) successRuns=$successRuns deterministic=$deterministic files=$($snapshotA.Count) artifacts=$($artifacts.Count) artifactBytes=$artifactBytes manifestBytes=$manifestBytes refreshValid=$refreshValid duplicateTransactionRejected=$duplicateTransactionRejected sourceMutations=$($sourceMutations.Count) unexpectedStderr=$unexpectedStderr"

$passed = $successRuns -eq 2 -and $deterministic -and
    $snapshotA.Count -eq $expectedFiles -and
    $artifacts.Count -eq $models.Count -and
    $artifactBytes -gt 0 -and $manifestBytes -gt 0 -and
    $refreshValid -and $duplicateTransactionRejected -and
    $sourceMutations.Count -eq 0 -and
    $unexpectedStderr -eq 0
if (-not $passed) {
    if ($first.ExitCode -ne 0 -or -not [string]::IsNullOrWhiteSpace($first.Stderr)) {
        'first stderr:'; $first.Stderr
    }
    if ($second.ExitCode -ne 0 -or -not [string]::IsNullOrWhiteSpace($second.Stderr)) {
        'second stderr:'; $second.Stderr
    }
    if (-not $refreshValid) { 'refresh stdout:'; $refresh.Stdout; 'refresh stderr:'; $refresh.Stderr }
    if (-not $duplicateTransactionRejected) {
        'duplicate transaction stdout:'; $duplicateReject.Stdout
        'duplicate transaction stderr:'; $duplicateReject.Stderr
    }
    if ($sourceMutations.Count -gt 0) { '원본 변경:'; $sourceMutations }
    exit 1
}

"전체 통과 — tracked $($trackedModels.Count) + local $($models.Count - $trackedModels.Count) model sidecar identity가 전수 UUIDv4이며 AssetCooker가 결정적 CEMC/CEMF를 만들고 명시적 refresh 외 source를 수정하지 않았다"
exit 0

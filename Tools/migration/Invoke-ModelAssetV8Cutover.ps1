param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$AssetCooker = (Join-Path $Root 'Bin\x64-Release\Tools\AssetCooker\AssetCooker.exe'),
    [string]$Baseline = (Join-Path $Root 'Tools\regression\mbc0_corpus_baseline.json'),
    [string]$Verifier = (Join-Path $Root 'Tools\regression\verify-model-corpus-v8.ps1'),
    [string]$IdentityEpoch = '2026-09-model-bigbang',
    [int]$ExpectedSubAssets = 310,
    [string]$Work = $env:TEMP,
    [switch]$Apply,
    [ValidateSet('None', 'AfterStage', 'AfterGenerations', 'AfterSidecars',
        'BeforeHeader', 'AfterHeader')]
    [string]$InjectFailure = 'None'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$resolvedWork = (Resolve-Path -LiteralPath $Work).Path
$assets = Join-Path $resolvedRoot 'Dynamic_CPP\Assets'
$header = Join-Path $resolvedRoot 'Dynamic_CPP\ProjectSetting\AssetIdentity.asset'
$generations = Join-Path $resolvedRoot 'Dynamic_CPP\Library\ModelAssetGenerations'
$baselineDoc = Get-Content -LiteralPath $Baseline -Raw | ConvertFrom-Json
$run = Join-Path $resolvedWork ('creator-mbc4-cutover-' + [guid]::NewGuid().ToString('N'))
$stageRoot = Join-Path $run 'Stage'
$stageAssets = Join-Path $stageRoot 'Dynamic_CPP\Assets'
$stageGenerations = Join-Path $stageRoot 'Dynamic_CPP\Library\ModelAssetGenerations'
$backupRoot = Join-Path $run 'Backup'
$utf8 = [Text.UTF8Encoding]::new($false)
$oldToNew = [Collections.Generic.Dictionary[string,string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
$oldIds = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
$newIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$publishedGenerations = [Collections.Generic.List[string]]::new()
$replacementFiles = [Collections.Generic.List[object]]::new()
$publishedHeader = $false
$uuidV8 = '^[0-9a-f]{8}-[0-9a-f]{4}-8[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$'

function Test-ContainedPath([string]$Parent, [string]$Child) {
    $parentFull = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $childFull = [IO.Path]::GetFullPath($Child)
    return $childFull.StartsWith($parentFull, [StringComparison]::OrdinalIgnoreCase)
}

function Invoke-CheckedProcess([string]$FilePath, [string[]]$Arguments, [string]$Label) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $FilePath
    $start.WorkingDirectory = $resolvedRoot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { [void]$start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw "process를 시작하지 못했다: $Label" }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(300000)) {
        $process.Kill($true)
        throw "process timeout: $Label"
    }
    $outText = $stdout.GetAwaiter().GetResult()
    $errorText = $stderr.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0) {
        throw "$Label 실패(exit=$($process.ExitCode)): $errorText $outText"
    }
    return $outText.Trim()
}

function Copy-IntoTree([string]$Source, [string]$DestinationRoot, [string]$Relative) {
    $destination = Join-Path $DestinationRoot $Relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $destination
    return $destination
}

function Get-PersistedDocuments([string]$AssetRoot) {
    $result = [Collections.Generic.List[IO.FileInfo]]::new()
    foreach ($file in @(Get-ChildItem -LiteralPath $AssetRoot -Recurse -File | Where-Object {
        $_.Extension.ToLowerInvariant() -in @('.creator', '.prefab', '.asset')
    })) {
        $stream = [IO.File]::OpenRead($file.FullName)
        try {
            $magic = [byte[]]::new(4)
            [void]$stream.Read($magic, 0, 4)
        } finally { $stream.Dispose() }
        if ([Text.Encoding]::ASCII.GetString($magic) -ne 'CEMA') { $result.Add($file) }
    }
    return @($result)
}

function Get-TopScalar([string]$Text, [string]$Key) {
    $match = [regex]::Match($Text, '(?m)^' + [regex]::Escape($Key) + ':\s*(\S.*?)\s*$')
    if ($match.Success) { return $match.Groups[1].Value }
    return $null
}

function Get-SubAssetRecords([string]$Text) {
    $records = [Collections.Generic.List[object]]::new()
    $current = $null
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^  - kind:\s*(\S+)\s*$') {
            if ($null -ne $current) { $records.Add([pscustomobject]$current) }
            $current = [ordered]@{ Kind = $Matches[1]; StableKey = ''; AssetId = ''; Binding = '' }
            continue
        }
        if ($null -eq $current) { continue }
        if ($line -match '^    stableKey:\s*(.*?)\s*$') { $current.StableKey = $Matches[1]; continue }
        if ($line -match '^    assetId:\s*(\S+)\s*$') { $current.AssetId = $Matches[1]; continue }
        if ($line -match '^    binding:\s*(.*?)\s*$') { $current.Binding = $Matches[1]; continue }
    }
    if ($null -ne $current) { $records.Add([pscustomobject]$current) }
    return @($records)
}

function Add-IdentityMapping([string]$Old, [string]$New, [string]$Context) {
    if ($Old -notmatch '^[0-9a-fA-F-]{36}$' -or $New -cnotmatch $uuidV8) {
        throw "identity mapping 형식이 잘못됐다: $Context"
    }
    if ($oldToNew.ContainsKey($Old) -and $oldToNew[$Old] -ne $New) {
        throw "legacy GUID가 두 UUIDv8로 매핑됐다: $Old"
    }
    if (-not $newIds.Add($New)) {
        throw "새 UUIDv8이 두 legacy identity에 배정됐다: $New"
    }
    $oldToNew[$Old] = $New
}

function Publish-File([string]$Staged, [string]$Destination) {
    if (-not (Test-ContainedPath $resolvedRoot $Destination)) {
        throw "publish 대상이 workspace 밖이다: $Destination"
    }
    $temporary = $Destination + '.mbc4-' + [guid]::NewGuid().ToString('N')
    try {
        Copy-Item -LiteralPath $Staged -Destination $temporary
        [IO.File]::Move($temporary, $Destination, $true)
    } finally {
        if (Test-Path -LiteralPath $temporary -PathType Leaf) {
            Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
        }
    }
}

function Invoke-InjectedFailure([string]$Point) {
    if ($InjectFailure -eq $Point) { throw "MBC4 regression failure injected: $Point" }
}

if (-not (Test-Path -LiteralPath $AssetCooker -PathType Leaf)) {
    throw "AssetCooker가 없다: $AssetCooker"
}
if (-not (Test-Path -LiteralPath $Verifier -PathType Leaf)) {
    throw "MBC4 verifier가 없다: $Verifier"
}
if (-not (Test-Path -LiteralPath $assets -PathType Container)) {
    throw "Assets root가 없다: $assets"
}
if (Test-Path -LiteralPath $header) {
    throw 'identity epoch header가 이미 존재한다. MBC4는 one-shot이며 기존 epoch를 덮지 않는다.'
}
if (-not (Test-ContainedPath $resolvedWork $run)) {
    throw "temporary run path가 Work 밖이다: $run"
}

try {
    New-Item -ItemType Directory -Path $stageAssets, $backupRoot -Force | Out-Null
    $originalHashes = [ordered]@{}
    $currentDocuments = @(Get-PersistedDocuments $assets)
    foreach ($document in $currentDocuments) {
        $relative = [IO.Path]::GetRelativePath($resolvedRoot, $document.FullName)
        [void](Copy-IntoTree $document.FullName $stageRoot $relative)
        $originalHashes[$document.FullName] = (Get-FileHash -LiteralPath $document.FullName -Algorithm SHA256).Hash
    }

    foreach ($model in @($baselineDoc.models)) {
        $source = Join-Path $resolvedRoot ([string]$model.source)
        $meta = $source + '.meta'
        if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or
            -not (Test-Path -LiteralPath $meta -PathType Leaf)) {
            throw "baseline model/source sidecar가 없다: $($model.source)"
        }
        $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($sourceHash -ne ([string]$model.sha256).ToLowerInvariant()) {
            throw "MBC0 이후 model source가 변했다: $($model.source)"
        }
        $legacyText = Get-Content -LiteralPath $meta -Raw
        if ((Get-TopScalar $legacyText 'guid') -ne [string]$model.guid -or
            $legacyText -match '(?m)^schemaVersion:\s*2\s*$') {
            throw "legacy sidecar가 baseline과 다르거나 부분 migration 상태다: $($model.source)"
        }
        if (-not $oldIds.Add(([string]$model.guid).ToLowerInvariant())) {
            throw "legacy model GUID가 중복됐다: $($model.guid)"
        }
        foreach ($item in @($model.materials) + @($model.embeddedTextures)) {
            if (-not $oldIds.Add(([string]$item.guid).ToLowerInvariant())) {
                throw "legacy subasset GUID가 중복됐다: $($item.guid)"
            }
        }
        foreach ($path in @($source, $meta)) {
            $relative = [IO.Path]::GetRelativePath($resolvedRoot, $path)
            [void](Copy-IntoTree $path $stageRoot $relative)
            $originalHashes[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        }
    }

    foreach ($shader in @('GBuffer.shadermeta.meta', 'Forward.shadermeta.meta')) {
        $relative = Join-Path 'Dynamic_CPP\Assets\Shaders\DefaultPassShader' $shader
        $source = Join-Path $resolvedRoot $relative
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "model authoring shader sidecar가 없다: $relative"
        }
        [void](Copy-IntoTree $source $stageRoot $relative)
    }

    $expectedReferences = [int]$baselineDoc.summary.modelReferences +
        [int]$baselineDoc.summary.subassetReferences
    $currentReferenceCount = 0
    foreach ($document in $currentDocuments) {
        $text = Get-Content -LiteralPath $document.FullName -Raw
        foreach ($old in $oldIds) {
            $currentReferenceCount += [regex]::Matches($text,
                [regex]::Escape($old), [Text.RegularExpressions.RegexOptions]::IgnoreCase).Count
        }
    }
    if ($currentReferenceCount -ne $expectedReferences) {
        throw "저장 참조가 MBC0 baseline과 다르다: $currentReferenceCount != $expectedReferences"
    }

    $epochOutput = Invoke-CheckedProcess $AssetCooker @(
        '--issue-model-identity-epoch', '--asset-root', $stageAssets,
        '--identity-epoch', $IdentityEpoch) 'identity epoch issuance'

    foreach ($model in @($baselineDoc.models)) {
        $relativeSource = [IO.Path]::GetRelativePath($assets,
            (Join-Path $resolvedRoot ([string]$model.source)))
        $authorOutput = Invoke-CheckedProcess $AssetCooker @(
            '--author-model-asset', '--asset-root', $stageAssets,
            '--output', $stageGenerations, '--model', $relativeSource) `
            ('model authoring ' + [string]$model.source)
        $stageMeta = (Join-Path $stageRoot ([string]$model.source)) + '.meta'
        $newText = Get-Content -LiteralPath $stageMeta -Raw
        $modelId = Get-TopScalar $newText 'assetId'
        Add-IdentityMapping ([string]$model.guid) $modelId ([string]$model.source)
        $records = @(Get-SubAssetRecords $newText)
        foreach ($legacy in @($model.materials)) {
            $found = @($records | Where-Object {
                $_.Kind -eq 'material' -and $_.Binding -eq [string]$legacy.key })
            if ($found.Count -ne 1) {
                throw "material binding을 새 closure에서 유일하게 찾지 못했다: $($model.source)::$($legacy.key)"
            }
            Add-IdentityMapping ([string]$legacy.guid) $found[0].AssetId `
                ([string]$model.source + '::' + [string]$legacy.key)
        }
        foreach ($legacy in @($model.embeddedTextures)) {
            $found = @($records | Where-Object {
                $_.Kind -eq 'texture' -and $_.Binding -eq [string]$legacy.key })
            if ($found.Count -ne 1) {
                throw "texture binding을 새 closure에서 유일하게 찾지 못했다: $($model.source)::$($legacy.key)"
            }
            Add-IdentityMapping ([string]$legacy.guid) $found[0].AssetId `
                ([string]$model.source + '::' + [string]$legacy.key)
        }
    }
    if ($oldToNew.Count -ne $oldIds.Count) {
        throw "legacy identity mapping closure가 불완전하다: $($oldToNew.Count) != $($oldIds.Count)"
    }

    $rewrittenReferences = 0
    foreach ($document in @(Get-PersistedDocuments $stageAssets)) {
        $text = Get-Content -LiteralPath $document.FullName -Raw
        $rewritten = $text
        foreach ($mapping in $oldToNew.GetEnumerator()) {
            $count = [regex]::Matches($rewritten, [regex]::Escape($mapping.Key),
                [Text.RegularExpressions.RegexOptions]::IgnoreCase).Count
            if ($count -gt 0) {
                $rewrittenReferences += $count
                $rewritten = [regex]::Replace($rewritten, [regex]::Escape($mapping.Key),
                    $mapping.Value, [Text.RegularExpressions.RegexOptions]::IgnoreCase)
            }
        }
        if ($rewritten -ne $text) {
            [IO.File]::WriteAllText($document.FullName, $rewritten, $utf8)
            $relative = [IO.Path]::GetRelativePath($stageRoot, $document.FullName)
            $replacementFiles.Add([pscustomobject]@{
                Stage = $document.FullName
                Actual = Join-Path $resolvedRoot $relative
                Kind = 'document'
            })
        }
    }
    if ($rewrittenReferences -ne $expectedReferences) {
        throw "rewrite 수가 baseline과 다르다: $rewrittenReferences != $expectedReferences"
    }

    foreach ($model in @($baselineDoc.models)) {
        $stageMeta = (Join-Path $stageRoot ([string]$model.source)) + '.meta'
        $actualMeta = (Join-Path $resolvedRoot ([string]$model.source)) + '.meta'
        $replacementFiles.Add([pscustomobject]@{
            Stage = $stageMeta
            Actual = $actualMeta
            Kind = 'sidecar'
        })
    }

    $verifyArgs = @('-NoProfile', '-File', $Verifier,
        '-Root', $stageRoot, '-Baseline', $Baseline,
        '-GenerationRoot', $stageGenerations, '-RequireFirstGeneration')
    if ($ExpectedSubAssets -gt 0) { $verifyArgs += @('-ExpectedSubAssets', [string]$ExpectedSubAssets) }
    $stageVerification = Invoke-CheckedProcess (Join-Path $PSHOME 'pwsh.exe') `
        $verifyArgs 'staged MBC4 verification'
    Invoke-InjectedFailure 'AfterStage'

    if (-not $Apply) {
        "MBC4 dry-run epoch=$IdentityEpoch models=$($baselineDoc.models.Count) identities=$($oldToNew.Count) rewrittenRefs=$rewrittenReferences"
        $epochOutput
        $stageVerification
        '검증 완료 — -Apply를 지정하지 않아 workspace에는 게시하지 않았다'
        exit 0
    }

    foreach ($entry in $originalHashes.GetEnumerator()) {
        if ((Get-FileHash -LiteralPath $entry.Key -Algorithm SHA256).Hash -ne $entry.Value) {
            throw "staging 중 원본이 바뀌었다: $($entry.Key)"
        }
    }
    if (Test-Path -LiteralPath $header) {
        throw 'staging 중 identity epoch header가 생겼다; 동시 writer를 거부한다.'
    }

    foreach ($file in $replacementFiles) {
        $relative = [IO.Path]::GetRelativePath($resolvedRoot, $file.Actual)
        [void](Copy-IntoTree $file.Actual $backupRoot $relative)
    }

    New-Item -ItemType Directory -Path $generations -Force | Out-Null
    foreach ($modelDirectory in @(Get-ChildItem -LiteralPath $stageGenerations -Directory)) {
        $destination = Join-Path $generations $modelDirectory.Name
        if (Test-Path -LiteralPath $destination) {
            throw "generation UUID directory가 이미 존재한다: $destination"
        }
        [IO.Directory]::Move($modelDirectory.FullName, $destination)
        $publishedGenerations.Add($destination)
    }
    Invoke-InjectedFailure 'AfterGenerations'

    foreach ($file in @($replacementFiles | Where-Object Kind -eq 'sidecar')) {
        Publish-File $file.Stage $file.Actual
    }
    Invoke-InjectedFailure 'AfterSidecars'
    foreach ($file in @($replacementFiles | Where-Object Kind -eq 'document')) {
        Publish-File $file.Stage $file.Actual
    }
    Invoke-InjectedFailure 'BeforeHeader'

    $stageHeader = Join-Path $stageRoot 'Dynamic_CPP\ProjectSetting\AssetIdentity.asset'
    New-Item -ItemType Directory -Path (Split-Path -Parent $header) -Force | Out-Null
    $headerTemporary = $header + '.mbc4-' + [guid]::NewGuid().ToString('N')
    try {
        Copy-Item -LiteralPath $stageHeader -Destination $headerTemporary
        [IO.File]::Move($headerTemporary, $header, $false)
    } finally {
        if (Test-Path -LiteralPath $headerTemporary -PathType Leaf) {
            Remove-Item -LiteralPath $headerTemporary -Force -ErrorAction SilentlyContinue
        }
    }
    $publishedHeader = $true
    Invoke-InjectedFailure 'AfterHeader'

    $actualVerifyArgs = @('-NoProfile', '-File', $Verifier,
        '-Root', $resolvedRoot, '-Baseline', $Baseline,
        '-GenerationRoot', $generations, '-RequireFirstGeneration')
    if ($ExpectedSubAssets -gt 0) { $actualVerifyArgs += @('-ExpectedSubAssets', [string]$ExpectedSubAssets) }
    $actualVerification = Invoke-CheckedProcess (Join-Path $PSHOME 'pwsh.exe') `
        $actualVerifyArgs 'published MBC4 verification'

    "MBC4 applied epoch=$IdentityEpoch models=$($baselineDoc.models.Count) identities=$($oldToNew.Count) rewrittenRefs=$rewrittenReferences generations=$($publishedGenerations.Count)"
    $actualVerification
    '완료 — old GUID mapping은 게시하지 않았고 임시 migration 메모리에서만 사용했다'
    exit 0
}
catch {
    $failure = $_
    if ($Apply) {
        if ($publishedHeader -and (Test-Path -LiteralPath $header -PathType Leaf)) {
            Remove-Item -LiteralPath $header -Force
        }
        foreach ($file in $replacementFiles) {
            $relative = [IO.Path]::GetRelativePath($resolvedRoot, $file.Actual)
            $backup = Join-Path $backupRoot $relative
            if (Test-Path -LiteralPath $backup -PathType Leaf) {
                Publish-File $backup $file.Actual
            }
        }
        foreach ($published in $publishedGenerations) {
            if ((Test-ContainedPath $generations $published) -and
                (Test-Path -LiteralPath $published -PathType Container)) {
                Remove-Item -LiteralPath $published -Recurse -Force
            }
        }
    }
    throw $failure
}
finally {
    if ((Test-ContainedPath $resolvedWork $run) -and
        (Test-Path -LiteralPath $run -PathType Container)) {
        Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
    }
}

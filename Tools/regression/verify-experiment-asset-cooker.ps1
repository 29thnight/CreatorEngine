param(
    [string]$AssetCooker = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Tools\AssetCooker\AssetCooker.exe'),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$assets = (Resolve-Path (Join-Path $root 'Dynamic_CPP\Assets')).Path
$model = (Resolve-Path (Join-Path $assets 'Models\Prim_Cube.glb')).Path
$modelMeta = $model + '.meta'
# D5-b2c-3 이후 재질 manifest entry가 shaderAssetId 의존을 갖는다. Prim_Cube
# 재질은 GBuffer만 참조하므로 GBuffer.shadermeta 하나가 cook 폐포를 닫는다.
$gbufferShaderMeta = Join-Path $assets 'Shaders\DefaultPassShader\GBuffer.shadermeta'
$gbufferMeta = $gbufferShaderMeta + '.meta'
# 재질 cook은 --shadermeta 인자와 별개로 asset root의 잘 알려진 경로에서
# Forward sidecar도 읽고(shader.forward), shadermeta cook은 sourceGuid가
# 가리키는 source 셰이더(.hlsl)와 그 sidecar까지 검증한다. fixture에 이들이
# 없으면 cook이 거부된다.
$forwardMeta = Join-Path $assets 'Shaders\DefaultPassShader\Forward.shadermeta.meta'
$gbufferHlsl = Join-Path $assets 'Shaders\DefaultPassShader\GBuffer.hlsl'
$gbufferHlslMeta = $gbufferHlsl + '.meta'

if (-not (Test-Path -LiteralPath $AssetCooker -PathType Leaf)) {
    "AssetCooker가 없다: $AssetCooker"
    exit 1
}

$sourcePaths = @($model, $modelMeta, $gbufferShaderMeta, $gbufferMeta, $forwardMeta,
    $gbufferHlsl, $gbufferHlslMeta)
$sourceHashes = @{}
foreach ($path in $sourcePaths) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        "producer 입력이 없다: $path"
        exit 1
    }
    $sourceHashes[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}

$modelMetaText = Get-Content -LiteralPath $modelMeta -Raw
$guidMatch = [regex]::Match($modelMetaText,
    '(?m)^guid:\s*([0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})\s*$')
if (-not $guidMatch.Success) {
    'Prim_Cube model sidecar의 canonical UUIDv4를 읽지 못했다.'
    exit 1
}
$modelGuid = $guidMatch.Groups[1].Value
$expectedArtifact = "Derived/Models/$($modelGuid.Substring(0, 2))/$modelGuid.cemc"

$gbufferMetaText = Get-Content -LiteralPath $gbufferMeta -Raw
$gbufferGuidMatch = [regex]::Match($gbufferMetaText,
    '(?m)^guid:\s*([0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})\s*$')
if (-not $gbufferGuidMatch.Success) {
    'GBuffer shadermeta sidecar의 canonical UUIDv4를 읽지 못했다.'
    exit 1
}
$gbufferGuid = $gbufferGuidMatch.Groups[1].Value

$run = Join-Path $Work ("CE_D5AssetCooker_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null

function Invoke-AssetCooker {
    param(
        [string]$AssetsRoot,
        [string]$OutputRoot,
        [string]$ModelPath,
        [string]$ShaderMetaPath
    )

    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $AssetCooker
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in @(
        '--asset-root', $AssetsRoot,
        '--output', $OutputRoot,
        '--model', $ModelPath,
        '--shadermeta', $ShaderMetaPath)) {
        [void]$start.ArgumentList.Add($argument)
    }

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw 'AssetCooker process를 시작하지 못했다.' }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill($true)
        throw "AssetCooker TIMEOUT: $OutputRoot"
    }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [pscustomobject]@{
        ExitCode = $process.ExitCode
        Stdout = $stdout
        Stderr = $stderr
    }
}

function Get-TreeSnapshot {
    param([string]$Path)

    $snapshot = [ordered]@{}
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) { return $snapshot }
    foreach ($file in Get-ChildItem -LiteralPath $Path -Recurse -File | Sort-Object FullName) {
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

    $leftKeys = @($Left.Keys)
    $rightKeys = @($Right.Keys)
    if ($leftKeys.Count -ne $rightKeys.Count) { return $false }
    foreach ($key in $leftKeys) {
        if (-not $Right.Contains($key)) { return $false }
        if ($Left[$key].Bytes -ne $Right[$key].Bytes -or
            $Left[$key].Sha256 -ne $Right[$key].Sha256) { return $false }
    }
    return $true
}

$outputA = Join-Path $run 'output-a'
$outputB = Join-Path $run 'output-b'
$first = Invoke-AssetCooker -AssetsRoot $assets -OutputRoot $outputA -ModelPath $model `
    -ShaderMetaPath $gbufferShaderMeta
$second = Invoke-AssetCooker -AssetsRoot $assets -OutputRoot $outputB -ModelPath $model `
    -ShaderMetaPath $gbufferShaderMeta

$snapshotA = Get-TreeSnapshot -Path $outputA
$snapshotB = Get-TreeSnapshot -Path $outputB
$deterministic = Test-SameSnapshot -Left $snapshotA -Right $snapshotB
$artifactPath = Join-Path $outputA $expectedArtifact.Replace('/', '\')
$manifestPath = Join-Path $outputA 'Derived\asset-manifest.cemf'
$artifactExists = Test-Path -LiteralPath $artifactPath -PathType Leaf
$manifestExists = Test-Path -LiteralPath $manifestPath -PathType Leaf
$artifactBytes = if ($artifactExists) { (Get-Item -LiteralPath $artifactPath).Length } else { 0 }
$manifestBytes = if ($manifestExists) { (Get-Item -LiteralPath $manifestPath).Length } else { 0 }

# manifestEntries=4: model + material + embedded texture + shadermeta.
# files=4: model CEMC + embedded texture + cooked shadermeta + manifest.
$summaryPattern = 'asset-cooker models=1 materials=1 embeddedTextures=1 textureReferences=1 externalTextureRefs=0 embeddedTextureBytes=\d+ textures=0 shaderMetas=1 standaloneMaterials=0 standaloneMaterialBytes=0 scenes=0 prefabs=0 sceneBytes=0 legacyTextureNameRefs=0 unproducedGuidRefs=0 artifactPaths=3 files=4 manifestEntries=4 artifactBytes=(\d+) textureBytes=0 shaderMetaBytes=\d+ manifest=Derived/asset-manifest\.cemf'
$modelPattern = "asset-cooker model=$([regex]::Escape($modelGuid)) artifact=$([regex]::Escape($expectedArtifact))"
$shaderMetaPattern = "asset-cooker shadermeta=$([regex]::Escape($gbufferGuid)) "
$successSummaries = @($first, $second | Where-Object {
    $_.ExitCode -eq 0 -and
    [regex]::IsMatch($_.Stdout, $summaryPattern) -and
    [regex]::IsMatch($_.Stdout, $modelPattern) -and
    [regex]::IsMatch($_.Stdout, $shaderMetaPattern)
}).Count
$unexpectedStderr = @($first, $second | Where-Object {
    -not [string]::IsNullOrWhiteSpace($_.Stderr)
}).Count

# 같은 Assets tree를 서로 다른 물리 경로에 놓아도 cooked bytes는 같아야 한다.
$relocatedAssets = Join-Path $run 'relocated\deeper\Assets'
$relocatedModelDir = Join-Path $relocatedAssets 'Models'
$relocatedShaderDir = Join-Path $relocatedAssets 'Shaders\DefaultPassShader'
New-Item -ItemType Directory -Path $relocatedModelDir -Force | Out-Null
New-Item -ItemType Directory -Path $relocatedShaderDir -Force | Out-Null
$relocatedModel = Join-Path $relocatedModelDir 'Prim_Cube.glb'
$relocatedShaderMeta = Join-Path $relocatedShaderDir 'GBuffer.shadermeta'
Copy-Item -LiteralPath $model -Destination $relocatedModel
Copy-Item -LiteralPath $modelMeta -Destination ($relocatedModel + '.meta')
Copy-Item -LiteralPath $gbufferShaderMeta -Destination $relocatedShaderMeta
Copy-Item -LiteralPath $gbufferMeta -Destination ($relocatedShaderMeta + '.meta')
Copy-Item -LiteralPath $forwardMeta `
    -Destination (Join-Path $relocatedShaderDir 'Forward.shadermeta.meta')
Copy-Item -LiteralPath $gbufferHlsl -Destination (Join-Path $relocatedShaderDir 'GBuffer.hlsl')
Copy-Item -LiteralPath $gbufferHlslMeta `
    -Destination (Join-Path $relocatedShaderDir 'GBuffer.hlsl.meta')
$relocatedOutput = Join-Path $run 'relocated-output'
$relocated = Invoke-AssetCooker -AssetsRoot $relocatedAssets `
    -OutputRoot $relocatedOutput -ModelPath $relocatedModel `
    -ShaderMetaPath $relocatedShaderMeta
$relocatedSnapshot = Get-TreeSnapshot -Path $relocatedOutput
$relocationDeterministic = $relocated.ExitCode -eq 0 -and
    (Test-SameSnapshot -Left $snapshotA -Right $relocatedSnapshot)
$relocationUnexpectedStderr = -not [string]::IsNullOrWhiteSpace($relocated.Stderr)

# Tracked package의 candidate/package-input 깊이에서도 atomic sibling staging이
# Windows 260-character 경계를 넘기지 않아야 한다. 최종 artifact 경로는 짧게
# 유지하고 parent만 실제 실패 당시와 같은 160자로 맞춘다.
$longParentBase = Join-Path $run 'tracked-package-parent-'
$longParent = $longParentBase + ('x' * [Math]::Max(1, 160 - $longParentBase.Length))
New-Item -ItemType Directory -Path $longParent -Force | Out-Null
$longOutput = Join-Path $longParent 'Assets'
$longPathRun = Invoke-AssetCooker -AssetsRoot $assets `
    -OutputRoot $longOutput -ModelPath $model -ShaderMetaPath $gbufferShaderMeta
$longPathSnapshot = Get-TreeSnapshot -Path $longOutput
$longPathDeterministic = $longPathRun.ExitCode -eq 0 -and
    (Test-SameSnapshot -Left $snapshotA -Right $longPathSnapshot)
$longPathUnexpectedStderr =
    -not [string]::IsNullOrWhiteSpace($longPathRun.Stderr)

# 이미 게시된 디렉터리는 덮어쓰지 않고 원래 tree를 그대로 보존해야 한다.
$beforeExistingReject = Get-TreeSnapshot -Path $outputA
$existingReject = Invoke-AssetCooker -AssetsRoot $assets -OutputRoot $outputA -ModelPath $model `
    -ShaderMetaPath $gbufferShaderMeta
$afterExistingReject = Get-TreeSnapshot -Path $outputA
$existingOutputRejected = $existingReject.ExitCode -ne 0 -and
    (Test-SameSnapshot -Left $beforeExistingReject -Right $afterExistingReject)

# Assets 아래로 cook output을 쓰려는 요청은 디렉터리조차 만들지 않는다.
$insideAssetsOutput = Join-Path $assets ('.asset-cooker-reject-' + [guid]::NewGuid().ToString('N'))
$insideAssetsReject = Invoke-AssetCooker -AssetsRoot $assets `
    -OutputRoot $insideAssetsOutput -ModelPath $model -ShaderMetaPath $gbufferShaderMeta
$insideAssetsRejected = $insideAssetsReject.ExitCode -ne 0 -and
    -not (Test-Path -LiteralPath $insideAssetsOutput)

# 손상 sidecar는 외부 fixture에서 태우고, partial output이 남지 않는지 본다.
$fixtureAssets = Join-Path $run 'fixture-assets'
$fixtureModelDir = Join-Path $fixtureAssets 'Models'
$fixtureShaderDir = Join-Path $fixtureAssets 'Shaders\DefaultPassShader'
New-Item -ItemType Directory -Path $fixtureModelDir -Force | Out-Null
New-Item -ItemType Directory -Path $fixtureShaderDir -Force | Out-Null
$fixtureModel = Join-Path $fixtureModelDir 'Prim_Cube.glb'
$fixtureShaderMeta = Join-Path $fixtureShaderDir 'GBuffer.shadermeta'
Copy-Item -LiteralPath $model -Destination $fixtureModel
# shadermeta 입력은 온전하게 두어 이 검사의 유일한 결함이 손상 model sidecar가
# 되도록 한다 — shadermeta 부재가 거부 원인을 가리면 안 된다.
Copy-Item -LiteralPath $gbufferShaderMeta -Destination $fixtureShaderMeta
Copy-Item -LiteralPath $gbufferMeta -Destination ($fixtureShaderMeta + '.meta')
Copy-Item -LiteralPath $forwardMeta -Destination (Join-Path $fixtureShaderDir 'Forward.shadermeta.meta')
Copy-Item -LiteralPath $gbufferHlsl -Destination (Join-Path $fixtureShaderDir 'GBuffer.hlsl')
Copy-Item -LiteralPath $gbufferHlslMeta -Destination (Join-Path $fixtureShaderDir 'GBuffer.hlsl.meta')
$invalidMeta = $modelMetaText -replace
    '(?m)^guid:\s*[0-9a-f-]+\s*$',
    'guid: 68b21a01-958e-14ed-8820-a2b9aa289587'
Set-Content -LiteralPath ($fixtureModel + '.meta') -Value $invalidMeta -Encoding UTF8
$invalidOutput = Join-Path $run 'invalid-output'
$invalidReject = Invoke-AssetCooker -AssetsRoot $fixtureAssets `
    -OutputRoot $invalidOutput -ModelPath $fixtureModel -ShaderMetaPath $fixtureShaderMeta
$invalidSidecarRejected = $invalidReject.ExitCode -ne 0 -and
    -not (Test-Path -LiteralPath $invalidOutput)

$sourceMutations = [Collections.Generic.List[string]]::new()
foreach ($path in $sourceHashes.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $sourceMutations.Add("삭제됨: $path")
    } elseif ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne
        $sourceHashes[$path]) {
        $sourceMutations.Add("변경됨: $path")
    }
}

$partialOutputs = @($insideAssetsOutput, $invalidOutput | Where-Object {
    Test-Path -LiteralPath $_
}).Count

"experiment-asset-cooker output=$run"
"successRuns=$successSummaries deterministic=$deterministic relocationDeterministic=$relocationDeterministic longPathDeterministic=$longPathDeterministic files=$($snapshotA.Count) artifactBytes=$artifactBytes manifestBytes=$manifestBytes existingOutputRejected=$existingOutputRejected insideAssetsRejected=$insideAssetsRejected invalidSidecarRejected=$invalidSidecarRejected partialOutputs=$partialOutputs sourceMutations=$($sourceMutations.Count) unexpectedStderr=$unexpectedStderr relocationUnexpectedStderr=$relocationUnexpectedStderr longPathUnexpectedStderr=$longPathUnexpectedStderr"

$passed = $successSummaries -eq 2 -and
    $deterministic -and
    $relocationDeterministic -and
    $longPathDeterministic -and
    $snapshotA.Count -eq 4 -and
    $artifactExists -and $artifactBytes -gt 0 -and
    $manifestExists -and $manifestBytes -gt 0 -and
    $existingOutputRejected -and
    $insideAssetsRejected -and
    $invalidSidecarRejected -and
    $partialOutputs -eq 0 -and
    $sourceMutations.Count -eq 0 -and
    $unexpectedStderr -eq 0 -and
    -not $relocationUnexpectedStderr -and
    -not $longPathUnexpectedStderr
if (-not $passed) {
    if ($sourceMutations.Count -gt 0) { '원본 변경:'; $sourceMutations }
    if ($first.ExitCode -ne 0) { 'first stderr:'; $first.Stderr }
    if ($second.ExitCode -ne 0) { 'second stderr:'; $second.Stderr }
    if ($relocated.ExitCode -ne 0) { 'relocated stderr:'; $relocated.Stderr }
    if ($longPathRun.ExitCode -ne 0) { 'long-path stderr:'; $longPathRun.Stderr }
    exit 1
}

'전체 통과 — AssetCooker가 실제 model sidecar identity로 결정적 CEMC/CEMF를 만들고 원본·부분 게시를 남기지 않았다'
exit 0

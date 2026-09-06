# PHASE 3.75 MBC5 — runtime ModelAssetGeneration closure/cache gate.
#
# Prim_Cube의 새 UUIDv8 fixture를 정식 MBC3 writer로 generation 1→2 authoring한
# 뒤 제품 C++ loader/cache를 실행한다. 모델/sidecar/record/texture 변조는 게시 전에
# 거부되고, current generation이 유지돼야 한다.
param(
    [string]$AssetCooker = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Tools\AssetCooker\AssetCooker.exe'),
    [string]$Editor = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Editor\CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sourceAssets = (Resolve-Path (Join-Path $root 'Dynamic_CPP\Assets')).Path
$run = Join-Path $Work ('creator-mbc5-generation-' + [guid]::NewGuid().ToString('N'))
$project = Join-Path $run 'Project'
$assets = Join-Path $project 'Assets'
$modelDir = Join-Path $assets 'Models'
$model = Join-Path $modelDir 'Prim_Cube.glb'
$meta = $model + '.meta'
$generationRoot = Join-Path $project 'Library\ModelAssetGenerations'
$header = Join-Path $project 'ProjectSetting\AssetIdentity.asset'
$failures = [System.Collections.Generic.List[string]]::new()

function Add-Failure([string]$Message) { $failures.Add($Message) }

function Invoke-Authoring([string]$Label) {
    $stdout = Join-Path $run ($Label + '.stdout.txt')
    $stderr = Join-Path $run ($Label + '.stderr.txt')
    $process = Start-Process -FilePath $AssetCooker -ArgumentList @(
        '--author-model-asset', '--asset-root', $assets, '--output',
        $generationRoot, '--model', $model) -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Stdout = if (Test-Path $stdout) { [IO.File]::ReadAllText($stdout) } else { '' }
        Stderr = if (Test-Path $stderr) { [IO.File]::ReadAllText($stderr) } else { '' }
    }
}

try {
    foreach ($binary in @($AssetCooker, $Editor)) {
        if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
            throw "실행 파일이 없다: $binary"
        }
    }
    if ($project -match '\s') {
        throw "console fixture path에 공백이 있어 명령 토큰으로 전달할 수 없다: $project"
    }

    New-Item -ItemType Directory -Path $modelDir,
        (Split-Path -Parent $header),
        (Join-Path $assets 'Shaders\DefaultPassShader') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $sourceAssets 'Models\Prim_Cube.glb') `
        -Destination $model
    foreach ($shader in @('GBuffer', 'Forward')) {
        Copy-Item -LiteralPath (Join-Path $sourceAssets (
            'Shaders\DefaultPassShader\' + $shader + '.shadermeta.meta')) `
            -Destination (Join-Path $assets (
                'Shaders\DefaultPassShader\' + $shader + '.shadermeta.meta'))
    }

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
    $headerText = @"
schemaVersion: 1
identityProfile: ce.uuidv8.sha256.v1
identityEpoch: mbc5-runtime-epoch
identityEpochSeed: 1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef
createdAt: 2026-09-02T00:00:00Z
"@
    [IO.File]::WriteAllText($header, $headerText.Replace("`r`n", "`n") + "`n",
        [Text.UTF8Encoding]::new($false))

    $one = Invoke-Authoring 'generation-1'
    $two = Invoke-Authoring 'generation-2'
    if ($one.ExitCode -ne 0 -or -not [string]::IsNullOrWhiteSpace($one.Stderr)) {
        Add-Failure "generation 1 authoring 실패: exit=$($one.ExitCode) $($one.Stderr)"
    }
    if ($two.ExitCode -ne 0 -or -not [string]::IsNullOrWhiteSpace($two.Stderr)) {
        Add-Failure "generation 2 authoring 실패: exit=$($two.ExitCode) $($two.Stderr)"
    }

    $scenario = Join-Path $run 'commands.txt'
    $stdout = Join-Path $run 'editor.stdout.txt'
    $stderr = Join-Path $run 'editor.stderr.txt'
    $runtimeContent = Join-Path $root 'Dynamic_CPP'
    $commands = @("assets.generation $($project.Replace('\', '/'))",
        "assets.generationcorpus $($runtimeContent.Replace('\', '/'))", 'quit')
    [IO.File]::WriteAllText($scenario, ($commands -join "`n") + "`n",
        [Text.UTF8Encoding]::new($false))
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Editor
    $start.Arguments = '--commandlet-script "' + $scenario.Replace('"', '\"') + '"'
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $editorProcess = [Diagnostics.Process]::new()
    $editorProcess.StartInfo = $start
    if (-not $editorProcess.Start()) { throw "CreatorEditor 시작 실패: $Editor" }
    $stdoutTask = $editorProcess.StandardOutput.ReadToEndAsync()
    $stderrTask = $editorProcess.StandardError.ReadToEndAsync()
    if (-not $editorProcess.WaitForExit($TimeoutSeconds * 1000)) {
        $editorProcess.Kill()
        throw "CreatorEditor timeout: $run"
    }
    $editorProcess.WaitForExit()
    $text = $stdoutTask.GetAwaiter().GetResult()
    $editorStderr = $stderrTask.GetAwaiter().GetResult()
    $editorExitCode = $editorProcess.ExitCode
    [IO.File]::WriteAllText($stdout, $text, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($stderr, $editorStderr, [Text.UTF8Encoding]::new($false))
    $cliPass = ([regex]::Matches($text,
        '\[CLI\] assets\.generation PASS')).Count
    $corpusPass = ([regex]::Matches($text,
        '\[CLI\] assets\.generationcorpus PASS')).Count
    $summary = [regex]::Match($text,
        'assertions total=(\d+) passed=(\d+) failed=(\d+)')
    $generation = [regex]::Match($text,
        'generation model=([0-9a-f-]{36}) meshes=(\d+) materials=(\d+) textures=(\d+) descriptors=(\d+) tamper=(\d+) replacements=(\d+) retires=(\d+)')
    $corpus = [regex]::Match($text,
        'corpus models=(\d+) loaded=(\d+) unique=(\d+) meshes=(\d+) materials=(\d+) textures=(\d+) skeletons=(\d+) animations=(\d+) descriptors=(\d+)')
    $assertions = if ($summary.Success) { [int]$summary.Groups[1].Value } else { 0 }
    $assertFailed = if ($summary.Success) { [int]$summary.Groups[3].Value } else { -1 }
    if ($cliPass -ne 1) { Add-Failure "assets.generation 통과가 1회가 아니다: $cliPass" }
    if ($corpusPass -ne 1) { Add-Failure "assets.generationcorpus 통과가 1회가 아니다: $corpusPass" }
    if (-not $summary.Success -or $assertFailed -ne 0 -or $assertions -lt 30) {
        Add-Failure "selftest 단정 요약 불충분: $($summary.Value)"
    }
    if (-not $generation.Success -or [int]$generation.Groups[2].Value -lt 1 -or
        [int]$generation.Groups[3].Value -lt 1 -or
        [int]$generation.Groups[4].Value -lt 1 -or
        [int]$generation.Groups[5].Value -lt 3 -or
        [int]$generation.Groups[6].Value -ne 4 -or
        [int]$generation.Groups[7].Value -ne 1 -or
        [int]$generation.Groups[8].Value -ne 2) {
        Add-Failure "generation closure/cache 요약이 기대와 다르다: $($generation.Value)"
    }
    if (-not $corpus.Success -or [int]$corpus.Groups[1].Value -lt 14 -or
        $corpus.Groups[1].Value -ne $corpus.Groups[2].Value -or
        $corpus.Groups[2].Value -ne $corpus.Groups[3].Value -or
        [int]$corpus.Groups[4].Value -lt 130 -or
        [int]$corpus.Groups[5].Value -lt 50 -or
        [int]$corpus.Groups[6].Value -lt 90 -or
        [int]$corpus.Groups[7].Value -lt 3 -or
        [int]$corpus.Groups[8].Value -lt 28) {
        Add-Failure "실 model corpus generation 요약이 기대와 다르다: $($corpus.Value)"
    }
    if ($editorExitCode -ne 0) { Add-Failure "CreatorEditor 종료 코드 $editorExitCode" }

    # 구조 게이트: 새 cache는 legacy 이름/mesh hash나 try_emplace로 신원을 만들지 않는다.
    $generationSource = [IO.File]::ReadAllText((Join-Path $root `
        'Engine\RenderEngine\Assets\ModelAssetGeneration.cpp'))
    $dataSystemSource = [IO.File]::ReadAllText((Join-Path $root `
        'Engine\RenderEngine\DataSystem.cpp'))
    if ($generationSource -match 'm_hashingMesh|try_emplace|BuildLegacyModelFromExperiment') {
        Add-Failure 'ModelAssetGeneration cache가 legacy 신원/역브리지에 의존한다.'
    }
    if ($dataSystemSource -notmatch 'm_modelAssetGenerations\.Publish' -or
        $dataSystemSource -notmatch 'm_modelAssetGenerations\.Retire' -or
        $dataSystemSource -notmatch 'schemaVersion.*kModelSidecarSchemaVersion') {
        Add-Failure 'DataSystem startup catalog/publish/retire 배선이 빠졌다.'
    }

    "model-asset-generation exit=$editorExitCode output=$run"
    "cliPass=$cliPass corpusPass=$corpusPass assertions=$assertions assertFailed=$assertFailed $($generation.Value)"
    $corpus.Value
    if ($failures.Count -gt 0) {
        '실패:'
        $failures | ForEach-Object { "  $_" }
        if ($text) { '--- editor stdout tail ---'; ($text -split "`n") | Select-Object -Last 80 }
        exit 1
    }
    '전체 통과 — generation 1→2 원자 교체, 변조 4종 게시 전 거부, MBC4 corpus 14 cold-load closure'
    exit 0
}
finally {
    if (Test-Path -LiteralPath $run) {
        Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
    }
}

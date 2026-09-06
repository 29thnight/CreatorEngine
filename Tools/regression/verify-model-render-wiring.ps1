# PHASE 3.75 MBC6 — ModelAssetGeneration -> RHI/GBuffer/Forward/Shadow gate.
param(
    [string]$Editor = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Editor\CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$runtimeContent = Join-Path $root 'Dynamic_CPP'
$run = Join-Path $Work ('creator-mbc6-render-' + [guid]::NewGuid().ToString('N'))
$failures = [System.Collections.Generic.List[string]]::new()

function Add-Failure([string]$Message) { $failures.Add($Message) }

try {
    if (-not (Test-Path -LiteralPath $Editor -PathType Leaf)) {
        throw "CreatorEditor 실행 파일이 없다: $Editor"
    }
    New-Item -ItemType Directory -Path $run -Force | Out-Null
    $scenario = Join-Path $run 'commands.txt'
    $commands = @(
        'assets.modelrender',
        "assets.generationcorpus $($runtimeContent.Replace('\', '/'))",
        'dx12.gbuffer',
        'dx12.skinning',
        'vk.shadow',
        'vk.gbuffer',
        'vk.forward',
        'quit')
    [IO.File]::WriteAllText($scenario, ($commands -join "`n") + "`n",
        [Text.UTF8Encoding]::new($false))

    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Editor
    $start.Arguments = '--commandlet-script "' + $scenario.Replace('"', '\"') + '"'
    $resultPath = Join-Path $run 'results.jsonl'
    $start.Arguments += ' --result-file "' + $resultPath + '"'
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    # 에디터 stdout은 UTF-8이다. 호출 셸 코드 페이지(cp949)로 해독하면 "통과" 토큰이
    # 깨져 5개 pass가 전부 "정확히 1회가 아니다"로 붉는다(MBC9 실측).
    $start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw 'CreatorEditor 시작 실패' }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        throw "CreatorEditor timeout: $run"
    }
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $run 'stdout.txt'), $stdout,
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $run 'stderr.txt'), $stderr,
        [Text.UTF8Encoding]::new($false))

    $results = @(Read-CommandResults $resultPath)
    Get-SucceededCommand $results 'assets.modelrender' | Out-Null
    Get-SucceededCommand $results 'assets.generationcorpus' | Out-Null
    $su = [regex]::Match($stdout,
        'su mask=(\d+) stride=(\d+) boneIndices=(\d+) boneWeights=(\d+) rhiView=(\d+)')
    if (-not $su.Success -or [int]$su.Groups[1].Value -ne 247 -or
        [int]$su.Groups[2].Value -ne 84 -or [int]$su.Groups[3].Value -ne 64 -or
        [int]$su.Groups[4].Value -ne 68 -or [int]$su.Groups[5].Value -ne 1) {
        Add-Failure "SU full-mask closure가 다르다: $($su.Value)"
    }
    if ($process.ExitCode -ne 0) {
        Add-Failure "CreatorEditor 종료 코드 $($process.ExitCode)"
    }
    foreach ($pass in @('dx12.gbuffer', 'dx12.skinning', 'vk.shadow',
        'vk.gbuffer', 'vk.forward')) {
        Get-SucceededCommand $results $pass | Out-Null
    }
    if (([regex]::Matches($stdout,
        'DX12/Vulkan ModelAssetGeneration direct upload 1/1')).Count -ne 1) {
        Add-Failure 'GBuffer가 양 backend typed model generation upload를 증명하지 않았다.'
    }
    if ($stdout -match 'Vulkan validation [1-9][0-9]*건') {
        Add-Failure 'Vulkan Shadow/GBuffer/Forward validation 메시지가 0건이 아니다.'
    }
    if (-not [string]::IsNullOrWhiteSpace($stderr)) {
        Add-Failure 'MBC6 runtime pass가 stderr를 남겼다.'
    }

    $layout = [IO.File]::ReadAllText((Join-Path $root `
        'Engine\RenderEngine\Assets\ModelVertexLayout.h'))
    if ($layout -notmatch 'namespace assets' -or
        $layout -notmatch 'kCoreColorSkinVertexAttributes' -or
        $layout -notmatch 'StrideOf\(kCoreColorSkinVertexAttributes\) == 84' -or
        $layout -notmatch 'BoneIndices\) == 64' -or
        $layout -notmatch 'BoneWeights\) == 68') {
        Add-Failure 'Assets model vertex 기술표의 SU compile-time 계약이 빠졌다.'
    }

    $rhi = [IO.File]::ReadAllText((Join-Path $root `
        'Engine\RenderEngine\RHI\IRenderDeviceServices.h'))
    if ($rhi -notmatch 'RHIModelMeshView' -or
        $rhi -notmatch 'BuildRHIModelMeshView' -or
        $rhi -notmatch 'GetOrUploadModel') {
        Add-Failure 'typed generation RHI upload 진입점이 빠졌다.'
    }
    foreach ($backend in @(
        'Engine\RenderEngine\RHI\DX12\DX12MeshCache.h',
        'Engine\RenderEngine\RHI\Vulkan\VulkanRenderServices.cpp')) {
        $text = [IO.File]::ReadAllText((Join-Path $root $backend))
        if ($text -notmatch 'map<assets::ModelMeshHandle' -or
            $text -notmatch 'GetOrUploadModel') {
            Add-Failure "$backend 가 정확한 model generation key를 쓰지 않는다."
        }
    }
    foreach ($pass in @('EnhancedGBufferPass.cpp', 'EnhancedForwardPass.cpp',
        'EnhancedShadowPass.cpp')) {
        $text = [IO.File]::ReadAllText((Join-Path $root `
            ('Engine\RenderEngine\Render\Passes\Geometry\' + $pass)))
        if ($text -notmatch 'GetOrUploadModel' -or
            $text -notmatch 'ModelVertexInput') {
            Add-Failure "$pass 직접 generation 소비 또는 공통 mask 유도가 빠졌다."
        }
    }
    $forward = [IO.File]::ReadAllText((Join-Path $root `
        'Engine\RenderEngine\Render\Passes\Geometry\EnhancedForwardPass.cpp'))
    if ($forward -notmatch 'RHILayout::Srv\(12, RHIShaderVisibility::Vertex\)' -or
        $forward -notmatch 'm_bonePalettes' -or
        $forward -notmatch 'boneOffset') {
        Add-Failure 'Forward model skin palette 직접 소비가 빠졌다.'
    }
    $shadow = [IO.File]::ReadAllText((Join-Path $root `
        'Engine\RenderEngine\Render\Passes\Geometry\EnhancedShadowPass.cpp'))
    if ($shadow -notmatch 'mask, consumedMask' -or
        $shadow -notmatch 'VertexAttribute::Position') {
        Add-Failure 'Shadow pass-specific input mask 유도가 빠졌다.'
    }
    foreach ($shader in @('GBuffer.slang', 'ForwardShade.slang', 'Shadow.slang')) {
        $text = [IO.File]::ReadAllText((Join-Path $root `
            ('Dynamic_CPP\Assets\Shaders\DefaultPassShader\' + $shader)))
        $hasModelContract = if ($shader -eq 'Shadow.slang') {
            $text -match 'MODEL_VERTEX_SKINNING'
        } else {
            $text -match 'MODEL_VERTEX_LAYOUT'
        }
        if (-not $hasModelContract -or $text -match 'EXPERIMENT_') {
            Add-Failure "$shader 가 model vertex 축으로 완전히 전환되지 않았다."
        }
    }

    "model-render-wiring exit=$($process.ExitCode) output=$run"
    $su.Value
    'runtime dx12.gbuffer/dx12.skinning/vk.shadow/vk.gbuffer/vk.forward pass, typed upload 1/1, Vulkan validation 0'
    if ($failures.Count -gt 0) {
        '실패:'
        $failures | ForEach-Object { "  $_" }
        if ($stdout) { '--- editor stdout tail ---'; ($stdout -split "`n") | Select-Object -Last 100 }
        if ($stderr) { '--- editor stderr ---'; $stderr }
        exit 1
    }
    '전체 통과 — typed generation upload, 8 mask PSO, SU 84B/64/68, DX12/Vulkan pass closure'
    exit 0
}
finally {
    if (Test-Path -LiteralPath $run) {
        $cleanupRoot = [IO.Path]::GetFullPath($Work).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
        $cleanupPath = (Resolve-Path -LiteralPath $run).Path
        if (-not $cleanupPath.StartsWith($cleanupRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Regression cleanup escaped its work directory: $cleanupPath"
        }
        Remove-Item -LiteralPath $cleanupPath -Recurse -Force -ErrorAction SilentlyContinue
    }
}

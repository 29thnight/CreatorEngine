[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateRange(1,10)][int]$Repeats = 3,
    [string]$Model = '',
    [string]$Work = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $Work) { $Work = Join-Path $repo "Build/Obj/Phase13S1/$Configuration" }
if (-not $Model) { $Model = Join-Path $repo 'Dynamic_CPP/Assets/Models/CreatorRobot.glb' }
$Work = [IO.Path]::GetFullPath($Work)
$Model = [IO.Path]::GetFullPath($Model)
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the existing Editor first' }
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$rows = @()
for ($repeat = 1; $repeat -le $Repeats; ++$repeat) {
    $run = Join-Path $Work "run-$repeat"
    New-Item -ItemType Directory -Force -Path $run | Out-Null
    $scenario = Join-Path $run 'scenario.txt'
    $result = Join-Path $run 'results.jsonl'
    @('scene.new AnimationBaseline', 'play', 'wait 2', 'play.pause',
        ('animation.baseline.probe "' + $Model + '" 10'),
        ('animation.baseline.probe "' + $Model + '" 50'),
        ('animation.baseline.probe "' + $Model + '" 100'), 'play') |
        Set-Content -LiteralPath $scenario -Encoding utf8
    if (Test-Path -LiteralPath $result) { Remove-Item -LiteralPath $result }
    $proc = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden `
        -ArgumentList @('--commandlet-script', ('"' + $scenario + '"'), '--result-file', ('"' + $result + '"')) `
        -RedirectStandardOutput (Join-Path $run 'editor.out') -RedirectStandardError (Join-Path $run 'editor.err') -PassThru
    try {
        $deadline = (Get-Date).AddSeconds(300)
        while (-not $proc.WaitForExit(1000)) { if ((Get-Date) -ge $deadline) { throw 'Animation baseline timed out' } }
        $proc.Refresh()
        $results = @(Get-Content -LiteralPath $result | ConvertFrom-Json)
        if ($proc.ExitCode -ne 0 -or $results.Count -ne 8 -or @($results | Where-Object status -ne succeeded).Count) {
            $results | Where-Object status -ne succeeded | ConvertTo-Json -Depth 5 | Write-Output
            throw "Baseline failed: $run (exit=$($proc.ExitCode))"
        }
        foreach ($probe in $results[4..6]) {
            $data = $probe.data
            if (-not $data.passed -or $data.frames.Count -ne 120 -or $data.bones -le 0 -or $data.workers -le 0) { throw 'Incomplete baseline' }
            foreach ($frame in $data.frames) {
                $chunks = [Math]::Min($data.actors, [Math]::Max(1, $data.workers) * 2)
                if ($frame.jobs -ne $chunks -or $frame.updatePassJobs -ne $chunks -or $frame.executePassJobs -ne $chunks -or $frame.evaluatedAnimators -ne $data.actors -or $frame.paletteCopies -ne $data.actors -or $frame.paletteBytes -ne $data.actors * $data.bones * 64 -or $frame.paletteArenaAllocations -ne 0 -or $frame.poseStorageGrowths -ne 0 -or $frame.localWrites -le 0 -or $frame.validBones -ne $data.sceneBones) { throw 'Empty measured path' }
            }
            $row = [ordered]@{ repeat=$repeat; actors=$data.actors; bones=$data.bones; meshes=$data.skinnedMeshes; workers=$data.workers; frames=120; paletteBytes=$data.frames[0].paletteBytes }
            foreach ($metric in @('prepareUs','submitUs','waitUs','workerSumUs','workerSpanUs','updatePassWaitUs','executePassWaitUs','updatePassWorkerSumUs','executePassWorkerSumUs','publishUs','socketUs','updateUs','syncUs','renderCommitUs','paletteUs','cpuFrameUs')) {
                $values = @(foreach ($frame in $data.frames) {
                    if ($metric -eq 'cpuFrameUs') { $frame.updateUs + $frame.syncUs + $frame.renderCommitUs }
                    else { $frame.$metric }
                }) | Sort-Object
                if (@($values | Where-Object { [double]::IsNaN($_) -or [double]::IsInfinity($_) -or $_ -lt 0 }).Count) { throw "Invalid $metric" }
                $row[$metric] = @{ mean=($values | Measure-Object -Average).Average; p50=$values[59]; p95=$values[113] }
            }
            $rows += [pscustomobject]$row
            Write-Output ("run={0} actors={1} cpu-p50={2:N3}ms worker-sum-p50={3:N3}ms wait-p50={4:N3}ms palette-p50={5:N3}ms" -f $repeat,$data.actors,($row.cpuFrameUs.p50/1000),($row.workerSumUs.p50/1000),($row.waitUs.p50/1000),($row.paletteUs.p50/1000))
        }
    } finally { if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit() } }
}
$sourceHashes = [ordered]@{}
foreach ($relative in @('Engine/SceneRuntime/AnimationScheduler.cpp', 'Engine/SceneRuntime/AnimationScheduler.h', 'Engine/SceneRuntime/AnimTaskList.h',
    'Engine/SceneRuntime/Scene.cpp', 'Engine/SceneRuntime/Scene.h',
    'Engine/SceneRuntime/BoneComponent.h', 'Engine/SceneRuntime/Transform.cpp',
    'Engine/SceneRuntime/AnimatorSystem.h', 'Engine/SceneRuntime/Animator.h',
    'Engine/SceneRuntime/Animator.cpp', 'Engine/SceneRuntime/ProxyCommand.cpp',
    'Engine/RenderEngine/Assets/ModelAssetGeneration.h', 'Engine/RenderEngine/Assets/ModelAssetGeneration.cpp',
    'Engine/RenderEngine/Assets/ModelAnimationSampler.cpp', 'Engine/RenderEngine/Assets/ModelAnimationSampler.h',
    'Engine/RenderEngine/LocalPose.h', 'Engine/RenderEngine/ClipSamplingCursor.h',
    'Engine/RenderEngine/AnimationPaletteArena.h', 'Engine/RenderEngine/ProxyCommandQueue.h',
    'Engine/RenderEngine/Render/Graph/PackedBoneMatrix.h',
    'Engine/RenderEngine/Render/Graph/EnhancedRenderPass.h',
    'Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PackedBones.slang',
    'Engine/RenderEngine/ProxyCommand.h', 'Engine/RenderEngine/PrimitiveRenderProxy.h',
    'Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp',
    'Editor/RenderTests/Animation/AnimationBaselineProbe.cpp')) {
    $sourceHashes[$relative] = (Get-FileHash -LiteralPath (Join-Path $repo $relative)).Hash
}
$runtime = Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll'
$runtimeHash = if (Test-Path -LiteralPath $runtime) { (Get-FileHash -LiteralPath $runtime).Hash } else { $null }
@{ configuration=$Configuration; model=$Model; modelSha256=(Get-FileHash -LiteralPath $Model).Hash; executableSha256=(Get-FileHash -LiteralPath $exe).Hash; runtimeSha256=$runtimeHash; sourceHashes=$sourceHashes; head=(git -C $repo rev-parse HEAD); cpu=(Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name); rows=$rows; clock='MSVC steady_clock / QPC'; unit='microseconds'; measured='CPU pose update + Scene sync + proxy commit; palette allocation/copy is a subset of commit; worker time overlaps wait; excludes GPU and presentation' } |
    ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $Work 'baseline.json') -Encoding utf8

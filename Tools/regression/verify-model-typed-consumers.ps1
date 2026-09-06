# PHASE 3.75 MBC8/MBC9 — Animator / Foliage / Editor 창구의 typed generation 직접 소비.
#
# ── 이 게이트가 재는 것 ──
#
# MBC9로 legacy Model·Assimp·역브리지·experiment 병행 핸들·A/B 스위치가 전부 은퇴했다.
# 남은 계약은 하나다 — typed generation이 **유일한** 제품 정본이라 재생·본 해석·
# 마스크·클립 열거·Foliage 뷰가 전량 generation 축이어야 하고, 재생 골든
# (poseDigest=8042DC1C — experiment 샘플러 시절 값)은 typed 샘플러가 그대로 내야 한다.
#
# ── 판정 항목 ──
#
#   1  배치·씬 로드가 typed다            — assets.modeldiag(읽기 전용 스냅샷, MBC10)
#      instantiateGeneration=1(Gunner) · meshResolveGeneration ≥ 10 · 실패 0 · 제품
#      stdout 토큰([mesh.resolve]/[model.instantiate]/[anim.tick]) 0
#   2  재생 틱이 typed다                  — tickGeneration ≥ 1, tickNone 0; animator.status는
#      publish를 다시 부르지 않고 제품 barrier 스냅샷을 읽는다(source=product)
#   3  ★ typed 샘플러 골든              — animtick pass poseDigest=8042DC1C path=generation
#   4  본 해석·신원이 typed다           — boneresolve pass bones=N generation=N
#      unresolved=0 serialGeneration=N roundtrip=0(name→index→name 독립 유도)
#   5  마스크 트리가 typed 원자료와 맞다  — animmask pass viaGeneration=1 structure=ok
#   6  에디터 클립 창구가 typed다         — editorsurface pass clipGeneration=animators guardMismatch=0
#   7  Foliage가 typed 뷰로 선다          — foliage verify pass generationTypes=types
#      generationViews=draws authoredMat=types(재질 저작 정본도 같은 closure)
#   8  정적 — Animator/AnimationJob/FoliageType/샘플러/cook 참조 스캔 typed 심볼,
#      legacy 타입(Skeleton.h·Model.h·AnimatorData.h·Assimp include) 0건, stderr 공백
#
# ★ 합성 seed(foliage)가 Assets\Foliage에 gate_foliage.foliage를 게시한다 — finally가 걷는다.
param(
    [string]$Editor = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Editor\CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300,
    [string]$ModelPath = (Join-Path $PSScriptRoot '../../Dynamic_CPP/Assets/Models/Gunner_F_Mythic.glb'),
    [string]$ScenePath = (Join-Path $PSScriptRoot '../../Dynamic_CPP/Assets/Scenes/FT_Primitives.creator'),
    [string]$ExpectedPoseDigest = '8042DC1C'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$run = Join-Path $Work ('creator-mbc9-typed-' + [guid]::NewGuid().ToString('N'))
$failures = [System.Collections.Generic.List[string]]::new()
function Add-Failure([string]$Message) { $failures.Add($Message) }
$foliageDir = Join-Path $root 'Dynamic_CPP\Assets\Foliage'

try {
    if (-not (Test-Path -LiteralPath $Editor -PathType Leaf)) { throw "CreatorEditor 실행 파일이 없다: $Editor" }
    $modelPathFull = [IO.Path]::GetFullPath($ModelPath)
    $modelName = [IO.Path]::GetFileNameWithoutExtension($modelPathFull)
    if (($PSBoundParameters.ContainsKey('ModelPath') -or $PSBoundParameters.ContainsKey('ScenePath')) -and -not $PSBoundParameters.ContainsKey('ExpectedPoseDigest')) { throw 'A custom model or scene requires its independent ExpectedPoseDigest.' }
    if ($ExpectedPoseDigest -notmatch '^[0-9A-Fa-f]{8}$') { throw 'ExpectedPoseDigest must be eight hexadecimal digits.' }
    if (-not (Test-Path -LiteralPath $modelPathFull -PathType Leaf)) { throw "스킨 모델이 없다: $modelPathFull" }
    $baseScene = [IO.Path]::GetFullPath($ScenePath)
    if (-not (Test-Path -LiteralPath $baseScene -PathType Leaf)) { throw "기준 씬이 없다: $baseScene" }
    New-Item -ItemType Directory -Path $run -Force | Out-Null
    if ($run -match '\s') { throw "console 인자로 넘길 작업 경로에 공백이 있다: $run" }
    $savedScene = (Join-Path $run 'mbc9_typed.creator').Replace('\', '/')

    $commands = @(
        "scene.switch `"$($baseScene.Replace('\', '/'))`"",
        'wait 60',
        "model.loadcached `"$($modelPathFull.Replace('\', '/'))`"",
        'wait 60',
        "model.place `"$modelName`"",
        'wait 60',
        "object.transform `"$modelName`" 1.5 0 0 0 0 0 0.3 0.3 0.3",
        'wait 10',
        "experiment.foliage seed `"$($foliageDir.Replace('\', '/'))`" `"$($modelPathFull.Replace('\', '/'))`"",
        'wait 30',
        "scene.save $savedScene",
        'wait 30',
        "scene.switch $savedScene",
        'wait 120',
        'experiment.animtick',
        'experiment.boneresolve',
        'experiment.animmask',
        'experiment.editorsurface',
        'experiment.foliage verify',
        'animator.status',
        'assets.modeldiag',
        'quit')
    $scenario = Join-Path $run 'commands.txt'
    [IO.File]::WriteAllText($scenario, ($commands -join "`n") + "`n", [Text.UTF8Encoding]::new($false))

    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Editor
    $start.Arguments = '--commandlet-script "' + $scenario.Replace('"', '\"') + '"'
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw "CreatorEditor 시작 실패: $Editor" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill(); throw "CreatorEditor timeout: $run" }
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $run 'stdout.txt'), $stdout, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $run 'stderr.txt'), $stderr, [Text.UTF8Encoding]::new($false))
    if ($process.ExitCode -ne 0) { Add-Failure "종료 코드 $($process.ExitCode)" }
    if (-not [string]::IsNullOrWhiteSpace($stderr)) { Add-Failure 'stderr가 비어 있지 않다.' }

    # 1·2 — MBC10: 제품 경로는 stdout 토큰을 찍지 않는다. 읽기 전용 스냅샷
    #        (assets.modeldiag)으로 배치 1회·메시 해석 ≥ 10(FT 8 + 저장 씬 재로드 10)·
    #        해석 실패 0·틱 generation ≥ 1·none 0을 잰다.
    $diag = [regex]::Match($stdout, '\[CLI\] assets\.modeldiag meshResolveGeneration=(\d+) meshResolveFailed=(\d+) instantiateGeneration=(\d+) instantiateRejected=(\d+) tickGeneration=(\d+) tickNone=(\d+) lastInstantiated=(\S+)')
    if (-not $diag.Success) { Add-Failure '1 assets.modeldiag 스냅샷이 없다.' }
    else {
        if ([int]$diag.Groups[3].Value -ne 1 -or $diag.Groups[7].Value -ne $modelName -or [int]$diag.Groups[4].Value -ne 0) {
            Add-Failure "1 배치가 typed generation 1회가 아니다: $($diag.Value)"
        }
        if ([int]$diag.Groups[1].Value -lt 10 -or [int]$diag.Groups[2].Value -ne 0) {
            Add-Failure "1 메시 해석 generation $($diag.Groups[1].Value) · 실패 $($diag.Groups[2].Value) — typed 전량이 아니다."
        }
        if ([int]$diag.Groups[5].Value -lt 1 -or [int]$diag.Groups[6].Value -ne 0) {
            Add-Failure "2 재생 틱 generation $($diag.Groups[5].Value) · none $($diag.Groups[6].Value) — typed 틱이 아니다."
        }
    }
    # 1c — 무조건 stdout 토큰이 제품 경로에 되살아나지 않았다(§5.2).
    if ($stdout -match '\[(mesh\.resolve|model\.instantiate|anim\.tick|material\.finalize)\]') {
        Add-Failure '1c 제품 경로가 무조건 진단 토큰을 다시 찍는다(MBC10 read-only 계약 위반).'
    }
    # 3
    if ($stdout -notmatch ('\[CLI\] experiment\.animtick pass animators=[1-9]\d* clips=[1-9]\d* samples=[1-9]\d* failedEval=0 poseDigest=' + [regex]::Escape($ExpectedPoseDigest) + ' path=generation')) {
        $line = [regex]::Match($stdout, '\[CLI\] experiment\.animtick [^\r\n]*').Value
        Add-Failure "3 typed 샘플러 골든 불일치 또는 typed 경로가 아니다: $line"
    }
    # 4
    if ($stdout -notmatch '\[CLI\] experiment\.boneresolve pass bones=(\d+) generation=\1 unresolved=0 serialGeneration=\1 roundtrip=0') {
        $line = [regex]::Match($stdout, '\[CLI\] experiment\.boneresolve [^\r\n]*').Value
        Add-Failure "4 본 해석·신원이 typed 전량이 아니다: $line"
    }
    # 5
    if ($stdout -notmatch '\[CLI\] experiment\.animmask pass masks=[1-9]\d* viaGeneration=1 structure=ok') {
        $line = [regex]::Match($stdout, '\[CLI\] experiment\.animmask [^\r\n]*').Value
        Add-Failure "5 마스크 트리가 typed 원자료와 맞지 않거나 typed 경로가 아니다: $line"
    }
    # 6
    if ($stdout -notmatch '\[CLI\] experiment\.editorsurface pass animators=([1-9]\d*) clipGeneration=\1 clips=[1-9]\d* countMismatch=0 nameMismatch=0 frameMismatch=0 renderers=[1-9]\d* meshPresent=[1-9]\d* guardMismatch=0') {
        $line = [regex]::Match($stdout, '\[CLI\] experiment\.editorsurface [^\r\n]*').Value
        Add-Failure "6 에디터 클립 창구가 typed 전량이 아니다: $line"
    }
    # 7
    $foliage = [regex]::Match($stdout, '\[CLI\] experiment\.foliage verify pass types=(\d+) draws=(\d+) authoredMat=(\d+) authoredMatDraws=(\d+) generationTypes=(\d+) generationViews=(\d+)')
    if (-not $foliage.Success -or [int]$foliage.Groups[1].Value -lt 1 -or
        $foliage.Groups[1].Value -ne $foliage.Groups[5].Value -or
        $foliage.Groups[1].Value -ne $foliage.Groups[3].Value -or
        $foliage.Groups[2].Value -ne $foliage.Groups[6].Value -or
        $foliage.Groups[2].Value -ne $foliage.Groups[4].Value -or [int]$foliage.Groups[2].Value -lt 1) {
        $line = [regex]::Match($stdout, '\[CLI\] experiment\.foliage verify [^\r\n]*').Value
        Add-Failure "7 Foliage typed 뷰가 전량이 아니다: $line"
    }
    if ($stdout -notmatch '\[CLI\] animator\.status \S+ path=generation') {
        Add-Failure '2b animator.status가 typed 경로를 보고하지 않는다.'
    }
    # 2c — animator.status는 읽기 전용이다: 제품 barrier가 남긴 publish 메트릭을 읽는다(source=product).
    if ($stdout -notmatch '\[CLI\] animator\.status publish \S+ source=product ') {
        Add-Failure '2c animator.status가 제품 publish 스냅샷을 읽지 못했다(source=product 없음).'
    }

    # 8 정적
    $animator = [IO.File]::ReadAllText((Join-Path $root 'Engine\SceneRuntime\Animator.h'))
    if ($animator -notmatch 'm_modelGeneration' -or $animator -notmatch 'AnimatorDataPath') {
        Add-Failure '8 Animator에 typed generation 정본이 없다.'
    }
    $job = [IO.File]::ReadAllText((Join-Path $root 'Engine\SceneRuntime\AnimationJob.cpp'))
    if ($job -notmatch 'GenerationPoseSource' -or $job -notmatch 'TickGeneration' -or $job -notmatch 'assets::animation::SampleLocal') {
        Add-Failure '8 AnimationJob이 typed 뷰로 틱하지 않는다.'
    }
    if (-not (Test-Path (Join-Path $root 'Engine\RenderEngine\Assets\ModelAnimationSampler.cpp'))) {
        Add-Failure '8 typed 샘플러(Assets/ModelAnimationSampler)가 없다.'
    }
    $foliageType = [IO.File]::ReadAllText((Join-Path $root 'Engine\RenderEngine\Interfaces\FoliageType.h'))
    if ($foliageType -notmatch 'm_modelGeneration') { Add-Failure '8 FoliageType에 typed generation이 없다.' }
    $sceneCook = [IO.File]::ReadAllText((Join-Path $root 'Engine\RenderEngine\Experiment\Cooked\SceneCookProducer.cpp'))
    $materialCook = [IO.File]::ReadAllText((Join-Path $root 'Engine\RenderEngine\Experiment\Cooked\MaterialCookProducer.cpp'))
    if ($sceneCook -notmatch 'TryParseCanonicalUuidV8' -or $materialCook -notmatch 'TryParseCanonicalUuidV8') {
        Add-Failure '8 cook 참조 스캔이 UUIDv8 모델/subasset 참조를 받지 않는다.'
    }
    # MBC9 — legacy 타입·Assimp가 제품 트리에 없다(파일 부재 + include 0건).
    foreach ($dead in @('Engine\RenderEngine\Model.h', 'Engine\RenderEngine\ModelLoader.h',
        'Engine\RenderEngine\Skeleton.h', 'Engine\RenderEngine\AnimatorData.h',
        'Engine\RenderEngine\Animation.h', 'Engine\RenderEngine\ExperimentModelMigration.cpp',
        'Engine\SceneRuntime\ModelSceneBridge.cpp')) {
        if (Test-Path (Join-Path $root $dead)) { Add-Failure "8 legacy 파일이 남아 있다: $dead" }
    }
    $assimpIncludes = 0
    foreach ($tree in @('Engine', 'Editor', 'Tools\AssetCooker')) {
        Get-ChildItem -Path (Join-Path $root $tree) -Recurse -File -Include '*.h', '*.hpp', '*.cpp' -ErrorAction SilentlyContinue |
            ForEach-Object {
                if ((Get-Content -LiteralPath $_.FullName -Raw) -match '#\s*include\s*[<"]assimp/') { $assimpIncludes++ }
            }
    }
    if ($assimpIncludes -ne 0) { Add-Failure "8 Assimp include가 남아 있다: $assimpIncludes 파일" }
    if ((Get-Content -LiteralPath (Join-Path $root 'vcpkg.json') -Raw) -match '"name"\s*:\s*"assimp"') {
        Add-Failure '8 vcpkg.json에 assimp port가 남아 있다.'
    }
}
catch {
    Add-Failure "예외: $($_.Exception.Message)"
}
finally {
    Get-ChildItem -LiteralPath $foliageDir -Filter 'gate_foliage.*' -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
}

$summary = "model-typed-consumers failures=$($failures.Count) run=$run"
if ($failures.Count -gt 0) {
    $summary
    $failures | ForEach-Object { "  $_" }
    exit 1
}
$summary
'통과 — legacy·experiment 축 없이 Animator 재생 골든·본 해석·마스크·클립 열거·Foliage 뷰가 전량 typed generation 축이다'
Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
exit 0

# PHASE 3.75 MBC7 — Scene/MeshRenderer/material 직접 소비 + Gunner cold-load closure.
#
# ── 이 게이트가 메우는 구멍 ──
#
# MBC6은 RHI·패스가 typed `RHIModelMeshView`를 소비하게 했지만 그 뷰를 채우는
# 제품 코드는 0곳이었다(합성 selftest만). MBC7은 MeshRenderer가 모델
# `ModelAssetGeneration`을 붙들고 프록시→drawPool이 그 뷰를 채우며, 재질의 embedded
# texture를 전역 임베디드 등록부·이름 폴백·로드 순서가 아니라 그 generation
# closure에서 푼다(§6.2 Gunner). 이 게이트는 그 세 가지를 **실씬·실프로세스**로 잰다.
#
# ── 시나리오 ──
#
#   A(저작 프로세스) FT_Primitives(카메라·광원·UUIDv8 프리미티브 8) 위에 Gunner를
#     cache 로드(model.loadcached — import를 타지 않아 tracked sidecar를 건드리지
#     않는다) → 배치(model.place) → 카메라를 삼키지 않게 이동 → assets.scenemodel
#     → 저장. dx12.scene은 활성 카메라를 요구하므로 빈 씬으로는 7을 잴 수 없다.
#   B(콜드 프로세스) 저장 씬 로드 → assets.scenemodel → reimport(assets.scenemodel
#     reload) → dx12.scene 렌더.
#
# ── 판정 항목 ──
#
#   1  배치가 typed 정본을 탔다             — [model.instantiate] generation: Gunner 1회
#   2  씬 전수 typed 폐포(A)                — assets.scenemodel pass, generation ≥ 2,
#      legacyOnly 0, embedded 6 = generationTextures 6, registryTextures 0, missing 0
#   3  저장 씬에 영속 MeshId가 적혔다        — m_meshAssetId: <UUIDv8> ≥ 2
#   4  콜드 로드 해석이 generation이다      — [mesh.resolve] generation: ≥ 10 · legacy 0 ·
#      experiment 0 (FT 프리미티브 8 + Gunner 2, 전부 UUIDv8이라 전량 typed여야 한다)
#   5  콜드 로드 폐포(B) — 2와 같은 조건    ★ 같은 프로세스의 이전 로드·등록부 없이
#      6/6이 closure에서 온다(순서 해킹 없이 성립하는 것을 증명하는 축)
#   6  reimport 뒤 이전 texture generation 재사용 0 — reload pass reused=0 retired=6
#   7  실GPU 렌더가 typed 업로드다          — dx12.scene 통과, 메시 업로드 N == generation N ≥ 1,
#      handle(experiment 핸들 진입점) 0, 커버리지 > 0. `experiment` 계수는 packed 정점
#      전체(attributeMask != 0 — generation 포함)라 판정 축이 아니다.
#   8  정적 — 순서 해킹 토큰(modelGuidHint) 0, typed 배선 심볼 존재, stderr 공백, exit 0
#
# ★ 자가 틀렸을 때: 2/5가 embedded=0이면 Gunner 재질 GUID가 실리지 않은 것이고
#   (0개 비교 통과 금지 — selftest가 gunner 6을 요구한다), 7의 experiment/handle이
#   0이 아니면 프록시가 typed 뷰를 흘려 폴백이 받친 것이다.
param(
    [string]$Editor = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Editor\CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$run = Join-Path $Work ('creator-mbc7-scene-' + [guid]::NewGuid().ToString('N'))
$failures = [System.Collections.Generic.List[string]]::new()
function Add-Failure([string]$Message) { $failures.Add($Message) }

function Invoke-Editor([string]$Label, [string[]]$Commands) {
    $scenario = Join-Path $run ($Label + '.commands.txt')
    [IO.File]::WriteAllText($scenario, ($Commands -join "`n") + "`n",
        [Text.UTF8Encoding]::new($false))
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Editor
    $start.Arguments = '--script "' + $scenario.Replace('"', '\"') + '"'
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
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        throw "CreatorEditor timeout($Label): $run"
    }
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $run ($Label + '.stdout.txt')), $stdout,
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $run ($Label + '.stderr.txt')), $stderr,
        [Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{ Stdout = $stdout; Stderr = $stderr; ExitCode = $process.ExitCode }
}

function Assert-SceneModelClosure([string]$Label, [string]$Stdout) {
    # MBC9 — legacy 축(legacyOnly/legacyParity/registryTextures)은 은퇴했다. UUIDv8인데
    # generation이 없는 renderer는 unbound다(그릴 것이 없다 — 0이어야 한다).
    $line = [regex]::Match($Stdout, '\[CLI\] assets\.scenemodel (pass|fail) renderers=(\d+) generation=(\d+) unbound=(\d+) handleInvalid=(\d+) rhiView=(\d+) meshIdPersisted=(\d+) textureProps=(\d+) embedded=(\d+) generationTextures=(\d+) otherTextures=(\d+) missing=(\d+) gunner=(\d+)/(\d+)')
    if (-not $line.Success) { Add-Failure "$Label assets.scenemodel 요약 줄이 없다."; return }
    if ($line.Groups[1].Value -ne 'pass') { Add-Failure "$Label assets.scenemodel fail: $($line.Value)" }
    $generation = [int]$line.Groups[3].Value
    if ($generation -lt 2) { Add-Failure "$Label typed generation renderer가 2 미만이다: $generation" }
    if ([int]$line.Groups[4].Value -ne 0) { Add-Failure "$Label generation 없는 UUIDv8 renderer(unbound)가 남았다." }
    if ([int]$line.Groups[6].Value -ne $generation) { Add-Failure "$Label rhiView가 generation 수와 다르다." }
    if ([int]$line.Groups[7].Value -ne $generation) { Add-Failure "$Label m_meshAssetId가 전부 채워지지 않았다." }
    if ([int]$line.Groups[9].Value -ne 6 -or [int]$line.Groups[10].Value -ne 6) {
        Add-Failure "$Label Gunner embedded texture closure가 6/6이 아니다: embedded=$($line.Groups[9].Value) generationTextures=$($line.Groups[10].Value)"
    }
    if ([int]$line.Groups[11].Value -ne 0 -or [int]$line.Groups[12].Value -ne 0) {
        Add-Failure "$Label texture owner가 비었거나 출처 불명이다."
    }
    if ([int]$line.Groups[14].Value -ne 6) { Add-Failure "$Label Gunner embedded 계수가 6이 아니다." }
}

try {
    if (-not (Test-Path -LiteralPath $Editor -PathType Leaf)) {
        throw "CreatorEditor 실행 파일이 없다: $Editor"
    }
    $gunner = Join-Path $root 'Dynamic_CPP\Assets\Models\Gunner_F_Mythic.glb'
    if (-not (Test-Path -LiteralPath $gunner -PathType Leaf)) { throw "스킨 모델이 없다: $gunner" }
    $baseScene = Join-Path $root 'Dynamic_CPP\Assets\Scenes\FT_Primitives.creator'
    if (-not (Test-Path -LiteralPath $baseScene -PathType Leaf)) { throw "기준 씬이 없다: $baseScene" }
    New-Item -ItemType Directory -Path $run -Force | Out-Null
    if ($run -match '\s') { throw "console 인자로 넘길 작업 경로에 공백이 있다: $run" }
    $savedScene = (Join-Path $run 'mbc7_gunner.creator').Replace('\', '/')

    # ── A: 저작 프로세스 — 배치·폐포·저장 ──
    $author = Invoke-Editor 'author' @(
        "scene.switch $($baseScene.Replace('\', '/'))",
        'wait 60',
        "model.loadcached $($gunner.Replace('\', '/'))",
        'wait 60',
        'model.place Gunner_F_Mythic',
        'wait 60',
        # 원점의 캐릭터는 씬 카메라를 삼켜 커버리지가 전면이 된다 — 저작값으로 옮겨
        # 저장·재로드에 반영한다(vertex-live 게이트와 같은 처방).
        'object.transform Gunner_F_Mythic 1.5 0 0 0 0 0 0.3 0.3 0.3',
        'wait 10',
        # 애니메이션 틱은 실시간이라 렌더가 비결정적이다 — 저장 전에 꺼서 B가
        # 결정적 바인드 포즈로 그리게 한다(vertex-live 게이트와 같은 처방).
        'object.property Gunner_F_Mythic Animator m_isEnabled false',
        'wait 10',
        'assets.modeldiag',
        'assets.scenemodel',
        "scene.save $savedScene",
        'wait 30',
        'quit')
    if ($author.ExitCode -ne 0) { Add-Failure "A 종료 코드 $($author.ExitCode)" }
    if (-not [string]::IsNullOrWhiteSpace($author.Stderr)) { Add-Failure 'A stderr가 비어 있지 않다.' }
    # MBC10 — 배치 관측은 읽기 전용 스냅샷이다(제품 stdout 토큰 없음).
    $authorDiag = [regex]::Match($author.Stdout, '\[CLI\] assets\.modeldiag meshResolveGeneration=(\d+) meshResolveFailed=(\d+) instantiateGeneration=(\d+) instantiateRejected=(\d+) tickGeneration=(\d+) tickNone=(\d+) lastInstantiated=(\S+)')
    if (-not $authorDiag.Success) { Add-Failure '1 A assets.modeldiag 스냅샷이 없다.' }
    elseif ([int]$authorDiag.Groups[3].Value -ne 1 -or $authorDiag.Groups[7].Value -ne 'Gunner_F_Mythic' -or [int]$authorDiag.Groups[4].Value -ne 0) {
        Add-Failure "1 배치가 typed generation 1회가 아니다: $($authorDiag.Value)"
    }
    if ($author.Stdout -match '\[(mesh\.resolve|model\.instantiate|anim\.tick|material\.finalize)\]') {
        Add-Failure '1c 제품 경로가 무조건 진단 토큰을 다시 찍는다(MBC10).'
    }
    Assert-SceneModelClosure '2(A)' $author.Stdout

    if (-not (Test-Path -LiteralPath $savedScene)) { throw "저장 씬이 없다: $savedScene" }
    $sceneText = [IO.File]::ReadAllText($savedScene)
    $meshIds = [regex]::Matches($sceneText,
        'm_meshAssetId: [0-9a-f]{8}-[0-9a-f]{4}-8[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}')
    if ($meshIds.Count -lt 2) { Add-Failure "3 저장 씬의 UUIDv8 m_meshAssetId가 2 미만이다: $($meshIds.Count)" }
    if ($sceneText -match 'm_meshAssetId: 00000000-0000-0000-0000-000000000000') {
        Add-Failure '3 nil m_meshAssetId가 저장됐다(해석 실패 renderer).'
    }

    # ── B: 콜드 프로세스 — 로드·폐포·reimport·렌더 ──
    $cold = Invoke-Editor 'cold' @(
        "scene.switch $savedScene",
        'wait 120',
        'assets.modeldiag',
        'assets.scenemodel',
        'assets.scenemodel reload Gunner_F_Mythic',
        'wait 10',
        'dx12.scene',
        'quit')
    if ($cold.ExitCode -ne 0) { Add-Failure "B 종료 코드 $($cold.ExitCode)" }
    if (-not [string]::IsNullOrWhiteSpace($cold.Stderr)) { Add-Failure 'B stderr가 비어 있지 않다.' }
    # FT 프리미티브 8 + Gunner 2 — UUIDv8 corpus라 전량 typed여야 한다(해석 실패 0).
    $coldDiag = [regex]::Match($cold.Stdout, '\[CLI\] assets\.modeldiag meshResolveGeneration=(\d+) meshResolveFailed=(\d+) instantiateGeneration=(\d+) instantiateRejected=(\d+) tickGeneration=(\d+) tickNone=(\d+) lastInstantiated=(\S+)')
    if (-not $coldDiag.Success) { Add-Failure '4 B assets.modeldiag 스냅샷이 없다.' }
    elseif ([int]$coldDiag.Groups[1].Value -lt 10 -or [int]$coldDiag.Groups[2].Value -ne 0) {
        Add-Failure "4 콜드 로드 메시 해석 generation $($coldDiag.Groups[1].Value) · 실패 $($coldDiag.Groups[2].Value) — typed 정본이 아니다(기대 generation 10)."
    }
    Assert-SceneModelClosure '5(B)' $cold.Stdout
    $reload = [regex]::Match($cold.Stdout,
        '\[CLI\] assets\.scenemodel reload (pass|fail) model=Gunner_F_Mythic textures=(\d+) reused=(\d+) created=(\d+) missing=(\d+) retired=(\d+) sameAggregate=(\d)')
    if (-not $reload.Success) { Add-Failure '6 reload 요약 줄이 없다.' }
    elseif ($reload.Groups[1].Value -ne 'pass' -or [int]$reload.Groups[2].Value -ne 6 -or
        [int]$reload.Groups[3].Value -ne 0 -or [int]$reload.Groups[6].Value -ne 6 -or
        $reload.Groups[7].Value -ne '0') {
        Add-Failure "6 reimport 뒤 texture generation 재사용 판정 실패: $($reload.Value)"
    }
    if ($cold.Stdout -notmatch '\[CLI\] dx12\.scene 통과') { Add-Failure '7 dx12.scene이 통과하지 않았다.' }
    # MBC9 — 업로드 요약은 (generation N, KB)뿐이다. 총계 == generation이어야 legacy 업로드 0.
    $upload = [regex]::Match($cold.Stdout, '메시 업로드\s+(\d+)\(generation\s+(\d+),')
    if (-not $upload.Success) { Add-Failure '7 dx12.scene 업로드 요약(generation 계수)이 없다.' }
    else {
        $total = [int]$upload.Groups[1].Value
        $generationUploads = [int]$upload.Groups[2].Value
        if ($total -lt 1 -or $generationUploads -ne $total) {
            Add-Failure "7 실GPU 업로드가 전량 typed generation이 아니다: 총 $total · generation $generationUploads"
        }
    }
    $coverage = [regex]::Match($cold.Stdout, '커버리지\s+(\d+)/65536')
    if (-not $coverage.Success -or [int]$coverage.Groups[1].Value -le 0) {
        Add-Failure '7 커버리지가 0이다 — typed 업로드로 그린 그림이 비었다.'
    }

    # ── 8: 정적 ──
    $meshRenderer = [IO.File]::ReadAllText((Join-Path $root 'Engine\SceneRuntime\MeshRenderer.cpp'))
    if ($meshRenderer -match 'modelGuidHint') {
        Add-Failure '8 MeshRenderer에 재질 노드 선독 순서 해킹(modelGuidHint)이 있다.'
    }
    if ($meshRenderer -notmatch 'BindModelGeneration' -or $meshRenderer -notmatch 'LoadModelAssetGeneration') {
        Add-Failure '8 MeshRenderer가 typed generation을 붙들지 않는다.'
    }
    $bridge = [IO.File]::ReadAllText((Join-Path $root 'Engine\SceneRuntime\PrimitiveProxyBridge.cpp'))
    if ($bridge -notmatch 'm_modelGeneration = component->m_modelGeneration') {
        Add-Failure '8 프록시가 typed generation을 복사하지 않는다.'
    }
    $renderer = [IO.File]::ReadAllText((Join-Path $root 'Engine\RenderEngine\Render\Scene\EnhancedSceneRenderer.cpp'))
    if ($renderer -notmatch 'BuildRHIModelMeshView\(\*proxy->m_modelGeneration' -or
        $renderer -notmatch 'generationSource') {
        Add-Failure '8 drawPool이 typed 뷰를 채우지 않거나 generation 수명을 붙들지 않는다.'
    }
    $dataSystem = [IO.File]::ReadAllText((Join-Path $root 'Engine\RenderEngine\DataSystem.cpp'))
    if ($dataSystem -notmatch 'ResolveModelGenerationTexture' -or
        $dataSystem -notmatch 'RetireModelGenerationTextures\(retired->Handle\(\)\)') {
        Add-Failure '8 generation texture 캐시 또는 generation 단위 retire가 빠졌다.'
    }
}
catch {
    Add-Failure "예외: $($_.Exception.Message)"
}

$summary = "model-scene-consumption failures=$($failures.Count) run=$run"
if ($failures.Count -gt 0) {
    $summary
    $failures | ForEach-Object { "  $_" }
    exit 1
}
$summary
'통과 — Scene/MeshRenderer가 typed generation을 붙들고 Gunner 콜드 로드가 등록부·순서 없이 closure 6/6, reimport 뒤 이전 texture generation 재사용 0, 실GPU 업로드 전량 typed'
Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
exit 0

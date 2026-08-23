# 렌더 파이프라인 구성 (E4 착수 전 필수 게이트)
#
# 왜 필요한가
# ───────────
# E4는 Editor 렌더 패스를 RenderCore 밖으로 옮긴다. 판정 기준은 "Grid/Gizmo는
# Scene View에만 기여하고 Player pipeline에는 node 자체가 없다"인데, **패스가 어느
# 뷰에 조립되는지를 밖에서 볼 수단이 없었다.**
#
# 패스 내부 렌더링은 이미 덮여 있다 — `dx12.*`/`vk.*` 자가 검사 35종이 리드백으로
# 픽셀까지 잰다(그래서 E4-1의 파일 이동이 기준선과 정확히 일치했다). 빈 구멍은
# 내부가 아니라 **조립**이다. 그래서 픽셀 캡처를 새로 만들지 않고
# `LivePipelineDesc::Dump()`를 `pipeline.nodes` CLI로 내보내 조립을 못 박는다.
#
# ⚠ 착수 시점의 실측 (2026-08-23)
# ───────────────────────────────
# 착수 전에는 Editor와 Player의 파이프라인이 **완전히 동일했다** — 둘 다 19노드,
# Grid·GizmoIcon·GizmoLine이 `always`로 들어 있었다. 즉 출하 게임이 에디터
# 그리드·기즈모 패스를 매 프레임 그래프에 선언하고 있었다.
#
# E4-2(같은 날)가 그 네 노드의 조립을 IRenderFeatureContributor 뒤로 옮겼다.
# Editor는 EditorSceneOverlayContributor가 같은 자리(UI 뒤, live_present 앞)에
# 같은 이름·순서·술어로 기여하므로 **Editor 관측값은 전후 동일**하고(그래서
# 판정 A/B의 기대값이 안 바뀌었다), Player는 기여자가 없어 노드 자체가 서지
# 않는다(19→15). 판정 A/B가 Editor에서 여전히 통과한다는 것이 곧 "기여자가
# 실제로 발화한다"의 런타임 증거다.
#
# Player 쪽은 CLI가 없어 여기서 몰지 못한다 — `PlayerMain`이 스모크 로그에 같은
# 값을 찍으므로(`[SMOKE] pipeline.node`) 게임 빌드 로그에서 15노드·에디터 노드
# 부재를 확인한다. 정적으로는 아래 판정 C가 조립 소유권을 못 박는다.
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$Work = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Editor executable is missing: $Exe"
}

# ── 판정 C (E4-2): 조립 소유권 — Core는 에디터 노드를 조립하지 않는다 ──
#
# 부재 단정만 두면 파일 경로가 틀려도 0건으로 통과하므로, 찾을 수 있어야 하는
# 것(기여자 쪽 등록·설치)을 먼저 찾는다. 주석은 걸러내고 코드만 본다 —
# 설명 주석을 지워야 통과하는 게이트를 만들지 않는다(E3-4의 교훈).
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

function Get-CodeText([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "판정 C 대상 파일이 없다: $Path"
    }
    # 줄 단위 // 주석 제거. 블록 주석·문자열 내 //는 다루지 않는다 — 이
    # 게이트가 찾는 패턴(node.name 대입, include, 호출)에는 충분하다.
    $lines = Get-Content -LiteralPath $Path
    return ($lines | ForEach-Object { ($_ -split '//', 2)[0] }) -join "`n"
}

$liveCode    = Get-CodeText (Join-Path $repoRoot "RenderEngine\Render\Scene\EnhancedSceneRendererLive.cpp")
$contribCode = Get-CodeText (Join-Path $repoRoot "EngineEntry\EditorSceneOverlayContributor.cpp")
$mainCode    = Get-CodeText (Join-Path $repoRoot "EngineEntry\EditorMain.cpp")

# C-1. 존재해야 하는 것부터 — 기여자가 네 노드를 실제로 등록한다.
foreach ($name in @('Grid', 'WireFrame', 'GizmoIcon', 'GizmoLine')) {
    if ($contribCode -notmatch [regex]::Escape("node.name = `"$name`"")) {
        throw "EditorSceneOverlayContributor가 '$name' 노드를 등록하지 않는다 — 기여자가 비었으면 부재 단정은 무의미하다"
    }
}

# C-2. Editor Host가 기여자를 설치하고, 그 설치가 렌더러 초기화(렌더 스레드
# 기동)보다 앞이다. 순서 단정은 등장 횟수를 먼저 못 박는다 — 파일 전체
# IndexOf가 엉뚱한 등장을 잡던 전례(E3-2·E3-7의 게이트 거짓 실패) 방지.
$initMatches = [regex]::Matches($mainCode, [regex]::Escape('EnhancedSceneRenderer::InitializeRuntime'))
if ($initMatches.Count -ne 1) {
    throw "EditorMain.cpp의 InitializeRuntime 등장이 $($initMatches.Count)회다(기대 1) — 순서 단정의 전제가 깨졌으니 게이트를 다시 범위 지정하라"
}
$installMatches = [regex]::Matches($mainCode, [regex]::Escape('SetRenderFeatureContributor'))
if ($installMatches.Count -ne 2) {
    throw "EditorMain.cpp의 SetRenderFeatureContributor 등장이 $($installMatches.Count)회다(기대 2: 설치+해제)"
}
if ($installMatches[0].Index -ge $initMatches[0].Index) {
    throw "기여자 설치가 렌더러 초기화보다 뒤다 — 첫 파이프라인 조립이 오버레이 없이 선다"
}

# C-3. Core 부재 — Live.cpp가 에디터 노드를 등록하지도, 에디터 패스를
# include하지도, GizmoRenderer를 호출하지도 않는다.
foreach ($name in @('Grid', 'WireFrame', 'GizmoIcon', 'GizmoLine')) {
    if ($liveCode -match [regex]::Escape("node.name = `"$name`"")) {
        throw "Core(EnhancedSceneRendererLive.cpp)가 '$name' 노드를 직접 등록한다 — E4-2 소유권 회귀"
    }
}
if ($liveCode -match '#include\s+"[^"]*Passes/Editor/') {
    throw "Core(EnhancedSceneRendererLive.cpp)가 에디터 패스 헤더를 include한다 — E4-2 소유권 회귀"
}
if ($liveCode -match 'GizmoRenderer') {
    throw "Core(EnhancedSceneRendererLive.cpp)가 GizmoRenderer를 참조한다 — §4.4 위반"
}

# C-4. Player는 기여자를 설치하지 않는다.
foreach ($playerFile in @('Player\PlayerMain.cpp', 'Player\PlayerApp.cpp')) {
    $playerCode = Get-CodeText (Join-Path $repoRoot $playerFile)
    if ($playerCode -match 'SetRenderFeatureContributor') {
        throw "$playerFile 이 파이프라인 기여자를 설치한다 — Player에는 씬 오버레이 노드가 서면 안 된다"
    }
}

if ([string]::IsNullOrWhiteSpace($Work)) {
    $Work = Join-Path ([IO.Path]::GetTempPath()) ("CE_Pipeline_" + [guid]::NewGuid().ToString("N"))
}
New-Item -ItemType Directory -Force -Path $Work | Out-Null

try {
    $commandFile = Join-Path $Work "commands.txt"
    $stdout = Join-Path $Work "stdout.txt"
    $stderr = Join-Path $Work "stderr.txt"

    # 워밍업이 필요하다. 파이프라인은 첫 리사이즈·첫 뷰 확정 뒤에 서므로 바로 물으면
    # 러너가 아직 비활성이거나 desc가 비어 있다(dx12 스위트가 240프레임을 쓰는 이유와 같다).
    [IO.File]::WriteAllLines($commandFile, @(
        "wait 240"
        "pipeline.nodes"
        "quit"
    ))

    $process = Start-Process -FilePath $Exe `
        -ArgumentList "--console", "--script", $commandFile `
        -WorkingDirectory (Split-Path $Exe -Parent) `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    if (-not $process.WaitForExit(300000)) {
        $process.Kill()
        throw "pipeline composition probe timed out"
    }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        $errorText = if (Test-Path -LiteralPath $stderr) { Get-Content -LiteralPath $stderr -Raw } else { "" }
        throw "pipeline composition probe exited with $($process.ExitCode): $errorText"
    }

    $output = Get-Content -LiteralPath $stdout -Raw

    $summary = [regex]::Match($output,
        '\[pipeline\.nodes\] 합계 (\d+) · valid=(\d) · ready=(\d)')
    if (-not $summary.Success) {
        throw "pipeline.nodes 요약이 안 나왔다 — 프로브가 그 지점에 도달하지 못했다"
    }
    $declaredCount = [int]$summary.Groups[1].Value
    $valid = [int]$summary.Groups[2].Value
    $ready = [int]$summary.Groups[3].Value

    $nodes = @([regex]::Matches($output, '\[pipeline\.node\] ([^|\r\n]+)\|(\w+)') |
        ForEach-Object {
            [pscustomobject]@{ Name = $_.Groups[1].Value; State = $_.Groups[2].Value }
        })

    # ── 무의미성 방지 1: 측정이 실제로 나왔는가 ──
    # 러너가 비활성이면 노드 0개로 "차이 없음"이 트리비얼하게 참이 된다.
    if ($declaredCount -le 0) {
        throw "파이프라인 노드가 0개다 — 러너가 비활성이거나 desc가 비어 있다"
    }
    if ($valid -ne 1 -or $ready -ne 1) {
        throw "파이프라인이 유효/준비 상태가 아니다: valid=$valid ready=$ready — 이 상태의 구성은 의미가 없다"
    }
    # ── 무의미성 방지 2: 요약과 실제 행 수가 맞는가 ──
    if ($nodes.Count -ne $declaredCount) {
        throw "노드 행이 다 파싱되지 않았다: 행=$($nodes.Count) 요약=$declaredCount"
    }

    # ── 판정 A: 알려진 런타임 패스가 전부 있다 ──
    # 이것이 양성 대조다. 이게 없으면 "Editor 패스가 없다"가 파이프라인이 통째로
    # 비어서 참이 되는 경우와 구분되지 않는다.
    $requiredRuntime = @('Shadow','GBuffer','Deferred','Forward+','PostChain','UI','live_present')
    foreach ($name in $requiredRuntime) {
        if (-not ($nodes | Where-Object { $_.Name -eq $name })) {
            throw "런타임 패스 '$name'이 파이프라인에 없다 — 구성이 무너졌다"
        }
    }

    # ── 판정 B: Editor 패스의 현재 상태를 못 박는다 ──
    # 착수 시점 실측: Grid·GizmoIcon·GizmoLine은 active 술어가 없어 always,
    # WireFrame만 술어를 가져 inactive다. E4-3이 이것을 바꾸면 여기서 드러난다.
    $expectedEditorState = @{
        'Grid'      = 'always'
        'GizmoIcon' = 'always'
        'GizmoLine' = 'always'
        'WireFrame' = 'inactive'
    }
    $editorReport = @()
    foreach ($name in ($expectedEditorState.Keys | Sort-Object)) {
        $node = $nodes | Where-Object { $_.Name -eq $name }
        if (-not $node) {
            throw ("Editor 패스 '$name'이 파이프라인에서 사라졌다. E4-3이 걷어낸 것이라면 " +
                "이 게이트의 기대값을 함께 고쳐라 — 그 수정이 곧 변경의 증거다")
        }
        if ($node.State -ne $expectedEditorState[$name]) {
            throw ("Editor 패스 '$name'의 활성 상태가 바뀌었다: 기대 $($expectedEditorState[$name]) " +
                "관측 $($node.State). 의도한 변경이면 기대값을 고쳐라")
        }
        $editorReport += "$name=$($node.State)"
    }

    "pipeline composition: PASS (nodes=$declaredCount, editor-passes=[$($editorReport -join ' ')], ownership=contributor)"
}
finally {
    if (Test-Path -LiteralPath $Work) {
        $verified = [IO.Path]::GetFullPath($Work)
        $tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
        if ($verified.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path $verified -Leaf).StartsWith("CE_Pipeline_", [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $verified -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

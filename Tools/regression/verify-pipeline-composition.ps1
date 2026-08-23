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
# Editor와 Player의 파이프라인이 **완전히 동일하다** — 둘 다 19노드이고 그 안에
# Grid·GizmoIcon·GizmoLine이 `always`로 들어 있다. `always`는 `active` 술어가 없다는
# 뜻이라 `IsActive()`가 참이고, 따라서 `DeclareAll`이 건너뛰지 않는다. 즉 출하 게임이
# 에디터 그리드·기즈모 패스를 매 프레임 그래프에 선언한다. 건너뛰는 것은 술어를 가진
# `WireFrame`과 `VolumetricFog` 둘뿐이다.
#
# 이 게이트는 그 현재 상태를 **그대로** 못 박는다. E4-3이 노드를 실제로 걷어내면 이
# 게이트가 붉어지고, 그때 기대값을 함께 고치는 것이 곧 변경의 증거가 된다.
# Player 쪽은 CLI가 없어 여기서 몰지 못한다 — `PlayerMain`이 스모크 로그에 같은 값을
# 찍으므로(`[SMOKE] pipeline.node`) 게임 빌드 로그에서 확인한다.
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$Work = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Editor executable is missing: $Exe"
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

    "pipeline composition: PASS (nodes=$declaredCount, editor-passes=[$($editorReport -join ' ')])"
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

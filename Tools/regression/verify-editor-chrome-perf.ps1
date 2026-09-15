[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-editor-chrome-perf'),
    [string]$Golden = (Join-Path $PSScriptRoot 'editor-chrome-perf.golden.json'),
    [switch]$Update
)
# PHASE 21 W2-4 — chrome 성능 계약: 비용의 출처를 이름으로 연다.
#
# 계획서 §8.2 는 셋을 요구한다.
#
#   · theme/W2 적용만으로 editor chrome p95 CPU 가 기준선 대비 max(0.10 ms, 5%)
#     이상 악화되면 **원인을 기록하고** 최적화 또는 rollback 한다.
#   · warm-up 뒤 정적 경로의 frame 당 heap allocation 은 0 을 목표로 한다.
#   · custom draw 의 vertex/index 수, draw command 수를 W0 기준선과 함께 기록한다.
#
# ── 원인을 기록할 수단이 없었다 ─────────────────────────────────────────────
#
# W0 이 기준선을 떠 뒀다(Release · ini 를 지운 선언 배치 · 표본 열둘):
# **정점 1,688 · 인덱스 3,858 · draw command 12 · UI CPU 중앙 0.38 ms.**
# 그런데 2026-09-15 실측은 **정점 4,172 · 인덱스 12,876 · draw 50 · 1.1 ms** 였다.
# 세 배가 넘게 벌어졌는데 **어디서 벌어졌는지 낼 자가 없었다** — `editor.panelcost`
# 의 슬롯은 셋뿐이고(W7 이 자기 축으로 세웠다) 그 합이 0.015 ms, 즉 프레임의 1%
# 였다. "악화되면 원인을 기록하고" 는 원인을 기록할 수 없으면 지킬 수 없는 문장이다.
# W2-1·W2-2·W2-3 과 같은 계통 — 판정문에 자가 없다.
#
# 그래서 층을 둘 더 세웠다(`editor::windows`, `editor.panelcost` 가 함께 낸다).
#
#   창 단위  — 셸이 창을 그리는 자리는 한 곳이라(`EditorWindowHost`) 거기 한 번
#              걸면 창 전부가 잡힌다. 창마다 계측을 흩지 않는다.
#   구간     — 창 본문 **밖**의 조각들: 호스트 BeginFrame · 도크스페이스 ·
#              창 앞뒤 · 게시/스냅샷.
#
# ── 그렇게 열고 보니 판이 뒤집혔다 ─────────────────────────────────────────
#
# 프레임 총계 0.880 ms 중 **`(host.beginframe)` 이 0.676 ms(77%)** 이고 에디터 창을
# 다 합쳐도 0.088 ms 다. chrome 이 비싼 것은 위젯 때문이 아니다. 그 구간에는
# `ImGui::NewFrame` 뿐 아니라 `m_renderer->Resize`·`NewFrame()` 이 들어 있어 **RHI
# 프레임 자원 획득**이 함께 잡힌다 — 이름을 `imgui.*` 로 두면 순수 UI CPU 로 읽힌다.
#
# ── 총계가 어디서 끊기는지가 판정을 가른다 ─────────────────────────────────
#
# 처음에 총계를 `EndFrame` **뒤**에서 닫았더니 잔차가 **음수**(-0.320 ms)가 됐다.
# `EndFrame` 안에는 `ImGui::Render()` 말고 **`RenderAndPresent`** 가 있어 GPU 제출과
# Present 가 든다. 그것을 "UI CPU" 에 넣으면 W0 이 기준선을 뜬 자(`ui_cpu_ms`)와
# 다른 것을 재게 되고, 기준선과의 비교가 통째로 끊긴다. 그래서 총계는 제출 앞에서
# 끊고 `(present)` 는 **총계 밖** 줄로 따로 낸다.
#
# ── 무엇을 판정하고 무엇을 기록만 하는가 ───────────────────────────────────
#
# 시간은 기계에 묶인다. 절대 ms 를 골든으로 박으면 다른 기계에서 제품과 무관하게
# 붉어진다. 그래서 **판정은 결정적인 축과 구조적인 축으로** 한다.
#
#   판정 : 정점·인덱스·draw command(같은 배치·같은 배율이면 결정적) ·
#          잔차 비율(설명하지 못한 몫) · 계측이 걸린 자리의 집합 ·
#          `(present)` 가 총계 밖에 있다는 것
#   기록 : 절대 ms 와 W0 기준선과의 차이. §8.2 의 셋째 항목이 요구하는 것이 이것이다.
#
# 환경이 골든과 다르면 수 판정은 **틀렸다고 말하지 않고 건너뛴다.** 다만 건너뛴
# 수를 세어 찍는다 — 조용한 건너뜀은 눈먼 초록과 구별되지 않는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)

if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
if ($Exe -notmatch 'x64-Release') {
    throw "성능 판정은 Release 로만 한다 — Debug 는 25 배 느리고 방향까지 뒤집는다. 받은 경로: $Exe"
}
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw '이미 떠 있는 에디터를 닫아라 — endpoint 파일은 프로젝트에 하나다.'
}
if (Test-Path -LiteralPath $Work) { Remove-Item -LiteralPath $Work -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$script:checks = 0
$script:skipped = 0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}

# W0 이 남긴 기준선. 이 수를 판정에 쓰지 않고 **나란히 찍는다** — 그때와 지금은
# 창 수도 배율도 다르고, 무엇 위에서 쟀는지가 수 자체만큼 중요하기 때문이다.
$w0 = @{ vertices = 1688; indices = 3858; drawCommands = 12; uiCpuMedianMs = 0.38 }

Write-Host ''
Write-Host 'chrome 성능 계약 (PHASE 21 W2-4)'
Write-Host ''

function Invoke-Editor([string[]]$Commands, [string]$Tag) {
    $ws = Join-Path $Work "ws-$Tag"
    New-Item -ItemType Directory -Force -Path $ws | Out-Null
    $scriptPath = Join-Path $Work "$Tag.txt"
    $resultPath = Join-Path $Work "$Tag.jsonl"
    Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value ($Commands + @('quit'))

    $priorWorkspace = $env:CREATOR_EDITOR_WORKSPACE_DIR
    $priorLegacy = $env:CREATOR_EDITOR_LEGACY_INI
    # W0 기준선과 같은 조건이다 — ini 를 지우고 **선언이 세운 배치** 위에서 잰다.
    # 개발자의 옛 배치를 물리면 값이 다섯 배 커진다(W0 이 한 번 그렇게 틀렸다).
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $ws
    $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $ws 'none.ini'
    try {
        $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
            -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                            '--result-file', ('"' + $resultPath + '"')) `
            -RedirectStandardOutput (Join-Path $Work "$Tag.out") `
            -RedirectStandardError (Join-Path $Work "$Tag.err")
        if (-not $proc.WaitForExit(600000)) { $proc.Kill(); throw "에디터가 600 초 안에 끝나지 않았다 ($Tag)." }
    }
    finally {
        if ($null -ne $priorWorkspace) { $env:CREATOR_EDITOR_WORKSPACE_DIR = $priorWorkspace }
        else { Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue }
        if ($null -ne $priorLegacy) { $env:CREATOR_EDITOR_LEGACY_INI = $priorLegacy }
        else { Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue }
    }
    Assert (Test-Path -LiteralPath $resultPath) "결과 파일이 없다 ($Tag, 종료 코드 $($proc.ExitCode))"
    $rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } |
              ForEach-Object { $_ | ConvertFrom-Json })
    $failed = @($rows | Where-Object { 'succeeded' -ne $_.status })
    Assert (0 -eq $failed.Count) `
        ("$Tag 회차에서 실패한 명령 " + $failed.Count + ' 건: ' +
         (@($failed | ForEach-Object { $_.command + '(' + $_.status + ')' }) -join ' · '))
    $exitText = '0x{0:X8}' -f $proc.ExitCode
    Assert (0 -eq $proc.ExitCode) "$Tag 회차 종료 코드가 비정상이다 ($exitText)"
    return $rows
}

function Measure-Round([string[]]$Pre, [string]$Tag) {
    $lines = [Collections.Generic.List[string]]::new()
    # 워밍업. 첫 프레임들은 셰이더 적재·폰트 아틀라스·도크 구축이 섞여 있어
    # 정적 경로의 비용이 아니다.
    $lines.Add('wait 240')
    foreach ($c in $Pre) { $lines.Add($c) }
    $lines.Add('editor.panelcost reset')
    # 표본을 모은다. `editor.dock` 은 정점·인덱스·draw command 와 W0 축의 UI CPU 를
    # 함께 내므로 같은 자리에서 부른다.
    foreach ($i in 1..24) { $lines.Add('editor.dock'); $lines.Add('wait 20') }
    $lines.Add('editor.panelcost')

    $rows = Invoke-Editor $lines.ToArray() $Tag
    $dock = @($rows | Where-Object { $_.command -eq 'editor.dock' })
    Assert ($dock.Count -ge 12) "$Tag 회차의 editor.dock 표본이 $($dock.Count) 개다"
    $pc = @($rows | Where-Object { $_.command -eq 'editor.panelcost' })
    Assert ($pc.Count -ge 1) "$Tag 회차에서 editor.panelcost 가 돌지 않았다"
    $last = $dock[$dock.Count - 1]
    $cost = $pc[$pc.Count - 1].data
    $cpu = @($dock | ForEach-Object { $_.data.uiCpuMs }) | Sort-Object
    $idx = [Math]::Min($cpu.Count - 1, [int][Math]::Ceiling(0.5 * $cpu.Count) - 1)
    return [pscustomobject]@{
        Tag = $Tag
        Vertices = [int]$last.data.imguiVertices
        Indices = [int]$last.data.imguiIndices
        DrawCommands = [int]$last.data.imguiDrawCommands
        UiScale = [double]$last.data.uiScale
        RootWidth = [double]$last.data.rootWidth
        RootHeight = [double]$last.data.rootHeight
        DockCpuMedianMs = [double]$cpu[$idx]
        Cost = $cost
    }
}

# 회차 둘. 선택 없음과 선택 있음 — 뒤엣것이 인스펙터에 W2 의 custom widget 을
# 그린다. 두 회차의 **차이**가 그 위젯들의 몫이고, 그것이 §8.1 이 요구한
# "기대치를 분리한다" 다.
$base = Measure-Round @() 'base'
$selected = Measure-Round @('scene.populate 6 0', 'wait 60', 'scene.select W7_2', 'wait 60') 'selected'

# ── 표를 먼저 찍는다. 판정보다 **보이는 것**이 먼저다 ────────────────────
foreach ($round in @($base, $selected)) {
    $c = $round.Cost
    Write-Host ("  [{0}] 정점 {1,6}  인덱스 {2,6}  draw {3,3}   프레임 avg {4:N3} p95 {5:N3}  설명 {6:N3}  미설명 {7:N3}  제출 {8:N3}" -f `
        $round.Tag, $round.Vertices, $round.Indices, $round.DrawCommands,
        $c.frame.avgMs, $c.frame.p95Ms, $c.explainedAvgMs, $c.unexplainedAvgMs, $c.presentAvgMs)
    foreach ($w in ($c.windows | Sort-Object -Property @{Expression={$_.avgMs}} -Descending)) {
        $mark = if ($w.outsideFrameTotal) { ' (총계 밖)' } else { '' }
        Write-Host ("      {0,-28} avg {1,7:N3}  p95 {2,7:N3}{3}" -f $w.id, $w.avgMs, $w.p95Ms, $mark)
    }
}

# ── ① 계측이 걸린 자리 ───────────────────────────────────────────────────
#
# 비용의 출처를 여는 것이 이 게이트의 목적이므로, **계측이 어디에 걸려 있는지**가
# 첫 판정이다. 걸린 자리가 줄면 잔차가 늘고, 잔차가 늘면 "원인을 기록한다" 가
# 다시 불가능해진다.
$expectedSections = @('(host.beginframe)', '(shell.beforeframe)', '(shell.dockspace)', '(shell.prewindows)',
                      '(shell.postwindows)', '(shell.endrender)', '(present)')
$expectedWindows = @('###Editor.Viewport', '###Editor.Hierarchy', '###Editor.Inspector',
                     '###Editor.AssetBundle', '###Editor.ContentBrowser')
foreach ($round in @($base, $selected)) {
    $ids = @($round.Cost.windows | ForEach-Object { $_.id })
    $missingSections = @($expectedSections | Where-Object { $ids -notcontains $_ })
    Assert (0 -eq $missingSections.Count) `
        ("[$($round.Tag)] 계측이 사라진 구간: " + ($missingSections -join ', ') +
         '. 구간이 빠지면 그 비용이 잔차로 숨어 원인을 기록할 수 없게 된다.')
    $missingWindows = @($expectedWindows | Where-Object { $ids -notcontains $_ })
    Assert (0 -eq $missingWindows.Count) `
        ("[$($round.Tag)] 계측이 사라진 창: " + ($missingWindows -join ', ') +
         '. 셸의 창 루프에 계약이 걸려 있는지 확인하라.')
    foreach ($w in $round.Cost.windows) {
        Assert ($w.frames -gt 0) "[$($round.Tag)] $($w.id) 이 표본 0 이다 — 표에 있는데 한 번도 그려지지 않았다"
    }
}

# ── ② 제출은 총계 밖이다 ─────────────────────────────────────────────────
#
# `(present)` 안에는 GPU 제출과 Present 가 든다. 설명 합에 섞이면 "UI CPU 가
# 설명됐다" 가 거짓이 되고, 총계를 그 뒤에서 끊으면 W0 기준선과 축이 갈린다.
foreach ($round in @($base, $selected)) {
    $present = @($round.Cost.windows | Where-Object { $_.id -eq '(present)' })
    Assert (1 -eq $present.Count) "[$($round.Tag)] (present) 줄이 없다"
    Assert ($present[0].outsideFrameTotal) `
        "[$($round.Tag)] (present) 가 총계 안으로 들어갔다 — UI CPU 가 GPU 제출을 포함하게 된다"
    Assert ($round.Cost.presentAvgMs -gt 0) "[$($round.Tag)] 제출 비용이 0 이다 — 재지 못했다"
    # 합이 맞는지 본다. 설명 + 미설명 = 총계여야 하고, 제출은 그 바깥이다.
    $sum = [double]$round.Cost.explainedAvgMs + [double]$round.Cost.unexplainedAvgMs
    $delta = [Math]::Abs($sum - [double]$round.Cost.frame.avgMs)
    Assert ($delta -lt 0.001) `
        ("[$($round.Tag)] 설명 + 미설명 이 총계와 다르다 ({0:N4} vs {1:N4}) — 잔차 계산이 깨졌다" -f `
         $sum, $round.Cost.frame.avgMs)
}

# ── ③ 설명력 ─────────────────────────────────────────────────────────────
#
# 잔차는 어느 구간에도 속하지 않은 비용이다. 계측을 세우기 전에는 그것이 99% 였고
# (프레임 1.1 ms 중 슬롯 합 0.015 ms) 지금은 10~20% 다.
#
# ★ 문턱을 50% 로 **느슨하게** 둔다. 비율은 총계가 작아질수록 커지기 때문이다 —
#   같은 코드로 잰 두 회차에서 총계가 0.918 과 0.492 로 갈리자 잔차 비율이 19%
#   와 26% 로 움직였다(실측). 기계가 한가한 날 오히려 붉어지는 판정은 쓸 수 없다.
#   계측이 **사라지는** 변이는 아래 ① 이 이름으로 잡는다 — 그것이 주 판정이고
#   여기는 건전성(음수가 아니고 총계를 삼키지 않는다)과 기록이다.
foreach ($round in @($base, $selected)) {
    $ratio = [double]$round.Cost.unexplainedAvgMs / [double]$round.Cost.frame.avgMs
    Assert ($ratio -ge 0) `
        ("[$($round.Tag)] 잔차가 음수다 ({0:P1}) — 구간이 서로 겹쳐 같은 시간을 두 번 세고 있다" -f $ratio)
    $ratioText = '{0:P1}' -f $ratio
    Assert ($ratio -le 0.50) `
        ("[$($round.Tag)] 설명하지 못한 비용이 $ratioText 다(문턱 50%). 프레임의 그만큼이 " +
         '이름 없이 남아 있으면 §8.2 의 "악화되면 원인을 기록하고" 를 지킬 수 없다. ' +
         '계측이 걸린 자리가 줄었는지 먼저 보라.')
    Write-Host ("  [{0}] 설명력 {1:P1} — 잔차 {2:N3} ms" -f $round.Tag, (1 - $ratio), $round.Cost.unexplainedAvgMs)
}

# ── ④ W2 custom widget 의 몫 ─────────────────────────────────────────────
#
# §8.1 은 *"custom draw 4종 — 작은 CPU/vertex 증가 가능 — widget별 micro scene"* 을
# 요구한다. 두 회차의 차이가 그 micro scene 이다: 같은 배치에서 선택만 달라지므로
# 늘어난 것은 인스펙터가 그리는 W2 위젯뿐이다.
$baseInspector = @($base.Cost.windows | Where-Object { $_.id -eq '###Editor.Inspector' })[0]
$selInspector = @($selected.Cost.windows | Where-Object { $_.id -eq '###Editor.Inspector' })[0]
$inspectorDelta = [double]$selInspector.avgMs - [double]$baseInspector.avgMs
$vertexDelta = $selected.Vertices - $base.Vertices
Write-Host ''
Write-Host ("  W2 위젯의 몫 — 인스펙터 {0:N3} → {1:N3} ms (+{2:N3}) · 정점 +{3} · 인덱스 +{4} · draw +{5}" -f `
    $baseInspector.avgMs, $selInspector.avgMs, $inspectorDelta, $vertexDelta,
    ($selected.Indices - $base.Indices), ($selected.DrawCommands - $base.DrawCommands))

# ★ "늘었다" 만 묻지 않는다. 선택을 아예 빼 보니 그래도 조금 늘어(정점 +132)
#   단정이 통과했다 — 씬에 엔티티가 생긴 것만으로도 목록이 움직이기 때문이다.
#   그때 붉어진 것은 골든(⑦)이었고, **맞는 이유로 붉은 것이 아니었다.** 실측치에
#   맞춰 좁힌다: 인스펙터는 여덟 배가 되고(0.007 → 0.058) 정점은 천오백이 는다.
$inspectorText = '{0:N3} → {1:N3} ms' -f $baseInspector.avgMs, $selInspector.avgMs
Assert ($inspectorDelta -ge ([double]$baseInspector.avgMs)) `
    ("엔티티를 골랐는데 인스펙터 비용이 두 배도 되지 않았다($inspectorText). " +
     '위젯을 그리지 않았거나 계측이 죽었다 — 어느 쪽인지 창 표의 frames 를 보라.')
Assert ($vertexDelta -ge 500) `
    "엔티티를 골랐는데 정점이 $vertexDelta 만 늘었다(실측 +1498). 인스펙터가 위젯을 그리지 않았다"

# 이 위젯들이 chrome 을 지배하지 않는다는 것. 지배하기 시작하면 §8.2 의 rollback
# 논의가 필요하고, 그때 이 단정이 먼저 말한다.
$frameShare = $inspectorDelta / [double]$selected.Cost.frame.avgMs
Assert ($frameShare -le 0.25) `
    ("W2 위젯이 프레임의 {0:P1} 를 쓴다(문턱 25%) — §8.2 의 최적화 또는 rollback 을 검토하라." -f $frameShare)
Write-Host ("  그 몫은 프레임의 {0:P1} 다. 가장 비싼 구간은 아래에 있다." -f $frameShare)

# ── ⑤ 가장 비싼 구간을 이름으로 남긴다 ──────────────────────────────────
#
# 기록이 이 게이트의 산출물이다. 수가 움직였을 때 다음 사람이 어디를 볼지 알아야
# 한다.
$top = @($base.Cost.windows | Where-Object { -not $_.outsideFrameTotal } |
         Sort-Object -Property @{Expression={$_.avgMs}} -Descending)[0]
$topShare = [double]$top.avgMs / [double]$base.Cost.frame.avgMs
Write-Host ("  가장 비싼 구간: {0} — {1:N3} ms (프레임의 {2:P1})" -f $top.id, $top.avgMs, $topShare)
if ($top.id -eq '(host.beginframe)') {
    Write-Host '    그 안에는 ImGui NewFrame 말고 RHI 프레임 자원 획득이 들어 있다(m_renderer->Resize/NewFrame).'
    Write-Host '    순수 UI CPU 가 아니므로 위젯 최적화로는 줄지 않는다.'
}

# ── ⑥ W0 기준선과 나란히 ─────────────────────────────────────────────────
#
# §8.2 의 셋째 항목이 요구하는 **기록**이다. 판정하지 않는다 — 그때와 지금은 창
# 수도 배율도 다르고, 절대 ms 는 기계에 묶인다.
Write-Host ''
Write-Host '  W0 기준선(2026-09-11 · Release · ini 를 지운 선언 배치) 과 지금'
Write-Host ("    정점         {0,8} → {1,8}  ({2:N1} 배)" -f $w0.vertices, $base.Vertices, ($base.Vertices / $w0.vertices))
Write-Host ("    인덱스       {0,8} → {1,8}  ({2:N1} 배)" -f $w0.indices, $base.Indices, ($base.Indices / $w0.indices))
Write-Host ("    draw command {0,8} → {1,8}  ({2:N1} 배)" -f $w0.drawCommands, $base.DrawCommands, ($base.DrawCommands / $w0.drawCommands))
Write-Host ("    UI CPU 중앙  {0,8:N3} → {1,8:N3} ms  ({2:N1} 배)" -f $w0.uiCpuMedianMs, $base.DockCpuMedianMs, ($base.DockCpuMedianMs / $w0.uiCpuMedianMs))

# ── ⑦ 결정적인 축은 골든으로 박는다 ─────────────────────────────────────
#
# 정점·인덱스·draw command 는 같은 배치·같은 배율이면 실행마다 같다(실측: 회차를
# 거듭해도 한 글자도 다르지 않다). 시간과 달리 골든이 성립한다. 환경이 다르면
# 판정하지 않고 **건너뛴 수를 센다.**
$environment = [ordered]@{
    uiScale = $base.UiScale
    rootWidth = $base.RootWidth
    rootHeight = $base.RootHeight
}
$current = [ordered]@{
    environment = $environment
    base = [ordered]@{ vertices = $base.Vertices; indices = $base.Indices; drawCommands = $base.DrawCommands }
    selected = [ordered]@{ vertices = $selected.Vertices; indices = $selected.Indices; drawCommands = $selected.DrawCommands }
}

if ($Update) {
    Set-Content -LiteralPath $Golden -Encoding UTF8 -Value ($current | ConvertTo-Json -Depth 6)
    Write-Host ''
    Write-Host "  골든을 지금 상태로 갱신했다: $Golden"
    Write-Host '  이 변경을 커밋에 함께 담을 것 — 수가 왜 움직였는지가 기록에 남아야 한다.'
}
elseif (-not (Test-Path -LiteralPath $Golden)) {
    throw "골든이 없다: $Golden — -Update 로 한 번 떠라."
}
else {
    $gold = Get-Content -LiteralPath $Golden -Raw | ConvertFrom-Json
    $sameEnvironment = ([Math]::Abs([double]$gold.environment.uiScale - $base.UiScale) -lt 0.001) -and
                       ([Math]::Abs([double]$gold.environment.rootWidth - $base.RootWidth) -lt 0.5) -and
                       ([Math]::Abs([double]$gold.environment.rootHeight - $base.RootHeight) -lt 0.5)
    if (-not $sameEnvironment) {
        $script:skipped++
        Write-Host ''
        Write-Host ("  ★ 수 판정을 건너뛴다 — 환경이 골든과 다르다(배율 {0} vs {1}, 크기 {2}x{3} vs {4}x{5})." -f `
            $gold.environment.uiScale, $base.UiScale, $gold.environment.rootWidth, $gold.environment.rootHeight,
            $base.RootWidth, $base.RootHeight)
    }
    else {
        foreach ($pair in @(@{ n = 'base'; now = $base; gold = $gold.base },
                            @{ n = 'selected'; now = $selected; gold = $gold.selected })) {
            foreach ($field in @('vertices', 'indices', 'drawCommands')) {
                $nowValue = switch ($field) {
                    'vertices' { $pair.now.Vertices }
                    'indices' { $pair.now.Indices }
                    default { $pair.now.DrawCommands }
                }
                $goldValue = [int]$pair.gold.$field
                Assert ($nowValue -eq $goldValue) `
                    ("[$($pair.n)] $field 가 골든과 다르다: $goldValue → $nowValue. " +
                     '그림이 바뀐 것이 의도라면 -Update 로 골든을 갱신하고 왜 움직였는지 커밋에 적어라.')
            }
        }
        Write-Host ''
        Write-Host '  정점·인덱스·draw command 가 골든과 한 글자도 다르지 않다.'
    }
}

Write-Host ''
if ($script:skipped -gt 0) { Write-Host ("  건너뛴 판정 " + $script:skipped + ' 건') }
Write-Host ("chrome 성능 계약 OK — 단정 " + $script:checks + ' 건 · 건너뜀 ' + $script:skipped + ' 건')
exit 0

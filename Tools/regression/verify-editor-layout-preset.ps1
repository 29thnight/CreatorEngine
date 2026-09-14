[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-layout-preset')
)
# PHASE 21 W6 — 배치 preset 다섯의 **런타임** 판정.
#
# 계획서 W6 의 판정을 그대로 잰다: *"각 preset을 연속 적용해도 orphan dock node와
# off-screen floating panel이 없고, 사용자가 수정한 workspace를 preset update가
# 덮어쓰지 않는다."* 여기에 같은 절의 요구 하나를 더 잰다 — *"small window와 DPI
# 변화에서 minimum central area를 보존한다."*
#
# **이 축은 런타임이 본다.** W7-4 와 반대다. 배치가 깨진 모습은 살아 있는 도크
# 트리에 그대로 남으므로(`editor.dock` 의 고아·유령·central 판정), 소스를 맞대는
# 대신 실제로 다섯을 적용해 보고 트리를 읽는다.
#
# ── 무엇을 어떻게 유도하는가 ──────────────────────────────────────────────
#
# preset 목록을 **여기 적지 않는다.** `editor.workspace presets` 가 내는 것을 그대로
# 돈다 — 손으로 적으면 preset 을 하나 더할 때 이 파일이 낡고, 낡은 줄 모른다.
# 개수도 못 박지 않되 **둘 이상**인 것만 요구한다(빈 목록을 성공으로 읽지 않기
# 위해서다. 이 저장소가 여러 번 데인 양식이다).
#
# ── 세 회차 ──────────────────────────────────────────────────────────────
#
#  ① 하나씩. preset 을 적용하고 도크 트리를 읽는다. 고아 0 · 유령 0 · central 1 ·
#     화면 밖 떠 있는 패널 0 · 가운데가 최소치 이상.
#  ② 연속. 같은 세션에서 다섯을 잇달아 적용하고 **마지막에** 다시 읽는다.
#     한 번씩 깨끗한 것과 연달아 적용해도 깨끗한 것은 다른 문장이다 —
#     `DockBuilderRemoveNode` 가 남긴 것이 다음 회차에 얹히면 거기서 갈린다.
#  ③ 사람이 만진 배치. 패널을 하나 닫고 preset 을 적용한 뒤, `before-preset`
#     백업이 생겼는지 본다. 덮어쓰기 전에 남기는 것이 계획서의 판정 뒷절이다.
#
# ── 작은 창 ───────────────────────────────────────────────────────────────
#
# ①을 두 크기에서 돈다. 넓은 창에서는 비율이 그대로 통하고, 좁은 창에서는
# 최소 중앙 보존이 실제로 옆과 아래를 줄여야 한다. 좁은 쪽이 없으면 그 코드가
# 한 번도 실행되지 않는다.
#
# **못 잡는 것.** 픽셀을 보지 않는다. 노드가 제자리에 서 있어도 그 안의 그림이
# 옳은지는 W8 visual golden 의 몫이다. DPI 축도 여기서는 창 크기로만 대신한다 —
# 모니터 배율을 바꾸는 것은 이 하네스가 할 수 없다.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw '이미 떠 있는 에디터를 닫아라 — endpoint 파일은 프로젝트에 하나다.'
}

$script:checks = 0
# 최소 중앙 보존 축이 **실제로 실행된 회차**의 수. 0 이면 이 게이트는 그 축을
# 한 번도 재지 않은 것이고, 그 사실이 보고서에 적혀야 한다 — 건너뛴 단정을 조용히
# 두면 "전부 통과" 가 그 축까지 덮는 것처럼 읽힌다.
$script:centralChecked = 0
$script:centralSkipped = 0
$script:lastScale = 0.0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}

# 최소 중앙 영역의 수는 **여기 적지 않는다.** 전에는 480/300 을 이 파일에 베껴
# 두고 배율을 1.0 으로 가정했는데, 그러면 두 가지가 한꺼번에 틀린다 — 제품 상수와
# 두 벌이 되고(이 조각이 없애려던 바로 그 문제다), 배율이 1 보다 크면 단정이 제품
# 규칙보다 **낮은 자리**에 서서 위반을 통과시킨다. 이제 `editor.dock` 이 배율을
# 곱한 실효 하한을 직접 싣는다(`minCentralWidth`/`minCentralHeight`).

function Invoke-Editor([string]$tag, [string[]]$lines) {
    $dir = Join-Path $Work $tag
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $scriptPath = Join-Path $dir 'script.txt'
    $resultPath = Join-Path $dir 'result.jsonl'
    Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value ($lines + @('quit'))

    $workspaceDir = Join-Path $dir 'workspace'
    New-Item -ItemType Directory -Force -Path $workspaceDir | Out-Null
    $savedWorkspaceDir = $env:CREATOR_EDITOR_WORKSPACE_DIR
    $savedLegacyIni = $env:CREATOR_EDITOR_LEGACY_INI
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $workspaceDir
    $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $workspaceDir 'none.ini'
    try {
        $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru `
            -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                            '--result-file', ('"' + $resultPath + '"')) `
            -RedirectStandardOutput (Join-Path $dir 'out.txt') `
            -RedirectStandardError (Join-Path $dir 'err.txt')
        if (-not $proc.WaitForExit(600000)) { $proc.Kill(); throw "editor did not exit in time ($tag)" }
    }
    finally {
        if ($null -ne $savedWorkspaceDir) { $env:CREATOR_EDITOR_WORKSPACE_DIR = $savedWorkspaceDir }
        else { Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue }
        if ($null -ne $savedLegacyIni) { $env:CREATOR_EDITOR_LEGACY_INI = $savedLegacyIni }
        else { Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue }
    }
    if (-not (Test-Path -LiteralPath $resultPath)) { throw "result file missing ($tag)" }
    [pscustomobject]@{
        Rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } |
                 ForEach-Object { $_ | ConvertFrom-Json })
        WorkspaceDir = $workspaceDir
        Dir = $dir
    }
}

function Get-Rows($run, [string]$command) {
    @($run.Rows | Where-Object { $_.command -eq $command })
}

Write-Host ''
Write-Host '배치 preset 판정 (PHASE 21 W6)'
Write-Host ''

# ── preset 목록을 제품에서 받는다 ──────────────────────────────────────────
$listRun = Invoke-Editor 'list' @('window.resize 1600 1000', 'wait 120', 'editor.workspace presets')
$listRow = @(Get-Rows $listRun 'editor.workspace') | Select-Object -First 1
Assert ($null -ne $listRow -and $listRow.status -eq 'succeeded') 'editor.workspace presets 가 실패했다'
$presets = @($listRow.data.presets)
Assert ($presets.Count -ge 2) "preset 목록이 비었거나 하나뿐이다 (=$($presets.Count))"
$ids = @($presets | ForEach-Object { $_.id })
Assert ($listRow.data.active -eq $ids[0]) `
    "시작 preset 이 목록의 첫째가 아니다 (active=$($listRow.data.active), 첫째=$($ids[0]))"
# 첫째는 재정의가 0 이어야 한다 — "현재 외관을 유지" 가 값의 일치가 아니라
# 출처의 동일성으로 서는 자리다(EditorLayoutPreset.h).
Assert (0 -eq [int]$presets[0].overrides) `
    "기본 preset 이 재정의를 갖는다 (=$($presets[0].overrides)) — 선언과 두 벌이 된다"
Write-Host ("  제품이 내는 preset {0} 개: {1}" -f $ids.Count, ($ids -join ', '))

function Test-DockRow($row, [string]$label) {
    Assert ($null -ne $row) "$label — editor.dock 결과가 없다"
    Assert ($row.status -eq 'succeeded') "$label — 도크 감사 실패: $($row.message)"
    Assert ([int]$row.data.centralNodes -eq 1) "$label — central 노드가 1 이 아니다 (=$($row.data.centralNodes))"
    Assert ([int]$row.data.undockedSlots -eq 0) "$label — 도크되지 않은 자리 $($row.data.undockedSlots) 개"
    Assert ([int]$row.data.ghostTabs -eq 0) "$label — 유령 탭 $($row.data.ghostTabs) 개"
    Assert ([int]$row.data.offscreenFloating -eq 0) `
        "$label — 화면 밖에 떠 있는 패널 $($row.data.offscreenFloating) 개 (되찾을 방법이 없다)"
    # 접힌 노드는 고아도 유령도 아니다 — 트리는 멀쩡하고 창도 붙어 있는데
    # 폭이나 높이가 0 이라 사람에게는 패널이 사라진 것으로 보인다. 이 게이트가
    # 처음 돌았을 때 900x620 에서 오른쪽 열이 정확히 그랬다.
    Assert ([int]$row.data.degenerateNodes -eq 0) `
        "$label — 폭이나 높이가 0 인 도크 잎 $($row.data.degenerateNodes) 개"
    # 그리고 크기는 멀쩡한데 **아무도 안 사는** 노드. 자리를 갈라 놓고 그 자리에
    # 들어올 창이 없으면 빈 잎이 남는다 — 계획서가 금지한 orphan dock node 다.
    Assert ([int]$row.data.emptyLeafNodes -eq 0) `
        "$label — 창이 하나도 없는 도크 잎 $($row.data.emptyLeafNodes) 개 (자리를 갈라 놓고 아무도 안 들어왔다)"
    # 제품이 실은 실효 하한을 그대로 읽는다. 값이 아예 안 실려 있으면 그것부터
    # 붉어야 한다 — 없는 필드를 0 으로 읽어 "0 이상" 을 통과시키면 눈먼 초록이다.
    # StrictMode 아래서 없는 속성을 **읽으면** 단정에 닿기도 전에 예외가 난다.
    # 그러면 게이트는 붉되 내가 쓴 문장이 아니라 PowerShell 의 오류로 붉다 —
    # 변이 증명에서 "맞는 이유로 붉은 것" 을 확인할 수 없다. 이름부터 본다.
    $have = @($row.data.PSObject.Properties.Name)
    foreach ($field in @('minCentralWidth', 'minCentralHeight', 'uiScale', 'rootWidth', 'rootHeight')) {
        Assert ($have -contains $field) "$label — editor.dock 이 $field 를 싣지 않았다"
    }
    Assert ([double]$row.data.minCentralWidth -gt 0) "$label — minCentralWidth 가 0 이다"
    Assert ([double]$row.data.uiScale -gt 0) "$label — uiScale 이 0 이다"
    $minW = [double]$row.data.minCentralWidth
    $minH = [double]$row.data.minCentralHeight

    # 다만 하한 **그 자체**는 창이 담을 수 있을 때만 요구한다. 배율이 커지면
    # 하한이 창보다 커질 수 있고(이 기계가 그렇다), 그때 제품은 규칙을 버리고
    # 비율을 그대로 쓴다 — 가운데를 지키려다 패널을 0 으로 만드는 것이 더 나쁘다.
    # 그 탈출 가지에서 하한을 요구하면 제품 규칙과 어긋난 단정이 된다.
    #
    # 전제는 **창 크기**에서 세운다. 가운데 크기에서 세우면(전에 그렇게 썼다)
    # `central + min >= 2*min` 이 되어 단정이 자기를 증명하는 동어반복이 된다.
    # 창이 최소치의 두 배는 되는데도 가운데가 최소치 미만이면 그것은 결함이다.
    $rootW = [double]$row.data.rootWidth
    $rootH = [double]$row.data.rootHeight
    Assert ($rootW -gt 0 -and $rootH -gt 0) "$label — 도크 뿌리 크기가 0 이다"
    $script:lastScale = [double]$row.data.uiScale
    if ($rootW -ge $minW * 2 -and $rootH -ge $minH * 2) {
        $script:centralChecked++
        Assert ([double]$row.data.centralWidth -ge $minW) `
            ("$label — 가운데 폭 {0:N0} < 최소 {1:N0} (배율 {2:N2})" -f `
                $row.data.centralWidth, $minW, $row.data.uiScale)
        Assert ([double]$row.data.centralHeight -ge $minH) `
            ("$label — 가운데 높이 {0:N0} < 최소 {1:N0} (배율 {2:N2})" -f `
                $row.data.centralHeight, $minH, $row.data.uiScale)
    }
    else { $script:centralSkipped++ }
}

# ── ① 하나씩 — 두 창 크기에서 ──────────────────────────────────────────────
#
# 좁은 쪽이 있어야 최소 중앙 보존 코드가 실제로 실행된다. 배율은 1.0 으로 본다 —
# 실제 배율이 그보다 크면 가운데도 함께 커지므로 하한 판정은 그대로 선다.
foreach ($size in @(@(1600, 1000), @(900, 620))) {
    foreach ($id in $ids) {
        $tag = "one-$($size[0])x$($size[1])-$id"
        $run = Invoke-Editor $tag @(
            "window.resize $($size[0]) $($size[1])",
            'wait 150',
            "editor.workspace preset $id",
            'wait 150',
            'editor.workspace',
            'editor.dock')
        # 적용은 **대기열에 들어간다.** 명령이 곧바로 내는 status 는 아직 이전
        # 것이므로(pending=true), 한 프레임 뒤에 다시 물어야 무엇이 섰는지 안다.
        # 처음 이 단정을 바로 앞 행에 걸었다가 게이트가 정확히 여기서 붉었다.
        $rows = @(Get-Rows $run 'editor.workspace')
        Assert ($rows.Count -eq 2) "$tag — editor.workspace 결과가 $($rows.Count) 개다"
        Assert ($rows[0].status -eq 'succeeded') "$tag — preset 적용 실패: $($rows[0].message)"
        $applied = $rows[1]
        Assert ($applied.data.preset -eq $id) `
            "$tag — 적용 뒤 active 가 $($applied.data.preset) 다"
        Assert (-not $applied.data.pending) "$tag — 적용이 끝나지 않았다"
        Assert ([string]::IsNullOrEmpty($applied.data.error)) "$tag — $($applied.data.error)"
        Test-DockRow (@(Get-Rows $run 'editor.dock') | Select-Object -First 1) $tag
        Write-Host ("  [{0,4}x{1,-4}] {2,-16} nodes={3} central={4:N0}x{5:N0}" -f `
            $size[0], $size[1], $id,
            (@(Get-Rows $run 'editor.dock')[0].data.nodes),
            (@(Get-Rows $run 'editor.dock')[0].data.centralWidth),
            (@(Get-Rows $run 'editor.dock')[0].data.centralHeight))
    }
}

# ── ② 연속 적용 ────────────────────────────────────────────────────────────
$chain = @('window.resize 1600 1000', 'wait 150')
foreach ($id in $ids) { $chain += "editor.workspace preset $id"; $chain += 'wait 100' }
$chain += 'wait 150'
$chain += 'editor.workspace'
$chain += 'editor.dock'
$chainRun = Invoke-Editor 'chain' $chain
$chainApplies = @(Get-Rows $chainRun 'editor.workspace')
Assert ($chainApplies.Count -eq $ids.Count + 1) `
    "연속 적용 결과가 $($chainApplies.Count) 개로 기대($($ids.Count + 1))와 다르다"
foreach ($row in $chainApplies) {
    Assert ($row.status -eq 'succeeded') "연속 적용 중 실패: $($row.message)"
}
$chainLast = $chainApplies[$chainApplies.Count - 1]
Assert ($chainLast.data.preset -eq $ids[$ids.Count - 1]) `
    "연속 적용 뒤 active 가 $($chainLast.data.preset) 로 마지막 preset 과 다르다"
Test-DockRow (@(Get-Rows $chainRun 'editor.dock') | Select-Object -First 1) 'chain'
Write-Host ("  [연속 {0} 회] 마지막 트리도 깨끗하다" -f $ids.Count)

# ── ③ 사람이 만진 배치를 덮어쓰지 않는다 ──────────────────────────────────
#
# 패널을 하나 닫고(사용자 수정) preset 을 적용한다. 적용 전에 `before-preset`
# 백업이 남아야 한다 — 남지 않으면 사람이 만든 배치가 조용히 사라진다.
$editTag = 'user-edit'
$lastId = $ids[$ids.Count - 1]
$editRun = Invoke-Editor $editTag @(
    'window.resize 1600 1000',
    'wait 150',
    'editor.workspace close ###Editor.AssetBundle',
    'wait 150',
    "editor.workspace preset $lastId",
    'wait 150',
    'editor.dock')
$closed = @(Get-Rows $editRun 'editor.workspace') | Select-Object -First 1
Assert ($closed.status -eq 'succeeded') "패널 닫기 실패: $($closed.message)"
$backups = @(Get-ChildItem -LiteralPath $editRun.WorkspaceDir -Filter '*before-preset*' -ErrorAction SilentlyContinue)
Assert ($backups.Count -ge 1) `
    'preset 적용이 `before-preset` 백업을 남기지 않았다 — 사람이 만진 배치가 덮이면 되돌릴 것이 없다'
Assert ($backups[0].Length -gt 0) 'before-preset 백업이 비어 있다'
Test-DockRow (@(Get-Rows $editRun 'editor.dock') | Select-Object -First 1) $editTag
Write-Host ("  [사용자 수정] 백업 {0} 개 남았다: {1}" -f $backups.Count, $backups[0].Name)

Write-Host ''
# 최소 중앙 보존 축이 몇 번 실제로 실행됐는지 **적는다.** 이 기계는 배율이 2.25 라
# 하한(480·2.25=1080 논리 px)이 창보다 커서 제품이 늘 탈출 가지로 간다 — 그 사실을
# 찍지 않으면 "전부 통과" 가 재지 않은 축까지 덮는 것처럼 읽힌다.
if ($script:centralChecked -eq 0) {
    Write-Host ("  [건너뜀] 최소 중앙 보존 축은 이번 실행에서 한 번도 재지 못했다 — " +
                "배율 {0:N2} 에서 하한이 창보다 크다(회차 {1} 건 전부 탈출 가지)." -f `
        $script:lastScale, $script:centralSkipped)
} else {
    Write-Host ("  최소 중앙 보존 축: {0} 회차에서 실제로 쟀다(배율 {1:N2}, 건너뜀 {2})" -f `
        $script:centralChecked, $script:lastScale, $script:centralSkipped)
}

Write-Host ("PASS verify-editor-layout-preset — {0} checks, preset {1} 종 · 창 크기 2 종 · 연속 적용 1 회" -f `
    $script:checks, $ids.Count)
exit 0

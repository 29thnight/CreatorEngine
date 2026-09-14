[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-workspace-named')
)
# PHASE 21 W6-2 — 이름 붙인 workspace 여럿.
#
# 계획서 W6 의 남은 절이다: *"Save As/Rename/Delete/Reset와 active workspace 표시를
# 만든다."* Reset 과 active 표시는 W6 이 끝냈고 여기는 **이름 붙인 배치 여럿**이다.
#
# ── 이 게이트가 지키는 문장 ───────────────────────────────────────────────
#
#  ① 이름을 붙여 저장하면 **파일 하나**가 생기고 목록에 그 이름이 나온다.
#  ② 다른 배치로 간 뒤 그 이름을 다시 열면 **저장할 때의 배치가 돌아온다.**
#     (preset 이 무엇이었는지로 판정한다 — 배치가 진짜 돌아왔는지는 도크
#     노드 수나 이름이 아니라 그 배치를 만든 preset 이 증언한다.)
#  ③ 덮어쓰기·이름 바꾸기·지우기는 전부 **되돌릴 것을 남긴다**(`before-*` 백업).
#  ④ 지금 쓰는 배치를 지워도 **화면은 그대로**다. 지우기는 파일을 지우라는
#     말이지 배치를 갈아엎으라는 말이 아니다.
#  ⑤ 파일 이름으로 쓸 수 없는 이름은 **만들 때** 거절한다. 저장할 때 실패하면
#     사람은 무엇이 문제인지 모른 채 배치를 잃는다.
#  ⑥ 한글과 공백이 든 이름이 선다. CLI 가 공백으로 자른 조각을 도로 붙이지
#     않으면 "My Layout" 을 GUI 로만 만들 수 있는 배치가 되어 표면이 갈린다.
#  ⑦ 없는 이름을 열거나 지우는 것은 **그 명령이** 실패한다. 대기열에 넣고
#     다음 프레임의 status.error 로 미루면 명령의 종료 코드가 초록이 된다.
#
# ── 못 잡는 것 ────────────────────────────────────────────────────────────
#
# 재시작을 넘지 않는다. 이름 붙인 파일이 다음 기동에서도 읽히는지는
# verify-editor-workspace.ps1 의 몫이고, 여기서는 한 세션 안의 왕복만 본다.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw '이미 떠 있는 에디터를 닫아라 — endpoint 파일은 프로젝트에 하나다.'
}

$script:checks = 0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}

# `-Reuse` 는 앞 회차가 남긴 workspace 디렉터리를 그대로 물린다. 에디터가 꺼진
# 사이에 파일을 손으로 놓아 두고 다시 띄우는 회차가 필요해서다 — 사람이 배치
# 파일을 복사해 두는 일은 실제로 일어나고, 그때 파일 안의 이름은 남의 것을
# 가리킨다.
function Invoke-Editor([string]$tag, [string[]]$lines, [switch]$Reuse) {
    $dir = Join-Path $Work $tag
    if (-not $Reuse -and (Test-Path -LiteralPath $dir)) { Remove-Item -LiteralPath $dir -Recurse -Force }
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
        Rows = @(Get-Content -LiteralPath $resultPath -Encoding UTF8 | Where-Object { $_.Trim() } |
                 ForEach-Object { $_ | ConvertFrom-Json })
        WorkspaceDir = $workspaceDir
        Dir = $dir
    }
}

function Get-Workspace($run) { @($run.Rows | Where-Object { $_.command -eq 'editor.workspace' }) }

# 결과 행의 순서는 시나리오가 정한다. 이름이 아니라 **몇 번째 editor.workspace
# 인가**로 집는다 — 같은 명령을 여러 번 부르므로 이름으로는 못 가른다.
function Step($rows, [int]$index) {
    Assert ($rows.Count -gt $index) "editor.workspace 결과가 $($rows.Count) 건뿐이다 (>$index 필요)"
    $rows[$index]
}

function Names($row) {
    if ($row.data.PSObject.Properties.Name -notcontains 'workspaces') { return @() }
    @($row.data.workspaces)
}

Write-Host ''
Write-Host '이름 붙인 workspace 판정 (PHASE 21 W6-2)'
Write-Host ''

# ── ① 만들기 · 목록 · 왕복 ────────────────────────────────────────────────
#
# 시작은 빈 목록이어야 한다. 비어 있지 않으면 격리가 깨진 것이고, 그 상태로
# 이어 가면 뒤의 단정이 남의 파일을 재게 된다.
$plainName = 'Lighting Pass'
$koreanName = '조명 작업'
$round = Invoke-Editor 'roundtrip' @(
    'window.resize 1400 900', 'wait 120',
    'editor.workspace list',                                   # 0 — 비어 있어야 한다
    "editor.workspace saveas $plainName", 'wait 90',           # 1
    'editor.workspace list',                                   # 2 — 하나
    'editor.workspace preset legacy_unity', 'wait 150',        # 3
    "editor.workspace saveas $koreanName", 'wait 90',          # 4
    'editor.workspace list',                                   # 5 — 둘
    'editor.workspace preset ui_editing', 'wait 150',          # 6
    'editor.workspace',                                        # 7 — 지금은 ui_editing
    "editor.workspace load $plainName", 'wait 180',            # 8
    'editor.workspace',                                        # 9 — 저장할 때의 preset 으로 돌아와야 한다
    "editor.workspace load $koreanName", 'wait 180',           # 10
    'editor.workspace'                                         # 11 — legacy_unity
)
$rows = Get-Workspace $round

$first = Step $rows 0
Assert ($first.status -eq 'succeeded') "editor.workspace list 가 실패했다: $($first.message)"
Assert (@(Names $first).Count -eq 0) `
    "격리가 깨졌다 — 시작부터 이름 붙인 배치가 $(@(Names $first).Count) 개 있다"

Assert ((Step $rows 1).status -eq 'succeeded') "saveas 가 실패했다: $((Step $rows 1).message)"
$afterFirst = @(Names (Step $rows 2))
Assert ($afterFirst.Count -eq 1 -and $afterFirst[0] -eq $plainName) `
    "저장 뒤 목록이 '$plainName' 하나가 아니다: [$($afterFirst -join ', ')]"
# 공백이 든 이름이 **파일 하나**로 선다. 조각으로 잘리면 여기서 둘이 된다.
Assert (Test-Path -LiteralPath (Join-Path $round.WorkspaceDir "$plainName.workspace")) `
    "'$plainName.workspace' 파일이 없다 — 공백이 든 이름이 파일로 서지 않았다"

Assert ((Step $rows 4).status -eq 'succeeded') "한글 이름 저장이 실패했다: $((Step $rows 4).message)"
$afterSecond = @(Names (Step $rows 5))
Assert ($afterSecond.Count -eq 2) "목록이 둘이 아니다: [$($afterSecond -join ', ')]"
Assert ($afterSecond -contains $koreanName) `
    "한글 이름이 목록에 없다: [$($afterSecond -join ', ')]"
Assert (Test-Path -LiteralPath (Join-Path $round.WorkspaceDir "$koreanName.workspace")) `
    "'$koreanName.workspace' 파일이 없다 — 한글 이름이 파일 이름으로 깨졌다"

# 왕복의 판정은 **preset** 으로 한다. 이름만 보면 이름을 바꿔 적기만 해도
# 통과하므로, 저장할 때의 배치가 실제로 돌아왔는지는 그 배치를 만든 preset 이
# 증언해야 한다.
$away = Step $rows 7
Assert ($away.data.preset -eq 'ui_editing') `
    "다른 배치로 옮기지 못했다 (preset=$($away.data.preset))"
$backPlain = Step $rows 9
Assert ($backPlain.data.name -eq $plainName) `
    "'$plainName' 을 열었는데 active 이름이 '$($backPlain.data.name)' 이다"
Assert ($backPlain.data.preset -eq 'sbox_compact') `
    "'$plainName' 은 sbox_compact 에서 저장했는데 preset 이 '$($backPlain.data.preset)' 로 돌아왔다"
Assert ([bool]$backPlain.data.named) "열어 둔 배치가 named 로 표시되지 않는다"
$backKorean = Step $rows 11
Assert ($backKorean.data.name -eq $koreanName) `
    "'$koreanName' 을 열었는데 active 이름이 '$($backKorean.data.name)' 이다"
Assert ($backKorean.data.preset -eq 'legacy_unity') `
    "'$koreanName' 은 legacy_unity 에서 저장했는데 preset 이 '$($backKorean.data.preset)' 다"

# 이름 붙인 것을 여는 것도 덮어쓰기다 — 되돌릴 것이 남아야 한다.
$loadBackups = @(Get-ChildItem -LiteralPath $round.WorkspaceDir -Filter '*before-load*' -ErrorAction SilentlyContinue)
Assert ($loadBackups.Count -ge 1) `
    '이름 붙인 배치를 여는데 before-load 백업이 없다 — 열기 전 배치를 되돌릴 방법이 없다'
Write-Host ("  [왕복] 저장 2 · 다시 열기 2 · preset 으로 판정 · before-load 백업 {0} 개" -f $loadBackups.Count)

# ── ② 덮어쓰기 · 이름 바꾸기 · 지우기 ─────────────────────────────────────
$renamed = 'Lighting Pass v2'
$edit = Invoke-Editor 'edit' @(
    'window.resize 1400 900', 'wait 120',
    "editor.workspace saveas $plainName", 'wait 90',           # 0
    "editor.workspace saveas $plainName", 'wait 90',           # 1 — 같은 이름으로 다시
    "editor.workspace rename $renamed", 'wait 120',            # 2
    'editor.workspace list',                                   # 3
    'editor.workspace',                                        # 4 — 이름이 따라왔는가
    "editor.workspace delete $renamed", 'wait 120',            # 5
    'editor.workspace list',                                   # 6 — 비었는가
    'editor.workspace',                                        # 7 — 화면은 그대로인가
    'editor.dock'                                              # 8
)
$erows = Get-Workspace $edit

Assert ((Step $erows 1).status -eq 'succeeded') "같은 이름으로 다시 저장이 실패했다: $((Step $erows 1).message)"
$overwrite = @(Get-ChildItem -LiteralPath $edit.WorkspaceDir -Filter '*before-overwrite*' -ErrorAction SilentlyContinue)
Assert ($overwrite.Count -ge 1) `
    '같은 이름으로 덮어썼는데 before-overwrite 백업이 없다 — 이전 배치가 조용히 사라진다'

Assert ((Step $erows 2).status -eq 'succeeded') "rename 이 실패했다: $((Step $erows 2).message)"
$afterRename = @(Names (Step $erows 3))
Assert ($afterRename.Count -eq 1 -and $afterRename[0] -eq $renamed) `
    "이름을 바꾼 뒤 목록이 '$renamed' 하나가 아니다: [$($afterRename -join ', ')]"
Assert (-not (Test-Path -LiteralPath (Join-Path $edit.WorkspaceDir "$plainName.workspace"))) `
    "이름을 바꿨는데 옛 파일 '$plainName.workspace' 가 남아 있다 — 사본이 둘이 된다"
Assert ((Step $erows 4).data.name -eq $renamed) `
    "이름을 바꿨는데 active 이름이 '$((Step $erows 4).data.name)' 이다"

Assert ((Step $erows 5).status -eq 'succeeded') "delete 가 실패했다: $((Step $erows 5).message)"
$afterDelete = @(Names (Step $erows 6))
Assert ($afterDelete.Count -eq 0) "지운 뒤에도 목록에 $($afterDelete.Count) 개 남았다: [$($afterDelete -join ', ')]"
Assert (-not (Test-Path -LiteralPath (Join-Path $edit.WorkspaceDir "$renamed.workspace"))) `
    "지웠는데 '$renamed.workspace' 가 남아 있다"
$deleteBackups = @(Get-ChildItem -LiteralPath $edit.WorkspaceDir -Filter '*before-delete*' -ErrorAction SilentlyContinue)
Assert ($deleteBackups.Count -ge 1) `
    '지우기 전에 백업을 남기지 않았다 — 지운 배치를 되돌릴 다른 방법이 없다'
Assert ($deleteBackups[0].Length -gt 0) 'before-delete 백업이 비어 있다'

# 지금 쓰는 배치를 지웠다. **화면은 그대로여야 한다** — 지우기는 파일을 지우라는
# 말이지 배치를 갈아엎으라는 말이 아니다.
$afterSelfDelete = Step $erows 7
Assert (-not [bool]$afterSelfDelete.data.named) `
    '지운 이름이 아직 named 로 표시된다 — 없는 파일을 가리킨다'
$dock = @($edit.Rows | Where-Object { $_.command -eq 'editor.dock' }) | Select-Object -First 1
Assert ($null -ne $dock -and $dock.status -eq 'succeeded') '지운 뒤 editor.dock 이 실패했다'
Assert ([int]$dock.data.centralNodes -eq 1) `
    "지운 뒤 central 노드가 1 이 아니다 (=$($dock.data.centralNodes)) — 배치가 갈아엎였다"
Assert ([int]$dock.data.undockedSlots -eq 0 -and [int]$dock.data.ghostTabs -eq 0) `
    '지운 뒤 도크 트리가 깨졌다'
Write-Host ("  [편집] 덮어쓰기·이름 바꾸기·지우기 — 백업 before-overwrite {0} · before-delete {1}" -f `
    $overwrite.Count, $deleteBackups.Count)

# ── ③ 거절해야 하는 이름과 없는 이름 ──────────────────────────────────────
#
# 전부 **그 명령이** 실패해야 한다. 대기열에 넣고 다음 프레임의 status.error 로
# 미루면 명령의 종료 코드가 초록이 되어 아무도 못 본다.
$reject = Invoke-Editor 'reject' @(
    'window.resize 1400 900', 'wait 120',
    'editor.workspace saveas active',          # 0 — 활성 파일과 같은 자리
    'editor.workspace saveas CON',             # 1 — 장치 이름
    'editor.workspace saveas nul',             # 2 — 대소문자 무관
    'editor.workspace saveas bad/name',        # 3 — 경로 구분자
    'editor.workspace saveas bad:name',        # 4 — Windows 예약 문자
    'editor.workspace saveas .hidden',         # 5 — 앞의 점
    'editor.workspace load 없는배치',            # 6 — 없는 것을 연다
    'editor.workspace delete 없는배치',          # 7 — 없는 것을 지운다
    'editor.workspace rename 새이름',            # 8 — 저장된 적 없는데 이름 바꾸기
    'editor.workspace list'                    # 9 — 아무것도 안 생겼어야 한다
)
$rrows = Get-Workspace $reject
$rejectLabels = @(
    "'active' 예약", '장치 이름 CON', '장치 이름 nul(소문자)', '경로 구분자',
    'Windows 예약 문자', '앞의 점', '없는 배치 열기', '없는 배치 지우기', '이름 없는데 rename')
for ($i = 0; $i -lt $rejectLabels.Count; $i++) {
    $row = Step $rrows $i
    Assert ($row.status -ne 'succeeded') `
        "$($rejectLabels[$i]) 이 통과했다 — status=$($row.status) message=$($row.message)"
    Assert (-not [string]::IsNullOrWhiteSpace($row.message)) `
        "$($rejectLabels[$i]) 이 거절됐는데 이유가 비어 있다"
}
$leftovers = @(Names (Step $rrows 9))
Assert ($leftovers.Count -eq 0) `
    "거절돼야 할 이름이 파일로 남았다: [$($leftovers -join ', ')]"
$stray = @(Get-ChildItem -LiteralPath $reject.WorkspaceDir -Filter '*.workspace' -ErrorAction SilentlyContinue |
           Where-Object { $_.BaseName -ne 'active' })
Assert ($stray.Count -eq 0) `
    "거절돼야 할 이름의 파일이 디스크에 남았다: [$(($stray | ForEach-Object Name) -join ', ')]"
Write-Host ("  [거절] {0} 가지 전부 그 명령에서 실패 · 남은 파일 0" -f $rejectLabels.Count)

# ── ④ 복사해 둔 파일 — 정본은 **파일 이름**이다 ──────────────────────────
#
# 사람이 배치 파일을 복사해 두는 일은 실제로 일어난다. 그때 파일 안에 적힌 이름은
# 원본의 것이라 남을 가리킨다. 둘 중 무엇을 믿을지는 정해져 있어야 하고, 정본은
# 파일 이름이다 — 목록에 보이는 것이 파일 이름이므로 안쪽을 믿으면 "목록에서 고른
# 것과 열린 것의 이름이 다른" 상태가 된다.
$sourceName = 'Copy Source'
$copyName = 'Copied Layout'
$copyRun = Invoke-Editor 'copied' @(
    'window.resize 1400 900', 'wait 120',
    'editor.workspace preset rendering_debug', 'wait 150',
    "editor.workspace saveas $sourceName", 'wait 120'
)
Assert ((Step (Get-Workspace $copyRun) 0).status -eq 'succeeded') '복사 회차의 preset 적용이 실패했다'
$sourceFile = Join-Path $copyRun.WorkspaceDir "$sourceName.workspace"
Assert (Test-Path -LiteralPath $sourceFile) "복사할 원본 '$sourceName.workspace' 가 없다"
# 에디터가 꺼진 사이에 놓는다. 파일 안의 `name` 은 여전히 원본의 것이다.
Copy-Item -LiteralPath $sourceFile -Destination (Join-Path $copyRun.WorkspaceDir "$copyName.workspace")
$copyBack = Invoke-Editor 'copied' @(
    'window.resize 1400 900', 'wait 120',
    'editor.workspace list',                         # 0 — 둘 다 보인다
    "editor.workspace load $copyName", 'wait 180',   # 1
    'editor.workspace'                               # 2 — 어느 이름으로 열렸는가
) -Reuse
$crows = Get-Workspace $copyBack
$listed = @(Names (Step $crows 0))
Assert ($listed.Count -eq 2 -and ($listed -contains $copyName)) `
    "복사해 둔 파일이 목록에 안 보인다: [$($listed -join ', ')]"
$opened = Step $crows 2
Assert ($opened.data.name -eq $copyName) `
    ("복사본을 열었는데 active 이름이 '{0}' 이다 — 파일 안의 이름을 믿으면 목록에서 고른 것과 열린 것이 갈린다" -f $opened.data.name)
Assert ([bool]$opened.data.named) '복사본을 열었는데 named 로 표시되지 않는다'
Assert ($opened.data.preset -eq 'rendering_debug') `
    "복사본의 배치가 따라오지 않았다 (preset=$($opened.data.preset))"
Write-Host "  [복사본] 파일 이름이 정본 — '$copyName' 으로 열렸고 배치는 원본의 것"

Write-Host ''
Write-Host ("PASS verify-editor-workspace-named — {0} checks, 에디터 5 회 기동" -f $script:checks)
exit 0

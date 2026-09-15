[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-editor-integration-matrix')
)
# PHASE 21 W8-3 — 통합 매트릭스. 축을 가로지르고, 그 위에 W8 의 판정을 얹는다.
#
# 계획서 W8 의 판정은 *"DX12 · DPI · 재시작 · 손상 ini · Play 왕복 · Game Preview ·
# preset 조합에서 검증 레이어 오류/비정상 종료 0"* 이다. 축마다 게이트는 이미 있다 —
# resize 는 `verify-editor-viewport-extent`, preset 은 `verify-editor-layout-preset`,
# 재시작·손상은 `verify-editor-workspace`, DPI 는 `verify-editor-theme`, Play 는
# `verify-play-roundtrip`. **이 파일이 새로 재는 것은 그 축들이 아니라 판정이다.**
#
# ── 판정이 잴 수 없는 것이었다 ────────────────────────────────────────────
#
# "검증 레이어 오류 0" 을 **출하 구성에서 읽을 수단이 없었다.**
# `DX12DeviceResources::DrainDebugMessages` 가 통째로 `#if defined(_DEBUG)` 였고
# 라이브 경로의 호출부 둘도 같은 가드 안이었다. 그래서
# `CREATOR_DX12_VALIDATION=basic` 으로 레이어를 켜면 디버그 레이어의 비용만 내고
# **아무도 큐를 읽지 않았다.** 그 상태에서 "오류 0" 은 빈 집합을 성공으로 읽는
# 것이다 — W8-1 의 `IMGUI_CHECKVERSION()` 과 똑같은 모양이다.
#
# W8-3 이 그 가드를 걷고 `rhi::validation` 장부를 세웠다. 이 게이트는 그 장부를
# `dx12.validation` 으로 읽어 축마다 판정한다.
#
# ── 눈먼 초록을 막는 세 장치 ──────────────────────────────────────────────
#
#  ① `layerEnabled` 를 **먼저** 본다. 레이어가 꺼져 있으면 `problems == 0` 은
#     아무것도 증명하지 않는다. 판정은 `problems == 0` 하나가 아니라
#     `layerEnabled == true` · `mode == basic` · `drains > 0` · `problems == 0`
#     넷이 함께 서야 성립한다.
#
#  ② **대조군 세션**을 함께 돈다(`control-off`). 같은 이진, 같은 스크립트,
#     환경 변수만 `CREATOR_DX12_VALIDATION=off`. 여기서 `layerEnabled` 가 거짓으로
#     나와야 한다. 이것이 없으면 `layerEnabled` 가 늘 참인 상수여도 게이트는
#     초록이다 — 대조군은 독립 유도를 가져야 한다는 그 규칙이다.
#
#  ③ 축마다 `dx12.validation reset` 으로 수를 비운다. 그래야 문제가 났을 때
#     **어느 축**에서 났는지 말할 수 있다. 한 번만 재면 "어딘가에서 났다" 뿐이다.
#
# ── 못 잡는 것 ────────────────────────────────────────────────────────────
#
#  · Vulkan. `rhi::validation` 장부는 백엔드 중립이지만 싣는 쪽이 DX12 뿐이다.
#    Vulkan 은 별도 보류이고 W8 의 판정문도 "현재 범위인 DX12" 라고 적는다.
#  · 픽셀. 배치가 서 있어도 그림이 옳은지는 `verify-editor-chrome-golden` 의 몫이다.
#  · 실제 모니터 DPI. 이 하네스는 모니터 배율을 바꿀 수 없어 user scale 로만 민다
#    (`verify-editor-theme` 와 같은 자리).
#  · Vulkan 백엔드의 검증 레이어. 장부에 실는 자리가 DX12 쪽뿐이다.
#  · 마지막 `dx12.validation` 뒤에 — 즉 해체 중에 — 나는 오류. 장부를 읽는
#    시각이 종료 앞이라 그 구간은 이 게이트의 밖이다. 종료 코드만 본다.
#
# ── 변이로 증명한 것 (2026-09-15 · Release) ────────────────────────────
#
#  ① `DrainDebugMessages` 를 다시 Release 에서 무력화 → **잡았다.**
#     `surface/boot : 큐를 한 번도 비우지 않았다(drains=0)` — 이 조각이 고친 바로 그 결함이다.
#  ② `declare_layer` 가 끈 실행도 켜진 것으로 적게 함 → **잡았다.**
#     `control-off: 레이어를 꺼는데 layerEnabled 가 참이다` — 대조군이 제 일을 한다.
#  ③ 디바이스 생성 직후 UPLOAD 힙 자원을 `RENDER_TARGET` 상태로 만드는
#     줄 하나 → **잡았다.** problems 2 건 · retained 에 해당 ERROR 두 줄
#     (디바이스가 둘이라 두 번 난다). 이것이 판정문의 이빨이다.
#  ④ ③ 위에 `Cmd_dx12_validation` 의 `Fail` 길을 닫아 제품이 성공으로 보고하게
#     함 → **잡았다.** 게이트 자신의 `problems == 0` 이 따로 붉었다. 제품과
#     게이트 두 팔을 따로 친 것이 이 둘이다.
#
#  ★ ③가 `surface/boot` 에서 잡힐 수 있게 된 것은 변이를 **설계하다가** 게이트의
#    구멍을 먼저 찾았기 때문이다. 처음엔 축마다 `reset` 으로 시작했는데, 그랬면
#    부팅 구간의 오류를 첫 reset 이 지워 버렸다 — DX12 오류가 가장 잘 나는 구간이다.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$settingsPath = Join-Path $repoRoot 'Dynamic_CPP/ProjectSetting/EngineSettings.asset'
$fixtureDir = Join-Path $PSScriptRoot 'fixtures/imgui-ini'

if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
if (-not (Test-Path -LiteralPath $settingsPath)) { throw "Project settings not found: $settingsPath" }
if (-not (Test-Path -LiteralPath $fixtureDir)) { throw "ini fixture directory is missing: $fixtureDir" }
# 프로젝트에 endpoint 파일은 하나다. 개발자가 띄워 둔 에디터를 죽이지 않는다.
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw '이미 떠 있는 에디터를 닫아라 — endpoint 파일은 프로젝트에 하나다.'
}

if (Test-Path -LiteralPath $Work) { Remove-Item -LiteralPath $Work -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$script:checks = 0
$script:axes = 0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}

# ── 세션 하나 = 폴더 하나 ─────────────────────────────────────────────────
#
# workspace 도 legacy ini 도 그 폴더 안에만 생긴다. 재시작 축만 폴더를 일부러
# 공유한다 — 그것이 그 축의 내용이기 때문이다.
function Invoke-Session {
    param(
        [string]$Tag,
        [string[]]$Lines,
        [string]$WorkspaceDir = '',
        [string]$LegacyIni = '',
        [string]$Validation = 'basic'
    )
    $dir = Join-Path $Work $Tag
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    if ([string]::IsNullOrEmpty($WorkspaceDir)) {
        $WorkspaceDir = Join-Path $dir 'workspace'
        New-Item -ItemType Directory -Force -Path $WorkspaceDir | Out-Null
    }
    if ([string]::IsNullOrEmpty($LegacyIni)) { $LegacyIni = Join-Path $WorkspaceDir 'legacy.ini' }

    $scriptPath = Join-Path $dir 'script.txt'
    $resultPath = Join-Path $dir 'result.jsonl'
    $stdoutPath = Join-Path $dir 'out.txt'
    $stderrPath = Join-Path $dir 'err.txt'
    Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value ($Lines + @('quit'))

    $priorWorkspace = $env:CREATOR_EDITOR_WORKSPACE_DIR
    $priorLegacy = $env:CREATOR_EDITOR_LEGACY_INI
    $priorValidation = $env:CREATOR_DX12_VALIDATION
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $WorkspaceDir
    $env:CREATOR_EDITOR_LEGACY_INI = $LegacyIni
    $env:CREATOR_DX12_VALIDATION = $Validation
    try {
        $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
            -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                            '--result-file', ('"' + $resultPath + '"')) `
            -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
        if (-not $proc.WaitForExit(600000)) {
            $proc.Kill()
            throw "에디터가 600 초 안에 끝나지 않았다 ($Tag)."
        }
    }
    finally {
        if ($null -ne $priorWorkspace) { $env:CREATOR_EDITOR_WORKSPACE_DIR = $priorWorkspace }
        else { Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue }
        if ($null -ne $priorLegacy) { $env:CREATOR_EDITOR_LEGACY_INI = $priorLegacy }
        else { Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue }
        if ($null -ne $priorValidation) { $env:CREATOR_DX12_VALIDATION = $priorValidation }
        else { Remove-Item Env:CREATOR_DX12_VALIDATION -ErrorAction SilentlyContinue }
    }

    # ★ 결과 줄은 **순서 있는 배열**로 든다. 이름으로 색인하는 해시에 담으면
    #   같은 명령을 축마다 여러 번 부르는 이 게이트에서 앞의 것이 뒤에 덮인다.
    $rows = @()
    if (Test-Path -LiteralPath $resultPath) {
        $rows = @(Get-Content -LiteralPath $resultPath |
                  Where-Object { $_.Trim().Length -gt 0 } |
                  ForEach-Object { $_ | ConvertFrom-Json })
    }
    [pscustomobject]@{
        Tag          = $Tag
        ExitCode     = $proc.ExitCode
        Rows         = $rows
        Result       = $resultPath
        Stdout       = $stdoutPath
        WorkspaceDir = $WorkspaceDir
        Dir          = $dir
    }
}

function Get-Rows($Session, [string]$Command) {
    @($Session.Rows | Where-Object { $_.command -eq $Command })
}

# 한 세션에서 같은 명령의 n 번째 결과. 0 부터 센다.
function Get-Row($Session, [string]$Command, [int]$Index = 0) {
    $rows = Get-Rows $Session $Command
    if ($rows.Count -le $Index) {
        throw "$($Session.Tag): $Command 의 $Index 번째 결과가 없다 (총 $($rows.Count) 개, 종료 코드 $($Session.ExitCode); $($Session.Stdout))"
    }
    $rows[$Index]
}

# ── W8 판정 ──────────────────────────────────────────────────────────────
#
# 넷이 함께 서야 한다. `problems == 0` 만 보면 레이어가 꺼진 실행도 통과한다.
function Assert-ValidationClean($Row, [string]$Where) {
    Assert ($Row.status -eq 'succeeded') "$Where : dx12.validation 이 실패했다 — $($Row.message)"
    Assert ($Row.data.layerEnabled -eq $true) `
        "$Where : 검증 레이어가 붙지 않았다(layerEnabled=false, mode=$($Row.data.mode)). 이 실행의 '오류 0' 은 아무것도 뜻하지 않는다"
    Assert ($Row.data.mode -eq 'basic') `
        "$Where : 검증 모드가 basic 이 아니다(=$($Row.data.mode)). CREATOR_DX12_VALIDATION 이 제품에 닿지 않았다"
    Assert ($Row.data.devices -ge 1) "$Where : 레이어를 켜고 만든 디바이스가 0 이다"
    Assert ($Row.data.drains -gt 0) `
        "$Where : 큐를 한 번도 비우지 않았다(drains=0). 드레인 호출부가 다시 닫혔을 수 있다"
    $detail = ''
    if (@($Row.data.retained).Count -gt 0) { $detail = ' — ' + (@($Row.data.retained) -join ' | ') }
    Assert ($Row.data.problems -eq 0) `
        "$Where : 검증 레이어가 문제 $($Row.data.problems) 건을 말했다(메시지 $($Row.data.messages) 건)$detail"
    $script:axes++
    [pscustomobject]@{ Axis = $Where; Drains = $Row.data.drains; Messages = $Row.data.messages }
}

function Assert-DockClean($Row, [string]$Where) {
    Assert ($Row.status -eq 'succeeded') "$Where : editor.dock 이 실패했다 — $($Row.message)"
    Assert ($Row.code -ne 'editor.dock.no_frame') "$Where : 프레임을 한 장도 못 냈다"
    Assert ($Row.data.clean -eq $true) "$Where : 도크 감사가 더럽다 — $($Row.message)"
    Assert ($Row.data.undockedSlots -eq 0) "$Where : 도크 자리를 잃은 창이 $($Row.data.undockedSlots) 개"
    Assert ($Row.data.ghostTabs -eq 0) "$Where : 선언에 없는 창 id 를 든 탭이 $($Row.data.ghostTabs) 개"
    Assert ($Row.data.centralNodes -eq 1) "$Where : 중앙 노드가 $($Row.data.centralNodes) 개(1 이어야 한다)"
    Assert ($Row.data.binaryMatchesHeader -eq $true) `
        "$Where : ImGui 헤더와 런타임 판이 갈렸다(header=$($Row.data.imguiHeaderVersion) runtime=$($Row.data.imguiRuntimeVersion))"
}

function Assert-CleanExit($Session) {
    # ★ 종료 코드 단정은 그 세션의 내용 단정을 **전부 지난 뒤**다. 앞에 세우면
    #   배치 러너의 exit 4 한 줄이 뒤의 열몇 단정을 통째로 가려, 붉은 줄이
    #   고칠 자리를 가리키지 못한다(9-11 에 그렇게 데었다).
    Assert ($Session.ExitCode -eq 0) `
        "$($Session.Tag): 에디터가 종료 코드 $($Session.ExitCode) 로 끝났다 — 비정상 종료 0 이 판정이다. $($Session.Stdout)"
}

$report = [Collections.Generic.List[object]]::new()

Write-Host ''
Write-Host '통합 매트릭스 (PHASE 21 W8-3)'
Write-Host ''

# ── ① 한 세션 안의 표면 왕복 — resize · Game Preview · Play ──────────────
#
# 셋을 한 세션에 두는 것이 요점이다. 축마다 따로 띄우면 "각각은 괜찮다" 만
# 말하고, 스왑체인을 다시 만든 뒤에 표시 타깃을 바꾸고 그 위에서 재생에 드는
# 조합은 한 번도 서지 않는다.
$surfaceLines = @(
    'window.resize 1600 1000'
    'wait 120'
    # ★ 첫 독서는 **reset 앞**이다. 디바이스 생성 · 스왈체인 · 파이프라인
    #   상태 물체 · 셔이더 적재는 전부 부팅 때 도는데, 그것을 읽기 전에
    #   비우면 DX12 오류가 가장 잘 나는 구간을 통째로 버리게 된다.
    'dx12.validation'
    'editor.workspace presets'      # ②가 돌 preset 목록을 제품에서 받는다
    'dx12.validation reset'
    # ── DX12 resize ──
    'window.resize 1280 800'
    'wait 60'
    'window.resize 960 640'
    'wait 60'
    'window.resize 1600 1000'
    'wait 60'
    'dx12.validation'
    'editor.dock'
    # ── Game Preview ──
    'dx12.validation reset'
    'editor.viewport game'
    'wait 90'
    'editor.viewport'
    'editor.viewport scene'
    'wait 90'
    'editor.viewport'
    'dx12.validation'
    # ── Play 왕복 ──
    'dx12.validation reset'
    'play'
    'wait 90'
    'play.state'
    'stop'
    'wait 90'
    'play.state'
    'dx12.validation'
    'editor.dock'
)
$surface = Invoke-Session -Tag 'surface' -Lines $surfaceLines

# resize 축
$resize = @(Get-Rows $surface 'window.resize')
Assert ($resize.Count -eq 4) "surface: window.resize 결과가 $($resize.Count) 개다(4 이어야 한다)"
foreach ($row in $resize) {
    Assert ($row.status -eq 'succeeded') "surface: window.resize 가 실패했다 — $($row.message)"
    # 요청값은 클램프될 수 있다(최소 크기). 제품이 보고한 실제 크기만 본다.
    Assert ($row.data.width -gt 0 -and $row.data.height -gt 0) `
        "surface: resize 뒤 창 크기가 $($row.data.width)x$($row.data.height) 다"
}
$report.Add((Assert-ValidationClean (Get-Row $surface 'dx12.validation' 0) 'surface/boot'))
$report.Add((Assert-ValidationClean (Get-Row $surface 'dx12.validation' 2) 'surface/resize'))
Assert-DockClean (Get-Row $surface 'editor.dock' 0) 'surface/resize'

# Game Preview 축 — 표시 타깃이 실제로 갔다 왔는가
$viewGame = Get-Row $surface 'editor.viewport' 1
$viewScene = Get-Row $surface 'editor.viewport' 3
Assert ($viewGame.data.mode -eq 'game') "surface: editor.viewport game 뒤에도 모드가 $($viewGame.data.mode) 다"
Assert ($viewGame.data.gameTarget -eq $true) 'surface: game 모드인데 게임 타깃을 요구하지 않았다'
Assert ($viewScene.data.mode -eq 'scene') "surface: editor.viewport scene 뒤에도 모드가 $($viewScene.data.mode) 다"
Assert ($viewScene.data.gameModeFrames -gt 0) 'surface: game 모드로 그린 프레임이 0 이다 — 전환이 그림에 닿지 않았다'
$report.Add((Assert-ValidationClean (Get-Row $surface 'dx12.validation' 4) 'surface/viewport'))

# Play 왕복 축
$playOn = Get-Row $surface 'play.state' 0
$playOff = Get-Row $surface 'play.state' 1
Assert ($playOn.data.committed -eq $true) 'surface: play 가 확정되지 않았다'
Assert ($playOn.data.gameStart -eq $true) 'surface: play 뒤에도 gameStart 가 거짓이다'
Assert ($playOff.data.gameStart -eq $false) 'surface: stop 뒤에도 gameStart 가 참이다'
Assert ($playOff.data.failureCount -eq 0) "surface: 재생 전이 실패 $($playOff.data.failureCount) 건 — $($playOff.data.lastFailure)"
$report.Add((Assert-ValidationClean (Get-Row $surface 'dx12.validation' 6) 'surface/play'))
Assert-DockClean (Get-Row $surface 'editor.dock' 1) 'surface/play'
Assert-CleanExit $surface

# ── ② preset 매트릭스 ────────────────────────────────────────────────────
#
# preset 이름을 **여기 적지 않는다.** ①이 제품에서 받아 온 것을 그대로 돈다.
$presetRow = Get-Row $surface 'editor.workspace' 0
Assert ($presetRow.status -eq 'succeeded') 'surface: editor.workspace presets 가 실패했다'
$presetIds = @($presetRow.data.presets | ForEach-Object { $_.id })
Assert ($presetIds.Count -ge 2) "preset 목록이 비었거나 하나뿐이다 (=$($presetIds.Count))"
Write-Host ("  preset: " + ($presetIds -join ', '))

$presetLines = [Collections.Generic.List[string]]::new()
$presetLines.Add('window.resize 1600 1000')
$presetLines.Add('wait 120')
$presetLines.Add('dx12.validation')          # 부팅 구간 — reset 앞
$presetLines.Add('dx12.validation reset')
foreach ($id in $presetIds) {
    $presetLines.Add("editor.workspace preset $id")
    $presetLines.Add('wait 60')
    $presetLines.Add('editor.dock')
    $presetLines.Add('editor.workspace')
}
$presetLines.Add('dx12.validation')
$presetRun = Invoke-Session -Tag 'preset' -Lines $presetLines.ToArray()

for ($i = 0; $i -lt $presetIds.Count; $i++) {
    $id = $presetIds[$i]
    Assert-DockClean (Get-Row $presetRun 'editor.dock' $i) "preset/$id"
    $status = Get-Row $presetRun 'editor.workspace' ($i * 2 + 1)
    Assert ($status.data.preset -eq $id) `
        "preset/${id}: 적용 뒤 활성 preset 이 $($status.data.preset) 다"
}
$report.Add((Assert-ValidationClean (Get-Row $presetRun 'dx12.validation' 0) 'preset/부팅'))
$report.Add((Assert-ValidationClean (Get-Row $presetRun 'dx12.validation' 2) 'preset/전체'))
Assert-CleanExit $presetRun

# ── ③ 재시작 ─────────────────────────────────────────────────────────────
#
# 폴더를 **일부러 공유한다.** 첫 세션이 기본이 아닌 preset 을 세우고 나가면
# 두 번째 세션이 그것을 물고 떠야 한다.
$restartPreset = $presetIds[$presetIds.Count - 1]
$restartDir = Join-Path $Work 'restart-workspace'
New-Item -ItemType Directory -Force -Path $restartDir | Out-Null
$restartFirst = Invoke-Session -Tag 'restart-first' -WorkspaceDir $restartDir -Lines @(
    'window.resize 1600 1000'
    'wait 120'
    "editor.workspace preset $restartPreset"
    'wait 60'
    'editor.workspace save'
    'editor.workspace'
    'dx12.validation'
)
$firstStatus = Get-Row $restartFirst 'editor.workspace' 2
Assert ($firstStatus.data.preset -eq $restartPreset) `
    "restart-first: 저장 직전 활성 preset 이 $($firstStatus.data.preset) 다"
$report.Add((Assert-ValidationClean (Get-Row $restartFirst 'dx12.validation' 0) 'restart/1회차'))
Assert-CleanExit $restartFirst

$restartSecond = Invoke-Session -Tag 'restart-second' -WorkspaceDir $restartDir -Lines @(
    'wait 150'
    'editor.workspace'
    'editor.dock'
    'dx12.validation'
)
$secondStatus = Get-Row $restartSecond 'editor.workspace' 0
Assert ($secondStatus.data.recovered -eq $false) `
    "restart-second: 멀쩡한 저장본이 손상으로 판정됐다 — $($secondStatus.message)"
Assert ($secondStatus.data.preset -eq $restartPreset) `
    "restart-second: 재시작 뒤 preset 이 $($secondStatus.data.preset) 다($restartPreset 이어야 한다)"
Assert-DockClean (Get-Row $restartSecond 'editor.dock' 0) 'restart/2회차'
$report.Add((Assert-ValidationClean (Get-Row $restartSecond 'dx12.validation' 0) 'restart/2회차'))
Assert-CleanExit $restartSecond

# ── ④ 손상 ini ───────────────────────────────────────────────────────────
#
# fixture 이름을 적지 않는다 — `damaged-*.ini` 를 전부 돈다. 새 손상본을
# 더하면 이 게이트가 자동으로 그것도 태운다.
$damaged = @(Get-ChildItem -LiteralPath $fixtureDir -Filter 'damaged-*.ini' | Sort-Object Name)
Assert ($damaged.Count -ge 2) "손상 fixture 가 $($damaged.Count) 개다(2 개 이상이어야 한다)"
foreach ($fixture in $damaged) {
    $name = [IO.Path]::GetFileNameWithoutExtension($fixture.Name)
    $dir = Join-Path $Work "damaged-$name-workspace"
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $legacy = Join-Path $dir 'legacy.ini'
    Copy-Item -LiteralPath $fixture.FullName -Destination $legacy -Force

    $run = Invoke-Session -Tag "damaged-$name" -WorkspaceDir $dir -LegacyIni $legacy -Lines @(
        'window.resize 1600 1000'
        'wait 150'
        'editor.workspace'
        'editor.dock'
        'dx12.validation'
    )
    $status = Get-Row $run 'editor.workspace' 0
    Assert ($status.data.recovered -eq $true) `
        "damaged/${name}: 손상본이 거절되지 않고 받아들여졌다 — $($status.message)"
    Assert-DockClean (Get-Row $run 'editor.dock' 0) "damaged/$name"
    $report.Add((Assert-ValidationClean (Get-Row $run 'dx12.validation' 0) "damaged/$name"))
    Assert-CleanExit $run
}

# ── ⑤ DPI (user scale) ───────────────────────────────────────────────────
#
# 모니터 배율은 못 바꾸므로 `imguiScale` 로 민다 — `verify-editor-theme` 과 같은
# 자리다. **저장소의 추적 파일을 고치므로** 원본 바이트를 들고 있다가
# `finally` 에서 되돌린다. 게이트가 중간에 죽어도 개발자 자리에 남지 않는다.
$settingsBytes = [IO.File]::ReadAllBytes($settingsPath)
$settingsText = [IO.File]::ReadAllText($settingsPath)
$scalePattern = '(?m)(^imguiScale: )[^\r\n]+'
Assert ([regex]::Matches($settingsText, $scalePattern).Count -eq 1) 'imguiScale 설정이 하나가 아니다'
$utf8 = [Text.UTF8Encoding]::new($false)
try {
    foreach ($userScale in @(1.0, 1.5)) {
        $scaleText = $userScale.ToString('0.0', [Globalization.CultureInfo]::InvariantCulture)
        [IO.File]::WriteAllText($settingsPath,
            [regex]::Replace($settingsText, $scalePattern, '${1}' + $scaleText), $utf8)

        $run = Invoke-Session -Tag "dpi-$scaleText" -Lines @(
            'window.resize 1600 1000'
            'wait 150'
            'editor.theme'
            'editor.dock'
            'dx12.validation'
        )
        $theme = Get-Row $run 'editor.theme' 0
        Assert ($theme.status -eq 'succeeded') "dpi/${scaleText}: editor.theme 이 실패했다 — $($theme.message)"
        Assert ($theme.data.scaleMatches -eq $true) `
            "dpi/${scaleText}: 배율 출처가 갈렸다 — FontScaleMain=$($theme.data.fontScaleMain) preference=$($theme.data.preferenceScale)"
        Assert ([math]::Abs([double]$theme.data.preferenceScale - $userScale) -lt 0.001) `
            "dpi/${scaleText}: 제품이 읽은 user scale 이 $($theme.data.preferenceScale) 다 — 설정 파일이 안 닿았다"
        Assert-DockClean (Get-Row $run 'editor.dock' 0) "dpi/$scaleText"
        $report.Add((Assert-ValidationClean (Get-Row $run 'dx12.validation' 0) "dpi/$scaleText"))
        Assert-CleanExit $run
    }
}
finally {
    [IO.File]::WriteAllBytes($settingsPath, $settingsBytes)
}
Assert ([Linq.Enumerable]::SequenceEqual($settingsBytes, [IO.File]::ReadAllBytes($settingsPath))) `
    '설정 파일을 원본 바이트로 되돌리지 못했다'

# ── ⑥ 대조군 — 레이어를 끄면 장부가 그렇게 말해야 한다 ───────────────────
#
# 같은 이진, 같은 스크립트, 환경 변수만 다르다. 이것이 붉지 않으면 위의 다섯
# 축은 전부 "늘 참인 상수" 를 읽고 있었을 수 있다.
$control = Invoke-Session -Tag 'control-off' -Validation 'off' -Lines @(
    'window.resize 1600 1000'
    'wait 120'
    'editor.dock'
    'dx12.validation'
)
$controlRow = Get-Row $control 'dx12.validation' 0
Assert ($controlRow.status -eq 'succeeded') "control-off: dx12.validation 이 실패했다 — $($controlRow.message)"
Assert ($controlRow.data.layerEnabled -eq $false) `
    'control-off: 레이어를 껐는데 layerEnabled 가 참이다 — 이 필드는 실물을 따르지 않는다'
Assert ($controlRow.data.mode -eq 'off') "control-off: 모드가 $($controlRow.data.mode) 다"
Assert ($controlRow.data.drains -eq 0) `
    "control-off: 큐가 없는데 드레인이 $($controlRow.data.drains) 번 셌다 — 장부가 무엇을 세는지 어긋났다"
Assert-DockClean (Get-Row $control 'editor.dock' 0) 'control-off'
Assert-CleanExit $control

# ── 보고 ─────────────────────────────────────────────────────────────────
Write-Host ''
foreach ($entry in $report) {
    Write-Host ("  {0,-24} drains={1,-6} messages={2}" -f $entry.Axis, $entry.Drains, $entry.Messages)
}
Write-Host ''
Write-Host ("통합 매트릭스 OK — 축 $($script:axes) 개(검증 레이어 문제 0 · 비정상 종료 0), " +
            "preset $($presetIds.Count) 종, 손상 fixture $($damaged.Count) 벌, 대조군 1, 단정 $($script:checks) 건")

[CmdletBinding()]
param(
    [string]$Exe  = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = (Join-Path $env:TEMP "lc6op"),

    [ValidateRange(5, 900)]
    [int]$BootTimeoutSec = 180,

    # `model.place` 케이스가 쓸 모델 stem. 이 기계에 게시된 generation 이 없으면
    # 그 케이스는 **측정 불가**이고, 그때 false 를 기록하면 고쳐 놓은 동작을
    # "Undo 안 남김"으로 못 박아 버린다 — 아래 ★★ 를 볼 것.
    [string]$ModelStem = 'Prim_Cone',

    # 지금 상태를 래칫에 다시 기록한다. **의도한 변경일 때만** 쓴다.
    [switch]$Update
)

# PHASE 14.5 LC6 (§9) — editor_operation 명령이 Undo 에 남기는 흔적을 못 박는다.
#
# ── 무엇을 지키려는가 ───────────────────────────────────────────────────
#
# §9 는 `editor_operation` 을 "GUI 와 Undo·selection·transaction 까지 같아야 하는
# 것"으로 정의한다. 그러면 그 14 개가 실제로 Undo 를 남기는지가 계약이다.
#
# ── 실측이 말한 것 ──────────────────────────────────────────────────────
#
# 서비스로 불러 `undo.state` 의 editUndo 를 앞뒤로 재 보면, **14 개 중 하나만**
# Undo 를 남긴다(`object.create.undoable`). `object.create` 도 `object.rename` 도
# `object.duplicate` 도 남기지 않는다. 사람이 GUI 로 한 것과 에이전트가 HTTP 로
# 한 것이 같은 조작인데 한쪽만 되돌릴 수 있다.
#
# ── 왜 고치지 않고 못 박는가 ────────────────────────────────────────────
#
# §9 의 완료 기준은 "GUI/서비스 의미 차이 **미분류** 0" 이다. 없애라가 아니라
# 분류하라다. 13 개에 Undo 를 붙이는 것은 각 명령의 저작 의미를 정하는 일이라
# 파일 분리와 같은 슬라이스에 들어갈 크기가 아니다.
#
# 그래서 지금 참인 것을 값으로 적어 둔다. 이 표가 바뀌면 — 누가 Undo 를 붙였든
# 있던 것을 떨어뜨렸든 — 붉어진다. 분류되지 않은 채 조용히 움직이는 것만 막는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ratchetPath = Join-Path $PSScriptRoot 'cli_editor_operation.ratchet.json'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir   = Split-Path -Parent $Exe
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$endpointPath = Join-Path $repoRoot 'Dynamic_CPP\Library\CommandService\endpoint.json'

function Stop-AllEditors {
    Get-Process CreatorEditor -ErrorAction SilentlyContinue | ForEach-Object {
        try { $_.Kill(); $_.WaitForExit(20000) | Out-Null } catch { }
    }
    if (Test-Path -LiteralPath $endpointPath) { Remove-Item -LiteralPath $endpointPath -Force }
}

Stop-AllEditors
$proc = Start-Process -FilePath $Exe -ArgumentList '--command-service' -WorkingDirectory $exeDir `
    -RedirectStandardOutput (Join-Path $Work 'op.out') `
    -RedirectStandardError  (Join-Path $Work 'op.err') -PassThru

$deadline = (Get-Date).AddSeconds($BootTimeoutSec)
$info = $null
while ((Get-Date) -lt $deadline) {
    if (Test-Path -LiteralPath $endpointPath) {
        try {
            $parsed = Get-Content -LiteralPath $endpointPath -Raw | ConvertFrom-Json
            if ($parsed.pid -eq $proc.Id -and $parsed.port -gt 0) { $info = $parsed; break }
        } catch { }
    }
    Start-Sleep -Milliseconds 300
}
if ($null -eq $info) { Stop-AllEditors; "서비스가 뜨지 않았다"; exit 1 }

$base = "http://127.0.0.1:$($info.port)"
$auth = @{ Authorization = "Bearer $($info.token)" }

function Invoke-Cmd([string]$Body, [int]$TimeoutSec = 40) {
    $r = Invoke-WebRequest ($base + '/command') -Method POST -Headers $auth `
         -ContentType 'application/json' -Body $Body -UseBasicParsing `
         -SkipHttpErrorCheck -TimeoutSec ($TimeoutSec + 15)
    return ($r.Content | ConvertFrom-Json)
}

function Get-EditUndo {
    $j = Invoke-Cmd '{"command":"undo.state","mode":"sync"}'
    return [int]$j.data.editUndo
}

# `model.place` 의 전제 판정용 — 명령이 실제로 씬을 건드렸는지 본다.
function Get-SceneObjects {
    $j = Invoke-Cmd '{"command":"scene.dump","args":["opgate"],"mode":"sync"}'
    return [int]$j.data.objects
}

# 각 항목: 라벨 · 보낼 명령 · 이 명령을 부르기 전에 준비할 것
$cases = @(
    @{ Name = 'object.create';          Body = '{"command":"object.create","args":["OpProbeA"],"mode":"sync"}' }
    @{ Name = 'object.create.undoable'; Body = '{"command":"object.create.undoable","args":["OpProbeB"],"mode":"sync"}' }
    @{ Name = 'object.rename';          Body = '{"command":"object.rename","args":["OpProbeA","OpProbeA2"],"mode":"sync"}' }
    @{ Name = 'object.duplicate';       Body = '{"command":"object.duplicate","args":["OpProbeB"],"mode":"sync"}' }
    @{ Name = 'object.parent';          Body = '{"command":"object.parent","args":["OpProbeB","OpProbeA2"],"mode":"sync"}' }
    @{ Name = 'object.transform';       Body = '{"command":"object.transform","args":["OpProbeB","position","1","0","0"],"mode":"sync"}' }
    @{ Name = 'component.add';          Body = '{"command":"component.add","args":["OpProbeB","MeshRenderer"],"mode":"sync"}' }
    @{ Name = 'object.property';        Body = '{"command":"object.property","args":["OpProbeB","MeshRenderer","m_IsEnabled","false"],"mode":"sync"}' }

    # ★ `model.place` 는 class 가 `engine_service` 라 이 게이트 밖에 있었다.
    #   그런데 GUI 는 이 조작을 `LoadModelToSceneObjCommand` 로 Undo 에 남기고
    #   CLI 만 안 남기고 있었다 — §9 이 잡으려는 바로 그 비대칭인데, 분류가
    #   `editor_operation` 이 아니라는 이유로 **측정 대상에서 빠져 있었다.**
    #   2026-09-05 에 CLI 도 같은 command 를 쓰게 고쳤고, 그것을 여기서 잰다.
    @{ Name = 'model.place'; RequiresModel = $true
       Body = '{"command":"model.place","args":["' + $ModelStem + '"],"mode":"sync"}' }
)

$observed = [ordered]@{}
$unmeasured = New-Object System.Collections.Generic.List[string]
try {
    foreach ($case in $cases) {
        $needsObjects = $case.ContainsKey('RequiresModel')
        $objectsBefore = if ($needsObjects) { Get-SceneObjects } else { 0 }
        $before = Get-EditUndo
        $null = Invoke-Cmd $case.Body
        $after = Get-EditUndo
        $objectsAfter = if ($needsObjects) { Get-SceneObjects } else { 0 }
        $records = ($after -gt $before)

        # ★★ **측정할 수 없었으면 값을 만들지 않는다.**
        #
        #   `model.place` 는 게시된 model generation 이 없으면 씬을 건드리지도
        #   못하고 빠져나온다. 그때 editUndo 는 당연히 그대로인데, 그 0 을
        #   "Undo 를 안 남긴다" 로 기록하면 **고쳐 놓은 동작을 안 고쳐진 것으로
        #   못 박는다.** 측정 실패와 측정 결과는 다른 것이고, 둘을 같은 칸에
        #   적으면 래칫이 거짓말을 하기 시작한다.
        #
        #   그래서 전제가 안 서면 observed 에 넣지 않고 따로 모아 **실패로** 낸다.
        #   조용히 건너뛰면 이 게이트는 그 명령에 대해 영원히 초록이다.
        # StrictMode 는 없는 키 접근에서 던진다 — 다른 케이스에는 이 키가 없다.
        #   전제가 섰는지는 **씬 오브젝트 수**로 판정한다. 결과 message 로는 알 수
        #   없다 — 이 핸들러는 legacy 라 낼 message 자체가 없고, "찾을 수 없음" 은
        #   stdout 으로만 나간다(그 방식으로 먼저 짰다가 못 잡았다).
        #
        #   수가 늘었는데 editUndo 가 그대로면 그것은 **진짜 판정**이다(Undo 를 안
        #   남긴다). 수도 안 늘었으면 명령이 아무것도 못 한 것이라 측정 불가다.
        if ($case.ContainsKey('RequiresModel') -and -not $records) {
            if ($objectsAfter -le $objectsBefore) {
                $unmeasured.Add(("{0} : 모델 '{1}' 의 generation 이 이 기계에 게시되어 있지 않아 명령이 성공 경로에 닿지 못했다" -f $case.Name, $ModelStem))
                "{0,-26} 측정 불가 — 모델 없음" -f $case.Name
                continue
            }
        }

        $observed[$case.Name] = $records
        "{0,-26} editUndo {1} -> {2}   undo={3}" -f `
            $case.Name, $before, $after, $(if ($records) { '기록함' } else { '기록 안 함' })
    }
}
finally { Stop-AllEditors }

if ($unmeasured.Count -ne 0) {
    "측정하지 못한 케이스가 있다 — 초록으로 넘기지 않는다:"
    $unmeasured | ForEach-Object { "  $_" }
    "  (모델 코퍼스가 있는 기계에서 돌리거나 -ModelStem 으로 있는 모델을 지정할 것)"
    exit 1
}

if ($Update) {
    $payload = [ordered]@{
        schema   = 'lc6-editor-operation-undo v1'
        recorded = (Get-Date).ToString('yyyy-MM-dd')
        note     = 'editor_operation 명령이 편집 Undo 스택에 항목을 남기는가. 실측값이다.'
        undo     = $observed
    }
    $payload | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ratchetPath -Encoding UTF8
    "래칫을 지금 상태로 기록했다: $ratchetPath"
    exit 0
}

if (-not (Test-Path -LiteralPath $ratchetPath)) {
    "래칫이 없다: $ratchetPath (-Update 로 최초 기록)"
    exit 1
}
$ratchet = Get-Content -LiteralPath $ratchetPath -Raw | ConvertFrom-Json

$failures = New-Object System.Collections.Generic.List[string]
foreach ($name in $observed.Keys) {
    $expected = $ratchet.undo.$name
    if ($null -eq $expected) {
        $failures.Add("$name : 래칫에 없는 명령이다(새로 생겼으면 -Update)")
        continue
    }
    if ([bool]$expected -ne [bool]$observed[$name]) {
        $failures.Add(("{0} : Undo 기록이 {1} 에서 {2} 로 바뀌었다" -f `
            $name, $expected, $observed[$name]))
    }
}
foreach ($name in $ratchet.undo.PSObject.Properties.Name) {
    if (-not $observed.Contains($name)) {
        $failures.Add("$name : 래칫에 있는데 이번에 재지 못했다")
    }
}

""
if ($failures.Count -gt 0) {
    "실패:"
    $failures | ForEach-Object { "  - $_" }
    ""
    "Undo 기록을 **늘린** 것이라면 좋은 변경이다 — -Update 로 래칫을 옮기고 커밋에"
    "함께 담을 것. 줄었다면 §9 의 동등성이 그만큼 더 벌어진 것이다."
    exit 1
}
$recorded = @($observed.Values | Where-Object { $_ }).Count
"editor_operation Undo 특성화 통과 — {0}개 중 {1}개가 Undo 를 남긴다(래칫과 일치)" -f `
    $observed.Count, $recorded
exit 0

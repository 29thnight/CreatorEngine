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

    # 프리팹 케이스가 쓸 이름. 실행이 끝나면 이 이름의 `.prefab`/`.meta` 를 지운다.
    [string]$PrefabName = 'OpGateFab',

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
# 서비스로 불러 `undo.state` 의 editUndo 를 앞뒤로 재 보면, 2026-09-04 시점에는
# **8 개 중 하나만** Undo 를 남겼다 — 그런데 그 하나가 `object.create.undoable`,
# 즉 `object.create` 를 못 고쳐서 만든 우회로였다. 조작이 하나인데 이름이 둘이고,
# 갈린 이유가 "게이트가 깨질까 봐" 였다.
#
# ── 2026-09-05 에 둘을 하나로 만들었다 ──────────────────────────────────
#
# `object.create` 가 GUI 와 같은 `CreateEntityCommand` 를 쓰게 하고,
# `object.create.undoable` 을 지웠다. 실측으로 갈렸다:
#
#   이행 전  create 후 editUndo 0 · undo 해도 오브젝트가 남음
#   이행 후  create 후 editUndo 1 · undo 하면 사라지고 redo 스택 1
#
# 부모 인덱스는 바뀌지 않았다(양쪽 digest 가 `|0|`) — `Scene::CreateEntity` 의
# `parentIndex >= size` 폴백이 `-1` 과 `0` 을 같은 곳으로 보낸다.
#
# 같은 날 `model.place` 도 붙였다. 그것은 class 가 `engine_service` 라 이 게이트
# 밖에 있었는데, **분류가 측정 범위를 정하고 있어서 분류가 틀린 자리가 보이지
# 않았다.**
#
# ── 왜 나머지는 고치지 않고 못 박는가 ───────────────────────────────────
#
# §9 의 완료 기준은 "GUI/서비스 의미 차이 **미분류** 0" 이다. 없애라가 아니라
# 분류하라다. 남은 것들에 Undo 를 붙이는 것은 각 명령의 저작 의미를 정하는
# 일이라(무엇을 하나의 되돌림 단위로 볼 것인가) 한 슬라이스에 다 들어가지 않는다.
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
#
# ★ `play`/`stop` 과 `undo`/`redo` 는 **일부러 뺐다.**
#
#   둘 다 편집 스택 자체를 다루는 명령이라 "editUndo 가 늘었는가" 로 재는 것이
#   말이 안 된다 — `undo` 는 스택을 **줄이는** 것이 일이고, `play` 는 진입할 때
#   양쪽 스택을 **비운다**(`EditorPlayModeController`). 여기서 재면 값이 늘 false
#   인데 그것은 "Undo 를 안 남긴다" 가 아니라 "그 질문이 이 명령에 안 맞는다" 다.
#   기록해 두면 다음 사람이 그 false 를 결함으로 읽는다.
#
#   그 둘의 계약은 `verify-play-selection-undo.ps1` 이 따로 단정한다 — play 전이가
#   스택을 비우는가, 선택이 정지에서 지워지는가.
$cases = @(
    @{ Name = 'object.create';          Body = '{"command":"object.create","args":["OpProbeA"],"mode":"sync"}' }
    @{ Name = 'object.rename';          Body = '{"command":"object.rename","args":["OpProbeA","OpProbeA2"],"mode":"sync"}' }
    @{ Name = 'object.duplicate';       Body = '{"command":"object.duplicate","args":["OpProbeB"],"mode":"sync"}' }
    @{ Name = 'object.parent';          Body = '{"command":"object.parent","args":["OpProbeB","OpProbeA2"],"mode":"sync"}' }
    # ★ 인자를 고쳤다(2026-09-06). 예전에는 `["OpProbeB","position","1","0","0"]`
    #   를 보냈는데 핸들러 문법은 `<이름> <px> <py> <pz>` 다. `"position"` 이
    #   `std::atof` 로 **0** 이 되어 (0,1,0) 을 넣고 있었다 — 명령이 돌기는 했으니
    #   판정 자체는 무의미하지 않았지만, 케이스 이름이 말하는 것을 검사하고 있지
    #   않았다.
    @{ Name = 'object.transform';       Body = '{"command":"object.transform","args":["OpProbeB","1","2","3"],"mode":"sync"}' }
    @{ Name = 'component.add';          Body = '{"command":"component.add","args":["OpProbeB","MeshRenderer"],"mode":"sync"}' }
    @{ Name = 'object.property';        Body = '{"command":"object.property","args":["OpProbeB","MeshRenderer","m_IsEnabled","false"],"mode":"sync"}' }
    @{ Name = 'object.rootref';         Body = '{"command":"object.rootref","args":["OpProbeB","OpProbeA2"],"mode":"sync"}' }

    # ★ 프리팹 셋은 **디스크에 파일을 쓴다.** 아래 finally 에서 지운다 —
    #   게이트가 저작 트리에 흔적을 남기면 다음 실행의 전제가 조용히 달라진다.
    @{ Name = 'prefab.create';          Body = '{"command":"prefab.create","args":["OpProbeB","' + $PrefabName + '"],"mode":"sync"}' }
    @{ Name = 'prefab.instantiate';     Body = '{"command":"prefab.instantiate","args":["' + $PrefabName + '"],"mode":"sync"}' }
    @{ Name = 'prefab.update';          Body = '{"command":"prefab.update","args":["OpProbeB","' + $PrefabName + '"],"mode":"sync"}' }

    # ★ `model.place` 는 class 가 `engine_service` 라 이 게이트 밖에 있었다.
    #   그런데 GUI 는 이 조작을 `LoadModelToSceneObjCommand` 로 Undo 에 남기고
    #   CLI 만 안 남기고 있었다 — §9 이 잡으려는 바로 그 비대칭인데, 분류가
    #   `editor_operation` 이 아니라는 이유로 **측정 대상에서 빠져 있었다.**
    #   2026-09-05 에 CLI 도 같은 command 를 쓰게 고쳤고, 그것을 여기서 잰다.
    @{ Name = 'model.place'; RequiresModel = $true
       Body = '{"command":"model.place","args":["' + $ModelStem + '"],"mode":"sync"}' }
)

# ★ 픽스처를 **측정 밖에서** 세운다.
#
#   `OpProbeB` 는 예전에 `object.create.undoable` 케이스가 만들어 주고 있었다.
#   그 명령이 2026-09-05 에 사라지면서(=`object.create` 가 Undo 를 남기게 되어
#   존재 이유가 없어졌다) 이 이름을 만드는 곳이 없어졌고, 뒤의 다섯 케이스
#   (`duplicate`·`parent`·`transform`·`component.add`·`property`)가 **없는
#   오브젝트를 가리키게 됐다.**
#
#   그러면 그것들은 아무것도 못 하고 editUndo 가 그대로라 `false` 를 기록하는데,
#   래칫에도 `false` 라 **초록으로 통과한다.** 판정이 맞아서가 아니라 둘 다
#   아무것도 안 해서 맞는 것이다 — 이 게이트가 잡으려는 바로 그 종류의 거짓말이다.
#
#   그래서 여기서 만든다. 이 호출은 측정 대상이 아니다.
$null = Invoke-Cmd '{"command":"object.create","args":["OpProbeB"],"mode":"sync"}'

# 전체 무의미성 방지: 이 게이트가 도는 동안 씬 오브젝트는 **늘어야 한다**
# (`object.create` · `object.duplicate` · `prefab.instantiate` 가 각각 더한다).
# 늘지 않았다면 케이스들이 없는 오브젝트를 가리키며 헛돌았다는 뜻이고, 그때
# 기록되는 `false` 는 판정이 아니라 사고다.
$objectsAtStart = Get-SceneObjects

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

    $objectsAtEnd = Get-SceneObjects
    if ($objectsAtEnd -le $objectsAtStart) {
        throw ("씬 오브젝트가 늘지 않았다($objectsAtStart -> $objectsAtEnd) — " +
               "케이스들이 헛돌았다. 이 상태의 판정은 전부 무의미하다.")
    }
    "오브젝트 {0} -> {1} (무의미성 방지 통과)" -f $objectsAtStart, $objectsAtEnd
}
finally {
    Stop-AllEditors

    # 프리팹 케이스가 저작 트리에 남긴 파일을 지운다. 에디터를 먼저 세운 뒤에
    # 지워야 핸들이 잡혀 있지 않다.
    $prefabDir = Join-Path $repoRoot 'Dynamic_CPP\Assets\Prefabs'
    foreach ($suffix in @('.prefab', '.prefab.meta')) {
        $leftover = Join-Path $prefabDir ($PrefabName + $suffix)
        if (Test-Path -LiteralPath $leftover) {
            Remove-Item -LiteralPath $leftover -Force -ErrorAction SilentlyContinue
        }
    }
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
    # ★ 측정 못 한 케이스는 **애초에 observed 에 없으므로** 래칫에도 안 들어간다.
    #   있던 값을 지우는 것이 아니라 없는 값을 안 만드는 것이다. 그래도 무엇을
    #   못 쟀는지는 말해 준다 — 조용히 빠지면 다음 사람이 그 명령이 검사되고
    #   있다고 믿는다.
    if ($unmeasured.Count -ne 0) {
        "다만 아래는 이번에 재지 못해 래칫에 넣지 않았다:"
        $unmeasured | ForEach-Object { "  $_" }
    }
    exit 0
}

# 검증 경로에서는 측정 실패를 통과로 넘기지 않는다.
if ($unmeasured.Count -ne 0) {
    "측정하지 못한 케이스가 있다 — 초록으로 넘기지 않는다:"
    $unmeasured | ForEach-Object { "  $_" }
    "  (모델 코퍼스가 있는 기계에서 돌리거나 -ModelStem 으로 있는 모델을 지정할 것)"
    exit 1
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
